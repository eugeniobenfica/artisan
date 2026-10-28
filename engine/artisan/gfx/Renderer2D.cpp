#include "artisan/gfx/Renderer2D.h"

#include "shaders/quad.frag.h"
#include "shaders/quad.vert.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include <stb_image.h>

#include <algorithm>
#include <cstring>

namespace artisan {

namespace {

constexpr uint32_t kMaxTextures = 1024;
constexpr int kMaxLights = 32;
constexpr VkDeviceSize kFrameUniformSize = 16 + kMaxLights * 32;

VkShaderModule shaderModule(VkDevice device, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytes;
    info.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &info, nullptr, &m);
    return m;
}

} // namespace

bool Renderer2D::fail(std::string* error, const std::string& what) {
    if (error) *error = what;
    shutdown();
    return false;
}

uint32_t Renderer2D::memoryType(uint32_t bits, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(s_.physicalDevice, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) return i;
    return UINT32_MAX;
}

bool Renderer2D::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& out) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(s_.device, &info, nullptr, &out.buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(s_.device, out.buffer, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (alloc.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(s_.device, &alloc, nullptr, &out.memory) != VK_SUCCESS) {
        destroyBuffer(out);
        return false;
    }
    vkBindBufferMemory(s_.device, out.buffer, out.memory, 0);
    vkMapMemory(s_.device, out.memory, 0, size, 0, &out.mapped);
    out.size = size;
    return true;
}

void Renderer2D::destroyBuffer(Buffer& b) {
    if (b.memory) vkFreeMemory(s_.device, b.memory, nullptr); // unmaps
    if (b.buffer) vkDestroyBuffer(s_.device, b.buffer, nullptr);
    b = {};
}

bool Renderer2D::init(const Setup& setup, std::string* error) {
    shutdown();
    s_ = setup;
    VkDevice dev = s_.device;

    // Set 0: a sprite's texture. Set 1: the frame (ambient and lights).
    VkDescriptorSetLayoutBinding tex{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 1;
    li.pBindings = &tex;
    if (vkCreateDescriptorSetLayout(dev, &li, nullptr, &textureLayout_) != VK_SUCCESS) return fail(error, "descriptor layout");
    VkDescriptorSetLayoutBinding ubo{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    li.pBindings = &ubo;
    if (vkCreateDescriptorSetLayout(dev, &li, nullptr, &frameLayout_) != VK_SUCCESS) return fail(error, "descriptor layout");

    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_)};
    VkDescriptorSetLayout sets[] = {textureLayout_, frameLayout_};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 2;
    pl.pSetLayouts = sets;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(dev, &pl, nullptr, &layout_) != VK_SUCCESS) return fail(error, "pipeline layout");

    VkShaderModule vs = shaderModule(dev, quad_vert, sizeof(quad_vert));
    VkShaderModule fs = shaderModule(dev, quad_frag, sizeof(quad_frag));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};

    VkVertexInputBindingDescription binding{0, sizeof(QuadInstance), VK_VERTEX_INPUT_RATE_INSTANCE};
    VkVertexInputAttributeDescription attrs[6];
    for (uint32_t i = 0; i < 6; ++i) attrs[i] = {i, 0, VK_FORMAT_R32G32B32A32_SFLOAT, i * 16};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 6;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = s_.samples;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE; // premultiplied alpha
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &ds;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dy;
    gp.layout = layout_;
    gp.renderPass = s_.renderPass;
    const VkResult pr = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline_);
    vkDestroyShaderModule(dev, vs, nullptr);
    vkDestroyShaderModule(dev, fs, nullptr);
    if (pr != VK_SUCCESS) return fail(error, "graphics pipeline");

    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures},
                                    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s_.framesInFlight}};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = kMaxTextures + s_.framesInFlight;
    pi.poolSizeCount = 2;
    pi.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(dev, &pi, nullptr, &pool_) != VK_SUCCESS) return fail(error, "descriptor pool");

    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = s_.pixelArt ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 1.0f;
    if (vkCreateSampler(dev, &si, nullptr, &sampler_) != VK_SUCCESS) return fail(error, "sampler");

    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cp.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cp.queueFamilyIndex = s_.queueFamily;
    if (vkCreateCommandPool(dev, &cp, nullptr, &uploadPool_) != VK_SUCCESS) return fail(error, "command pool");

    frames_.resize(s_.framesInFlight);
    for (Frame& f : frames_) {
        if (!createBuffer(kFrameUniformSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, f.uniforms) ||
            !createBuffer(sizeof(QuadInstance) * 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, f.instances))
            return fail(error, "frame buffers");
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &frameLayout_;
        if (vkAllocateDescriptorSets(dev, &ai, &f.set) != VK_SUCCESS) return fail(error, "descriptor set");
        VkDescriptorBufferInfo bi{f.uniforms.buffer, 0, kFrameUniformSize};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = f.set;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
    }

    const uint8_t white[4] = {255, 255, 255, 255};
    upload(white, 1, 1);
    if (textures_.empty()) return fail(error, "default texture");
    return true;
}

