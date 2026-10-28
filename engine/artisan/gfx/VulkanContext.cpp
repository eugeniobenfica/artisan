#include "artisan/gfx/VulkanContext.h"

#include <algorithm>
#include <cstring>

namespace artisan {

namespace {

bool hasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
    return std::any_of(list.begin(), list.end(), [&](const VkExtensionProperties& e) { return !std::strcmp(e.extensionName, name); });
}

} // namespace

bool VulkanContext::init(const Options& options, uint32_t width, uint32_t height, std::string* error) {
    opt_ = options;
    wantW_ = width;
    wantH_ = height;
    auto fail = [&](const std::string& what) {
        if (error) *error = what;
        shutdown();
        return false;
    };
    if (opt_.loader) volkInitializeCustom(opt_.loader);
    else if (volkInitialize() != VK_SUCCESS) return fail("No Vulkan driver found (the Vulkan loader is missing)");

    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
    std::vector<const char*> exts = opt_.instanceExtensions;
    VkInstanceCreateFlags flags = 0;
    // macOS and iOS: MoltenVK is a "portability" driver, listed only when asked for.
    if (hasExtension(available, "VK_KHR_portability_enumeration")) {
        exts.push_back("VK_KHR_portability_enumeration");
        flags |= 0x00000001; // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
    }
    std::vector<const char*> layers;
    if (opt_.validation) {
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> ls(count);
        vkEnumerateInstanceLayerProperties(&count, ls.data());
        for (const VkLayerProperties& l : ls)
            if (!std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation")) layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = opt_.appName;
    app.pEngineName = "Artisan";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ii.flags = flags;
    ii.pApplicationInfo = &app;
    ii.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ii.ppEnabledExtensionNames = exts.data();
    ii.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ii.ppEnabledLayerNames = layers.data();
    if (vkCreateInstance(&ii, nullptr, &instance_) != VK_SUCCESS) return fail("Cannot create a Vulkan instance");
    volkLoadInstance(instance_);

    surface_ = opt_.createSurface ? opt_.createSurface(instance_) : VK_NULL_HANDLE;
    if (!surface_) return fail("Cannot create the window's Vulkan surface");

    // A GPU that can draw to the surface: a discrete one first.
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> gpus(count);
    vkEnumeratePhysicalDevices(instance_, &count, gpus.data());
    int best = -1;
    for (VkPhysicalDevice gpu : gpus) {
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, qs.data());
        for (uint32_t i = 0; i < qn; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface_, &present);
            if (!(qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(gpu, &props);
            const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                              : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > best) {
                best = score;
                physical_ = gpu;
                family_ = i;
                deviceName_ = props.deviceName;
            }
            break;
        }
    }
    if (!physical_) return fail("No GPU can draw to this window with Vulkan");

    vkEnumerateDeviceExtensionProperties(physical_, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> devExts(count);
    vkEnumerateDeviceExtensionProperties(physical_, nullptr, &count, devExts.data());
    std::vector<const char*> dexts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (hasExtension(devExts, "VK_KHR_portability_subset")) dexts.push_back("VK_KHR_portability_subset");
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family_;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = static_cast<uint32_t>(dexts.size());
    di.ppEnabledExtensionNames = dexts.data();
    if (vkCreateDevice(physical_, &di, nullptr, &device_) != VK_SUCCESS) return fail("Cannot create the Vulkan device");
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, family_, 0, &queue_);

    // The surface's format, chosen once (the render pass depends on it).
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data());
    format_ = formats.empty() ? VK_FORMAT_B8G8R8A8_UNORM : formats[0].format;
    for (const VkSurfaceFormatKHR& f : formats)
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format_ = f.format; // UNORM: colors are blended as authored, like every 2D tool shows them
            break;
        }

    VkAttachmentDescription color{};
    color.format = format_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.srcStageMask = dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    if (vkCreateRenderPass(device_, &rp, nullptr, &renderPass_) != VK_SUCCESS) return fail("Cannot create the render pass");

    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cp.queueFamilyIndex = family_;
    vkCreateCommandPool(device_, &cp, nullptr, &pool_);
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = kFramesInFlight;
    vkAllocateCommandBuffers(device_, &ai, cmd_);
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        vkCreateSemaphore(device_, &si, nullptr, &acquired_[i]);
        vkCreateFence(device_, &fi, nullptr, &fences_[i]);
    }
    return createSwapchain(error) || fail(error ? *error : "Cannot create the swapchain");
}

