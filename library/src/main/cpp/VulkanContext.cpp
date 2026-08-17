#include "VulkanContext.h"
#include "VulkanBuffer.h"

#include <android/log.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VulkanBlur", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "VulkanBlur", __VA_ARGS__)

namespace {

const char* vkResultName(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        default: return "VK_ERROR";
    }
}

std::string versionString(uint32_t v) {
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." +
           std::to_string(VK_API_VERSION_MINOR(v)) + "." +
           std::to_string(VK_API_VERSION_PATCH(v));
}

const char* deviceTypeName(VkPhysicalDeviceType t) {
    switch (t) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
        default: return "other";
    }
}

int deviceTypeRank(VkPhysicalDeviceType t) {
    switch (t) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 4;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2;
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return 1;
        default: return 0;
    }
}

std::string queueFlagString(VkQueueFlags flags) {
    std::string s;
    if (flags & VK_QUEUE_GRAPHICS_BIT) s += "GRAPHICS|";
    if (flags & VK_QUEUE_COMPUTE_BIT) s += "COMPUTE|";
    if (flags & VK_QUEUE_TRANSFER_BIT) s += "TRANSFER|";
    if (s.empty()) return "none";
    s.pop_back();
    return s;
}

const char* formatName(VkFormat f) {
    switch (f) {
        case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_SRGB: return "R8G8B8A8_SRGB";
        case VK_FORMAT_B8G8R8A8_SRGB: return "B8G8R8A8_SRGB";
        default: return "other";
    }
}

const char* presentModeName(VkPresentModeKHR m) {
    switch (m) {
        case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
        case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
        default: return "other";
    }
}

constexpr uint32_t kTestCell = 32;
// ponytail: cap work edge so rotate doesn't re-upload a full 4K checker every time.
constexpr uint32_t kMaxWorkEdge = 1280;

VkExtent2D workExtentFor(VkExtent2D swap) {
    if (swap.width == 0 || swap.height == 0) return {256, 256};
    uint32_t w = swap.width;
    uint32_t h = swap.height;
    const uint32_t m = std::max(w, h);
    if (m > kMaxWorkEdge) {
        w = std::max(1u, (w * kMaxWorkEdge) / m);
        h = std::max(1u, (h * kMaxWorkEdge) / m);
    }
    return {w, h};
}

std::vector<VkLayerProperties> instanceLayers() {
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> layers(n);
    if (n) vkEnumerateInstanceLayerProperties(&n, layers.data());
    return layers;
}

std::vector<VkExtensionProperties> instanceExtensions() {
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    if (n) vkEnumerateInstanceExtensionProperties(nullptr, &n, exts.data());
    return exts;
}

std::vector<VkExtensionProperties> deviceExtensions(VkPhysicalDevice pd) {
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    if (n) vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, exts.data());
    return exts;
}

bool hasLayer(const std::vector<VkLayerProperties>& layers, const char* name) {
    return std::any_of(layers.begin(), layers.end(),
                       [&](const VkLayerProperties& p) { return std::strcmp(p.layerName, name) == 0; });
}

bool hasExt(const std::vector<VkExtensionProperties>& exts, const char* name) {
    return std::any_of(exts.begin(), exts.end(),
                       [&](const VkExtensionProperties& p) { return std::strcmp(p.extensionName, name) == 0; });
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void*) {
    int level = ANDROID_LOG_INFO;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        level = ANDROID_LOG_ERROR;
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        level = ANDROID_LOG_WARN;
    }
    __android_log_print(level, "VulkanValidation", "%s", data && data->pMessage ? data->pMessage : "");
    return VK_FALSE;
}

}  // namespace

