#pragma once

#include <vulkan/vulkan.h>
#include <string>

struct VulkanBuffer {
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;

    bool create(VkDevice dev, VkPhysicalDevice physical, VkDeviceSize bytes, VkBufferUsageFlags usage,
                VkMemoryPropertyFlags memFlags, std::string* error);
    void destroy();
    void* map(std::string* error);
    void unmap();
};
