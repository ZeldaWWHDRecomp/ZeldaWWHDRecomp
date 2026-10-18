// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_scene_manifest.h"
#include "story_native_event_program.h"
#include <optional>
namespace story_native_scene_compile {
class Compiled;
std::optional<Compiled>compile(const story_native_scene_manifest::Definition&,std::string&why);
// Immutable definition/image data only. No guest lookup, actor identity token,
// live Stage/allocator proof, retained claim, publication or native-call lease.
class Compiled final {
 std::array<std::string,2>actors_;std::string stage_;unsigned room_;
 story_native_event_program::Program program_;
 Compiled(const story_native_scene_manifest::Definition&,story_native_event_program::Program);
 friend std::optional<Compiled>compile(const story_native_scene_manifest::Definition&,std::string&);
public:
 const std::array<std::string,2>&actor_ids()const{return actors_;}
 const std::string&stage()const{return stage_;}unsigned room()const{return room_;}
 const story_native_event_program::Program&program()const{return program_;}
};
}