#define VK_TRY(expr)                                                       \
    do {                                                                   \
        VkResult _r = (expr);                                              \
        if (_r != VK_SUCCESS) {                                            \
            error_ = std::string(#expr) + " -> " + vkResultName(_r);       \
            LOGE("%s", error_.c_str());                                    \
            return false;                                                  \
        }                                                                  \
    } while (0)

VulkanContext* VulkanContext::create(ANativeWindow* window, bool enableValidation, std::string* error) {
    auto* ctx = new VulkanContext();
    if (!ctx->init(window, enableValidation)) {
        if (error) *error = ctx->error_;
        delete ctx;
        return nullptr;
    }
    return ctx;
}

VulkanContext::~VulkanContext() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    blur_.destroy();
    testImage_.destroy();
    destroySwapchain();
    if (acquireSem_ != VK_NULL_HANDLE) vkDestroySemaphore(device_, acquireSem_, nullptr);
    if (presentSem_ != VK_NULL_HANDLE) vkDestroySemaphore(device_, presentSem_, nullptr);
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    }
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    if (debugMessenger_ != VK_NULL_HANDLE) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance_, debugMessenger_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
}

bool VulkanContext::init(ANativeWindow* window, bool enableValidation) {
    window_ = window;
    if (!createInstance(enableValidation)) return false;
    if (validation_ && !createDebugMessenger()) return false;
    if (!createSurface()) return false;
    if (!pickDevice()) return false;
    if (!createDevice()) return false;
    if (!createCommandPool()) return false;
    if (!createSyncObjects()) return false;
    if (!createSwapchain()) return false;
    if (swapchainExtent_.width > 0 && swapchainExtent_.height > 0) {
        if (!ensureWorkingResources()) return false;
        if (!presentTest()) return false;
    }
    LOGI("%s", info().c_str());
    return true;
}

