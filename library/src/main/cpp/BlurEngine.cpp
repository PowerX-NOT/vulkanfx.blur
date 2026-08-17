#include "BlurEngine.h"

#include <android/log.h>
#include <algorithm>
#include <cmath>

static const uint32_t kCompositeSpv[] =
#include "kawase_composite_comp_spv.inc"
        ;

static const uint32_t kGlassRimSpv[] =
#include "glass_rim_comp_spv.inc"
        ;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VulkanBlur", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "VulkanBlur", __VA_ARGS__)

namespace {

constexpr float kMaxCrossFadeRadius = 10.0f;

bool invertAffine3(const float m[9], float out[9]) {
    const float a = m[0];
    const float b = m[1];
    const float c = m[3];
    const float d = m[4];
    const float tx = m[6];
    const float ty = m[7];
    const float det = a * d - b * c;
    if (std::abs(det) < 1e-8f) return false;
    const float id = 1.f / det;
    out[0] = d * id;
    out[1] = -b * id;
    out[2] = 0.f;
    out[3] = -c * id;
    out[4] = a * id;
    out[5] = 0.f;
    out[6] = (c * ty - d * tx) * id;
    out[7] = (b * tx - a * ty) * id;
    out[8] = 1.f;
    return true;
}

#define BE_TRY(expr)                                                       \
    do {                                                                   \
        VkResult _r = (expr);                                              \
        if (_r != VK_SUCCESS) {                                            \
            if (error) *error = std::string(#expr) + " failed";            \
            LOGE("%s", error ? error->c_str() : #expr);                    \
            return false;                                                  \
        }                                                                  \
    } while (0)

}  // namespace

void BlurEngine::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    generator_.destroy();
    composite_.destroy();
    originalSnapshot_.destroy();
    if (descPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descPool_, nullptr);
    if (rimDescPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, rimDescPool_, nullptr);
    if (compositePipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, compositePipeline_, nullptr);
    if (glassRimPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, glassRimPipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (rimPipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, rimPipelineLayout_, nullptr);
    if (setLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    if (rimSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, rimSetLayout_, nullptr);
    if (samplerMirror_ != VK_NULL_HANDLE) vkDestroySampler(device_, samplerMirror_, nullptr);
    descPool_ = VK_NULL_HANDLE;
    rimDescPool_ = VK_NULL_HANDLE;
    compositePipeline_ = VK_NULL_HANDLE;
    glassRimPipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    rimPipelineLayout_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    rimSetLayout_ = VK_NULL_HANDLE;
    samplerMirror_ = VK_NULL_HANDLE;
    compositeSet_ = VK_NULL_HANDLE;
    glassRimSet_ = VK_NULL_HANDLE;
    inputView_ = VK_NULL_HANDLE;
    compositeReady_ = false;
    originalSnapshotReady_ = false;
    originalView_ = VK_NULL_HANDLE;
    srcW_ = srcH_ = 0;
}

bool BlurEngine::init(VkDevice device, VkPhysicalDevice physical, uint32_t queueFamily,
                      PFN_vkCmdPipelineBarrier2 cmdBarrier2, std::string* error) {
    destroy();
    device_ = device;
    physical_ = physical;
    queueFamily_ = queueFamily;
    cmdBarrier2_ = cmdBarrier2;
    generator_.setExternalComposite(true);
    if (!ensureCompositePipeline(error)) return false;
    return ensureGlassRimPipeline(error);
}

bool BlurEngine::ensureCompositePipeline(std::string* error) {
    if (compositePipeline_ != VK_NULL_HANDLE) return true;

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    BE_TRY(vkCreateSampler(device_, &sci, nullptr, &samplerMirror_));

    VkDescriptorSetLayoutBinding binds[3]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[2].binding = 2;
    binds[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[2].descriptorCount = 1;
    binds[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo sl{};
    sl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sl.bindingCount = 3;
    sl.pBindings = binds;
    BE_TRY(vkCreateDescriptorSetLayout(device_, &sl, nullptr, &setLayout_));

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.offset = 0;
    pc.size = sizeof(DrawBlurPush);
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &setLayout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pc;
    BE_TRY(vkCreatePipelineLayout(device_, &pl, nullptr, &pipelineLayout_));

    VkShaderModuleCreateInfo sm{};
    sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sm.codeSize = sizeof(kCompositeSpv);
    sm.pCode = kCompositeSpv;
    VkShaderModule module = VK_NULL_HANDLE;
    BE_TRY(vkCreateShaderModule(device_, &sm, nullptr, &module));
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    VkComputePipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage = stage;
    ci.layout = pipelineLayout_;
    VkResult pipe = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &compositePipeline_);
    vkDestroyShaderModule(device_, module, nullptr);
    if (pipe != VK_SUCCESS) {
        if (error) *error = "BlurEngine composite pipeline failed";
        return false;
    }
    return true;
}

bool BlurEngine::ensureGlassRimPipeline(std::string* error) {
    if (glassRimPipeline_ != VK_NULL_HANDLE) return true;

    VkDescriptorSetLayoutBinding bind{};
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo sl{};
    sl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sl.bindingCount = 1;
    sl.pBindings = &bind;
    BE_TRY(vkCreateDescriptorSetLayout(device_, &sl, nullptr, &rimSetLayout_));

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.offset = 0;
    pc.size = sizeof(GlassRimPush);
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &rimSetLayout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pc;
    BE_TRY(vkCreatePipelineLayout(device_, &pl, nullptr, &rimPipelineLayout_));

    VkShaderModuleCreateInfo sm{};
    sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sm.codeSize = sizeof(kGlassRimSpv);
    sm.pCode = kGlassRimSpv;
    VkShaderModule module = VK_NULL_HANDLE;
    BE_TRY(vkCreateShaderModule(device_, &sm, nullptr, &module));
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    VkComputePipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage = stage;
    ci.layout = rimPipelineLayout_;
    VkResult pipe = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &glassRimPipeline_);
    vkDestroyShaderModule(device_, module, nullptr);
    if (pipe != VK_SUCCESS) {
        if (error) *error = "BlurEngine glass rim pipeline failed";
        return false;
    }
    return true;
}

bool BlurEngine::ensureCompositeImage(uint32_t w, uint32_t h, std::string* error) {
    if (!ensureGlassRimPipeline(error)) return false;
    if (composite_.image != VK_NULL_HANDLE && composite_.width == w && composite_.height == h &&
        compositeSet_ != VK_NULL_HANDLE && glassRimSet_ != VK_NULL_HANDLE) {
        return true;
    }
    composite_.destroy();
    if (descPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descPool_, nullptr);
        descPool_ = VK_NULL_HANDLE;
        compositeSet_ = VK_NULL_HANDLE;
    }
    if (rimDescPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, rimDescPool_, nullptr);
        rimDescPool_ = VK_NULL_HANDLE;
        glassRimSet_ = VK_NULL_HANDLE;
    }
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!composite_.create(device_, physical_, w, h, VK_FORMAT_R8G8B8A8_UNORM, usage, error)) {
        return false;
    }
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 2;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = poolSizes;
    BE_TRY(vkCreateDescriptorPool(device_, &pool, nullptr, &descPool_));
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = descPool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &setLayout_;
    BE_TRY(vkAllocateDescriptorSets(device_, &alloc, &compositeSet_));

