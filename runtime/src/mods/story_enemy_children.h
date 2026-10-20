// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_enemy_ownership.h"
#include <functional>
#include <initializer_list>
#include <string>
struct Cpu;
namespace story_enemy_children {
using Token=story_enemy_ownership::Token;
using Kind=story_enemy_ownership::Kind;
// Adapter wraps each original parent Create/Execute with enter/leave. Nested scopes are bounded.
bool enter(Token,Kind,uint32_t parent,uint32_t epoch);
void leave();
bool ready(Token,uint32_t parent);
// Parent retirement preserves pending/live child lineage; no native deletion here.
void retire(Token,uint32_t parent);
using Retired=std::function<void(const story_enemy_ownership::LineageInfo&)>; // host-only exact-source notification
bool configure(void(*original)(Cpu*,uint32_t),uint32_t(*invoke)(Cpu*,uint32_t,std::initializer_list<uint32_t>),Retired={});
bool available(const std::function<std::string(uint32_t)>& owner);
bool install(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
void state_loaded();
// Actor creation (fopAcM_create) refused while an owned enemy runs, e.g. the Boko Baba's head and stem actors:
// `deny(profile, parameters)` true makes the create fail as the game's own full-heap case does. Set before install.
void set_create_filter(std::function<bool(uint32_t profile,uint32_t parameters)> deny);
// A fopAcM_create a Wizzrobe summoner's door makes: the hook asks `summon(cpu)` first (true: handled there)
void set_summon_filter(std::function<bool(Cpu*)> summon);
// Exact admitted child validation, also usable by the shared carry guard.
bool owned(uint32_t actor);
}
