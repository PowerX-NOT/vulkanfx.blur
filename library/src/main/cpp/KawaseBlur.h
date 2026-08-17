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
                float radius, uint32_t queueFamily, PFN_vkCmdPipelineBarrier2 cmdBarrier2,
                std::string* error);
    bool setRadius(float radius, const VulkanImage& src, std::string* error);
    bool setBlurAlpha(float alpha);
    bool setBlurScale(float scale);
    void setExternalComposite(bool external) { externalComposite_ = external; }
    bool needsComposite() const;
    bool resize(const VulkanImage& src, std::string* error);
    /** Rebind downsample input (AOSP blurInput snapshot) without rebuilding the pyramid. */
    void bindInputSource(VkImageView view);
    /** Force the next [record] to treat pyramid images as needing layout refresh. */
    void invalidateInput();
    void destroy();
    bool record(VkCommandBuffer cmd, std::string* error);
    void collectTimestamps();
    void setDebugLevel(int level);
    int debugLevel() const { return debugLevel_; }
    /** Pure Kawase output (no crossfade / composite). Valid after [record]. */
    const VulkanImage& blurOutput() const;
    const VulkanImage& presentImage() const;
    const VulkanImage& output() const { return presentImage(); }
    float radius() const { return radius_; }
    float blurAlpha() const { return blurAlpha_; }
    float blurScale() const { return blurScale_; }
    float offset() const { return step_; }
    uint32_t passes() const { return static_cast<uint32_t>(downImages_.size()); }
    bool timestampsEnabled() const { return queryPool_ != VK_NULL_HANDLE; }
    float downMs() const { return lastDownMs_; }
    float upMs() const { return lastUpMs_; }
    float totalMs() const { return lastTotalMs_; }
    std::string pyramidInfo() const;

private:
    bool ensurePipelines(std::string* error);
    bool ensureQueryPool(std::string* error);
    bool rebuildPyramid(const VulkanImage& src, uint32_t extraPasses, std::string* error);
    void destroyPyramid();
    bool createComputePipeline(const uint32_t* spv, size_t bytes, VkPipeline* out, std::string* error,
                               const int32_t* specKind = nullptr);
    void writeSet(VkDescriptorSet set, VkImageView src, VkImageView dst, VkImageView mix,
                  VkSampler sampler);
    void writeTimestamp(VkCommandBuffer cmd, uint32_t query);

    bool ensureCompositeResources(const VulkanImage& src, std::string* error);
    void destroyComposite();

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    PFN_vkCmdPipelineBarrier2 cmdBarrier2_ = nullptr;
    uint32_t srcW_ = 0;
    uint32_t srcH_ = 0;
    uint32_t queueFamily_ = 0;
    float radius_ = 0.0f;
    float blurAlpha_ = 1.0f;
    float blurScale_ = 1.0f;
    float step_ = 0.0f;
    float filterDepth_ = 0.0f;
    int debugLevel_ = 0;
    bool externalComposite_ = false;
    bool pyramidReady_ = false;
    float timestampPeriodNs_ = 0.0f;
    float lastDownMs_ = -1.0f;
    float lastUpMs_ = -1.0f;
    float lastTotalMs_ = -1.0f;
    std::vector<VulkanImage> downImages_;
    std::vector<VulkanImage> upImages_;
    VulkanImage drawImage_;
    VulkanImage compositeImage_;
    VkImageView inputView_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> downSets_;
    std::vector<VkDescriptorSet> upSets_;
    VkDescriptorSet drawSet_ = VK_NULL_HANDLE;
    VkDescriptorSet compositeSet_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkSampler samplerMirror_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline downPipeline_ = VK_NULL_HANDLE;
    VkPipeline downHalfPipeline_ = VK_NULL_HANDLE;
    VkPipeline upPipeline_ = VK_NULL_HANDLE;
    VkPipeline drawPipeline_ = VK_NULL_HANDLE;
    VkPipeline compositePipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkQueryPool queryPool_ = VK_NULL_HANDLE;
};
