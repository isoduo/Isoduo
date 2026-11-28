# Settings demo

The web and material executables share navigation, settings persistence, undo and redo, About information and semantic access. Windows also provides UIA access and the native renderer recovery prompts.

## Running on Linux

Build with the [Linux native desktop configuration](../../README.md#linux-native-desktop-build), then launch `isoduo_settings_demo_web` or `isoduo_settings_demo_material` from its build directory in a Wayland or X11 session. Both appearances use `$XDG_CONFIG_HOME/Isoduo/SettingsDemo/settings.json`. If `XDG_CONFIG_HOME` is empty, unset or relative, the root is `$HOME/.config`.

Use `--settings-dir "path with spaces"` to read and save settings in a separate directory. About displays the selected file. A missing file starts with defaults. A malformed file is moved to `settings.json.bad` when possible. If a save fails, the dialog shows the reason and you can retry with your edits intact.

The **Appearance and Text** page has an **Accent Color** dropdown (`appearance.accent`) with **Default**, **Green**, and **Orange** choices. Each choice is a palette override list (`Ui::setPaletteOverrides`) that sets `accent`, `on_accent` and `selection` for both appearances in light and dark; every choice passes the contrast rules. The choice is saved as `presentation.accent` in `settings.json`; files without that field load as the default, and **Reset Appearance Defaults** resets it too.

The **Appearance and Text** page also has five components for the focus ring (#90). The **Focus Indicator** dropdown (`appearance.focus_mode`) offers **Automatic**, **Always**, and **Off** (`FocusIndicatorMode::never`). The **Focus Ring Width** slider (`appearance.focus_width`) ranges from 2-6 DIP. The **Match Component Corner Radius** checkbox (`appearance.focus_radius_same`) shows the **Focus Ring Corner Radius** slider (`appearance.focus_radius`, 0-32 DIP) when unchecked. The **Focus Ring Color** dropdown (`appearance.focus_color`) defaults to the accent. Green and orange override the palette role `focus`, each passing the 3:1 rule on every surface. With the indicator off, the four styling components are disabled and keep their values. They are saved in `presentation` as `focusIndicator`, `focusRingWidth` / `focusRingRadius` (a number, or `null` for the appearance's / component's own) and `focusColor`. Files without them load the defaults. An unknown or out-of-range value makes the whole file corrupt, as for the accent. The **Reset Appearance Defaults** button resets them too.

The **Appearance and Text** page also has the **Align Justified Text to the Left** checkbox (`appearance.justify_left`). It is `PresentationSettings::justifyAsLeft` (WCAG 1.4.8, see "Justified text" in the top-level README): while it is on, every justified or distributed text is drawn left-aligned. It is saved as `presentation.justifyAsLeft` in `settings.json` (files without it load as off) and the **Reset Appearance Defaults** button turns it off. The About page's license text is left-aligned.

Right-clicking a row on the Items page selects it and opens a menu with **Edit** and **Delete**. Their access keys are E and D, and their shortcut hints are Enter and Del. They run the same actions as the corresponding buttons. Every text field has the built-in edit menu.