bool VulkanContext::createInstance(bool enableValidation) {
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&loaderVersion);
    instanceApi_ = loaderVersion;
    if (VK_API_VERSION_MAJOR(instanceApi_) > 1 ||
        (VK_API_VERSION_MAJOR(instanceApi_) == 1 && VK_API_VERSION_MINOR(instanceApi_) > 3)) {
        instanceApi_ = VK_API_VERSION_1_3;
    }
    if (instanceApi_ < VK_API_VERSION_1_1) {
        error_ = "Vulkan 1.1 loader required, got " + versionString(loaderVersion);
        LOGE("%s", error_.c_str());
        return false;
    }

    const auto layers = instanceLayers();
    const auto exts = instanceExtensions();
    std::vector<const char*> enabledLayers;
    std::vector<const char*> enabledExts;

    if (!hasExt(exts, VK_KHR_SURFACE_EXTENSION_NAME) ||
        !hasExt(exts, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME)) {
        error_ = "VK_KHR_surface + VK_KHR_android_surface are required";
        LOGE("%s", error_.c_str());
        return false;
    }
    enabledExts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    enabledExts.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);

    if (enableValidation && hasLayer(layers, "VK_LAYER_KHRONOS_validation")) {
        enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
        validation_ = true;
        if (hasExt(exts, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            enabledExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
    } else if (enableValidation) {
        LOGI("validation requested but VK_LAYER_KHRONOS_validation is not installed");
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "VulkanBlur";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "VulkanBlur";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = instanceApi_;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
    ci.ppEnabledLayerNames = enabledLayers.empty() ? nullptr : enabledLayers.data();
    ci.enabledExtensionCount = static_cast<uint32_t>(enabledExts.size());
    ci.ppEnabledExtensionNames = enabledExts.data();
    VK_TRY(vkCreateInstance(&ci, nullptr, &instance_));
    return true;
}

bool VulkanContext::createDebugMessenger() {
    auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (!create) {
        LOGI("VK_EXT_debug_utils loaded without messenger entry points");
        return true;
    }
    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debugCallback;
    VK_TRY(create(instance_, &ci, nullptr, &debugMessenger_));
    return true;
}

bool VulkanContext::createSurface() {
    VkAndroidSurfaceCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    ci.window = window_;
    VK_TRY(vkCreateAndroidSurfaceKHR(instance_, &ci, nullptr, &surface_));
    return true;
}

bool VulkanContext::pickDevice() {
    uint32_t count = 0;
    VK_TRY(vkEnumeratePhysicalDevices(instance_, &count, nullptr));
    if (count == 0) {
        error_ = "no VkPhysicalDevice";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    VK_TRY(vkEnumeratePhysicalDevices(instance_, &count, devices.data()));

    VkPhysicalDevice best = VK_NULL_HANDLE;
    uint32_t bestFamily = 0;
    VkQueueFlags bestFlags = 0;
    VkPhysicalDeviceProperties bestProps{};
    int bestRank = -1;
    uint32_t bestApi = 0;

    for (VkPhysicalDevice pd : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(pd, &props);
        if (props.apiVersion < VK_API_VERSION_1_1) continue;

        const auto exts = deviceExtensions(pd);
        if (!hasExt(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;

        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qprops.data());

        for (uint32_t i = 0; i < qn; ++i) {
            const VkQueueFlags flags = qprops[i].queueFlags;
            if ((flags & VK_QUEUE_COMPUTE_BIT) == 0) continue;
            if ((flags & VK_QUEUE_GRAPHICS_BIT) == 0) continue;
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &present);
            if (!present) continue;

            const int rank = deviceTypeRank(props.deviceType);
            if (rank > bestRank || (rank == bestRank && props.apiVersion > bestApi)) {
                best = pd;
                bestFamily = i;
                bestFlags = flags;
                bestProps = props;
                bestRank = rank;
                bestApi = props.apiVersion;
            }
            break;
        }
    }

    if (best == VK_NULL_HANDLE) {
        error_ = "no GPU with GRAPHICS+COMPUTE+present+swapchain";
        LOGE("%s", error_.c_str());
        return false;
    }

    physical_ = best;
    queueFamily_ = bestFamily;
    queueFlags_ = bestFlags;
    props_ = bestProps;
    LOGI("selected %s (%s) family=%u", props_.deviceName, deviceTypeName(props_.deviceType), queueFamily_);
    return true;
}

bool VulkanContext::createDevice() {
    const auto exts = deviceExtensions(physical_);
    std::vector<const char*> enabled;
    enabled.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    const bool vulkan13 = props_.apiVersion >= VK_API_VERSION_1_3;
    const bool sync2Ext = hasExt(exts, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);

    VkPhysicalDeviceSynchronization2Features sync2Available{};
    sync2Available.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &sync2Available;
    vkGetPhysicalDeviceFeatures2(physical_, &features2);

    VkPhysicalDeviceSynchronization2Features sync2{};
    sync2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    sync2.synchronization2 = VK_TRUE;

    void* pNext = nullptr;
    if (sync2Available.synchronization2 && (vulkan13 || sync2Ext)) {
        if (!vulkan13) enabled.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        pNext = &sync2;
        sync2_ = true;
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = queueFamily_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pNext = pNext;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qci;
    ci.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
    ci.ppEnabledExtensionNames = enabled.data();
    VK_TRY(vkCreateDevice(physical_, &ci, nullptr, &device_));
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    if (sync2_) {
        cmdBarrier2_ = reinterpret_cast<PFN_vkCmdPipelineBarrier2>(
                vkGetDeviceProcAddr(device_, "vkCmdPipelineBarrier2"));
        if (!cmdBarrier2_) {
            cmdBarrier2_ = reinterpret_cast<PFN_vkCmdPipelineBarrier2>(
                    vkGetDeviceProcAddr(device_, "vkCmdPipelineBarrier2KHR"));
        }
    }
    return true;
}

bool VulkanContext::createCommandPool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = queueFamily_;
    VK_TRY(vkCreateCommandPool(device_, &ci, nullptr, &commandPool_));
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = commandPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VK_TRY(vkAllocateCommandBuffers(device_, &ai, &cmd_));
    return true;
}

bool VulkanContext::createSyncObjects() {
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_TRY(vkCreateSemaphore(device_, &si, nullptr, &acquireSem_));
    VK_TRY(vkCreateSemaphore(device_, &si, nullptr, &presentSem_));
    return true;
}

void VulkanContext::barrierImage(VkCommandBuffer cmd, VkImage image,
                                 VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkImageLayout oldLayout,
                                 VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess, VkImageLayout newLayout) {
    imageBarrier(cmd, cmdBarrier2_, image, srcStage, srcAccess, oldLayout, dstStage, dstAccess, newLayout);
}

bool VulkanContext::createTestTexture(uint32_t w, uint32_t h) {
    return testImage_.create(
            device_, physical_, w, h, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &error_);
}

bool VulkanContext::ensureWorkingResources() {
    const VkExtent2D work = workExtentFor(swapchainExtent_);
    const bool sizeChanged = testImage_.image == VK_NULL_HANDLE ||
                             testImage_.width != work.width || testImage_.height != work.height;
    if (sizeChanged) {
        if (!createTestTexture(work.width, work.height)) return false;
        if (!uploadTestTexture()) return false;
        if (blur_.passes() == 0) {
            if (!blur_.create(device_, physical_, testImage_, radius_, cmdBarrier2_, &error_)) {
                return false;
            }
        } else if (!blur_.resize(testImage_, &error_)) {
            return false;
        }
        if (!blur_.execute(cmd_, queue_, &error_)) return false;
    } else if (blur_.passes() == 0) {
        if (!blur_.create(device_, physical_, testImage_, radius_, cmdBarrier2_, &error_)) {
            return false;
        }
        if (!blur_.execute(cmd_, queue_, &error_)) return false;
    }
    return true;
}

bool VulkanContext::uploadTestTexture() {
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(testImage_.width) * testImage_.height * 4;
    VulkanBuffer staging;
    if (!staging.create(device_, physical_, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &error_)) {
        return false;
    }
    auto* pixels = static_cast<uint8_t*>(staging.map(&error_));
    if (!pixels) {
        staging.destroy();
        return false;
    }
    for (uint32_t y = 0; y < testImage_.height; ++y) {
        for (uint32_t x = 0; x < testImage_.width; ++x) {
            const bool on = ((x / kTestCell) + (y / kTestCell)) % 2 == 0;
            uint8_t* p = pixels + (static_cast<size_t>(y) * testImage_.width + x) * 4;
            if (on) {
                p[0] = 255;
                p[1] = 64;
                p[2] = 160;
                p[3] = 255;
            } else {
                p[0] = 32;
                p[1] = 200;
                p[2] = 220;
                p[3] = 255;
            }
        }
    }
    staging.unmap();

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_TRY(vkBeginCommandBuffer(cmd_, &begin));

    // First use of the test image: discard UNDEFINED, make it a copy destination.
    // src NONE: no prior GPU work. dst COPY+TRANSFER_WRITE: vkCmdCopyBufferToImage writes.
    barrierImage(cmd_, testImage_.image,
                 VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {testImage_.width, testImage_.height, 1};
    vkCmdCopyBufferToImage(cmd_, staging.buffer, testImage_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Upload finished. Next pass samples this image with a linear sampler.
    // src COPY+TRANSFER_WRITE: the copy above.
    // dst COMPUTE+SHADER_SAMPLED_READ: textureLod in kawase_down.comp.
    barrierImage(cmd_, testImage_.image,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VK_TRY(vkEndCommandBuffer(cmd_));
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_;
    VK_TRY(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE));
    VK_TRY(vkQueueWaitIdle(queue_));
    staging.destroy();
    vkResetCommandBuffer(cmd_, 0);
    return true;
}

bool VulkanContext::createSwapchain() {
    destroySwapchain();

    VkSurfaceCapabilitiesKHR caps{};
    VK_TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps));
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
        error_ = "swapchain does not support TRANSFER_DST (needed to blit the test image)";
        LOGE("%s", error_.c_str());
        return false;
    }

    uint32_t formatCount = 0;
    VK_TRY(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VK_TRY(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, formats.data()));
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    if (chosen.format != VK_FORMAT_R8G8B8A8_UNORM) {
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM) {
                chosen = f;
                break;
            }
        }
    }
    swapchainFormat_ = chosen.format;
    swapchainColorSpace_ = chosen.colorSpace;

    uint32_t modeCount = 0;
    VK_TRY(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &modeCount, nullptr));
    std::vector<VkPresentModeKHR> modes(modeCount);
    VK_TRY(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &modeCount, modes.data()));
    presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
            presentMode_ = m;
            break;
        }
    }

    if (caps.currentExtent.width != UINT32_MAX) {
        swapchainExtent_ = caps.currentExtent;
    } else {
        uint32_t w = static_cast<uint32_t>(ANativeWindow_getWidth(window_));
        uint32_t h = static_cast<uint32_t>(ANativeWindow_getHeight(window_));
        w = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, w));
        h = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, h));
        swapchainExtent_ = {w, h};
    }
    if (swapchainExtent_.width == 0 || swapchainExtent_.height == 0) {
        LOGI("swapchain extent 0x0, deferring");
        return true;
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    const VkCompositeAlphaFlagBitsKHR kComposite[] = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
    };
    for (auto c : kComposite) {
        if (caps.supportedCompositeAlpha & c) {
            composite = c;
            break;
        }
    }

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface_;
    ci.minImageCount = imageCount;
    ci.imageFormat = swapchainFormat_;
    ci.imageColorSpace = swapchainColorSpace_;
    ci.imageExtent = swapchainExtent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage = usage;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = composite;
    ci.presentMode = presentMode_;
    ci.clipped = VK_TRUE;
    VK_TRY(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_));

    uint32_t n = 0;
    VK_TRY(vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr));
    swapchainImages_.resize(n);
    VK_TRY(vkGetSwapchainImagesKHR(device_, swapchain_, &n, swapchainImages_.data()));
    LOGI("swapchain %ux%u %s %s images=%u", swapchainExtent_.width, swapchainExtent_.height,
         formatName(swapchainFormat_), presentModeName(presentMode_), n);
    return true;
}

