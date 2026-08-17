#include "VulkanContext.h"

#include <android/log.h>
#include <algorithm>
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
    return true;
}

bool VulkanContext::createCommandPool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = queueFamily_;
    VK_TRY(vkCreateCommandPool(device_, &ci, nullptr, &commandPool_));
    return true;
}

std::string VulkanContext::info() const {
    const int w = window_ ? ANativeWindow_getWidth(window_) : 0;
    const int h = window_ ? ANativeWindow_getHeight(window_) : 0;
    const bool timestamps = props_.limits.timestampComputeAndGraphics == VK_TRUE &&
                            props_.limits.timestampPeriod > 0.0f;
    std::string s;
    s += "VulkanBlur Phase 1\n";
    s += "status=initialized\n";
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
    return s;
}
