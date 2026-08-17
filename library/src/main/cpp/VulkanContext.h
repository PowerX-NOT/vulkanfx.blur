#pragma once

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <android/native_window.h>
#include <string>
#include <vector>

#include "BlurEngine.h"

class VulkanContext {
public:
    static VulkanContext* create(ANativeWindow* window, bool enableValidation, std::string* error);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    std::string info() const;
    bool resize(uint32_t width, uint32_t height);
    bool setRadius(float radius);
    bool setLayerAlpha(float alpha);
    bool setBlurAlpha(float alpha);
    bool setBlurScale(float scale);
    bool setBlurRegions(const std::vector<BlurRegion>& regions);
    bool setBlurRegionTransform(const float m[9]);
    bool setDebugLevel(int level);
    bool setInputRgba(const uint8_t* rgba, uint32_t width, uint32_t height);
    bool render();
    void releaseSurface();
    bool setSurface(ANativeWindow* window);
    const std::string& lastError() const { return error_; }
    float downMs() const { return engine_.downMs(); }
    float upMs() const { return engine_.upMs(); }
    float totalMs() const { return engine_.totalMs(); }
    float layerAlpha() const { return engine_.layerAlpha(); }
    float blurAlpha() const { return engine_.fullFrameBlurAlpha(); }
    float blurScale() const { return engine_.backgroundBlurScale(); }
    int debugLevel() const { return engine_.debugLevel(); }

private:
    VulkanContext() = default;
    bool init(ANativeWindow* window, bool enableValidation);
    bool createInstance(bool enableValidation);
    bool createDebugMessenger();
    bool createSurface();
    bool pickDevice();
    bool createDevice();
    bool createCommandPool();
    bool createSyncObjects();
    bool createSwapchain();
    void destroySwapchain();
    bool createInputImage(uint32_t w, uint32_t h);
    bool uploadInputRgba(const uint8_t* rgba);
    bool rebuildBlurFromInput();
    bool ensureEngineReady();
    bool ensureWorkingResources();
    bool presentFrame();
    bool tryPresent();
    void barrierImage(VkCommandBuffer cmd, VkImage image,
                      VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkImageLayout oldLayout,
                      VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess, VkImageLayout newLayout);

    std::string error_;
    ANativeWindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkSemaphore acquireSem_ = VK_NULL_HANDLE;
    VkSemaphore presentSem_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR swapchainColorSpace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    VulkanImage inputImage_;
    BlurEngine engine_;
    float requestedRadius_ = 24.0f;
    PFN_vkCmdPipelineBarrier2 cmdBarrier2_ = nullptr;
    VkPhysicalDeviceProperties props_{};
    uint32_t instanceApi_ = VK_API_VERSION_1_1;
    uint32_t queueFamily_ = 0;
    VkQueueFlags queueFlags_ = 0;
    bool validation_ = false;
    bool sync2_ = false;
    VkImageLayout inputLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
};
