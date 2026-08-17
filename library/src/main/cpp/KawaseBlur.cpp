#include "KawaseBlur.h"

#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

static const uint32_t kDownSpv[] =
#include "kawase_down_comp_spv.inc"
        ;
static const uint32_t kUpSpv[] =
#include "kawase_up_comp_spv.inc"
        ;
static const uint32_t kDrawSpv[] =
#include "kawase_draw_comp_spv.inc"
        ;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VulkanBlur", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "VulkanBlur", __VA_ARGS__)

namespace {

struct KawasePush {
    float resX, resY;
    float offset;
    float alpha;
    int32_t kind;
};
static_assert(sizeof(KawasePush) == 20, "push constant layout");

struct KawaseMapped {
    uint32_t extraPasses;
    float step;
    float depth;
};

// RenderEngine KawaseBlurDualFilterV2::generate — same constants and step solve.
constexpr float kSigmaScale = 0.57735f;
constexpr float kInputScale = 0.25f;
constexpr float kInverseInputScale = 4.0f;
constexpr int kMaxSurfaces = 4;
constexpr float kMaxCrossFadeRadius = 10.0f;

KawaseMapped mapRadius(float blurRadius) {
    const float radius = blurRadius * kSigmaScale;
    const float depth = std::min(static_cast<float>(kMaxSurfaces - 1), radius * kInputScale / 2.5f);
    const uint32_t extra = static_cast<uint32_t>(
            std::min(kMaxSurfaces - 1, static_cast<int>(std::ceil(depth))));

    float sumSquaredR = 0.0f;
    float sumSquaredStep = 0.0f;
    for (uint32_t i = 0; i < extra; ++i) {
        const float alpha = std::min(1.0f, depth - static_cast<float>(i));
        sumSquaredR += std::pow(std::pow(2.0f, static_cast<float>(i) - 1.0f) * alpha * std::sqrt(2.0f),
                                2.0f);
        sumSquaredStep += std::pow(std::pow(2.0f, static_cast<float>(i)) * alpha, 2.0f);
    }
    const float target = radius * kInputScale;
    const float step = std::sqrt(std::max(0.0f, target * target - sumSquaredR) /
                                 (sumSquaredStep == 0.0f ? 1.0f : sumSquaredStep));
    return {extra, step, depth};
}

void checkRadiusMap() {
    const KawaseMapped m24 = mapRadius(24);
    const KawaseMapped m1 = mapRadius(1);
    const KawaseMapped m64 = mapRadius(64);
    if (m24.extraPasses != 2 || m1.extraPasses != 1 || m64.extraPasses != 3) {
        LOGE("mapRadius self-check failed extra 24=%u 1=%u 64=%u", m24.extraPasses, m1.extraPasses,
             m64.extraPasses);
    }
}

#define KB_TRY(expr)                                                       \
    do {                                                                   \
        VkResult _r = (expr);                                              \
        if (_r != VK_SUCCESS) {                                            \
            if (error) *error = std::string(#expr) + " failed";            \
            LOGE("%s", error ? error->c_str() : #expr);                    \
            return false;                                                  \
        }                                                                  \
    } while (0)

constexpr uint32_t kTsStart = 0;
constexpr uint32_t kTsAfterDown = 1;
constexpr uint32_t kTsEnd = 2;
constexpr uint32_t kTsCount = 3;

}  // namespace

void KawaseBlur::destroyPyramid() {
    if (device_ == VK_NULL_HANDLE) return;
    if (descPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descPool_, nullptr);
        descPool_ = VK_NULL_HANDLE;
    }
    for (auto& img : upImages_) img.destroy();
    for (auto& img : downImages_) img.destroy();
    drawImage_.destroy();
    upImages_.clear();
    downImages_.clear();
    downSets_.clear();
    upSets_.clear();
    drawSet_ = VK_NULL_HANDLE;
}

void KawaseBlur::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    destroyPyramid();
    if (queryPool_ != VK_NULL_HANDLE) vkDestroyQueryPool(device_, queryPool_, nullptr);
    if (drawPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, drawPipeline_, nullptr);
    if (upPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, upPipeline_, nullptr);
    if (downHalfPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, downHalfPipeline_, nullptr);
    if (downPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, downPipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (setLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    if (samplerMirror_ != VK_NULL_HANDLE) vkDestroySampler(device_, samplerMirror_, nullptr);
    if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, sampler_, nullptr);
    queryPool_ = VK_NULL_HANDLE;
    drawPipeline_ = VK_NULL_HANDLE;
    upPipeline_ = VK_NULL_HANDLE;
    downHalfPipeline_ = VK_NULL_HANDLE;
    downPipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    samplerMirror_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    cmdBarrier2_ = nullptr;
    physical_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    srcW_ = srcH_ = 0;
    radius_ = 0.0f;
    debugLevel_ = 0;
    pyramidReady_ = false;
    timestampPeriodNs_ = 0.0f;
    lastDownMs_ = lastUpMs_ = lastTotalMs_ = -1.0f;
}

bool KawaseBlur::createComputePipeline(const uint32_t* spv, size_t bytes, VkPipeline* out,
                                       std::string* error, const int32_t* specKind) {
    VkShaderModuleCreateInfo sm{};
    sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sm.codeSize = bytes;
    sm.pCode = spv;
    VkShaderModule module = VK_NULL_HANDLE;
    KB_TRY(vkCreateShaderModule(device_, &sm, nullptr, &module));
    VkSpecializationMapEntry specEntry{};
    specEntry.constantID = 0;
    specEntry.offset = 0;
    specEntry.size = sizeof(int32_t);
    VkSpecializationInfo specInfo{};
    specInfo.mapEntryCount = 1;
    specInfo.pMapEntries = &specEntry;
    specInfo.dataSize = sizeof(int32_t);
    specInfo.pData = specKind;
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    if (specKind) stage.pSpecializationInfo = &specInfo;
    VkComputePipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage = stage;
    ci.layout = pipelineLayout_;
    VkResult pipe = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, out);
    vkDestroyShaderModule(device_, module, nullptr);
    if (pipe != VK_SUCCESS) {
        if (error) *error = "vkCreateComputePipelines failed";
        LOGE("vkCreateComputePipelines failed");
        return false;
    }
    return true;
}

