#pragma once

// Instance, device, swapchain and frame pacing for a window the platform layer owns. The window
// layer says which instance extensions it needs and makes the surface; everything else is here.

#include <volk.h>

#include <functional>
#include <string>
#include <vector>

namespace artisan {

class VulkanContext {
public:
    static constexpr uint32_t kFramesInFlight = 2;

    struct Options {
        std::vector<const char*> instanceExtensions; // from the window layer
        std::function<VkSurfaceKHR(VkInstance)> createSurface;
        bool vsync = true;
        bool validation = false; // the Khronos validation layer, when installed
        const char* appName = "Artisan game";
        PFN_vkGetInstanceProcAddr loader = nullptr; // the window layer's Vulkan loader, if it has one
    };

    ~VulkanContext() { shutdown(); }
    bool init(const Options& options, uint32_t width, uint32_t height, std::string* error);
    void shutdown();

    // A frame: begin() gives the command buffer inside the render pass (cleared to `clear`), or
    // nullptr when there is nothing to draw into (minimized, swapchain being rebuilt).
    VkCommandBuffer begin(const float clear[4]);
    void end();
    void resize(uint32_t width, uint32_t height); // the window's new size in pixels

    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physicalDevice() const { return physical_; }
    VkDevice device() const { return device_; }
    VkQueue queue() const { return queue_; }
    uint32_t queueFamily() const { return family_; }
    VkRenderPass renderPass() const { return renderPass_; }
    VkExtent2D extent() const { return extent_; }
    uint32_t frameIndex() const { return frame_; }
    const std::string& deviceName() const { return deviceName_; }

private:
    bool createSwapchain(std::string* error);
    void destroySwapchain();

    Options opt_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t family_ = 0;
    std::string deviceName_;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent_{0, 0};
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkSemaphore> renderDone_; // per swapchain image

    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_[kFramesInFlight]{};
    VkSemaphore acquired_[kFramesInFlight]{};
    VkFence fences_[kFramesInFlight]{};
    uint32_t frame_ = 0, image_ = 0;
    uint32_t wantW_ = 0, wantH_ = 0;
    bool stale_ = false, inFrame_ = false;
};

} // namespace artisan