bool VulkanContext::createSwapchain(std::string* error) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps);
    if (caps.currentExtent.width != UINT32_MAX) extent_ = caps.currentExtent;
    else
        extent_ = {std::clamp(wantW_, caps.minImageExtent.width, caps.maxImageExtent.width),
                   std::clamp(wantH_, caps.minImageExtent.height, caps.maxImageExtent.height)};
    if (extent_.width == 0 || extent_.height == 0) return true; // minimized: made when it comes back

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &count, modes.data());
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR; // always there: vsync
    if (!opt_.vsync) {
        for (VkPresentModeKHR m : modes)
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) mode = m;
        if (mode == VK_PRESENT_MODE_FIFO_KHR)
            for (VkPresentModeKHR m : modes)
                if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) mode = m;
    }
    uint32_t images = std::max(caps.minImageCount + 1, 3u);
    if (caps.maxImageCount) images = std::min(images, caps.maxImageCount);

    VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sc.surface = surface_;
    sc.minImageCount = images;
    sc.imageFormat = format_;
    sc.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sc.imageExtent = extent_;
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc.preTransform = caps.currentTransform;
    sc.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                                                                            : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    sc.presentMode = mode;
    sc.clipped = VK_TRUE;
    sc.oldSwapchain = swapchain_;
    VkSwapchainKHR next = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device_, &sc, nullptr, &next) != VK_SUCCESS) {
        if (error) *error = "Cannot create the swapchain";
        return false;
    }
    destroySwapchain();
    swapchain_ = next;

    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    std::vector<VkImage> imgs(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, imgs.data());
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkImage img : imgs) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = img;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view;
        vkCreateImageView(device_, &vi, nullptr, &view);
        views_.push_back(view);
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = renderPass_;
        fb.attachmentCount = 1;
        fb.pAttachments = &view;
        fb.width = extent_.width;
        fb.height = extent_.height;
        fb.layers = 1;
        VkFramebuffer f;
        vkCreateFramebuffer(device_, &fb, nullptr, &f);
        framebuffers_.push_back(f);
        VkSemaphore done;
        vkCreateSemaphore(device_, &si, nullptr, &done);
        renderDone_.push_back(done);
    }
    stale_ = false;
    return true;
}

// Keeps swapchain_ (it may be the "old swapchain" of the next one); the caller replaces it.
void VulkanContext::destroySwapchain() {
    for (VkFramebuffer f : framebuffers_) vkDestroyFramebuffer(device_, f, nullptr);
    for (VkImageView v : views_) vkDestroyImageView(device_, v, nullptr);
    for (VkSemaphore s : renderDone_) vkDestroySemaphore(device_, s, nullptr);
    framebuffers_.clear();
    views_.clear();
    renderDone_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void VulkanContext::shutdown() {
    if (device_) {
        vkDeviceWaitIdle(device_);
        destroySwapchain();
        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            if (acquired_[i]) vkDestroySemaphore(device_, acquired_[i], nullptr);
            if (fences_[i]) vkDestroyFence(device_, fences_[i], nullptr);
            acquired_[i] = VK_NULL_HANDLE;
            fences_[i] = VK_NULL_HANDLE;
        }
        if (pool_) vkDestroyCommandPool(device_, pool_, nullptr);
        if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    pool_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    if (instance_) vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
    physical_ = VK_NULL_HANDLE;
}

void VulkanContext::resize(uint32_t width, uint32_t height) {
    wantW_ = width;
    wantH_ = height;
    stale_ = true;
}

VkCommandBuffer VulkanContext::begin(const float clear[4]) {
    if (!device_) return nullptr;
    vkWaitForFences(device_, 1, &fences_[frame_], VK_TRUE, UINT64_MAX);
    if (stale_ || !swapchain_) {
        vkDeviceWaitIdle(device_);
        if (!createSwapchain(nullptr) || !swapchain_) return nullptr;
    }
    VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, acquired_[frame_], VK_NULL_HANDLE, &image_);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        stale_ = true;
        return nullptr;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return nullptr;
    vkResetFences(device_, 1, &fences_[frame_]);

    VkCommandBuffer cmd = cmd_[frame_];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkClearValue cv{};
    std::memcpy(cv.color.float32, clear, sizeof(float) * 4);
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = renderPass_;
    rp.framebuffer = framebuffers_[image_];
    rp.renderArea = {{0, 0}, extent_};
    rp.clearValueCount = 1;
    rp.pClearValues = &cv;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    inFrame_ = true;
    return cmd;
}

void VulkanContext::end() {
    if (!inFrame_) return;
    inFrame_ = false;
    VkCommandBuffer cmd = cmd_[frame_];
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
    const VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &acquired_[frame_];
    si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderDone_[image_];
    vkQueueSubmit(queue_, 1, &si, fences_[frame_]);
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderDone_[image_];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &image_;
    const VkResult r = vkQueuePresentKHR(queue_, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) stale_ = true;
    frame_ = (frame_ + 1) % kFramesInFlight;
}

} // namespace artisan