    VkDescriptorPoolSize rimPoolSize{};
    rimPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    rimPoolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo rimPool{};
    rimPool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    rimPool.maxSets = 1;
    rimPool.poolSizeCount = 1;
    rimPool.pPoolSizes = &rimPoolSize;
    BE_TRY(vkCreateDescriptorPool(device_, &rimPool, nullptr, &rimDescPool_));
    VkDescriptorSetAllocateInfo rimAlloc{};
    rimAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    rimAlloc.descriptorPool = rimDescPool_;
    rimAlloc.descriptorSetCount = 1;
    rimAlloc.pSetLayouts = &rimSetLayout_;
    BE_TRY(vkAllocateDescriptorSets(device_, &rimAlloc, &glassRimSet_));

    compositeReady_ = false;
    return true;
}

bool BlurEngine::ensureOriginalSnapshot(uint32_t w, uint32_t h, std::string* error) {
    if (originalSnapshot_.image != VK_NULL_HANDLE && originalSnapshot_.width == w &&
        originalSnapshot_.height == h) {
        return true;
    }
    originalSnapshot_.destroy();
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!originalSnapshot_.create(device_, physical_, w, h, VK_FORMAT_R8G8B8A8_UNORM, usage, error)) {
        return false;
    }
    originalSnapshotReady_ = false;
    return true;
}

