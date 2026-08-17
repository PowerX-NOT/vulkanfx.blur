#pragma once

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <android/native_window.h>
#include <string>

// Owns the Vulkan objects needed before any compute work: instance, surface,
// physical/logical device, the graphics+compute+present queue, command pool.
class VulkanContext {
public:
    static VulkanContext* create(ANativeWindow* window, bool enableValidation, std::string* error);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    std::string info() const;

private:
    VulkanContext() = default;
    bool init(ANativeWindow* window, bool enableValidation);
    bool createInstance(bool enableValidation);
    bool createDebugMessenger();
    bool createSurface();
    bool pickDevice();
    bool createDevice();
    bool createCommandPool();

    std::string error_;
    ANativeWindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props_{};
    uint32_t instanceApi_ = VK_API_VERSION_1_1;
    uint32_t queueFamily_ = 0;
    VkQueueFlags queueFlags_ = 0;
    bool validation_ = false;
    bool sync2_ = false;
};
