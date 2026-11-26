#pragma once
#ifdef _WIN32
#include <windows.h>
#endif
#include "render.hpp"
#include "isoduo/desktop.hpp"
#include <filesystem>
#include <chrono>
#include <memory>
namespace isoduo::detail {
#ifndef _WIN32
class EglWindow;
#endif
struct VulkanSession;
struct RendererTestHooks {
    bool failContext33 = false, failRebuildOnce = false, hideRobustness = false, hideKhrDebug = false, misuseOnce = false;
    std::optional<std::uint32_t> apiVersionOverride;
    bool hideVulkanLoader = false, hideInstanceProcAddr = false, hidePhysicalDevices = false, hideInstanceExtension = false;
    bool hideDynamicRendering = false, hideSynchronization2 = false;
    bool hideDeviceExtension = false, hideSurfaceFormats = false, hidePresentQueue = false;
    std::optional<std::pair<std::string, int>> startupResultOnce;
    std::pair<int, int> submitResultAtFrame{}, acquireResultAtFrame{}, presentResultAtFrame{};
    bool zeroSurfaceExtent = false, hideBufferMemoryTypes = false;
    bool hideValidationLayer = false, hideDebugUtils = false;
    unsigned injectDebugMessage = 0;
    int fenceResultAtFrame = 0, swapchainCreateResultOnce = 0;
    int lastAcquire=0, lastPresent=0, retryTimers=0;
    unsigned compositeAlphaMask=0;
    double presentMs=0;
    double acquireTimeoutMs=0, maxAcquireMs=0;
    unsigned acquireCalls=0, acquireTimeouts=0;
    std::string fragmentShaderOverride;
    std::string startupResourceError;
    int errorAtFrame = 0, resetAtFrame = 0;
    // From this frame on the backend reports a lost device on every error query, as after a real TDR.
    int contextLostAtFrame = 0;
    int rebuildAttempts = 0;
    unsigned injectedError = 0x0505;
};
struct StartupError { StartupFailure failure; };
struct DeferredFrame {};
struct FrameError { RendererFailure failure; std::vector<unsigned> codes; };
RenderDiagnostic frameDiagnostic(unsigned code);
struct GpuRegion {
    Rect bounds{};
    std::string owner;
    unsigned role = 0;
    bool popup = false, ink = false, passed = false;
};
struct GpuComparison {
    Image reference, actual;
    ImageComparison comparison;
    unsigned roles = 0, failedRoles = 0;
    std::vector<GpuRegion> regions;
    bool passed = false;
    explicit operator bool() const { return passed; }
};
class Renderer {
public:
#ifdef _WIN32
    using Window = HWND;
#else
    using Window = EglWindow&;
#endif
    explicit Renderer(Window window, RendererTestHooks* hooks = nullptr, DebugContext debug = DebugContext::buildDefault, std::function<void(RenderDiagnostic)> sink = {});
    virtual ~Renderer();
    virtual double draw(const DrawList& list, int width, int height, bool measure = false, bool offscreen = false, double* submissionMs = nullptr, std::chrono::steady_clock::time_point* presented = nullptr);
    virtual Image readBack(const DrawList& list, int width, int height);
    virtual std::string name() const;
    virtual RendererCapabilities capabilities() const;
    virtual void restoreAtlas(const std::vector<const AtlasPage*>& pages);
    virtual void resized() {}
protected:
    Renderer();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
GpuComparison compareDrawList(Renderer&, const DrawList&, int width, int height, int fault = 0);
}
