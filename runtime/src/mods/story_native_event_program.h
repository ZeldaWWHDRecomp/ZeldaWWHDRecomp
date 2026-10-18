// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_turn_camera_image.h"
#include <array>
#include <optional>
#include <span>
namespace story_native_event_program {
enum class Kind {Wait,TurnCamera};
struct Cut {uint32_t index{},action{},maximum_timer{};};
// Immutable canonical grammar data, not an ownership/publication capability.
// Factories accept typed definitions only; no guest offsets or live bytes can
// construct a descriptor. WAIT and TURN retain distinct record codecs.
class Program final {
 Kind kind_;uint32_t frames_,watchdog_;
 std::optional<story_native_turn_camera_image::Program> turn_;
 std::vector<uint8_t> image_;
 Program(Kind,uint32_t,uint32_t,std::optional<story_native_turn_camera_image::Program>,std::vector<uint8_t>);
public:
 static std::optional<Program> wait(uint32_t frames);
 static std::optional<Program> turn(const story_native_turn_camera_image::Program&,uint32_t watchdog);
 bool operator==(const Program&p)const{return kind_==p.kind_&&frames_==p.frames_&&watchdog_==p.watchdog_&&image_==p.image_;}
 Kind kind()const{return kind_;}
 uint32_t frames()const{return frames_;}
 uint32_t watchdog()const{return watchdog_;}
 uint32_t bytes()const{return uint32_t(image_.size());}
 std::span<const uint8_t> pristine()const{return image_;}
 const story_native_turn_camera_image::Program* turn_definition()const{return turn_?&*turn_:nullptr;}
 std::array<uint32_t,8> pool_offsets()const;
 uint32_t completion_mask()const{return kind_==Kind::Wait?0x3Fu:0x1FFu;}
 uint32_t initial_cut(unsigned staff)const;
 std::optional<Cut> cut(unsigned staff,uint32_t index)const;
 // Exact immutable image bytes only; mutable staff/event policy, flags,
 // identities, native receipts and barriers remain caller obligations.
 bool immutable_matches(std::span<const uint8_t>)const;
};
}
