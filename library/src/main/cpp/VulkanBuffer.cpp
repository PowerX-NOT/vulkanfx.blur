#include "VulkanBuffer.h"
#include "VulkanMemory.h"

bool VulkanBuffer::create(VkDevice dev, VkPhysicalDevice physical, VkDeviceSize bytes, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags memFlags, std::string* error) {
    destroy();
    device = dev;
    size = bytes;

    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = bytes;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult r = vkCreateBuffer(device, &ci, nullptr, &buffer);
    if (r != VK_SUCCESS) {
        if (error) *error = "vkCreateBuffer failed";
        destroy();
        return false;
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, buffer, &req);
    const uint32_t type = findMemoryType(physical, req.memoryTypeBits, memFlags);
    if (type == UINT32_MAX) {
        if (error) *error = "no matching memory type for buffer";
        destroy();
        return false;
    }

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    r = vkAllocateMemory(device, &ai, nullptr, &memory);
    if (r != VK_SUCCESS) {
        if (error) *error = "vkAllocateMemory(buffer) failed";
        destroy();
        return false;
    }
    vkBindBufferMemory(device, buffer, memory, 0);
    return true;
}

void VulkanBuffer::destroy() {
    if (device == VK_NULL_HANDLE) return;
    if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, nullptr);
    if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
    size = 0;
}

void* VulkanBuffer::map(std::string* error) {
    void* p = nullptr;
    if (vkMapMemory(device, memory, 0, size, 0, &p) != VK_SUCCESS) {
        if (error) *error = "vkMapMemory failed";
        return nullptr;
    }
    return p;
}

void VulkanBuffer::unmap() {
    vkUnmapMemory(device, memory);
}
