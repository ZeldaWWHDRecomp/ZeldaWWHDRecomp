// Small widgets of the Settings pages (overlay.cpp and the dialogs drawn from headers, e.g. gyro_popup.h).
#pragma once
#include "imgui.h"
#include "tooltip.h"
#include <algorithm>
#include <cstdarg>

namespace overlay::widgets {
// a help text shown as a tooltip of the item before
inline void help(const char* text) {
    if (overlay::tooltip_hovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", text);
}
// a grey note on its own wrapped lines
inline void note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
inline void note(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.78f, 0.84f, 1.0f));
    ImGui::TextWrappedV(fmt, ap);
    ImGui::PopStyleColor();
    va_end(ap);
}
// a radio button: true when chosen (not when it was already)
inline bool radio(const char* label, bool active, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    bool clicked = ImGui::RadioButton(label, active) && !active;
    ImGui::EndDisabled();
    return clicked;
}
inline bool check(const char* label, bool value, bool* out, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    bool v = value;
    bool changed = ImGui::Checkbox(label, &v);
    ImGui::EndDisabled();
    *out = v;
    return changed;
}
// a list entry; on Android as tall as a button, to tap
inline bool selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0, ImVec2 size = ImVec2(0, 0)) {
#ifdef __ANDROID__
    size.y = std::max(size.y, ImGui::GetFrameHeight());
#endif
    return ImGui::Selectable(label, selected, flags, size);
}
}  // namespace overlay::widgets
