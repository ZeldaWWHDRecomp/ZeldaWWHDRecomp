// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_camera_image.h"
namespace story_native_turn_camera_image {
inline constexpr uint32_t kFormatVersion=2,kImageBytes=0xE60;
struct Program {
 story_native_camera_image::Program camera;
 story_native_camera_image::Point actor_a_target,actor_b_target;
 uint32_t wait_timer=30,timekeeper_timer=300;
};
// Separate pristine grammar: two verified Uo2 subtype1 consumers only.
// TURN_TO_POS(Pos XYZ count3,Timer1,TurnSpeed1024)->WAIT; parallel camera.
// TIMEKEEPER is not a hard deadline: backend requires external watchdog.
bool make(const Program&,std::vector<uint8_t>&,std::string&why);
bool validate(const std::vector<uint8_t>&,Program&,std::string&why);
}
