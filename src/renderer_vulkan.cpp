#include "renderer_vulkan.hpp"
#include "utf.hpp"
#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#else
#define VK_USE_PLATFORM_WAYLAND_KHR
#define VK_USE_PLATFORM_XCB_KHR
#include <X11/Xlib-xcb.h>
#include <dlfcn.h>
#endif
#include <vulkan/vulkan.h>
#include <cstring>
#include <atomic>
#ifdef ISODUO_TEST_VULKAN_VALIDATION
#include <cstdio>
#include <cstdlib>
#endif
#include <limits>
#include <algorithm>
#include <string_view>
#include <utility>

namespace isoduo::detail {
namespace {
using Kind = StartupFailureKind;
std::string resultName(VkResult result) {
    switch (result) {
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
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
    case VK_ERROR_VALIDATION_FAILED: return "VK_ERROR_VALIDATION_FAILED";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
    case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
    case VK_PIPELINE_COMPILE_REQUIRED: return "VK_PIPELINE_COMPILE_REQUIRED";
    case VK_ERROR_NOT_PERMITTED: return "VK_ERROR_NOT_PERMITTED";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
    case VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR: return "VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR";
    case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT: return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
    case VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT: return "VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT";
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT: return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
    case VK_THREAD_IDLE_KHR: return "VK_THREAD_IDLE_KHR";
    case VK_THREAD_DONE_KHR: return "VK_THREAD_DONE_KHR";
    case VK_OPERATION_DEFERRED_KHR: return "VK_OPERATION_DEFERRED_KHR";
    case VK_OPERATION_NOT_DEFERRED_KHR: return "VK_OPERATION_NOT_DEFERRED_KHR";
    case VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR: return "VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR";
    case VK_ERROR_COMPRESSION_EXHAUSTED_EXT: return "VK_ERROR_COMPRESSION_EXHAUSTED_EXT";
    case VK_INCOMPATIBLE_SHADER_BINARY_EXT: return "VK_INCOMPATIBLE_SHADER_BINARY_EXT";
    case VK_PIPELINE_BINARY_MISSING_KHR: return "VK_PIPELINE_BINARY_MISSING_KHR";
    case VK_ERROR_NOT_ENOUGH_SPACE_KHR: return "VK_ERROR_NOT_ENOUGH_SPACE_KHR";
    default: return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
    }
}
[[noreturn]] void unavailable(const char* reason) {
    throw StartupError{{Kind::renderBackendUnavailable, {}, {}, utf16FromUtf8(reason)}};
}
void check(const char* call, VkResult result) {
    if (result == VK_SUCCESS) return;
    const auto kind = result == VK_ERROR_INCOMPATIBLE_DRIVER ? Kind::renderBackendUnavailable : Kind::contextCreation;
    throw StartupError{{kind, {}, {}, utf16FromUtf8(std::string(call) + ": " + resultName(result))}};
}
template<class Call> VkResult invoke(RendererTestHooks* hooks, const char* name, Call call) {
    if (hooks && hooks->startupResultOnce && hooks->startupResultOnce->first == name) {
        int result = hooks->startupResultOnce->second;
        hooks->startupResultOnce.reset();
        if (result) return static_cast<VkResult>(result);
    }
    return call();
}
template<class T, class Query> std::vector<T> enumerate(const char* name, Query query) {
    for (;;) {
        uint32_t count = 0;
        check(name, query(&count, nullptr));
        std::vector<T> entries(count);
        if (count == 0) return entries;
        auto result = query(&count, entries.data());
        if (result == VK_INCOMPLETE) continue;
        check(name, result);
        entries.resize(count);
        return entries;
    }
}
}
struct VulkanSession {
    DebugContext debug = DebugContext::buildDefault;
    std::function<void(RenderDiagnostic)> sink;
    std::atomic<bool> testingScissorCount = false;
    bool validationEnabled = false;
    VkPhysicalDevice previousDevice{};
    bool surfaceLost = false;
#ifdef _WIN32
    HMODULE library = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
    void* library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
    bool debugUtils = false;
    PFN_vkGetInstanceProcAddr lookup{};
    VkInstance instance{};
    VkDebugUtilsMessengerEXT validationMessenger{};
    PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger{};
    VkSurfaceKHR surface{};
    VkDevice device{};
    PFN_vkDestroyDevice destroyDevice{};
    PFN_vkDestroyInstance destroyInstance{};
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices{};
    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties{};
    PFN_vkGetPhysicalDeviceFeatures2 getPhysicalDeviceFeatures2{};
    PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensionProperties{};
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getPhysicalDeviceQueueFamilyProperties{};
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR getPhysicalDeviceSurfaceSupportKHR{};
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR getPhysicalDeviceSurfaceFormatsKHR{};
    PFN_vkDestroySurfaceKHR destroySurfaceKHR{};
    PFN_vkCreateDevice createDevice{};
    PFN_vkGetDeviceProcAddr getDeviceProcAddr{};
    ~VulkanSession() {
        if (device) destroyDevice(device, nullptr);
        if (surface) destroySurfaceKHR(instance, surface, nullptr);
        if (validationMessenger) destroyMessenger(instance, validationMessenger, nullptr);
        if (instance && destroyInstance) destroyInstance(instance, nullptr);
#ifdef _WIN32
        if (library) FreeLibrary(library);
#else
        if (library) dlclose(library);
#endif
    }
    template<class F> F function(const char* name) {
        auto fn = lookup(instance, name);
        if (!fn) throw StartupError{{Kind::contextCreation, {}, {}, utf16FromUtf8(std::string("Missing Vulkan function: ") + name)}};
        return reinterpret_cast<F>(fn);
    }
    void load() {
        if (!library) unavailable("Vulkan loader unavailable");
#ifdef _WIN32
        lookup = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(library, "vkGetInstanceProcAddr"));
#else
        lookup = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
#endif
        if (!lookup) unavailable("Missing vkGetInstanceProcAddr");
    }
    void loadInstanceFunctions(bool surfaceNeeded) {
        getPhysicalDeviceProperties = function<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties");
        getPhysicalDeviceFeatures2 = function<PFN_vkGetPhysicalDeviceFeatures2>("vkGetPhysicalDeviceFeatures2");
        enumerateDeviceExtensionProperties = function<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
        getPhysicalDeviceQueueFamilyProperties = function<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
        if (surfaceNeeded) {
        getPhysicalDeviceSurfaceSupportKHR = function<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>("vkGetPhysicalDeviceSurfaceSupportKHR");
        getPhysicalDeviceSurfaceFormatsKHR = function<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>("vkGetPhysicalDeviceSurfaceFormatsKHR");
        destroySurfaceKHR = function<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR");
        }
        createDevice = function<PFN_vkCreateDevice>("vkCreateDevice");
        getDeviceProcAddr = function<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
    }
};
namespace {
using Session = VulkanSession;
VKAPI_ATTR VkBool32 VKAPI_CALL diagnosticMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    auto& session = *static_cast<Session*>(user);
    RenderDiagnostic event;
    event.backend = u"Vulkan";
    event.kind = type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT ? DiagnosticKind::apiMisuse :
                 type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT ? DiagnosticKind::performance : DiagnosticKind::other;
    event.severity = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? DiagnosticSeverity::error :
                     severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ? DiagnosticSeverity::warning : DiagnosticSeverity::info;
    event.nativeCode = data->messageIdNumber;
    if (data->pMessage) event.nativeMessage = data->pMessage;
    if (data->pMessageIdName) event.code = data->pMessageIdName;
    event.backendDetail = event.code + " type=" + std::to_string(type);
    if (data->cmdBufLabelCount && data->pCmdBufLabels[0].pLabelName) event.componentId=data->pCmdBufLabels[0].pLabelName;
#ifdef ISODUO_TEST_VULKAN_VALIDATION
    if (event.severity == DiagnosticSeverity::error && !(session.testingScissorCount &&
        event.code == "VUID-vkCmdSetScissor-scissorCount-arraylength")) {
        std::fprintf(stderr,"Vulkan validation error: %s\n",event.nativeMessage.c_str());
        std::fflush(stderr);
        std::_Exit(EXIT_FAILURE);
    }
#endif
    if (session.sink) session.sink(std::move(event));
    return VK_FALSE;
}
StartupFailure missing(std::string required, std::string actual = {}) {
    return {Kind::contextVersion, utf16FromUtf8(required), utf16FromUtf8(actual), utf16FromUtf8("Vulkan requires " + required)};
}
std::string versionString(uint32_t version) {
    return std::to_string(VK_API_VERSION_MAJOR(version)) + "." + std::to_string(VK_API_VERSION_MINOR(version));
}
bool hasExtension(const std::vector<VkExtensionProperties>& available, std::string_view name) {
    return std::any_of(available.begin(), available.end(), [&](const auto& ext){ return ext.extensionName == name; });
}
const char* platformExtension(VulkanWindow window) {
#ifdef _WIN32
    (void)window;
    return VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#else
    return window.backend == DesktopBackendKind::wayland ? VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME : VK_KHR_XCB_SURFACE_EXTENSION_NAME;
#endif
}
std::vector<VkPhysicalDevice> createInstance(Session& s, VulkanWindow window, RendererTestHooks* hooks, bool surfaceNeeded = true) {
    auto getVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(s.lookup(nullptr, "vkEnumerateInstanceVersion"));
    uint32_t version = VK_API_VERSION_1_0;
    if (getVersion) check("vkEnumerateInstanceVersion", getVersion(&version));
    if (version < VK_API_VERSION_1_3) throw StartupError{missing("Vulkan 1.3", versionString(version))};
    std::function<std::remove_pointer_t<PFN_vkEnumerateInstanceExtensionProperties>> query = s.function<PFN_vkEnumerateInstanceExtensionProperties>("vkEnumerateInstanceExtensionProperties");
    if (hooks && hooks->hideDebugUtils) query = [real=query](auto layer, auto count, auto out) {
        auto result=real(layer,count,out);
        if (out && result==VK_SUCCESS) *count=uint32_t(std::remove_if(out,out+*count,[](auto& entry) {
            return std::string_view(entry.extensionName)==VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        })-out);
        return result;
    };
    auto extensions = enumerate<VkExtensionProperties>("vkEnumerateInstanceExtensionProperties",
        [&](uint32_t* count, VkExtensionProperties* out){ return query(nullptr, count, out); });
    const char* required[] = {VK_KHR_SURFACE_EXTENSION_NAME, platformExtension(window)};
    const char* missingExtension = nullptr;
    for (auto name : required) {
        if (surfaceNeeded && (!hasExtension(extensions, name) || (hooks && hooks->hideInstanceExtension))) {
            missingExtension = name;
            break;
        }
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    // An installed loader with no ICD may advertise no surface extensions.
    // Probe without WSI in that case so absence of a driver remains distinguishable.
    std::vector<const char*> enabled;
    if (surfaceNeeded && !missingExtension) enabled.assign(std::begin(required),std::end(required));
    s.debugUtils = hasExtension(extensions,VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (s.debugUtils) enabled.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    info.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
    info.ppEnabledExtensionNames = enabled.data();
    const char* layer = "VK_LAYER_KHRONOS_validation";
    bool requested = s.debug == DebugContext::on;
#ifndef NDEBUG
    requested |= s.debug == DebugContext::buildDefault;
#endif
    if (requested) {
        std::function<std::remove_pointer_t<PFN_vkEnumerateInstanceLayerProperties>> layers =
            s.function<PFN_vkEnumerateInstanceLayerProperties>("vkEnumerateInstanceLayerProperties");
        if (hooks && hooks->hideValidationLayer) layers = [](auto count, auto) { *count=0; return VK_SUCCESS; };
        auto available=enumerate<VkLayerProperties>("vkEnumerateInstanceLayerProperties",layers);
        s.validationEnabled=std::any_of(available.begin(),available.end(),[&](auto& entry){return std::string_view(entry.layerName)==layer;});
        if (s.validationEnabled) {
            info.enabledLayerCount=1;
            info.ppEnabledLayerNames=&layer;
        } else {
#ifdef ISODUO_TEST_VULKAN_VALIDATION
            if (!hooks || !hooks->hideValidationLayer) check("vkCreateInstance",VK_ERROR_LAYER_NOT_PRESENT);
#endif
            if (s.sink) {
                RenderDiagnostic event;
                event.backend=u"Vulkan";
                event.severity=DiagnosticSeverity::info;
                event.nativeMessage="VK_LAYER_KHRONOS_validation is unavailable";
                s.sink(std::move(event));
            }
        }
    }
    VkDebugUtilsMessengerCreateInfoEXT validation{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    validation.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    validation.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
    validation.pfnUserCallback = diagnosticMessage;
    validation.pUserData = &s;
    if (s.debugUtils) info.pNext = &validation;
    auto create = s.function<PFN_vkCreateInstance>("vkCreateInstance");
    check("vkCreateInstance", invoke(hooks, "vkCreateInstance", [&]{ return create(&info, nullptr, &s.instance); }));
    s.destroyInstance = s.function<PFN_vkDestroyInstance>("vkDestroyInstance");
    if (s.debugUtils) {
    s.destroyMessenger = s.function<PFN_vkDestroyDebugUtilsMessengerEXT>("vkDestroyDebugUtilsMessengerEXT");
    auto createMessenger = s.function<PFN_vkCreateDebugUtilsMessengerEXT>("vkCreateDebugUtilsMessengerEXT");
    check("vkCreateDebugUtilsMessengerEXT", createMessenger(s.instance, &validation, nullptr, &s.validationMessenger));
    }
    s.enumeratePhysicalDevices = s.function<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
    auto devices = enumerate<VkPhysicalDevice>("vkEnumeratePhysicalDevices",
        [&](uint32_t* n, VkPhysicalDevice* out){ return s.enumeratePhysicalDevices(s.instance, n, out); });
    if (devices.empty() || (hooks && hooks->hidePhysicalDevices)) unavailable("No Vulkan physical devices");
    if (missingExtension) throw StartupError{missing(missingExtension)};
    s.loadInstanceFunctions(surfaceNeeded);
    return devices;
}
template<class F, class Info> void surfaceCall(Session& s, const char* name, Info& info, RendererTestHooks* hooks) {
    auto create = s.function<F>(name);
    check(name, invoke(hooks, name, [&]{ return create(s.instance, &info, nullptr, &s.surface); }));
}
void createSurface(Session& s, VulkanWindow window, RendererTestHooks* hooks) {
#ifdef _WIN32
    VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    info.hwnd = window;
    info.hinstance = GetModuleHandleW(nullptr);
    surfaceCall<PFN_vkCreateWin32SurfaceKHR>(s, "vkCreateWin32SurfaceKHR", info, hooks);
#else
    if (window.backend == DesktopBackendKind::wayland) {
        VkWaylandSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
        info.display = static_cast<wl_display*>(window.display);
        info.surface = static_cast<wl_surface*>(window.surface);
        surfaceCall<PFN_vkCreateWaylandSurfaceKHR>(s, "vkCreateWaylandSurfaceKHR", info, hooks);
    } else {
        VkXcbSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR};
        info.connection = XGetXCBConnection(static_cast<Display*>(window.display));
        info.window = window.windowId;
        surfaceCall<PFN_vkCreateXcbSurfaceKHR>(s, "vkCreateXcbSurfaceKHR", info, hooks);
    }
#endif
}
struct Selection {
    VkPhysicalDevice physical{};
    std::optional<uint32_t> graphicsQueue, presentQueue;
    VkSurfaceFormatKHR format{};
    uint32_t timestampBits{};
    const char* faultExtension{};
};
std::optional<StartupFailure> inspect(Session& s, Selection& selected, RendererTestHooks* hooks) {
    auto dev = selected.physical;
    VkPhysicalDeviceProperties props{};
    s.getPhysicalDeviceProperties(dev, &props);
    auto version = hooks && hooks->apiVersionOverride ? *hooks->apiVersionOverride : props.apiVersion;
    if (version < VK_API_VERSION_1_3) return missing("Vulkan 1.3", versionString(version));
    VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features};
    s.getPhysicalDeviceFeatures2(dev, &query);
    if (!features.dynamicRendering || (hooks && hooks->hideDynamicRendering)) return missing("dynamicRendering");
    if (!features.synchronization2 || (hooks && hooks->hideSynchronization2)) return missing("synchronization2");
    auto extensions = enumerate<VkExtensionProperties>("vkEnumerateDeviceExtensionProperties",
        [&](uint32_t* n, VkExtensionProperties* out){ return s.enumerateDeviceExtensionProperties(dev, nullptr, n, out); });
    VkPhysicalDeviceFaultFeaturesKHR khr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_KHR};
    VkPhysicalDeviceFaultFeaturesEXT ext{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
    if (hasExtension(extensions,VK_KHR_DEVICE_FAULT_EXTENSION_NAME)) {
        query.pNext=&khr;
        s.getPhysicalDeviceFeatures2(dev,&query);
        if (khr.deviceFault) selected.faultExtension=VK_KHR_DEVICE_FAULT_EXTENSION_NAME;
    }
    if (!selected.faultExtension && hasExtension(extensions,VK_EXT_DEVICE_FAULT_EXTENSION_NAME)) {
        query.pNext=&ext;
        s.getPhysicalDeviceFeatures2(dev,&query);
        if (ext.deviceFault) selected.faultExtension=VK_EXT_DEVICE_FAULT_EXTENSION_NAME;
    }
    if (s.surface && ((hooks && hooks->hideDeviceExtension) || !hasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)))
        return missing(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    uint32_t count{};
    s.getPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    s.getPhysicalDeviceQueueFamilyProperties(dev, &count, queues.data());
    for (uint32_t index = 0; index < count; ++index) {
        VkBool32 present{};
        if (s.surface) check("vkGetPhysicalDeviceSurfaceSupportKHR", s.getPhysicalDeviceSurfaceSupportKHR(dev, index, s.surface, &present));
        bool graphics = (queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        if (graphics && present) {
            selected.graphicsQueue = selected.presentQueue = index;
            break;
        }
        if (graphics && !selected.graphicsQueue) selected.graphicsQueue = index;
        if (present && !selected.presentQueue) selected.presentQueue = index;
    }
    if (!selected.graphicsQueue) return missing("graphics queue family");
    selected.timestampBits=queues[*selected.graphicsQueue].timestampValidBits;
    if (!s.surface) return {};
    if (!selected.presentQueue || (hooks && hooks->hidePresentQueue)) return missing("present queue family");
    auto formats = enumerate<VkSurfaceFormatKHR>("vkGetPhysicalDeviceSurfaceFormatsKHR",
        [&](uint32_t* n, VkSurfaceFormatKHR* out){ return s.getPhysicalDeviceSurfaceFormatsKHR(dev, s.surface, n, out); });
    for (auto preferred : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
        for (auto format : formats) {
            if (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR && format.format == preferred && !(hooks && hooks->hideSurfaceFormats)) {
                selected.format = format;
                return {};
            }
        }
    }
    return missing("B8G8R8A8_UNORM or R8G8B8A8_UNORM with SRGB_NONLINEAR");
}
void createDevice(Session& s, const Selection& selected, RendererTestHooks* hooks) {
    float priority = 1;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = *selected.graphicsQueue;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features.dynamicRendering = features.synchronization2 = VK_TRUE;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &features};
    VkDeviceQueueCreateInfo queues[] = {queue, queue};
    queues[1].queueFamilyIndex = selected.presentQueue.value_or(*selected.graphicsQueue);
    info.queueCreateInfoCount = queues[0].queueFamilyIndex == queues[1].queueFamilyIndex ? 1 : 2;
    info.pQueueCreateInfos = queues;
    std::vector<const char*> names;
    if (s.surface) names.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    VkPhysicalDeviceFaultFeaturesKHR khr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_KHR};
    VkPhysicalDeviceFaultFeaturesEXT ext{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
    if (selected.faultExtension) {
        names.push_back(selected.faultExtension);
        if (std::string_view(selected.faultExtension)==VK_KHR_DEVICE_FAULT_EXTENSION_NAME) {
            features.pNext=&khr;
            khr.deviceFault=VK_TRUE;
        } else {
            features.pNext=&ext;
            ext.deviceFault=VK_TRUE;
        }
    }
    info.enabledExtensionCount=uint32_t(names.size());
    info.ppEnabledExtensionNames=names.data();
    check("vkCreateDevice", invoke(hooks, "vkCreateDevice", [&]{ return s.createDevice(selected.physical, &info, nullptr, &s.device); }));
    s.destroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(s.getDeviceProcAddr(s.device, "vkDestroyDevice"));
}
}
namespace {
Selection initialize(Session& session, VulkanWindow window, RendererTestHooks* hooks, bool surfaceNeeded) {
    if (hooks && hooks->hideVulkanLoader) unavailable("Vulkan loader unavailable");
    std::vector<VkPhysicalDevice> devices;
    if (session.instance) {
        if (session.device) session.destroyDevice(std::exchange(session.device, {}), nullptr);
        if (session.surfaceLost && session.surface)
            session.destroySurfaceKHR(session.instance, std::exchange(session.surface, {}), nullptr);
        session.surfaceLost = false;
        devices = enumerate<VkPhysicalDevice>("vkEnumeratePhysicalDevices", [&](auto n, auto out) {
            return session.enumeratePhysicalDevices(session.instance, n, out);
        });
        if (devices.empty()) unavailable("No Vulkan physical devices");
        if (hooks && std::exchange(hooks->failRebuildOnce, false))
            session.createDevice = [](VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*) {
                return VK_ERROR_DEVICE_LOST;
            };
        else session.createDevice = session.function<PFN_vkCreateDevice>("vkCreateDevice");
    } else {
        session.load();
        devices = createInstance(session, window, hooks, surfaceNeeded);
    }
    if (surfaceNeeded && !session.surface) createSurface(session, window, hooks);
    std::optional<StartupFailure> firstFailure;
    for (auto device : devices) {
        Selection selection{device};
        if (auto failure = inspect(session, selection, hooks)) {
            if (!firstFailure) firstFailure = std::move(failure);
            continue;
        }
        createDevice(session, selection, hooks);
        return selection;
    }
    throw StartupError{std::move(*firstFailure)};
}

#include "renderer_vulkan_draw.hpp"
#include "renderer_vulkan_frame.hpp"
#include "renderer_vulkan_present.hpp"
}
Image renderVulkanOffscreen(const DrawList& list,int width,int height) {
    if (width<=0 || height<=0) return {width,height,{}};
#ifdef _WIN32
    Drawing renderer(nullptr,false,nullptr);
#else
    Drawing renderer(NativeWindow{},false,nullptr);
#endif
    return readPixels(renderer,list,width,height);
}
#ifdef _WIN32
std::unique_ptr<Renderer> createVulkanRenderer(HWND window,RendererTestHooks* hooks, std::shared_ptr<VulkanSession>& retained, DebugContext debug, std::function<void(RenderDiagnostic)> sink) {
    if (!retained) retained = std::make_shared<Session>();
    retained->debug = debug;
    retained->sink = std::move(sink);
    RECT client{};
    GetClientRect(window,&client);
    return std::make_unique<SurfaceRenderer>(window,hooks,VkExtent2D{uint32_t(client.right),uint32_t(client.bottom)}, retained);
}
#else
std::unique_ptr<Renderer> createVulkanRenderer(VulkanWindow window, RendererTestHooks* hooks, int width, int height, std::shared_ptr<VulkanSession>& retained, DebugContext debug, std::function<void(RenderDiagnostic)> sink) {
    if (!retained) retained = std::make_shared<Session>();
    retained->debug = debug;
    retained->sink = std::move(sink);
    if (window.backend == DesktopBackendKind::wayland)
        return std::make_unique<SurfaceRenderer>(window, hooks, VkExtent2D{uint32_t(width), uint32_t(height)}, retained,
                                                 VK_PRESENT_MODE_MAILBOX_KHR);
    Session session;
    initialize(session, window, hooks, true);
    unavailable("Vulkan drawing is not connected to this native desktop backend yet");
}
#endif
}
