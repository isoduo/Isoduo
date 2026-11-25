# UI Automation tests

The UIA trial workflow runs the `uia_*` CTests for both appearances using Mesa llvmpipe. The trial requires 20 consecutive first-attempt passes before becoming a required check.

Shortcut metadata exposes the first registered chord for a button, including scoped shortcuts; the semantic tree is the provider's source. The UIA root still subscribes lazily on first observation.

UIA23 (`isoduo_uia_client`, edit-form host) checks option focus for tabs, an open dropdown, and radio groups:
keyboard movement must raise focus events on the option returned by `GetFocusedElement`, with keyboard focusability
set. `SetFocus` on tab `v03` preserves the active page; Esc restores the dropdown component's focus event, and
closed dropdown options are not keyboard focusable. The core `option_nodes` matrix and event tests exercise the
same contract, using the shared focus predicate in `option_properties.hpp`; `focus_ring` covers disabled radio
selections and navigation candidates in both appearances.

## Platform accessibility configuration (#109)

`desktop_accessibility` checks disabled Handle/semantic operations, focus/property/structure events through the host pump, keyboard and pointer input, and per-host provider exposure. Each `uia_accessibility` run exercises default-on, explicit-on, disabled, and simultaneous enabled/disabled hosts through an external UIA client. Enabled cases provide query/action/event controls; disabled cases check known Isoduo components and their property/focus/structure events. Native system window information and events are allowed. The existing `uia_web` and `uia_material` runs guard unrelated UIA behavior and localized expectations.
