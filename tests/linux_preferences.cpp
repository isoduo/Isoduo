#include "linux_preferences.hpp"
#include "check.hpp"
#include "core_internal.hpp"
#include <cstdlib>
#include <gio/gio.h>

using isoduo::MotionPreference;
using test::check;
using Variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;
using Error = std::unique_ptr<GError, decltype(&g_error_free)>;

static Error remoteError(const char* name, const char* message) {
    return {g_dbus_error_new_for_dbus_error(name, message), g_error_free};
}

static void desktopSettingValues() {
    using isoduo::detail::gnomeMotion;
    using isoduo::detail::xfceMotion;
    Variant disabled(g_variant_new_boolean(FALSE), g_variant_unref);
    Variant enabled(g_variant_new_boolean(TRUE), g_variant_unref);
    Variant reduced(g_variant_new_string("reduce"), g_variant_unref);
    Variant zero(g_variant_new_int32(0), g_variant_unref);
    check(gnomeMotion(disabled.get(), nullptr) == MotionPreference::off, "GNOME disables animations");
    check(gnomeMotion(enabled.get(), reduced.get()) == MotionPreference::reduce, "GNOME reduces motion");
    check(gnomeMotion(disabled.get(), reduced.get()) == MotionPreference::off, "Disabled takes precedence");
    check(gnomeMotion(nullptr, reduced.get()) == MotionPreference::reduce, "Reduced motion alone");
    check(xfceMotion(disabled.get()) == MotionPreference::off && xfceMotion(zero.get()) == MotionPreference::off,
          "Xfce disables animations through either supported setting type");
    check(xfceMotion(enabled.get()) == MotionPreference::noPreference, "Xfce enables animations");
}

// xfconfd's GetProperty error for a property the channel does not hold. Source: xfconf common/xfconf-errors.c
// (XFCONF_ERROR_PROPERTY_NOT_FOUND) and xfconfd/xfconf-backend-perchannel-xml.c, xfconf 4.18.0 to master 16f66347.
static void xfceRemovedSettingRestoresNormalMotion() {
    auto missing = remoteError("org.xfce.Xfconf.Error.PropertyNotFound",
                               "Property \"/Gtk/EnableAnimations\" does not exist on channel \"xsettings\"");
    check(isoduo::detail::xfceMotionError(missing.get()) == MotionPreference::noPreference,
          "An absent Xfce setting reads as the normal motion rules");
}

static void xfceReadFailuresKeepThePreference() {
    using isoduo::detail::xfceMotionError;
    GError* connectFailure = nullptr;
    std::unique_ptr<GDBusConnection, decltype(&g_object_unref)> bus(g_dbus_connection_new_for_address_sync(
        "unix:path=/nonexistent/isoduo-session-bus",
        GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &connectFailure), g_object_unref);
    Error noBus(connectFailure, g_error_free);
    check(!bus && noBus, "The missing session bus produces a connection error");
    check(!xfceMotionError(noBus.get()), "A missing session bus is a read failure");
    Error timeout(g_error_new_literal(G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "Timeout was reached"), g_error_free);
    check(!xfceMotionError(timeout.get()), "A timeout is a read failure");
    auto noService = remoteError("org.freedesktop.DBus.Error.ServiceUnknown",
                                 "The name org.xfce.Xfconf was not provided by any .service files");
    check(!xfceMotionError(noService.get()), "A missing Xfconf service is a read failure");
    auto readFailure = remoteError("org.xfce.Xfconf.Error.ReadFailure", "Unable to read the channel");
    check(!xfceMotionError(readFailure.get()), "An Xfconf read error is a read failure");
    // Only the error name identifies an absent property, never the message text.
    auto sameText = remoteError("org.freedesktop.DBus.Error.Failed",
                                "Property \"/Gtk/EnableAnimations\" does not exist on channel \"xsettings\"");
    check(!xfceMotionError(sameText.get()), "Another error with the same message is a read failure");
    check(!xfceMotionError(nullptr), "A failure without an error is a read failure");
}

static void hostFollowsXfceReads() {
    using isoduo::detail::applyLinuxDesktopPreferences;
    using isoduo::detail::xfceMotionError;
    isoduo::Ui ui;
    isoduo::detail::HostClaim host(ui);
    auto& state = host.state();
    auto missing = remoteError("org.xfce.Xfconf.Error.PropertyNotFound", "Property does not exist");
    Error timeout(g_error_new_literal(G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "Timeout was reached"), g_error_free);
    state.setSystemMotion(MotionPreference::off);
    applyLinuxDesktopPreferences(state, {xfceMotionError(timeout.get()), {}});
    check(ui.motionPreference() == MotionPreference::off, "A failed read keeps the disabled animations");
    applyLinuxDesktopPreferences(state, {xfceMotionError(missing.get()), {}});
    check(ui.motionPreference() == MotionPreference::noPreference, "A removed setting restores the normal motion rules");
}

static void absentDesktopSettings() {
    // A missing settings service or schema is a valid desktop state.
    ::setenv("XDG_CURRENT_DESKTOP", "XFCE", 1);
    ::setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/isoduo-session-bus", 1);
    const auto preferences = isoduo::detail::readLinuxDesktopPreferences();
    check(!preferences.motion && !preferences.keyboardCues, "Absent desktop settings leave both choices to the framework");
}

int main() {
    static const test::Case cases[] = {
        {"desktopSettingValues", desktopSettingValues},
        {"xfceRemovedSettingRestoresNormalMotion", xfceRemovedSettingRestoresNormalMotion},
        {"xfceReadFailuresKeepThePreference", xfceReadFailuresKeepThePreference},
        {"hostFollowsXfceReads", hostFollowsXfceReads},
        {"absentDesktopSettings", absentDesktopSettings},
    };
    return test::run("linux_preferences", cases);
}