void Renderer2D::shutdown() {
    if (!s_.device) return;
    VkDevice dev = s_.device;
    for (Texture& t : textures_) {
        if (t.view) vkDestroyImageView(dev, t.view, nullptr);
        if (t.image) vkDestroyImage(dev, t.image, nullptr);
        if (t.memory) vkFreeMemory(dev, t.memory, nullptr);
    }
    textures_.clear();
    byPath_.clear();
    for (Frame& f : frames_) {
        destroyBuffer(f.instances);
        destroyBuffer(f.uniforms);
    }
    frames_.clear();
    if (uploadPool_) vkDestroyCommandPool(dev, uploadPool_, nullptr);
    if (sampler_) vkDestroySampler(dev, sampler_, nullptr);
    if (pool_) vkDestroyDescriptorPool(dev, pool_, nullptr);
    if (pipeline_) vkDestroyPipeline(dev, pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(dev, layout_, nullptr);
    if (frameLayout_) vkDestroyDescriptorSetLayout(dev, frameLayout_, nullptr);
    if (textureLayout_) vkDestroyDescriptorSetLayout(dev, textureLayout_, nullptr);
    uploadPool_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    frameLayout_ = textureLayout_ = VK_NULL_HANDLE;
    s_ = {};
}

void Renderer2D::begin(uint32_t frame) {
    frame_ = frames_.empty() ? 0 : frame % static_cast<uint32_t>(frames_.size());
    draws_.clear();
    lights_.clear();
}

void Renderer2D::setCamera(Vec2 center, float zoom, Vec2 viewport, float time) {
    const float p[8] = {center.x, center.y, zoom, time, viewport.x, viewport.y, 0, 0};
    std::memcpy(push_, p, sizeof(p));
}

void Renderer2D::addLight(Vec2 position, float radius, float intensity, Color color) {
    if (lights_.size() < kMaxLights) lights_.push_back({position.x, position.y, radius, intensity, color.r, color.g, color.b, 0});
}

void Renderer2D::draw(const QuadInstance& q, int layer, TextureId texture) {
    draws_.push_back({layer, texture < textures_.size() ? texture : 0, static_cast<uint32_t>(draws_.size()), q});
}

void Renderer2D::record(VkCommandBuffer cmd, VkExtent2D extent) {
    lastQuads_ = draws_.size();
    if (!ready() || frames_.empty()) return;
    Frame& f = frames_[frame_];

    // Lights and ambient.
    auto* u = static_cast<float*>(f.uniforms.mapped);
    u[0] = ambient_.r;
    u[1] = ambient_.g;
    u[2] = ambient_.b;
    u[3] = static_cast<float>(lights_.size());
    if (!lights_.empty()) std::memcpy(u + 4, lights_.data(), lights_.size() * sizeof(LightData));

    // Layers low to high, the order given within one: runs of the same texture are one draw call.
    std::sort(draws_.begin(), draws_.end(), [](const Draw& a, const Draw& b) { return a.layer != b.layer ? a.layer < b.layer : a.order < b.order; });
    const VkDeviceSize need = draws_.size() * sizeof(QuadInstance);
    if (need > f.instances.size) {
        // The buffer this frame used last time is no longer in flight (the frame's fence was waited on).
        destroyBuffer(f.instances);
        if (!createBuffer(std::max<VkDeviceSize>(need * 2, sizeof(QuadInstance) * 1024), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, f.instances)) return;
    }
    auto* dst = static_cast<QuadInstance*>(f.instances.mapped);
    for (size_t i = 0; i < draws_.size(); ++i) dst[i] = draws_[i].quad;

    VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_), push_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &f.set, 0, nullptr);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &f.instances.buffer, &offset);
    size_t i = 0;
    while (i < draws_.size()) {
        size_t j = i + 1;
        while (j < draws_.size() && draws_[j].texture == draws_[i].texture) ++j;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &textures_[draws_[i].texture].set, 0, nullptr);
        vkCmdDraw(cmd, 6, static_cast<uint32_t>(j - i), 0, static_cast<uint32_t>(i));
        i = j;
    }
}