void VulkanContext::destroySwapchain() {
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    swapchainImages_.clear();
    swapchainExtent_ = {};
}

bool VulkanContext::presentTest() {
    if (swapchain_ == VK_NULL_HANDLE || swapchainImages_.empty()) return true;

    for (int attempt = 0; attempt < 2; ++attempt) {
        uint32_t index = 0;
        VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, acquireSem_, VK_NULL_HANDLE, &index);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDeviceWaitIdle(device_);
            if (!createSwapchain()) return false;
            if (swapchain_ == VK_NULL_HANDLE || swapchainImages_.empty()) return true;
            if (!ensureWorkingResources()) return false;
            continue;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
            error_ = std::string("vkAcquireNextImageKHR -> ") + vkResultName(acquired);
            LOGE("%s", error_.c_str());
            return false;
        }

        VK_TRY(vkResetCommandBuffer(cmd_, 0));
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_TRY(vkBeginCommandBuffer(cmd_, &begin));

        // Acquired swapchain image: contents discarded. Make it a blit destination.
        // Acquire semaphore (submit wait) covers GPU availability. Layout is UNDEFINED until we transition.
        // dst BLIT+TRANSFER_WRITE: vkCmdBlitImage writes the swapchain image.
        barrierImage(cmd_, swapchainImages_[index],
                     VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1;
        const VulkanImage& src = blur_.output();
        blit.srcOffsets[1] = {static_cast<int32_t>(src.width), static_cast<int32_t>(src.height), 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[1] = {static_cast<int32_t>(swapchainExtent_.width), static_cast<int32_t>(swapchainExtent_.height), 1};
        vkCmdBlitImage(cmd_, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swapchainImages_[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_NEAREST);

        // Blit done. Present requires PRESENT_SRC_KHR. dst NONE: no later GPU work in this submit;
        // the present semaphore is signaled when this command buffer completes.
        barrierImage(cmd_, swapchainImages_[index],
                     VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        VK_TRY(vkEndCommandBuffer(cmd_));

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &acquireSem_;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd_;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &presentSem_;
        VK_TRY(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE));

        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &presentSem_;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &index;
        VkResult presented = vkQueuePresentKHR(queue_, &present);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
            VK_TRY(vkQueueWaitIdle(queue_));
            vkDeviceWaitIdle(device_);
            if (!createSwapchain()) return false;
            if (!ensureWorkingResources()) return false;
            continue;
        }
        if (presented != VK_SUCCESS) {
            error_ = std::string("vkQueuePresentKHR -> ") + vkResultName(presented);
            LOGE("%s", error_.c_str());
            return false;
        }
        // ponytail: QueueWaitIdle after the test present; in-flight fences when we have a frame loop.
        VK_TRY(vkQueueWaitIdle(queue_));
        return true;
    }
    return true;
}

