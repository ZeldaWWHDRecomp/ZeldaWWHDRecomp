// Settings > Controls > Gyro...: the "Gyro aiming" dialog (source, axis, sensitivity, invert,
// recalibrate, Cemuhook server). It closes like every Settings dialog (popup.h): the X, a tap outside, Esc,
// B, or Close, which stays below the scrolling options. A header so the offscreen test
// (runtime/tools/settings_popup_ui_test.cpp) draws the same dialog as the game.
#pragma once
#include "flow_layout.h"
#include "popup.h"
#include "text_entry.h"
#include "widgets.h"
#include "../input_map.h"
#include "../motion/motion.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace overlay::gyro {
constexpr const char* kTitle = "Gyro aiming##gyro";

// open: Gyro... was chosen this frame. g: the settings shown, changed in place (the caller saves a change).
// back: controller B this frame. Returns true when B closed the dialog (the menu stays open then).
inline bool dialog(bool open, motion::Settings& g, bool back) {
    using namespace widgets;
    if (!popup::begin(kTitle, open)) return false;
    bool v;
    popup::body_begin();
    ImGui::PushTextWrapPos(popup::wrap_pos(34));
    // a slider's width: 220 when the line has room for it and its label
    auto slider_width = [](const char* label) { ImGui::SetNextItemWidth(flow::item_width(220, label, ImGui::GetFontSize() * 6)); };
    note("On the Wii U you aim in first person (bow, hookshot, boomerang, telescope, Picto Box, grappling hook) by "
         "moving the GamePad. This works with the GamePad and the Pro Controller choice alike. The game's own "
         "Options > Gyro switch still decides whether it uses the motion, and it ignores the motion while the right "
         "stick is pushed.");
    for (int i = 0; i < motion::kSourceCount; i++)
        if (radio(motion::source_label(i), g.source == i)) {
            // an untouched sensitivity follows the new source's default (the device's own sensors: 1.0)
            const float was = motion::default_sensitivity(g.source), now = motion::default_sensitivity(i);
            if (g.tuning.sensitivity_x == was) g.tuning.sensitivity_x = now;
            if (g.tuning.sensitivity_y == was) g.tuning.sensitivity_y = now;
            g.source = i;
        }
    if (motion::env_override()) note("WWHD_GYRO=%s overrides the saved source.", getenv("WWHD_GYRO"));
    if (g.source == motion::kOff && motion::gyro_controllers() > 0) note("A controller with a gyro is connected: choose Controller gyro to use it.");
    if (g.source == motion::kController || g.source == motion::kCemuhook || g.source == motion::kDevice) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Turn left/right by");
        for (int a = 0; a < motion::kAxisModeCount; a++) {
            flow::next(flow::check(motion::axis_label(a)));
            if (radio(motion::axis_label(a), g.tuning.axis == a)) g.tuning.axis = a;
        }
        help("Player space: turning the controller left or right about the real vertical, however you hold it "
             "(recommended). Yaw: turning it about its own vertical axis (as if it lay flat). Roll: tilting it to "
             "the side like a steering wheel. Tilting its top up or down always looks up or down.");
    }
    const float lo = motion::Tuning::kMinSensitivity, hi = motion::Tuning::kMaxSensitivity;
    slider_width("Sensitivity left/right");
    ImGui::SliderFloat("Sensitivity left/right", &g.tuning.sensitivity_x, lo, hi, "%.2fx", ImGuiSliderFlags_Logarithmic);
    flow::next(flow::check("Invert"), 16);
    if (check("Invert##x", g.tuning.invert_x, &v)) g.tuning.invert_x = v;
    slider_width("Sensitivity up/down");
    ImGui::SliderFloat("Sensitivity up/down", &g.tuning.sensitivity_y, lo, hi, "%.2fx", ImGuiSliderFlags_Logarithmic);
    flow::next(flow::check("Invert"), 16);
    if (check("Invert##y", g.tuning.invert_y, &v)) g.tuning.invert_y = v;
    if (ImGui::Button("Default sensitivity"))
        g.tuning.sensitivity_x = g.tuning.sensitivity_y = motion::default_sensitivity(g.source);
    help("1.0 turns the view as far as moving a real Wii U GamePad by the same angle would (about twice the "
         "controller's turn). The default for a controller, 0.5, lets the view follow it about one to one; "
         "with the device gyro the screen you hold is the GamePad, so its default is 1.0.");
    if (g.source == motion::kMouse) {
        slider_width("Mouse: degrees per point");
        ImGui::SliderFloat("Mouse: degrees per point", &g.mouse_degrees, 0.01f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        help("How far one point of mouse movement turns the GamePad (before the sensitivity above). With Steam "
             "Input's gyro to mouse, tune this and Steam's own sensitivity together.");
        note("While the game aims, the pointer is captured and the mouse turns the GamePad (the mouse camera mod "
             "leaves it alone then).");
    }
    if (g.source == motion::kCemuhook) {
        static char host[256] = "";
        static int port = 0;
        static bool init = false;
        if (!init || ImGui::IsWindowAppearing()) { snprintf(host, sizeof host, "%s", g.dsu_host.c_str()); port = g.dsu_port; init = true; }
        slider_width("Server");
        if (text_entry::field("Server", host, sizeof host, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
            g.dsu_host = host[0] ? host : "127.0.0.1";
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("Port", &port, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
            g.dsu_port = std::clamp(port, 1, 65535);
        ImGui::SetNextItemWidth(120);
        int slot = g.dsu_slot + 1;
        if (ImGui::SliderInt("Controller slot", &slot, 1, 4)) g.dsu_slot = slot - 1;
        note("A Cemuhook (DSU) server: DS4Windows, BetterJoy, SteamDeckGyroDSU or a phone app; default 127.0.0.1, port 26760.");
    }
    // recalibrate: a controller input and/or a key
    const char* pad_name = g.recenter_pad > 0 ? input_map::pad_label(g.recenter_pad) : "None";
    slider_width("Recalibrate: controller");
    if (ImGui::BeginCombo("Recalibrate: controller", pad_name)) {
        for (int p = 0; p < input_map::kPadCount; p++)
            if (selectable(p ? input_map::pad_label(p) : "None", g.recenter_pad == p)) g.recenter_pad = p;
        ImGui::EndCombo();
    }
    std::string key_name = g.recenter_key >= 0 ? input_map::key_label(g.recenter_key) : "None";
    slider_width("Recalibrate: key");
    if (ImGui::BeginCombo("Recalibrate: key", key_name.c_str())) {
        if (selectable("None", g.recenter_key < 0)) g.recenter_key = -1;
        for (int k = 0; k < 256; k++) {
            std::string id = input_map::key_id(k);
            if (id.rfind("Key", 0) == 0) continue;  // unnamed codes
            if (selectable(input_map::key_label(k).c_str(), g.recenter_key == k)) g.recenter_key = k;
        }
        ImGui::EndCombo();
    }
    help("If the view drifts while the controller rests, recalibrate and put the controller down for a second: "
         "the gyro's offset is learnt anew. The button or key also reaches the game if the controls use it; pick "
         "a free one.");
    if (ImGui::Button("Recalibrate now")) motion::recalibrate();
    const std::string status = motion::status();
    flow::next(flow::text(status.c_str()));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(status.c_str());
    ImGui::PopTextWrapPos();
    popup::body_end();
    // the footer: always on screen
    const bool close = ImGui::Button("Close##gyro", ImVec2(ImGui::GetFontSize() * 7, 0));
    const bool dismissed = popup::dismissed(back);
    if (close || dismissed) ImGui::CloseCurrentPopup();
    popup::end();
    return dismissed && back;
}
}  // namespace overlay::gyro
