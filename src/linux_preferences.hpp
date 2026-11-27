#pragma once
#include "isoduo/presentation.hpp"
#include <memory>
#include <optional>
#include <span>

struct pollfd;
typedef struct _GMainContext GMainContext;
typedef struct _GVariant GVariant;
typedef struct _GError GError;

namespace isoduo::detail {
class UiState;
struct LinuxDesktopPreferences {
    // A setting the desktop reports as absent reads as noPreference. Empty when the setting could not be read,
    // or the desktop publishes none: the host keeps its current system motion preference.
    std::optional<MotionPreference> motion;
    std::optional<bool> keyboardCues;
};

// Decode the values returned by the desktop settings services. Missing keys pass nullptr.
MotionPreference gnomeMotion(GVariant* enabled, GVariant* reduced);
std::optional<MotionPreference> xfceMotion(GVariant* enabled);
// Decode a failed Xfconf GetProperty call: noPreference when Xfconf reports the property absent,
// empty for every other failure.
std::optional<MotionPreference> xfceMotionError(const GError* error);

// Missing values leave the shared UI's normal motion and focus rules in effect.
LinuxDesktopPreferences readLinuxDesktopPreferences();
// Sets the host's system preferences. An empty motion keeps the current system motion preference.
void applyLinuxDesktopPreferences(UiState& state, const LinuxDesktopPreferences& preferences);

// Owns the desktop preference subscriptions on the UI thread. Its sources
// wake the native poll alongside the display socket, even when the UI is idle.
class LinuxPreferenceMonitor {
public:
    LinuxPreferenceMonitor();
    ~LinuxPreferenceMonitor();
    bool changed();
    // Sources attached here run on the desktop's owner thread after native input dispatch.
    static GMainContext* context();
    int poll(std::span<pollfd> native, int timeout);
    // Any thread: makes the owner thread's poll return, so work queued for it runs without waiting for input.
    void wake();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