void KawaseBlur::writeSet(VkDescriptorSet set, VkImageView src, VkImageView dst, VkImageView mix,
                          VkSampler sampler) {
    VkDescriptorImageInfo images[3]{};
    images[0].sampler = sampler;
    images[0].imageView = src;
    images[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    images[1].imageView = dst;
    images[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    images[2].sampler = sampler;
    images[2].imageView = mix;
    images[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet writes[3]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &images[0];
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &images[1];
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = set;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &images[2];
    vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
}

bool KawaseBlur::ensurePipelines(std::string* error) {
    if (downPipeline_ != VK_NULL_HANDLE && downHalfPipeline_ != VK_NULL_HANDLE) return true;

    VkFormatProperties fmt{};
    vkGetPhysicalDeviceFormatProperties(physical_, VK_FORMAT_R8G8B8A8_UNORM, &fmt);
    if ((fmt.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) == 0) {
        if (error) *error = "R8G8B8A8_UNORM lacks linear sampled image";
        LOGE("R8G8B8A8_UNORM lacks linear sampled image");
        return false;
    }
    if ((fmt.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0) {
        if (error) *error = "R8G8B8A8_UNORM lacks STORAGE_IMAGE";
        LOGE("R8G8B8A8_UNORM lacks STORAGE_IMAGE");
        return false;
    }

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 0.0f;
    KB_TRY(vkCreateSampler(device_, &sci, nullptr, &sampler_));
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    KB_TRY(vkCreateSampler(device_, &sci, nullptr, &samplerMirror_));

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
    KB_TRY(vkCreateDescriptorSetLayout(device_, &sl, nullptr, &setLayout_));

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.offset = 0;
    pc.size = sizeof(KawasePush);
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &setLayout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pc;
    KB_TRY(vkCreatePipelineLayout(device_, &pl, nullptr, &pipelineLayout_));

    const int32_t kindQuarter = 0;
    const int32_t kindHalf = 1;
    if (!createComputePipeline(kDownSpv, sizeof(kDownSpv), &downPipeline_, error, &kindQuarter)) {
        return false;
    }
    if (!createComputePipeline(kDownSpv, sizeof(kDownSpv), &downHalfPipeline_, error, &kindHalf)) {
        return false;
    }
    if (!createComputePipeline(kUpSpv, sizeof(kUpSpv), &upPipeline_, error)) return false;
    if (!createComputePipeline(kDrawSpv, sizeof(kDrawSpv), &drawPipeline_, error)) return false;
    return true;
}

bool KawaseBlur::ensureQueryPool(std::string* error) {
    if (queryPool_ != VK_NULL_HANDLE) return true;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_, &props);
    if (props.limits.timestampComputeAndGraphics != VK_TRUE || props.limits.timestampPeriod <= 0.0f) {
        LOGI("GPU timestamps unavailable (compute+graphics / period)");
        return true;
    }

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &familyCount, families.data());
    if (queueFamily_ >= familyCount || families[queueFamily_].timestampValidBits == 0) {
        LOGI("GPU timestamps unavailable (queue timestampValidBits=0)");
        return true;
    }

    VkQueryPoolCreateInfo qi{};
    qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kTsCount;
    KB_TRY(vkCreateQueryPool(device_, &qi, nullptr, &queryPool_));
    timestampPeriodNs_ = props.limits.timestampPeriod;
    LOGI("GPU timestamps enabled period=%.3fns", timestampPeriodNs_);
    return true;
}

void KawaseBlur::writeTimestamp(VkCommandBuffer cmd, uint32_t query) {
    if (queryPool_ == VK_NULL_HANDLE) return;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool_, query);
}

bool KawaseBlur::rebuildPyramid(const VulkanImage& src, uint32_t extraPasses, std::string* error) {
    destroyPyramid();
    srcW_ = src.width;
    srcH_ = src.height;

    const VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    // V2: surfaces[i] = size / ((1<<i) * 4), i.e. 4x, 8x, 16x, 32x from source.
    const uint32_t nDown = extraPasses + 1;
    downImages_.resize(nDown);
    for (uint32_t i = 0; i < nDown; ++i) {
        const float scale = static_cast<float>(1u << i) * kInverseInputScale;
        const uint32_t w = std::max(1u, static_cast<uint32_t>(static_cast<float>(srcW_) / scale));
        const uint32_t h = std::max(1u, static_cast<uint32_t>(static_cast<float>(srcH_) / scale));
        if (!downImages_[i].create(device_, physical_, w, h, VK_FORMAT_R8G8B8A8_UNORM, usage, error)) {
            return false;
        }
    }
    upImages_.resize(extraPasses);
    for (uint32_t i = 0; i < extraPasses; ++i) {
        const VulkanImage& mix = downImages_[extraPasses - 1 - i];
        if (!upImages_[i].create(device_, physical_, mix.width, mix.height, VK_FORMAT_R8G8B8A8_UNORM,
                                 usage, error)) {
            return false;
        }
    }
    const bool needDraw = radius_ < kMaxCrossFadeRadius;
    if (needDraw) {
        if (!drawImage_.create(device_, physical_, srcW_, srcH_, VK_FORMAT_R8G8B8A8_UNORM, usage, error)) {
            return false;
        }
    }

    const uint32_t nSets = nDown + extraPasses + (needDraw ? 1u : 0u);
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = nSets * 2;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = nSets;
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = nSets;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = poolSizes;
    KB_TRY(vkCreateDescriptorPool(device_, &pool, nullptr, &descPool_));

    auto allocSets = [&](uint32_t n, std::vector<VkDescriptorSet>* out) -> bool {
        if (n == 0) {
            out->clear();
            return true;
        }
        std::vector<VkDescriptorSetLayout> layouts(n, setLayout_);
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = descPool_;
        alloc.descriptorSetCount = n;
        alloc.pSetLayouts = layouts.data();
        out->resize(n);
        KB_TRY(vkAllocateDescriptorSets(device_, &alloc, out->data()));
        return true;
    };
    if (!allocSets(nDown, &downSets_)) return false;
    if (!allocSets(extraPasses, &upSets_)) return false;
    if (needDraw) {
        std::vector<VkDescriptorSet> drawSets;
        if (!allocSets(1, &drawSets)) return false;
        drawSet_ = drawSets[0];
    } else {
        drawSet_ = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < nDown; ++i) {
        const VulkanImage& in = (i == 0) ? src : downImages_[i - 1];
        writeSet(downSets_[i], in.view, downImages_[i].view, in.view, sampler_);
    }
    for (uint32_t i = 0; i < extraPasses; ++i) {
        const VulkanImage& in = (i == 0) ? downImages_.back() : upImages_[i - 1];
        const VulkanImage& mix = downImages_[extraPasses - 1 - i];
        writeSet(upSets_[i], in.view, upImages_[i].view, mix.view, sampler_);
    }
    const VulkanImage& blurOut = extraPasses > 0 ? upImages_.back() : downImages_.front();
    if (needDraw) {
        writeSet(drawSet_, blurOut.view, drawImage_.view, src.view, samplerMirror_);
    }
    pyramidReady_ = false;
    LOGI("kawase2 rebuild %ux%u extra=%u step=%.2f depth=%.2f", srcW_, srcH_, extraPasses, step_,
         filterDepth_);
    return true;
}

bool KawaseBlur::create(VkDevice device, VkPhysicalDevice physical, const VulkanImage& src,
                        float radius, uint32_t queueFamily, PFN_vkCmdPipelineBarrier2 cmdBarrier2,
                        std::string* error) {
    destroy();
    checkRadiusMap();
    device_ = device;
    physical_ = physical;
    queueFamily_ = queueFamily;
    cmdBarrier2_ = cmdBarrier2;
    radius_ = radius;
    const KawaseMapped mapped = mapRadius(radius);
    step_ = mapped.step;
    filterDepth_ = mapped.depth;
    if (!ensurePipelines(error)) return false;
    if (!ensureQueryPool(error)) return false;
    return rebuildPyramid(src, mapped.extraPasses, error);
}

bool KawaseBlur::setRadius(float radius, const VulkanImage& src, std::string* error) {
    if (device_ == VK_NULL_HANDLE) {
        if (error) *error = "KawaseBlur::setRadius before create";
        return false;
    }
    const bool hadDraw = radius_ < kMaxCrossFadeRadius;
    const KawaseMapped mapped = mapRadius(radius);
    const bool needDraw = radius < kMaxCrossFadeRadius;
    radius_ = radius;
    step_ = mapped.step;
    filterDepth_ = mapped.depth;
    if (mapped.extraPasses + 1 == downImages_.size() && src.width == srcW_ && src.height == srcH_) {
        if (hadDraw == needDraw) return true;
        return rebuildPyramid(src, mapped.extraPasses, error);
    }
    return rebuildPyramid(src, mapped.extraPasses, error);
}

bool KawaseBlur::resize(const VulkanImage& src, std::string* error) {
    if (device_ == VK_NULL_HANDLE) {
        if (error) *error = "KawaseBlur::resize before create";
        return false;
    }
    if (src.width == srcW_ && src.height == srcH_) return true;
    const KawaseMapped mapped = mapRadius(radius_);
    step_ = mapped.step;
    filterDepth_ = mapped.depth;
    return rebuildPyramid(src, mapped.extraPasses, error);
}

bool KawaseBlur::record(VkCommandBuffer cmd, std::string* error) {
    (void)error;
    if (queryPool_ != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(cmd, queryPool_, 0, kTsCount);
        writeTimestamp(cmd, kTsStart);
    }

    const VkImageLayout wrOld =
            pyramidReady_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;

    const uint32_t nDown = static_cast<uint32_t>(downImages_.size());
    for (uint32_t i = 0; i < nDown; ++i) {
        VulkanImage& dst = downImages_[i];
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, i == 0 ? downPipeline_ : downHalfPipeline_);
        imageBarrier(cmd, cmdBarrier2_, dst.image,
                     VK_PIPELINE_STAGE_2_NONE, 0, wrOld,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                     VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1,
                                &downSets_[i], 0, nullptr);
        KawasePush push{};
        push.resX = static_cast<float>(dst.width);
        push.resY = static_cast<float>(dst.height);
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, (dst.width + 15u) / 16u, (dst.height + 15u) / 16u, 1);
        imageBarrier(cmd, cmdBarrier2_, dst.image,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                     VK_IMAGE_LAYOUT_GENERAL,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    writeTimestamp(cmd, kTsAfterDown);

    const uint32_t extra = static_cast<uint32_t>(upImages_.size());
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, upPipeline_);
    for (uint32_t i = 0; i < extra; ++i) {
        VulkanImage& dst = upImages_[i];
        const uint32_t v2Index = extra - 1 - i;
        const float alpha = std::min(1.0f, filterDepth_ - static_cast<float>(v2Index));
        imageBarrier(cmd, cmdBarrier2_, dst.image,
                     VK_PIPELINE_STAGE_2_NONE, 0, wrOld,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                     VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1,
                                &upSets_[i], 0, nullptr);
        KawasePush push{};
        push.resX = static_cast<float>(dst.width);
        push.resY = static_cast<float>(dst.height);
        push.offset = step_;
        push.alpha = alpha;
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, (dst.width + 15u) / 16u, (dst.height + 15u) / 16u, 1);
        imageBarrier(cmd, cmdBarrier2_, dst.image,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                     VK_IMAGE_LAYOUT_GENERAL,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    const bool mixOriginal =
            debugLevel_ <= 0 && radius_ < kMaxCrossFadeRadius && drawImage_.image != VK_NULL_HANDLE;
    if (mixOriginal) {
        const VkImageLayout drawOld =
                pyramidReady_ ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier(cmd, cmdBarrier2_, drawImage_.image,
                     VK_PIPELINE_STAGE_2_NONE, 0, drawOld,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                     VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, drawPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1, &drawSet_,
                                0, nullptr);
        KawasePush drawPush{};
        drawPush.resX = static_cast<float>(drawImage_.width);
        drawPush.resY = static_cast<float>(drawImage_.height);
        drawPush.alpha = std::min(1.0f, radius_ / kMaxCrossFadeRadius);
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(drawPush),
                           &drawPush);
        vkCmdDispatch(cmd, (drawImage_.width + 15u) / 16u, (drawImage_.height + 15u) / 16u, 1);
        imageBarrier(cmd, cmdBarrier2_, drawImage_.image,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                     VK_IMAGE_LAYOUT_GENERAL,
                     VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    } else {
        const VulkanImage& out = presentImage();
        imageBarrier(cmd, cmdBarrier2_, out.image,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    }
    writeTimestamp(cmd, kTsEnd);
    pyramidReady_ = true;
    return true;
}

void KawaseBlur::collectTimestamps() {
    lastDownMs_ = lastUpMs_ = lastTotalMs_ = -1.0f;
    if (queryPool_ == VK_NULL_HANDLE) return;
    uint64_t stamps[kTsCount]{};
    VkResult qr = vkGetQueryPoolResults(device_, queryPool_, 0, kTsCount, sizeof(stamps), stamps,
                                        sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (qr != VK_SUCCESS) return;
    const double nsPerTick = static_cast<double>(timestampPeriodNs_);
    lastDownMs_ = static_cast<float>(
            static_cast<double>(stamps[kTsAfterDown] - stamps[kTsStart]) * nsPerTick / 1.0e6);
    lastUpMs_ = static_cast<float>(
            static_cast<double>(stamps[kTsEnd] - stamps[kTsAfterDown]) * nsPerTick / 1.0e6);
    lastTotalMs_ = static_cast<float>(
            static_cast<double>(stamps[kTsEnd] - stamps[kTsStart]) * nsPerTick / 1.0e6);
}

void KawaseBlur::setDebugLevel(int level) {
    if (level < 0) level = 0;
    const int maxLevel = static_cast<int>(downImages_.size());
    if (maxLevel > 0 && level > maxLevel) level = maxLevel;
    debugLevel_ = level;
}

const VulkanImage& KawaseBlur::presentImage() const {
    if (debugLevel_ > 0 && !downImages_.empty()) {
        size_t i = static_cast<size_t>(debugLevel_) - 1;
        if (i >= downImages_.size()) i = downImages_.size() - 1;
        return downImages_[i];
    }
    if (radius_ < kMaxCrossFadeRadius && drawImage_.image != VK_NULL_HANDLE) return drawImage_;
    if (!upImages_.empty()) return upImages_.back();
    return downImages_.front();
}

std::string KawaseBlur::pyramidInfo() const {
    std::string s = std::to_string(srcW_) + "x" + std::to_string(srcH_);
    for (const auto& img : downImages_) {
        s += " -> ";
        s += std::to_string(img.width);
        s += "x";
        s += std::to_string(img.height);
    }
    for (const auto& img : upImages_) {
        s += " => ";
        s += std::to_string(img.width);
        s += "x";
        s += std::to_string(img.height);
    }
    if (drawImage_.image != VK_NULL_HANDLE) {
        s += " => ";
        s += std::to_string(drawImage_.width);
        s += "x";
        s += std::to_string(drawImage_.height);
    }
    return s;
}
