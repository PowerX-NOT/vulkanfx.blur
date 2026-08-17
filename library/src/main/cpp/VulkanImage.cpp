#include "VulkanImage.h"
#include "VulkanMemory.h"

bool VulkanImage::create(VkDevice dev, VkPhysicalDevice physical, uint32_t w, uint32_t h, VkFormat fmt,
                         VkImageUsageFlags usage, std::string* error) {
    destroy();
    device = dev;
    width = w;
    height = h;
    format = fmt;

    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = fmt;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = vkCreateImage(device, &ci, nullptr, &image);
    if (r != VK_SUCCESS) {
        if (error) *error = "vkCreateImage failed";
        destroy();
        return false;
    }

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device, image, &req);
    const uint32_t type = findMemoryType(physical, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) {
        if (error) *error = "no DEVICE_LOCAL memory type for image";
        destroy();
        return false;
    }

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    r = vkAllocateMemory(device, &ai, nullptr, &memory);
    if (r != VK_SUCCESS) {
        if (error) *error = "vkAllocateMemory(image) failed";
        destroy();
        return false;
    }
    vkBindImageMemory(device, image, memory, 0);

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    r = vkCreateImageView(device, &vi, nullptr, &view);
    if (r != VK_SUCCESS) {
        if (error) *error = "vkCreateImageView failed";
        destroy();
        return false;
    }
    return true;
}

void VulkanImage::destroy() {
    if (device == VK_NULL_HANDLE) return;
    if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
    if (image != VK_NULL_HANDLE) vkDestroyImage(device, image, nullptr);
    if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
    view = VK_NULL_HANDLE;
    image = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
}
