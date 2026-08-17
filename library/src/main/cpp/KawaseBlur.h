#pragma once

#include "VulkanImage.h"

#include <string>
#include <vector>

class KawaseBlur {
public:
    KawaseBlur() = default;
    ~KawaseBlur() { destroy(); }

    KawaseBlur(const KawaseBlur&) = delete;
    KawaseBlur& operator=(const KawaseBlur&) = delete;

    bool create(VkDevice device, VkPhysicalDevice physical, const VulkanImage& src,
                float radius, PFN_vkCmdPipelineBarrier2 cmdBarrier2, std::string* error);
    bool setRadius(float radius, const VulkanImage& src, std::string* error);
    bool resize(const VulkanImage& src, std::string* error);
    void destroy();
    bool execute(VkCommandBuffer cmd, VkQueue queue, std::string* error);
    const VulkanImage& output() const { return upImages_.back(); }
    float radius() const { return radius_; }
    float offset() const { return offset_; }
    uint32_t passes() const { return static_cast<uint32_t>(downImages_.size()); }
    std::string pyramidInfo() const;

private:
    bool ensurePipelines(std::string* error);
    bool rebuildPyramid(const VulkanImage& src, uint32_t passes, std::string* error);
    void destroyPyramid();
    bool createComputePipeline(const uint32_t* spv, size_t bytes, VkPipeline* out, std::string* error);
    void writeSet(VkDescriptorSet set, VkImageView src, VkImageView dst);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    PFN_vkCmdPipelineBarrier2 cmdBarrier2_ = nullptr;
    uint32_t srcW_ = 0;
    uint32_t srcH_ = 0;
    float radius_ = 0.0f;
    float offset_ = 1.0f;
    std::vector<VulkanImage> downImages_;
    std::vector<VulkanImage> upImages_;
    std::vector<VkDescriptorSet> downSets_;
    std::vector<VkDescriptorSet> upSets_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline downPipeline_ = VK_NULL_HANDLE;
    VkPipeline upPipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
};
