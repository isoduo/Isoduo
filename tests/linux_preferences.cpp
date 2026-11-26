#include "linux_preferences.hpp"
#include "check.hpp"
#include <cstdlib>
#include <gio/gio.h>

int main() {
    using isoduo::MotionPreference;
    using isoduo::detail::gnomeMotion;
    using isoduo::detail::xfceMotion;
    using Variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;
    Variant disabled(g_variant_new_boolean(FALSE), g_variant_unref);
    Variant enabled(g_variant_new_boolean(TRUE), g_variant_unref);
    Variant reduced(g_variant_new_string("reduce"), g_variant_unref);
    Variant zero(g_variant_new_int32(0), g_variant_unref);
    test::check(gnomeMotion(disabled.get(), nullptr) == MotionPreference::off, "GNOME disables animations");
    test::check(gnomeMotion(enabled.get(), reduced.get()) == MotionPreference::reduce, "GNOME reduces motion");
    test::check(gnomeMotion(disabled.get(), reduced.get()) == MotionPreference::off, "Disabled takes precedence");
    test::check(gnomeMotion(nullptr, reduced.get()) == MotionPreference::reduce, "Reduced motion alone");
    test::check(xfceMotion(disabled.get()) == MotionPreference::off && xfceMotion(zero.get()) == MotionPreference::off,
                "Xfce disables animations through either supported setting type");
    test::check(xfceMotion(enabled.get()) == MotionPreference::noPreference, "Xfce enables animations");
    // A missing settings service or schema is a valid desktop state.
    ::setenv("XDG_CURRENT_DESKTOP", "XFCE", 1);
    ::setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/isoduo-session-bus", 1);
    const auto preferences = isoduo::detail::readLinuxDesktopPreferences();
    test::check(!preferences.motion && !preferences.keyboardCues,
        "Absent desktop settings leave both choices to the framework");
}
