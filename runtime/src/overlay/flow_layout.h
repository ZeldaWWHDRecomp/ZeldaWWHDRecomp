// Rows of options that wrap in a narrow window (a phone held sideways, a small desktop window): before
// each item but a line's first, flow::next(width) keeps it on the current line when it fits and starts
// the next line otherwise, so nothing ends up past the right edge where it cannot be reached. The
// widths come from the style and the labels, so the same code serves every font and UI size.
#pragma once
#include "imgui.h"
#include <algorithm>

namespace overlay::flow {
inline float text(const char* s) { return ImGui::CalcTextSize(s, nullptr, true).x; }
// an item `item` wide followed by its label (sliders, combos, checkboxes)
inline float labeled(float item, const char* label) {
    const float t = text(label);
    return t > 0 ? item + ImGui::GetStyle().ItemInnerSpacing.x + t : item;
}
inline float check(const char* label) { return labeled(ImGui::GetFrameHeight(), label); }
inline float button(const char* label) { return text(label) + ImGui::GetStyle().FramePadding.x * 2; }
// the next item (`width` wide) on this line, `spacing` after the previous one, if it fits; else below
inline void next(float width, float spacing = -1.0f) {
    ImGui::SameLine(0, spacing);
    if (ImGui::GetContentRegionAvail().x < width) ImGui::NewLine();
}
// the width of a slider or combo that wants `wanted` beside its label: narrower when the line is,
// never below `least`
inline float item_width(float wanted, const char* label, float least) {
    const float room = ImGui::GetContentRegionAvail().x - labeled(0, label);
    return std::max(least, std::min(wanted, room));
}
// wrapped text beside the previous item while at least `min_width` remains on the line, else on a
// line of its own (a help text squeezed beside wide controls would read one word per line)
inline void note(const char* s, float min_width, const ImVec4& colour, float spacing = -1.0f) {
    next(min_width, spacing);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextWrapped("%s", s);
    ImGui::PopStyleColor();
}
}  // namespace overlay::flow
