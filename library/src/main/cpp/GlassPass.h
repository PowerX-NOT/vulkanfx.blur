#pragma once

#include "VulkanImage.h"

#include <string>

// Post-blur Liquid Glass composite. Phase 12: tint + rim only.
class GlassPass {
public:
    GlassPass() = default;
    ~GlassPass() { destroy(); }

    GlassPass(const GlassPass&) = delete;
    GlassPass& operator=(const GlassPass&) = delete;

    bool create(VkDevice device, VkPhysicalDevice physical, uint32_t w, uint32_t h,
                PFN_vkCmdPipelineBarrier2 cmdBarrier2, std::string* error);
    bool resize(uint32_t w, uint32_t h, std::string* error);
    void destroy();
    bool execute(VkCommandBuffer cmd, VkQueue queue, const VulkanImage& blurred, std::string* error);
    const VulkanImage& output() const { return output_; }

private:
    bool ensurePipeline(std::string* error);
    bool rebuildOutput(uint32_t w, uint32_t h, std::string* error);
    void writeSet(VkImageView src);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    PFN_vkCmdPipelineBarrier2 cmdBarrier2_ = nullptr;
    VulkanImage output_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descSet_ = VK_NULL_HANDLE;
};
