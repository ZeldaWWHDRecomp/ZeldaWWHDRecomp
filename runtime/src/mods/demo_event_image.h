// SPDX-License-Identifier: MPL-2.0
// The event list a mod demo plays through: one event with one PACKAGE staff whose PLAY cut names the
// demo's STB file (dEvDtStaff_c::specialProcPackage loads it from the demo archive, starts it with
// dDemo_manager_c::create and, when the data runs out, removes the demo and ends the cut itself).
// Same pool layout as the game's event_list.dat (event 0xB0, staff/cut 0x50, property 0x40 bytes).
//
// A demo that changes the time or weather adds a LIGHT staff, as retail events do (ARRIVAL_BRK,
// HO_PREACH): a WAIT cut, then one CHANGE cut per change. A CHANGE cut starts when its start flag is
// set (the port sets it at the change's demo frame) and has the game's own properties: Hour (float,
// dKy_instant_timechg(Hour * 15)) and Weather (int 0: dKy_instant_rainchg, rain and rainy colours).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace demo_event_image {
struct Origin { float x = 0, y = 0, z = 0, angle_y = 0; };
struct LightCut {
    uint32_t start_flag = 0, end_flag = 0;  // event manager flags (< 10240): set by the port / set when the cut ends
    std::optional<float> hour;              // 0..24
    bool rain = false;                      // Weather 0: the game's instant rain
    bool operator==(const LightCut&) const = default;
};
struct Spec {
    std::string event = "MOD_DEMO";     // event name (<32)
    std::string file = "moddemo.stb";   // STB name inside the (virtual) demo archive (<32)
    std::optional<Origin> origin;       // OffsetPos / OffsetAngY: tracks relative to this point
    std::vector<LightCut> light;        // the LIGHT staff's CHANGE cuts (none: no LIGHT staff)
    uint32_t light_wait_flag = 20;      // the LIGHT staff's first (WAIT) cut's end flag
};
inline constexpr size_t kMaxLightCuts = 8;
inline constexpr size_t kMaxImageBytes = 0xC00;
constexpr uint32_t kEventStateOffset = 0x40 + 0xA4;  // the event's run state word inside the image
bool make(const Spec& spec, std::vector<uint8_t>& out, std::string& why);
// Reads a made image back; accepts only what make() writes.
bool validate(const std::vector<uint8_t>& image, Spec& out, std::string& why);
}  // namespace demo_event_image
