// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace story_native_camera_image {
inline constexpr uint32_t kImageBytes=0xC20,kFormatVersion=1;
struct Point {float x=0,y=0,z=0;};
struct Program {
 // Native30Hz Timer values, NOT fallback60Hz scene timeline units. Engines
 // initialize at elapsed0; manager advancement is a separate native phase.
 uint32_t hold_timer=30,move_timer=30;
 Point start_eye,start_center,end_eye,end_center;
 float fovy=60;
};
// Isolated canonical native CAMERA FIXEDFRM4 -> UNITRANS5 image, with two
// parallel owned NPC WAIT staff and TIMEKEEPER barriers. No publication,
// native execution, allocation, actor lookup or public scene routing here.
bool make(const Program&,std::vector<uint8_t>& image,std::string&why);
// Validates only pristine canonical images; native mutable staff/event state
// must be checked separately by the ownership-aware observation collector.
bool validate(const std::vector<uint8_t>& image,Program& decoded,std::string&why);
}
