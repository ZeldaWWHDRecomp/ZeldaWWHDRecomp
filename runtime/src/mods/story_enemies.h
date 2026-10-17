// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_enemy_ownership.h"
#include "story_items.h"
#include <array>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>
struct Cpu;
namespace story_enemies {
using Kind=story_enemy_ownership::Kind;
using Token=story_enemy_ownership::Token;
struct Spec {Kind kind;std::string mod,id,defeated_flag;std::vector<story_items::PropDrop> drops;story_enemy_ownership::Fingerprint fingerprint{};uint32_t epoch=0;};
constexpr uint32_t parameters(Kind k){return k==Kind::ChuChu?0xFF000000u:k==Kind::Bokoblin?0xFFFF0004u:(k==Kind::Keese||k==Kind::Moblin)?0xFFFF0000u:0u;}
constexpr uint16_t home_z(Kind k){return k==Kind::Bokoblin||k==Kind::Moblin?0xFF:0;}
struct ChildBridge {
 std::function<bool(Token,Kind,uint32_t parent,uint32_t epoch)> enter;
 std::function<void()> leave;
 std::function<bool(Token,uint32_t parent)> ready;
 std::function<void(Token,uint32_t parent)> retire;
};
// Root enables this only after the child module and persisted provenance gate pass.
// Empty/default bridge keeps BK/MO declaration and execution closed.
bool configure_children(ChildBridge,bool provenance_verified=false);
// Normalize only the currently selected retired child of the exact live generation.
// Preserve native AI action and Moblin innate/pending-spawn state; never award defeat.
void child_retired(const story_enemy_ownership::LineageInfo&);
// BK/MO remain closed until their weapon-child ownership routes are admitted.
bool supported(Kind);
Token declare(const Spec&,std::string& why);
size_t checkpoint();void rollback(size_t) noexcept;
void bind(Token,uint32_t pid,bool restored=false);
bool available(Token);void quarantine(Token);bool begin_epoch(Token,uint32_t epoch);
void state_loaded();
// Pure bounded native reads: suitable for the ownership core's mandatory callback.
bool verify_actor(Token,Kind,uint32_t actor);
// Compose with the shared player lift dispatcher; no competing player hook.
bool owns_actor(uint32_t actor);
bool deny_carry(uint32_t actor);
void configure(void(*original)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>),const std::array<uint32_t,4>& profile_storage,std::function<void(Token,uint32_t)> retired={});
bool install(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
}
