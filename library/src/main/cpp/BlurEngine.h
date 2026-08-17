#pragma once

#include "BlurRegion.h"
#include "KawaseBlur.h"
#include "VulkanImage.h"

#include <vector>

class BlurEngine {
public:
    BlurEngine() = default;
    ~BlurEngine() { destroy(); }

    BlurEngine(const BlurEngine&) = delete;
    BlurEngine& operator=(const BlurEngine&) = delete;

    bool init(VkDevice device, VkPhysicalDevice physical, uint32_t queueFamily,
              PFN_vkCmdPipelineBarrier2 cmdBarrier2, std::string* error);
    void destroy();

    bool setInput(const VulkanImage& src, std::string* error);
    bool setBackgroundBlurRadius(int radius);
    bool setBackgroundBlurScale(float scale);
    bool setLayerAlpha(float alpha);
    /** Full-frame compositing alpha when no blur regions (demo / legacy API). */
    bool setFullFrameBlurAlpha(float alpha);
    bool setLegacyRadius(float radius);
    bool setBlurRegions(const std::vector<BlurRegion>& regions);
    void setDebugLevel(int level);
    int debugLevel() const { return generator_.debugLevel(); }

    bool record(VkCommandBuffer cmd, std::string* error);
    void collectTimestamps();

    const VulkanImage& output() const;
    bool ready() const { return generator_.passes() > 0; }
    bool timestampsEnabled() const { return generator_.timestampsEnabled(); }
    float downMs() const { return generator_.downMs(); }
    float upMs() const { return generator_.upMs(); }
    float totalMs() const { return generator_.totalMs(); }
    int backgroundBlurRadius() const { return backgroundBlurRadius_; }
    float backgroundBlurScale() const { return backgroundBlurScale_; }
    float layerAlpha() const { return layerAlpha_; }
    float fullFrameBlurAlpha() const { return fullFrameBlurAlpha_; }
    float legacyRadius() const { return legacyRadius_; }
    const std::vector<BlurRegion>& blurRegions() const { return blurRegions_; }
    std::string infoExtra() const;

private:
    struct DrawBlurPush {
        float resX, resY;
        float blurAlpha;
        float blurScale;
        float mixFactor;
        float rectL, rectT, rectR, rectB;
        float radTL, radTR, radBR, radBL;
        int32_t clipRRect;
    };

    bool ensureCompositePipeline(std::string* error);
    bool ensureCompositeImage(uint32_t w, uint32_t h, std::string* error);
    void writeCompositeSet(VkImageView blurred, VkImageView original);
    bool copyInputToComposite(VkCommandBuffer cmd);
    bool drawBlurRegion(VkCommandBuffer cmd, const VulkanImage& blurred, float radius,
                        float blurAlpha, float blurScale, const BlurRegion* region);
    bool runBlur(VkCommandBuffer cmd, float radius, std::string* error);
    const VulkanImage& pureBlurImage() const { return generator_.blurOutput(); }
    int effectiveBackgroundRadius() const;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    PFN_vkCmdPipelineBarrier2 cmdBarrier2_ = nullptr;
    uint32_t queueFamily_ = 0;

    KawaseBlur generator_;
    VulkanImage composite_;
    const VulkanImage* inputRef_ = nullptr;
    VkImage inputImage_ = VK_NULL_HANDLE;
    VkImageView inputView_ = VK_NULL_HANDLE;
    uint32_t srcW_ = 0;
    uint32_t srcH_ = 0;

    int backgroundBlurRadius_ = 0;
    float backgroundBlurScale_ = 1.f;
    float layerAlpha_ = 1.f;
    float fullFrameBlurAlpha_ = 1.f;
    float legacyRadius_ = 24.f;
    std::vector<BlurRegion> blurRegions_;

    VkSampler samplerMirror_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline compositePipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet compositeSet_ = VK_NULL_HANDLE;
    bool compositeReady_ = false;
};