TextureId Renderer2D::texture(const std::string& path, const FileReader& read) {
    if (path.empty() || !ready()) return 0;
    if (auto it = byPath_.find(path); it != byPath_.end()) return it->second;
    TextureId id = 0;
    std::vector<uint8_t> bytes;
    if (read && read(path, bytes) && !bytes.empty()) {
        int w = 0, h = 0, n = 0;
        if (stbi_uc* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4)) {
            id = upload(px, w, h);
            stbi_image_free(px);
        }
    }
    byPath_[path] = id; // a missing file is not tried again every frame
    return id;
}

Vec2 Renderer2D::textureSize(TextureId id) const {
    if (id == 0 || id >= textures_.size()) return {};
    return {static_cast<float>(textures_[id].width), static_cast<float>(textures_[id].height)};
}

TextureId Renderer2D::upload(const uint8_t* rgba, int width, int height) {
    if (textures_.size() >= kMaxTextures) return 0;
    VkDevice dev = s_.device;
    Texture t;
    t.width = width;
    t.height = height;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(dev, &ii, nullptr, &t.image) != VK_SUCCESS) return 0;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, t.image, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (alloc.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(dev, &alloc, nullptr, &t.memory) != VK_SUCCESS) {
        vkDestroyImage(dev, t.image, nullptr);
        return 0;
    }
    vkBindImageMemory(dev, t.image, t.memory, 0);

    // Straight alpha in the file, premultiplied in the texture (the blending expects it).
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    Buffer staging;
    if (!createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging)) return 0;
    auto* px = static_cast<uint8_t*>(staging.mapped);
    for (size_t i = 0; i < bytes; i += 4) {
        const unsigned a = rgba[i + 3];
        px[i] = static_cast<uint8_t>(rgba[i] * a / 255);
        px[i + 1] = static_cast<uint8_t>(rgba[i + 1] * a / 255);
        px[i + 2] = static_cast<uint8_t>(rgba[i + 2] * a / 255);
        px[i + 3] = static_cast<uint8_t>(a);
    }

    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = uploadPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(dev, &ai, &cmd);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = ii.extent;
    vkCmdCopyBufferToImage(cmd, staging.buffer, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(s_.queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(s_.queue);
    vkFreeCommandBuffers(dev, uploadPool_, 1, &cmd);
    destroyBuffer(staging);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ii.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(dev, &vi, nullptr, &t.view);

    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &textureLayout_;
    vkAllocateDescriptorSets(dev, &dai, &t.set);
    VkDescriptorImageInfo info{sampler_, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = t.set;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &info;
    vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
    textures_.push_back(t);
    return static_cast<TextureId>(textures_.size() - 1);
}

} // namespace artisan
