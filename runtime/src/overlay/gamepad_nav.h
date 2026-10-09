#pragma once
#include "imgui.h"
#include "input_map.h"

namespace overlay {
// Mouse activity wins over an already-held controller input. Only a new navigation
// press can hand control back; analog resting values are not new presses.
class GamepadNavigation {
    bool mouse_owned_ = false;
public:
    bool controller_active() const { return !mouse_owned_; }
    bool pressed(const float* values, const float* previous, int input) const {
        return controller_active() && values[input] > 0.5f && previous[input] <= 0.5f;
    }
    void feed(ImGuiIO& io, const float* values, const float* previous, bool enabled, bool mouse_used) {
        using namespace input_map;
        constexpr int navigation[] = {kPadA, kPadB, kPadX, kPadY,
            kPadDUp, kPadDDown, kPadDLeft, kPadDRight,
            kPadLSUp, kPadLSDown, kPadLSLeft, kPadLSRight,
            kPadLT, kPadRT, kPadLB, kPadRB, kPadMenu};
        bool pressed = false;
        if (enabled)
            for (int p : navigation)
                pressed |= values[p] > 0.5f && previous[p] <= 0.5f;
        if (mouse_used) mouse_owned_ = true;
        else if (pressed) mouse_owned_ = false;
        const bool active = enabled && controller_active();
        // ImGui 1.92 scrolls directly from LStick AnalogValue, even when Down=false.
        // Disabling navigation while the mouse owns it also suppresses that path.
        if (active) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        else io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        auto key = [&](ImGuiKey k, int p) {
            const float v = active && values[p] >= 0.35f ? values[p] : 0.0f;
            io.AddKeyAnalogEvent(k, v > 0.5f, v);
        };
        key(ImGuiKey_GamepadFaceDown, kPadA);
        key(ImGuiKey_GamepadFaceRight, kPadB);
        key(ImGuiKey_GamepadFaceLeft, kPadX);
        key(ImGuiKey_GamepadFaceUp, kPadY);
        key(ImGuiKey_GamepadDpadUp, kPadDUp);
        key(ImGuiKey_GamepadDpadDown, kPadDDown);
        key(ImGuiKey_GamepadDpadLeft, kPadDLeft);
        key(ImGuiKey_GamepadDpadRight, kPadDRight);
        key(ImGuiKey_GamepadLStickUp, kPadLSUp);
        key(ImGuiKey_GamepadLStickDown, kPadLSDown);
        key(ImGuiKey_GamepadLStickLeft, kPadLSLeft);
        key(ImGuiKey_GamepadLStickRight, kPadLSRight);
        // Right-stick movement is not overlay navigation and cannot reclaim it.
        key(ImGuiKey_GamepadL2, kPadLT);
        key(ImGuiKey_GamepadR2, kPadRT);
        key(ImGuiKey_GamepadStart, kPadMenu);
    }
};
} // namespace overlay