bool BlurEngine::setInput(const VulkanImage& src, std::string* error) {
    inputRef_ = &src;
    inputImage_ = src.image;
    inputView_ = src.view;
    srcW_ = src.width;
    srcH_ = src.height;
    if (!ensureCompositeImage(src.width, src.height, error)) return false;
    if (!ensureOriginalSnapshot(src.width, src.height, error)) return false;
    generator_.invalidateInput();
    if (generator_.passes() == 0) {
        return generator_.create(device_, physical_, src, legacyRadius_, queueFamily_, cmdBarrier2_, error);
    }
    return generator_.resize(src, error);
}

bool BlurEngine::setBackgroundBlurRadius(int radius) {
    backgroundBlurRadius_ = std::max(0, radius);
    return true;
}

bool BlurEngine::setBackgroundBlurScale(float scale) {
    backgroundBlurScale_ = std::max(0.1f, scale);
    return true;
}

bool BlurEngine::setLayerAlpha(float alpha) {
    layerAlpha_ = std::clamp(alpha, 0.f, 1.f);
    return true;
}

bool BlurEngine::setFullFrameBlurAlpha(float alpha) {
    fullFrameBlurAlpha_ = std::clamp(alpha, 0.f, 1.f);
    return true;
}

bool BlurEngine::setLegacyRadius(float radius) {
    legacyRadius_ = std::max(1.f, radius);
    return true;
}

bool BlurEngine::setBlurRegions(const std::vector<BlurRegion>& regions) {
    blurRegions_ = regions;
    return true;
}

bool BlurEngine::setBlurRegionTransform(const float m[9]) {
    float inv[9]{};
    if (!invertAffine3(m, inv)) return false;
    for (int i = 0; i < 9; ++i) blurRegionInvTransform_[i] = inv[i];
    return true;
}

bool BlurEngine::setGlassRimEnabled(bool enabled) {
    glassRimEnabled_ = enabled;
    return true;
}

bool BlurEngine::setGlassRimNightMode(bool night) {
    glassRimNightMode_ = night;
    return true;
}

void BlurEngine::setDebugLevel(int level) {
    generator_.setDebugLevel(level);
}

int BlurEngine::effectiveBackgroundRadius() const {
    if (backgroundBlurRadius_ > 0) {
        if (layerAlpha_ <= 0.f) return 0;
        return std::max(0, static_cast<int>(static_cast<float>(backgroundBlurRadius_) * layerAlpha_));
    }
    if (blurRegions_.empty() && legacyRadius_ >= 1.f) {
        if (layerAlpha_ <= 0.f) return 0;
        return std::max(1, static_cast<int>(legacyRadius_ * layerAlpha_));
    }
    return 0;
}

bool BlurEngine::runBlur(VkCommandBuffer cmd, float radius, std::string* error) {
    if (inputRef_ == nullptr) {
        if (error) *error = "BlurEngine: no input";
        return false;
    }
    const VkImageView blurSrc = originalView_ != VK_NULL_HANDLE ? originalView_ : inputView_;
    if (blurSrc != VK_NULL_HANDLE) {
        generator_.bindInputSource(blurSrc);
    }
    if (!generator_.setRadius(radius, *inputRef_, error)) return false;
    return generator_.record(cmd, error);
}

