#include "GlassPass.h"

#include <android/log.h>
#include <cstdint>
#include <string>

static const uint32_t kGlassSpv[] =
#include "glass_comp_spv.inc"
        ;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VulkanBlur", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "VulkanBlur", __VA_ARGS__)

namespace {

struct GlassPush {
    float resX, resY;
    float tintStrength;
    float rimStrength;
};
static_assert(sizeof(GlassPush) == 16, "glass push layout");

constexpr float kTint = 0.35f;
constexpr float kRim = 0.45f;

#define GP_TRY(expr)                                                       \
    do {                                                                   \
        VkResult _r = (expr);                                              \
        if (_r != VK_SUCCESS) {                                            \
            if (error) *error = std::string(#expr) + " failed";            \
            LOGE("%s", error ? error->c_str() : #expr);                    \
            return false;                                                  \
        }                                                                  \
    } while (0)

}  // namespace

void GlassPass::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    output_.destroy();
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (descPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descPool_, nullptr);
    if (setLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, sampler_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    descPool_ = VK_NULL_HANDLE;
    descSet_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    cmdBarrier2_ = nullptr;
    physical_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

bool GlassPass::ensurePipeline(std::string* error) {
    if (pipeline_ != VK_NULL_HANDLE) return true;

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 0.0f;
    GP_TRY(vkCreateSampler(device_, &sci, nullptr, &sampler_));

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
    GP_TRY(vkCreateDescriptorSetLayout(device_, &sl, nullptr, &setLayout_));

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.size = sizeof(GlassPush);
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &setLayout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pc;
    GP_TRY(vkCreatePipelineLayout(device_, &pl, nullptr, &pipelineLayout_));

    VkShaderModuleCreateInfo sm{};
    sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sm.codeSize = sizeof(kGlassSpv);
    sm.pCode = kGlassSpv;
    VkShaderModule module = VK_NULL_HANDLE;
    GP_TRY(vkCreateShaderModule(device_, &sm, nullptr, &module));
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    VkComputePipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage = stage;
    ci.layout = pipelineLayout_;
    VkResult pipe = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline_);
    vkDestroyShaderModule(device_, module, nullptr);
    if (pipe != VK_SUCCESS) {
        if (error) *error = "vkCreateComputePipelines(glass) failed";
        LOGE("vkCreateComputePipelines(glass) failed");
        return false;
    }

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = poolSizes;
    GP_TRY(vkCreateDescriptorPool(device_, &pool, nullptr, &descPool_));

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = descPool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &setLayout_;
    GP_TRY(vkAllocateDescriptorSets(device_, &alloc, &descSet_));
    return true;
}

void GlassPass::writeSet(VkImageView src) {
    VkDescriptorImageInfo images[2]{};
    images[0].sampler = sampler_;
    images[0].imageView = src;
    images[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    images[1].imageView = output_.view;
    images[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descSet_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &images[0];
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descSet_;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &images[1];
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
}

bool GlassPass::rebuildOutput(uint32_t w, uint32_t h, std::string* error) {
    if (!output_.create(device_, physical_, w, h, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, error)) {
        return false;
    }
    return true;
}

bool GlassPass::create(VkDevice device, VkPhysicalDevice physical, uint32_t w, uint32_t h,
                       PFN_vkCmdPipelineBarrier2 cmdBarrier2, std::string* error) {
    destroy();
    device_ = device;
    physical_ = physical;
    cmdBarrier2_ = cmdBarrier2;
    if (!ensurePipeline(error)) return false;
    if (!rebuildOutput(w, h, error)) return false;
    LOGI("glass pass %ux%u", w, h);
    return true;
}

bool GlassPass::resize(uint32_t w, uint32_t h, std::string* error) {
    if (device_ == VK_NULL_HANDLE) {
        if (error) *error = "GlassPass::resize before create";
        return false;
    }
    if (output_.width == w && output_.height == h) return true;
    return rebuildOutput(w, h, error);
}

bool GlassPass::execute(VkCommandBuffer cmd, VkQueue queue, const VulkanImage& blurred,
                        std::string* error) {
    if (pipeline_ == VK_NULL_HANDLE || output_.image == VK_NULL_HANDLE) {
        if (error) *error = "GlassPass::execute before create";
        return false;
    }
    writeSet(blurred.view);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    GP_TRY(vkBeginCommandBuffer(cmd, &begin));

    // Blur output was left TRANSFER_SRC for present; sample it as a texture instead.
    imageBarrier(cmd, cmdBarrier2_, blurred.image,
                 VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    imageBarrier(cmd, cmdBarrier2_, output_.image,
                 VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                 VK_IMAGE_LAYOUT_GENERAL);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1, &descSet_, 0,
                            nullptr);
    GlassPush push{};
    push.resX = static_cast<float>(output_.width);
    push.resY = static_cast<float>(output_.height);
    push.tintStrength = kTint;
    push.rimStrength = kRim;
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, (output_.width + 15u) / 16u, (output_.height + 15u) / 16u, 1);

    imageBarrier(cmd, cmdBarrier2_, output_.image,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                 VK_IMAGE_LAYOUT_GENERAL,
                 VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    GP_TRY(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    GP_TRY(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    GP_TRY(vkQueueWaitIdle(queue));
    vkResetCommandBuffer(cmd, 0);
    return true;
}
