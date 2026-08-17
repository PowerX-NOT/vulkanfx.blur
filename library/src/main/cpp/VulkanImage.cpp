#include "VulkanImage.h"
#include "VulkanMemory.h"

namespace {

VkPipelineStageFlags mapStage1(VkPipelineStageFlags2 s, bool isSrc) {
    if (s == VK_PIPELINE_STAGE_2_NONE) {
        return isSrc ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    VkPipelineStageFlags out = 0;
    if (s & (VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT)) {
        out |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    if (s & VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT) out |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (s & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) out |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    if (!out) {
        return isSrc ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    return out;
}

VkAccessFlags mapAccess1(VkAccessFlags2 a) {
    VkAccessFlags out = 0;
    if (a & VK_ACCESS_2_TRANSFER_READ_BIT) out |= VK_ACCESS_TRANSFER_READ_BIT;
    if (a & VK_ACCESS_2_TRANSFER_WRITE_BIT) out |= VK_ACCESS_TRANSFER_WRITE_BIT;
    if (a & (VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
             VK_ACCESS_2_SHADER_SAMPLED_READ_BIT)) {
        out |= VK_ACCESS_SHADER_READ_BIT;
    }
    if (a & (VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) {
        out |= VK_ACCESS_SHADER_WRITE_BIT;
    }
    return out;
}

}  // namespace

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

void imageBarrier(VkCommandBuffer cmd, PFN_vkCmdPipelineBarrier2 cmdBarrier2, VkImage image,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkImageLayout oldLayout,
                  VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess, VkImageLayout newLayout) {
    if (cmdBarrier2) {
        VkImageMemoryBarrier2 b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask = srcStage;
        b.srcAccessMask = srcAccess;
        b.dstStageMask = dstStage;
        b.dstAccessMask = dstAccess;
        b.oldLayout = oldLayout;
        b.newLayout = newLayout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = 1;
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        cmdBarrier2(cmd, &dep);
        return;
    }
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = mapAccess1(srcAccess);
    b.dstAccessMask = mapAccess1(dstAccess);
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, mapStage1(srcStage, true), mapStage1(dstStage, false), 0, 0, nullptr, 0, nullptr, 1, &b);
}
