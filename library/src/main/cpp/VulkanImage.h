#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>

struct VulkanImage {
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;

    bool create(VkDevice dev, VkPhysicalDevice physical, uint32_t w, uint32_t h, VkFormat fmt,
                VkImageUsageFlags usage, std::string* error);
    void destroy();
};