bool VulkanContext::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return true;
    const bool extentSame = swapchain_ != VK_NULL_HANDLE &&
                            width == swapchainExtent_.width && height == swapchainExtent_.height;
    if (!extentSame) {
        vkDeviceWaitIdle(device_);
        if (!createSwapchain()) return false;
    }
    if (swapchainExtent_.width == 0 || swapchainExtent_.height == 0) return true;
    // Always re-check work size: present OUT_OF_DATE can update swapchain before surfaceChanged.
    if (!ensureWorkingResources()) return false;
    return presentTest();
}

void VulkanContext::releaseSurface() {
    if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
    destroySwapchain();
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
}

bool VulkanContext::setSurface(ANativeWindow* window) {
    if (!window) {
        error_ = "setSurface: null window";
        return false;
    }
    releaseSurface();
    window_ = window;
    if (!createSurface()) return false;
    if (!createSwapchain()) return false;
    if (swapchainExtent_.width == 0 || swapchainExtent_.height == 0) return true;
    if (!ensureWorkingResources()) return false;
    return presentTest();
}

bool VulkanContext::setRadius(float radius) {
    radius_ = radius;
    if (radius == blur_.radius() && blur_.passes() > 0) return true;
    if (testImage_.image == VK_NULL_HANDLE) {
        return true;  // applied on next ensureWorkingResources / surface
    }
    vkDeviceWaitIdle(device_);
    if (!blur_.setRadius(radius, testImage_, &error_)) return false;
    if (!blur_.execute(cmd_, queue_, &error_)) return false;
    return presentTest();
}

