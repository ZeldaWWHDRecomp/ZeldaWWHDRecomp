// Settings > Controls: the options under the controller drawing (stick dead zone, camera inversion,
// rumble, phone vibration, gyro, reset, stick ranges). They flow (flow_layout.h): on a desktop window
// they stay on two lines as before, in a phone's narrow window they stack instead of running past the
// right edge, and the closing hint keeps a readable width. Kept free of the game so
// runtime/tools/controls_options_ui_test.cpp can lay it out offscreen at phone and desktop sizes.
#pragma once
#include "flow_layout.h"
#include "tooltip.h"
#include <algorithm>
#include <cmath>

namespace overlay::controls_options {
struct Values {
    float deadzone = 0.15f;  // 0..0.9
    bool invert_camera_y = false;
    bool rumble = true, rumble_available = true;
    int phone_vibration = -1;  // 0..2; -1: no phone vibration option (not Android)
    const char* gyro_label = "Gyro: off...";
    float range[2] = {1.0f, 1.0f};
    float range_min = 0.5f, range_max = 1.5f;
};
enum Changed : unsigned {
    kDeadzone = 1, kInvert = 2, kRumble = 4, kPhone = 8, kGyro = 16, kReset = 32, kRangeLeft = 64, kRangeRight = 128
};
// where each option landed (tests)
enum Item { kItemDeadzone, kItemInvert, kItemRumble, kItemPhone, kItemGyro, kItemReset, kItemRangeLeft, kItemRangeRight,
            kItemHint, kItemCount };
struct Layout { ImVec2 min[kItemCount], max[kItemCount]; bool drawn[kItemCount] = {}; };

constexpr const char* kHint =
    "Raise if your stick can't reach full deflection, e.g. the Wind Waker baton's directions don't register.";

// Returns the Changed bits; `v` holds the new values.
inline unsigned draw(Values& v, Layout* layout = nullptr) {
    unsigned changed = 0;
    const float gap = 24, slider = 200, least = ImGui::GetFontSize() * 6;
    auto mark = [&](Item item) {
        if (!layout) return;
        layout->min[item] = ImGui::GetItemRectMin();
        layout->max[item] = ImGui::GetItemRectMax();
        layout->drawn[item] = true;
    };
    auto tip = [](const char* text) {
        if (overlay::tooltip_hovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", text);
    };
    float dz = v.deadzone * 100.0f;
    ImGui::SetNextItemWidth(flow::item_width(slider, "Stick dead zone", least));
    if (ImGui::SliderFloat("Stick dead zone", &dz, 0.0f, 90.0f, "%.0f%%")) {
        v.deadzone = std::clamp(dz / 100.0f, 0.0f, 0.9f);
        changed |= kDeadzone;
    }
    mark(kItemDeadzone);
    tip("Default: 15%. Filters resting-stick drift on both sticks.");
    flow::next(flow::check("Invert camera up/down"), gap);
    if (ImGui::Checkbox("Invert camera up/down", &v.invert_camera_y)) changed |= kInvert;
    mark(kItemInvert);
    tip("Camera only, including first-person views and item aiming (bow, hookshot, telescope, Picto Box) and the "
        "mouse camera. The Wind Waker baton and other right-stick controls are not inverted.");
    // issue #35: a way to keep the controller motors still (saved; WWHD_RUMBLE=0 starts with it off)
    flow::next(flow::check("Rumble"), gap);
    ImGui::BeginDisabled(!v.rumble_available);
    if (ImGui::Checkbox("Rumble", &v.rumble)) changed |= kRumble;
    ImGui::EndDisabled();
    mark(kItemRumble);
    tip(v.rumble_available ? "Controller vibration when the game asks for it. Off keeps the motors still."
                           : "Controller vibration: this host does not drive controller motors yet.");
    if (v.phone_vibration >= 0) {
        const float combo = 210;
        flow::next(flow::labeled(combo, "Phone vibration"), gap);
        ImGui::SetNextItemWidth(flow::item_width(combo, "Phone vibration", least));
        if (ImGui::Combo("Phone vibration", &v.phone_vibration, "Off\0Touch only\0Touch or no rumble controller\0"))
            changed |= kPhone;
        mark(kItemPhone);
        tip("Touch only is the default. Phone vibration follows game rumble while using touch controls. "
            "The fallback also works with controllers without rumble. Rumble above and Android's vibration settings apply.");
    }
    flow::next(flow::button(v.gyro_label), gap);
    if (ImGui::Button(v.gyro_label)) changed |= kGyro;
    mark(kItemGyro);
    tip("Aim in first person by moving a controller with a gyro, the phone, a Cemuhook (DSU) source or the mouse "
        "(Steam Input)");
    flow::next(flow::button("Reset to defaults"), gap);
    if (ImGui::Button("Reset to defaults")) changed |= kReset;
    mark(kItemReset);
    // issue #40: per-stick range, applied after the dead zone (live, saved with the controls); a line of their own
    const char* const labels[2] = {"Left stick range", "Right stick range"};
    for (int k = 0; k < 2; k++) {
        if (k) flow::next(flow::labeled(slider, labels[k]), gap);
        float r = v.range[k] * 100.0f;
        ImGui::SetNextItemWidth(flow::item_width(slider, labels[k], least));
        if (ImGui::SliderFloat(labels[k], &r, v.range_min * 100.0f, v.range_max * 100.0f, "%.0f%%")) {
            v.range[k] = std::clamp(std::round(r) / 100.0f, v.range_min, v.range_max);
            changed |= k ? kRangeRight : kRangeLeft;
        }
        mark(k ? kItemRangeRight : kItemRangeLeft);
        tip("Default: 100% (no change). Scales the stick after the dead zone so a short stick reaches full deflection.");
    }
    // the hint: beside the ranges with room for a readable line, else below them
    flow::note(kHint, ImGui::GetFontSize() * 18, ImVec4(0.70f, 0.78f, 0.84f, 1.0f), gap);
    mark(kItemHint);
    return changed;
}
}  // namespace overlay::controls_options
