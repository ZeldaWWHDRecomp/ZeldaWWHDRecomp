// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_turn_camera_image.h"
#include <array>
#include <span>
namespace story_native_parallel_spec {
struct ActorDeclaration {std::string owner,id,stage;uint32_t room{},token{};bool uo2=false;};
struct SceneDeclaration {std::string owner,id;bool fallback_eligible=false;};
struct Spec {std::array<std::string,2> actors;std::array<uint32_t,2> tokens{};std::string stage,fallback_scene;uint32_t room{},watchdog_ticks{};story_native_turn_camera_image::Program program;};
// Isolated unused parser. Raw JSON preserves duplicate-key rejection; no public
// manifest route calls this. Catalog must be same immutable declaration set.
bool parse(const std::string&json,const std::string&owner,const std::string&scene,
 std::span<const ActorDeclaration>,std::span<const SceneDeclaration>,Spec&,std::string&why);
bool fallback_allowed(const Spec&,bool ever_published,bool native_mutation,bool admission_refused);
}
