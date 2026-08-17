#include "KawaseBlur.h"

#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

static const uint32_t kDownSpv[] =
#include "kawase_down_comp_spv.inc"
        ;
static const uint32_t kUpSpv[] =
#include "kawase_up_comp_spv.inc"
        ;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VulkanBlur", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "VulkanBlur", __VA_ARGS__)

namespace {

struct KawasePush {
    float texelX, texelY;
    float offset;
    float intensity;
    float resX, resY;
};
static_assert(sizeof(KawasePush) == 24, "push constant layout");

struct KawaseMapped {
    uint32_t passes;
    float offset;
};

// Dual Kawase: each extra level doubles source-space kernel (~2^levels px).
// floor(log2(r)) matches 5→2, 10→3, 20→4, 40→5. Fractional log2 scales offset in [1,2).
// ponytail: cap 6 levels; bigger radius only raises offset, not more mips.
KawaseMapped mapRadius(float radius, uint32_t minDim) {
    uint32_t maxPasses = 0;
    for (uint32_t d = minDim; d > 1 && maxPasses < 6; d /= 2) ++maxPasses;
    if (maxPasses < 1) maxPasses = 1;
    const float lg = std::log2(std::max(radius, 1.0f));
    uint32_t passes = static_cast<uint32_t>(std::floor(lg));
    if (passes < 1) passes = 1;
    if (passes > maxPasses) passes = maxPasses;
    return {passes, 1.0f + (lg - std::floor(lg))};
}

void checkRadiusMap() {
    if (mapRadius(5, 256).passes != 2 || mapRadius(10, 256).passes != 3 ||
        mapRadius(20, 256).passes != 4 || mapRadius(40, 256).passes != 5 ||
        mapRadius(1, 256).passes != 1) {
        LOGE("mapRadius self-check failed");
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

}  // namespace

void KawaseBlur::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    if (upPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, upPipeline_, nullptr);
    if (downPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, downPipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (descPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descPool_, nullptr);
    if (setLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, sampler_, nullptr);
    for (auto& img : upImages_) img.destroy();
    for (auto& img : downImages_) img.destroy();
    upImages_.clear();
    downImages_.clear();
    downSets_.clear();
    upSets_.clear();
    upPipeline_ = VK_NULL_HANDLE;
    downPipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    descPool_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    cmdBarrier2_ = nullptr;
    device_ = VK_NULL_HANDLE;
}

bool KawaseBlur::createComputePipeline(const uint32_t* spv, size_t bytes, VkPipeline* out,
                                       std::string* error) {
    VkShaderModuleCreateInfo sm{};
    sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sm.codeSize = bytes;
    sm.pCode = spv;
    VkShaderModule module = VK_NULL_HANDLE;
    KB_TRY(vkCreateShaderModule(device_, &sm, nullptr, &module));
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
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

void KawaseBlur::writeSet(VkDescriptorSet set, VkImageView src, VkImageView dst) {
    VkDescriptorImageInfo images[2]{};
    images[0].sampler = sampler_;
    images[0].imageView = src;
    images[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    images[1].imageView = dst;
    images[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet writes[2]{};
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
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
}

bool KawaseBlur::create(VkDevice device, VkPhysicalDevice physical, const VulkanImage& src,
                        float radius, PFN_vkCmdPipelineBarrier2 cmdBarrier2, std::string* error) {
    destroy();
    checkRadiusMap();
    const KawaseMapped mapped = mapRadius(radius, std::min(src.width, src.height));
    const uint32_t passes = mapped.passes;
    device_ = device;
    physical_ = physical;
    cmdBarrier2_ = cmdBarrier2;
    srcW_ = src.width;
    srcH_ = src.height;
    radius_ = radius;
    offset_ = mapped.offset;

    VkFormatProperties fmt{};
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, &fmt);
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

    const VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    downImages_.resize(passes);
    uint32_t w = srcW_;
    uint32_t h = srcH_;
    for (uint32_t i = 0; i < passes; ++i) {
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
        if (!downImages_[i].create(device_, physical, w, h, VK_FORMAT_R8G8B8A8_UNORM, usage, error)) {
            return false;
        }
    }
    upImages_.resize(passes);
    w = downImages_.back().width;
    h = downImages_.back().height;
    for (uint32_t i = 0; i < passes; ++i) {
        w *= 2;
        h *= 2;
        if (!upImages_[i].create(device_, physical, w, h, VK_FORMAT_R8G8B8A8_UNORM, usage, error)) {
            return false;
        }
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

    VkDescriptorSetLayoutBinding binds[2]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo sl{};
    sl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sl.bindingCount = 2;
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

    if (!createComputePipeline(kDownSpv, sizeof(kDownSpv), &downPipeline_, error)) return false;
    if (!createComputePipeline(kUpSpv, sizeof(kUpSpv), &upPipeline_, error)) return false;

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = passes * 2;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = passes * 2;
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = passes * 2;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = poolSizes;
    KB_TRY(vkCreateDescriptorPool(device_, &pool, nullptr, &descPool_));

    std::vector<VkDescriptorSetLayout> layouts(passes, setLayout_);
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = descPool_;
    alloc.descriptorSetCount = passes;
    alloc.pSetLayouts = layouts.data();
    downSets_.resize(passes);
    KB_TRY(vkAllocateDescriptorSets(device_, &alloc, downSets_.data()));
    upSets_.resize(passes);
    KB_TRY(vkAllocateDescriptorSets(device_, &alloc, upSets_.data()));

    for (uint32_t i = 0; i < passes; ++i) {
        const VulkanImage& in = (i == 0) ? src : downImages_[i - 1];
        writeSet(downSets_[i], in.view, downImages_[i].view);
    }
    for (uint32_t i = 0; i < passes; ++i) {
        const VulkanImage& in = (i == 0) ? downImages_.back() : upImages_[i - 1];
        writeSet(upSets_[i], in.view, upImages_[i].view);
    }
    LOGI("kawase radius=%.1f passes=%u offset=%.2f", radius_, passes, offset_);
    return true;
}

bool KawaseBlur::setRadius(float radius, const VulkanImage& src, std::string* error) {
    if (device_ == VK_NULL_HANDLE) {
        if (error) *error = "KawaseBlur::setRadius before create";
        return false;
    }
    const KawaseMapped mapped = mapRadius(radius, std::min(src.width, src.height));
    if (mapped.passes == downImages_.size() && src.width == srcW_ && src.height == srcH_) {
        radius_ = radius;
        offset_ = mapped.offset;
        return true;
    }
    return create(device_, physical_, src, radius, cmdBarrier2_, error);
}

bool KawaseBlur::execute(VkCommandBuffer cmd, VkQueue queue, std::string* error) {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    KB_TRY(vkBeginCommandBuffer(cmd, &begin));

    const uint32_t passes = static_cast<uint32_t>(downImages_.size());
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, downPipeline_);
    for (uint32_t i = 0; i < passes; ++i) {
        const uint32_t srcW = (i == 0) ? srcW_ : downImages_[i - 1].width;
        const uint32_t srcH = (i == 0) ? srcH_ : downImages_[i - 1].height;
        VulkanImage& dst = downImages_[i];

        imageBarrier(cmd, cmdBarrier2_, dst.image,
                     VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                     VK_IMAGE_LAYOUT_GENERAL);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1,
                                &downSets_[i], 0, nullptr);
        KawasePush push{};
        push.texelX = 1.0f / static_cast<float>(srcW);
        push.texelY = 1.0f / static_cast<float>(srcH);
        push.offset = offset_;
        push.intensity = 1.0f;
        push.resX = static_cast<float>(dst.width);
        push.resY = static_cast<float>(dst.height);
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, (dst.width + 15u) / 16u, (dst.height + 15u) / 16u, 1);

        imageBarrier(cmd, cmdBarrier2_, dst.image,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                     VK_IMAGE_LAYOUT_GENERAL,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        LOGI("kawase down[%u] %ux%u -> %ux%u", i, srcW, srcH, dst.width, dst.height);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, upPipeline_);
    for (uint32_t i = 0; i < passes; ++i) {
        const VulkanImage& src = (i == 0) ? downImages_.back() : upImages_[i - 1];
        VulkanImage& dst = upImages_[i];

        imageBarrier(cmd, cmdBarrier2_, dst.image,
                     VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                     VK_IMAGE_LAYOUT_GENERAL);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1,
                                &upSets_[i], 0, nullptr);
        KawasePush push{};
        push.texelX = 1.0f / static_cast<float>(src.width);
        push.texelY = 1.0f / static_cast<float>(src.height);
        push.offset = offset_;
        push.intensity = 1.0f;
        push.resX = static_cast<float>(dst.width);
        push.resY = static_cast<float>(dst.height);
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, (dst.width + 15u) / 16u, (dst.height + 15u) / 16u, 1);

        if (i + 1 < passes) {
            imageBarrier(cmd, cmdBarrier2_, dst.image,
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                         VK_IMAGE_LAYOUT_GENERAL,
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        } else {
            imageBarrier(cmd, cmdBarrier2_, dst.image,
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                         VK_IMAGE_LAYOUT_GENERAL,
                         VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        }
        LOGI("kawase up[%u] %ux%u -> %ux%u", i, src.width, src.height, dst.width, dst.height);
    }

    KB_TRY(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    KB_TRY(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    KB_TRY(vkQueueWaitIdle(queue));
    vkResetCommandBuffer(cmd, 0);
    return true;
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
    return s;
}
