// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_actor_identity.h"
#include <functional>
#include <initializer_list>
#include <span>
#include <string_view>
struct Cpu;
namespace story_world_visuals {
inline constexpr uint32_t kStorageBytes=0x3000,kMaxKeys=128,kMaxActive=16;
enum class Role {Distant,Shoreline};
using Point=story_notebook::WorldPoint;
struct Spec {
    std::string mod,id;
    Role role=Role::Distant;
    uint32_t token=0;
    int home_room=0; // Native sea room1..49, row-major west/east then north/south.
    Point position{},rotation{},scale{1,1,1};
    story_actors::identity::Fingerprint identity{};
};
using Call=uint32_t(*)(Cpu*,uint32_t,std::initializer_list<uint32_t>);
using Eligible=std::function<bool(const std::string&,const std::string&)>;
using Owns=std::function<bool(uint32_t actor,uint32_t token)>;
struct Config {Call invoke=nullptr;uint32_t arena=0;Eligible eligible;Owns owns;};
bool configure(Config);
// Atomic metadata replacement. Stable (mod,id,role) keys are never recycled;
// the editor's128 historical keys last until restart. No native calls here.
// apply=false performs exactly the same admission checks without changing metadata.
bool replace(const std::string& mod,std::span<const Spec>,std::string& why,bool apply=true);
// Game thread, only after native room setup. Also rejects draining scene ancestry.
// Native open ocean uses room0; observer_square is its independent geographic
// sea square1..49. Omitted square retains the legacy current-room fallback.
void service(Cpu*,std::string_view stage,int current_room,int observer_square=-1);
// Guest rows own all PID/layer lifecycle state; loads never clear valid rows.
void state_loaded();
}
