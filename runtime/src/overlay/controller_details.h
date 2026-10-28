// SPDX-License-Identifier: MPL-2.0
// Settings > Controls > Controller details: what the host reports about each connected controller (the
// one in use first): name, IDs, GUID, connection, battery, rumble motors, motion sensors, lights, buttons
// and axes, and whether it has bindings of its own. Handy when a controller doesn't behave as expected.
//
// Some controllers have motion sensors or rumble that Android hands to apps only with the right Linux
// kernel driver (DualSense / DualShock 4: hid-playstation, Linux 5.12 or newer; Nintendo's Pro Controller
// and Joy-Cons: hid-nintendo, 5.16 or newer). Without it they work as plain gamepads, buttons and sticks
// only: missing_note() says so, here and under Gyro / Rumble on. No app can get past that; a system
// update or a kernel with the driver brings them.
//
// Idea from the GreenNaugahyde Android fork (ControllerInfo.java), reimplemented. A header so
// runtime/tools/controller_setup_test.cpp draws the same dialog offscreen.
#pragma once
#include "popup.h"
#include "widgets.h"
#include "../input.h"
#include "../input_map.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace overlay::controller_details {
constexpr const char* kTitle = "Controller details##ctldetails";

struct KernelDriver {
    const char* family;   // "DualSense"
    const char* driver;   // "hid-playstation"
    const char* kernel;   // "5.12": the Linux version that has it
};

// The Linux driver a controller's sensors and rumble need on Android; nullptr: none known
inline const KernelDriver* kernel_driver(uint16_t vendor, uint16_t product) {
    static const KernelDriver kSony = {"PlayStation controller", "hid-playstation", "5.12"};
    static const KernelDriver kNintendo = {"Nintendo controller", "hid-nintendo", "5.16"};
    if (vendor == 0x054C)  // Sony: DualShock 4 (both models, the USB dongle), DualSense, DualSense Edge
        switch (product) {
        case 0x05C4: case 0x09CC: case 0x0BA0: case 0x0CE6: case 0x0DF2: return &kSony;
        default: return nullptr;
        }
    if (vendor == 0x057E)  // Nintendo: Joy-Con (L), (R), Pro Controller, the charging grip
        switch (product) {
        case 0x2006: case 0x2007: case 0x2009: case 0x200E: return &kNintendo;
        default: return nullptr;
        }
    return nullptr;
}

// What is missing, and why, for a controller that should have motion sensors and rumble but reaches
// the app without (Android only). Empty when nothing is missing or nothing is known.
inline std::string missing_note(const input::ControllerInfo& c, bool android) {
    if (!android || c.is_virtual) return "";
    const KernelDriver* d = kernel_driver(c.vendor, c.product);
    if (!d) return "";
    const bool no_motion = !(c.gyro && c.accel), no_rumble = !c.rumble;
    if (!no_motion && !no_rumble) return "";
    const char* what = no_motion && no_rumble ? "motion sensors and rumble" : no_motion ? "motion sensors" : "rumble";
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "%s: Android doesn't hand its %s to apps on this device. A %s needs the Linux kernel driver %s "
                  "(Linux %s or newer); without it the controller works as a plain gamepad, buttons and sticks only. "
                  "No app can get past that; a system update or a kernel with the driver brings them.",
                  c.name.empty() ? d->family : c.name.c_str(), what, d->family, d->driver, d->kernel);
    return buf;
}

// The connected controllers, the one in use first, the on-screen controls last
inline std::vector<input::ControllerInfo> ordered(std::vector<input::ControllerInfo> list) {
    std::stable_sort(list.begin(), list.end(), [](const input::ControllerInfo& a, const input::ControllerInfo& b) {
        if (a.is_virtual != b.is_virtual) return !a.is_virtual;
        return a.active && !b.active;
    });
    return list;
}

// One controller's lines, label and value
inline std::vector<std::pair<std::string, std::string>> lines(const input::ControllerInfo& c, const input_map::Mapping& m) {
    std::vector<std::pair<std::string, std::string>> out;
    char buf[160];
    auto add = [&](const char* k, const std::string& v) { if (!v.empty()) out.emplace_back(k, v); };
    std::snprintf(buf, sizeof buf, "%04X / %04X (version %04X)", c.vendor, c.product, c.version);
    add("Vendor / product", buf);
    add("Type", c.type);
    add("GUID", c.guid);
    add("Connection", c.connection);
    if (c.battery >= 0) std::snprintf(buf, sizeof buf, "%d %%, %s", c.battery, c.power);
    else std::snprintf(buf, sizeof buf, "%s", c.power);
    add("Battery", buf);
    add("Rumble", c.rumble ? (c.trigger_rumble ? "motors, trigger motors" : "motors") : "none reported");
    add("Motion sensors", c.gyro && c.accel ? "gyro and accelerometer" : c.gyro ? "gyro only" : c.accel ? "accelerometer only" : "none reported");
    add("Lights", c.led ? "yes" : "none reported");
    std::snprintf(buf, sizeof buf, "%d buttons, %d axes, %d hats, %d touchpads", c.buttons, c.axes, c.hats, c.touchpads);
    add("Inputs", buf);
    add("Path", c.path);
    add("Serial", c.serial);
    add("Bindings", c.is_virtual ? "the shared ones (on-screen controls)"
                    : input_map::profile_index(m, c.guid, c.name) >= 0 ? "its own (Set up controller)" : "the shared ones");
    return out;
}

// open: Controller details was chosen this frame. Returns true when B closed it (the menu stays open).
inline bool dialog(bool open, const std::vector<input::ControllerInfo>& connected, const input_map::Mapping& m,
                   bool android, bool back) {
    using namespace widgets;
    if (!popup::begin(kTitle, open, ImGui::GetFontSize() * 24)) return false;
    bool b_closed = false;
    popup::body_begin();
    ImGui::PushTextWrapPos(popup::wrap_pos(34));
    const auto list = ordered(connected);
    if (list.empty()) note("No controller is connected.");
    for (const auto& c : list) {
        ImGui::PushID(int(c.id));
        ImGui::SeparatorText(c.name.empty() ? "(no name)" : c.name.c_str());
        if (c.active) ImGui::TextDisabled("in use");
        for (const auto& [k, v] : lines(c, m)) {
            ImGui::TextDisabled("%s", k.c_str());
            ImGui::SameLine(ImGui::GetFontSize() * 9);
            ImGui::TextWrapped("%s", v.c_str());
        }
        // what is pressed right now, to check every button
        std::string held;
        for (int p = 1; p < input_map::kPadCount && p < int(c.values.size()); p++)
            if (c.values[p] > 0.5f) held += std::string(held.empty() ? "" : ", ") + input_map::pad_short_label(p);
        ImGui::TextDisabled("Pressed");
        ImGui::SameLine(ImGui::GetFontSize() * 9);
        ImGui::TextWrapped("%s", held.empty() ? "-" : held.c_str());
        const std::string why = missing_note(c, android);
        if (!why.empty()) note("%s", why.c_str());
        ImGui::PopID();
    }
    ImGui::PopTextWrapPos();
    popup::body_end();
    if (ImGui::Button("Close", ImVec2(ImGui::GetFontSize() * 8, 0)) || popup::dismissed(back)) {
        b_closed = back;
        ImGui::CloseCurrentPopup();
    }
    popup::end();
    return b_closed;
}
}  // namespace overlay::controller_details
