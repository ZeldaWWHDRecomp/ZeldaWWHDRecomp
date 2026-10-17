// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
#include "story_world_manifest.h"
struct Cpu;
namespace story_props {
inline constexpr uint16_t kProfile=0x7FFD;
inline constexpr uint32_t kProfileBytes=0x30,kClassBytes=0x93C;
bool is_prop(const story_notebook::ManifestActor&);
uint32_t parameters(const story_notebook::ManifestActor&);
// Tokens stay private to the startup batch until commit; no hooks/CAPI expose provisional tokens.
uint32_t declare(const std::string& mod,const story_notebook::ManifestActor&,std::string& why);
// Startup batch staging only; rollback never changes a loaded story area.
size_t checkpoint();
void rollback(size_t checkpoint) noexcept;
void bind(uint32_t token,uint32_t process);
bool available(uint32_t token);
void retry(uint32_t token);
void quarantine(uint32_t token);
void state_loaded();
// Wired by the trusted runtime only. The callback must verify declaration,
// process, snapshot fingerprint, heap provenance and exact private model slot.
using TextureApply=std::function<bool(Cpu*,uint32_t prop_token,uint32_t actor,uint32_t pid,uint32_t texture_token,uint32_t heap_begin,uint32_t heap_end,bool ready_only)>;
void set_texture_apply(TextureApply);
bool texture_heap(uint32_t actor,uint32_t& begin,uint32_t& end);
// Shared carry guards are registered here when declarations exist. Configure all owner modules
// before installing any of them; a new guard cannot be added after the shared hook is installed.
void configure(void(*original)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>),uint32_t guest_profile,std::function<void(uint32_t token,uint32_t old_pid)> retired={});
bool install(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
}