std::string VulkanContext::info() const {
    const int w = window_ ? ANativeWindow_getWidth(window_) : 0;
    const int h = window_ ? ANativeWindow_getHeight(window_) : 0;
    const bool timestamps = props_.limits.timestampComputeAndGraphics == VK_TRUE &&
                            props_.limits.timestampPeriod > 0.0f;
    std::string s;
    s += "VulkanBlur Phase 9\n";
    s += "status=resize\n";
    s += "device=";
    s += props_.deviceName;
    s += "\n";
    s += "type=";
    s += deviceTypeName(props_.deviceType);
    s += "\n";
    s += "api=";
    s += versionString(props_.apiVersion);
    s += "\n";
    s += "loader=";
    s += versionString(instanceApi_);
    s += "\n";
    s += "queueFamily=";
    s += std::to_string(queueFamily_);
    s += "\n";
    s += "queueFlags=";
    s += queueFlagString(queueFlags_);
    s += "\n";
    s += "present=yes\n";
    s += "sync2=";
    s += sync2_ ? "yes" : "no";
    s += "\n";
    s += "timestamps=";
    s += timestamps ? "yes" : "no";
    s += "\n";
    s += "validation=";
    s += validation_ ? "yes" : "no";
    s += "\n";
    s += "surface=";
    s += std::to_string(w);
    s += "x";
    s += std::to_string(h);
    s += "\n";
    s += "swapchain=";
    s += std::to_string(swapchainExtent_.width);
    s += "x";
    s += std::to_string(swapchainExtent_.height);
    s += " ";
    s += formatName(swapchainFormat_);
    s += " ";
    s += presentModeName(presentMode_);
    s += "\n";
    s += "testImage=";
    s += std::to_string(testImage_.width);
    s += "x";
    s += std::to_string(testImage_.height);
    s += " R8G8B8A8_UNORM checker\n";
    s += "radius=";
    s += std::to_string(blur_.radius());
    s += "\n";
    s += "passes=";
    s += std::to_string(blur_.passes());
    s += "\n";
    s += "offset=";
    s += std::to_string(blur_.offset());
    s += "\n";
    s += "pyramid=";
    s += blur_.pyramidInfo();
    s += "\n";
    return s;
}
