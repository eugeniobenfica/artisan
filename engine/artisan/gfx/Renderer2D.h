#pragma once

// The 2D renderer: everything is an instanced quad (one draw call per texture run, whatever the
// number of objects). Shapes are signed distance fields drawn per pixel, so they stay sharp at any
// zoom and get outlines and glows for free; up to 32 dynamic lights shade lit objects per pixel.
//
// It records into a render pass someone else owns: the player's swapchain (VulkanContext) or the
// editor's QVulkanWindow. Vulkan calls go through volk.

#include "artisan/core/Math.h"

#include <volk.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace artisan {

using TextureId = uint32_t; // 0: plain white (untextured)

enum class Lighting : int { None = 0, Lit = 1, Air = 2 };

// One quad, as the shader reads it (6 x vec4).
struct QuadInstance {
    float x, y, w, h;
    float rotation, shape, corner, outline;
    float r, g, b, a;
    float or_, og, ob, oa;
    float glow, lighting, textured, pad;
    float u0, v0, u1, v1;
};
static_assert(sizeof(QuadInstance) == 96);

enum class QuadShape : int { Rectangle = 0, RoundedRectangle = 1, Circle = 2, Capsule = 3, Grid = 4 };

class Renderer2D {
public:
    struct Setup {
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE; // graphics, for texture uploads
        uint32_t queueFamily = 0;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        uint32_t framesInFlight = 2;
        bool pixelArt = false; // nearest-neighbor sampling
    };

    Renderer2D() = default;
    ~Renderer2D() { shutdown(); }
    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    bool init(const Setup& setup, std::string* error = nullptr);
    void shutdown(); // waits for nothing: call after the device is idle
    bool ready() const { return pipeline_ != VK_NULL_HANDLE; }

    // --- a frame -----------------------------------------------------------------------
    void begin(uint32_t frame);
    void setCamera(Vec2 center, float zoom, Vec2 viewportPixels, float time = 0.0f);
    void setAmbient(Color ambient) { ambient_ = ambient; }
    void addLight(Vec2 position, float radius, float intensity, Color color);
    // Drawn in the order given within a layer; layers low to high.
    void draw(const QuadInstance& q, int layer = 0, TextureId texture = 0);
    // Records the frame's draws into `cmd`, inside the render pass, at `extent`.
    void record(VkCommandBuffer cmd, VkExtent2D extent);

    // --- textures --------------------------------------------------------------------------
    // Loads (once per key) an image file read with `read`; 0 if it cannot be read or decoded.
    using FileReader = std::function<bool(const std::string& path, std::vector<uint8_t>& out)>;
    TextureId texture(const std::string& path, const FileReader& read);
    // Pixel size of a texture ({0, 0} for 0 or unknown).
    Vec2 textureSize(TextureId id) const;

    size_t lastQuadCount() const { return lastQuads_; }

private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0;
    };
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        int width = 0, height = 0;
    };
    struct Draw {
        int layer;
        TextureId texture;
        uint32_t order;
        QuadInstance quad;
    };
    struct Frame {
        Buffer instances;
        Buffer uniforms;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& out);
    void destroyBuffer(Buffer& b);
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags flags) const;
    TextureId upload(const uint8_t* rgba, int width, int height);
    bool fail(std::string* error, const std::string& what);

    Setup s_;
    VkDescriptorSetLayout textureLayout_ = VK_NULL_HANDLE, frameLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkCommandPool uploadPool_ = VK_NULL_HANDLE;
    std::vector<Frame> frames_;
    std::vector<Texture> textures_; // [0] is white
    std::unordered_map<std::string, TextureId> byPath_;

    uint32_t frame_ = 0;
    std::vector<Draw> draws_;
    std::vector<QuadInstance> sorted_;
    struct LightData {
        float x, y, radius, intensity, r, g, b, pad;
    };
    std::vector<LightData> lights_;
    Color ambient_{1, 1, 1, 1};
    float push_[8] = {};
    size_t lastQuads_ = 0;
};

} // namespace artisan