void BlurEngine::writeCompositeSet(VkImageView blurred, VkImageView original) {
    VkDescriptorImageInfo images[3]{};
    images[0].sampler = samplerMirror_;
    images[0].imageView = blurred;
    images[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    images[1].imageView = composite_.view;
    images[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    images[2].sampler = samplerMirror_;
    images[2].imageView = original;
    images[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet writes[3]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = compositeSet_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &images[0];
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = compositeSet_;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &images[1];
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = compositeSet_;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &images[2];
    vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
}

bool BlurEngine::copyInputSnapshot(VkCommandBuffer cmd) {
    if (inputImage_ == VK_NULL_HANDLE || originalSnapshot_.image == VK_NULL_HANDLE) return false;

    imageBarrier(cmd, cmdBarrier2_, originalSnapshot_.image,
                 VK_PIPELINE_STAGE_2_NONE, 0,
                 originalSnapshotReady_ ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                        : VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    imageBarrier(cmd, cmdBarrier2_, inputImage_,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.extent = {srcW_, srcH_, 1};
    vkCmdCopyImage(cmd, inputImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, originalSnapshot_.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    imageBarrier(cmd, cmdBarrier2_, originalSnapshot_.image,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT |
                                                                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    imageBarrier(cmd, cmdBarrier2_, inputImage_,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    originalSnapshotReady_ = true;
    originalView_ = originalSnapshot_.view;
    return true;
}

bool BlurEngine::copySourceToComposite(VkCommandBuffer cmd, VkImage srcImage, bool srcReady) {
    imageBarrier(cmd, cmdBarrier2_, composite_.image,
                 VK_PIPELINE_STAGE_2_NONE, 0,
                 compositeReady_ ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    imageBarrier(cmd, cmdBarrier2_, srcImage,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                 srcReady ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                          : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.extent = {srcW_, srcH_, 1};
    vkCmdCopyImage(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, composite_.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    imageBarrier(cmd, cmdBarrier2_, composite_.image,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT |
                                                                VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                                                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                 VK_IMAGE_LAYOUT_GENERAL);
    imageBarrier(cmd, cmdBarrier2_, srcImage,
                 VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    compositeReady_ = true;
    return true;
}

bool BlurEngine::copyInputToComposite(VkCommandBuffer cmd) {
    return copySourceToComposite(cmd, inputImage_, true);
}

bool BlurEngine::drawBlurRegion(VkCommandBuffer cmd, const VulkanImage& blurred, float radius,
                                float blurAlpha, float blurScale, const BlurRegion* region) {
    const VkImageView original =
            originalView_ != VK_NULL_HANDLE ? originalView_ : inputView_;
    writeCompositeSet(blurred.view, original);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compositePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1, &compositeSet_,
                            0, nullptr);
    DrawBlurPush push{};
    push.resX = static_cast<float>(composite_.width);
    push.resY = static_cast<float>(composite_.height);
    push.blurAlpha = blurAlpha;
    push.blurScale = blurScale;
    push.mixFactor = radius < kMaxCrossFadeRadius ? std::min(1.0f, radius / kMaxCrossFadeRadius) : 1.0f;
    if (region != nullptr) {
        push.clipRRect = 1;
        push.rectL = static_cast<float>(region->left);
        push.rectT = static_cast<float>(region->top);
        push.rectR = static_cast<float>(region->right);
        push.rectB = static_cast<float>(region->bottom);
        push.radTL = region->cornerRadiusTL;
        push.radTR = region->cornerRadiusTR;
        push.radBR = region->cornerRadiusBR;
        push.radBL = region->cornerRadiusBL;
        for (int i = 0; i < 9; ++i) push.invTransform[i] = blurRegionInvTransform_[i];
    } else {
        push.clipRRect = 0;
        push.rectL = 0.f;
        push.rectT = 0.f;
        push.rectR = static_cast<float>(composite_.width);
        push.rectB = static_cast<float>(composite_.height);
        for (int i = 0; i < 9; ++i) {
            push.invTransform[i] = (i % 4 == 0) ? 1.f : 0.f;
        }
    }
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, (composite_.width + 15u) / 16u, (composite_.height + 15u) / 16u, 1);
    imageBarrier(cmd, cmdBarrier2_, composite_.image,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                 VK_IMAGE_LAYOUT_GENERAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                 VK_IMAGE_LAYOUT_GENERAL);
    return true;
}

bool BlurEngine::drawGlassRim(VkCommandBuffer cmd, const BlurRegion& region) {
    VkDescriptorImageInfo dst{};
    dst.imageView = composite_.view;
    dst.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = glassRimSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &dst;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, glassRimPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rimPipelineLayout_, 0, 1, &glassRimSet_,
                            0, nullptr);
    GlassRimPush push{};
    push.resX = static_cast<float>(composite_.width);
    push.resY = static_cast<float>(composite_.height);
    push.rectL = static_cast<float>(region.left);
    push.rectT = static_cast<float>(region.top);
    push.rectR = static_cast<float>(region.right);
    push.rectB = static_cast<float>(region.bottom);
    push.radTL = region.cornerRadiusTL;
    push.radTR = region.cornerRadiusTR;
    push.radBR = region.cornerRadiusBR;
    push.radBL = region.cornerRadiusBL;
    for (int i = 0; i < 9; ++i) push.invTransform[i] = blurRegionInvTransform_[i];
    push.nightMode = glassRimNightMode_ ? 1 : 0;
    vkCmdPushConstants(cmd, rimPipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, (composite_.width + 15u) / 16u, (composite_.height + 15u) / 16u, 1);
    imageBarrier(cmd, cmdBarrier2_, composite_.image,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                 VK_IMAGE_LAYOUT_GENERAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                 VK_IMAGE_LAYOUT_GENERAL);
    return true;
}

bool BlurEngine::record(VkCommandBuffer cmd, std::string* error) {
    if (generator_.passes() == 0 || composite_.image == VK_NULL_HANDLE) {
        if (error) *error = "BlurEngine not ready";
        return false;
    }

    if (generator_.debugLevel() > 0) {
        if (inputRef_ == nullptr) {
            if (error) *error = "BlurEngine: no input";
            return false;
        }
        if (!generator_.setRadius(legacyRadius_, *inputRef_, error)) return false;
        return generator_.record(cmd, error);
    }

    const bool regionMode = !blurRegions_.empty();
    originalView_ = VK_NULL_HANDLE;

    // AOSP: snapshot blurInput once, copy sharp to output, generate()+drawBlurRegion per clip.
    if (regionMode) {
        if (!copyInputSnapshot(cmd)) return false;
        if (!copySourceToComposite(cmd, originalSnapshot_.image, originalSnapshotReady_)) return false;
    } else if (!copyInputToComposite(cmd)) {
        return false;
    }

    int cachedBlurRadius = -1;
    const int bgRadius = effectiveBackgroundRadius();
    if (bgRadius > 0) {
        if (!runBlur(cmd, static_cast<float>(bgRadius), error)) return false;
        cachedBlurRadius = bgRadius;
        const VulkanImage& blurred = pureBlurImage();
        imageBarrier(cmd, cmdBarrier2_, blurred.image,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!drawBlurRegion(cmd, blurred, static_cast<float>(bgRadius), fullFrameBlurAlpha_,
                            backgroundBlurScale_, nullptr)) {
            return false;
        }
    }

    for (const BlurRegion& region : blurRegions_) {
        if (region.blurRadius == 0 || region.right <= region.left || region.bottom <= region.top) {
            continue;
        }
        const float drawAlpha = region.alpha * layerAlpha_;
        if (drawAlpha <= 0.f) continue;
        if (static_cast<int>(region.blurRadius) != cachedBlurRadius) {
            if (!runBlur(cmd, static_cast<float>(region.blurRadius), error)) return false;
            cachedBlurRadius = static_cast<int>(region.blurRadius);
        }
        const VulkanImage& blurred = pureBlurImage();
        imageBarrier(cmd, cmdBarrier2_, blurred.image,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!drawBlurRegion(cmd, blurred, static_cast<float>(region.blurRadius), drawAlpha, 1.0f,
                            &region)) {
            return false;
        }
        if (glassRimEnabled_ && !drawGlassRim(cmd, region)) {
            return false;
        }
    }

    imageBarrier(cmd, cmdBarrier2_, composite_.image,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                 VK_IMAGE_LAYOUT_GENERAL,
                 VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    return true;
}

std::string BlurEngine::infoExtra() const {
    std::string s;
    s += "backgroundBlurRadius=";
    s += std::to_string(backgroundBlurRadius_);
    s += "\nbackgroundBlurScale=";
    s += std::to_string(backgroundBlurScale_);
    s += "\nlayerAlpha=";
    s += std::to_string(layerAlpha_);
    s += "\nfullFrameBlurAlpha=";
    s += std::to_string(fullFrameBlurAlpha_);
    s += "\nblurRegions=";
    s += std::to_string(blurRegions_.size());
    s += "\nglassRim=";
    s += glassRimEnabled_ ? "yes" : "no";
    s += "\n";
    return s;
}

void BlurEngine::collectTimestamps() {
    generator_.collectTimestamps();
}

const VulkanImage& BlurEngine::output() const {
    if (generator_.debugLevel() > 0) return generator_.presentImage();
    return composite_;
}
