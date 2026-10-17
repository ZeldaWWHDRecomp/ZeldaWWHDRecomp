// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace story_particle_format {
// Experimental, independently authored HD JPAC/FRES serialization only.
// No registration, private-bank claim, native pointer, or guest API is provided.
// Draw-selector semantics and native lifetime/visual gates remain mandatory.
inline constexpr bool kNativeRegistrationReady=false;
struct Sprite {
 std::string name;
 uint32_t width=64,height=64;
 std::vector<uint8_t> rgba;
 uint32_t tint=0xFFFFFFFF;
 uint32_t lifetime=20;
 float size=20;
};
struct Bundle {std::vector<uint8_t> jpac,texture_context;};
// Exactly one finite point emitter, one experimental fixed render profile,
// one texture index, one original RGBA8 FTEX and its own ResDict/context.
// Resource ID is private-bank local zero; never publish in retail banks0/1.
bool write(const Sprite&,Bundle&,std::string&why);
// Strict bounded candidate-format validation, NOT native-render certification.
bool validate(std::span<const uint8_t> jpac,std::span<const uint8_t> context,std::string&why);
}
