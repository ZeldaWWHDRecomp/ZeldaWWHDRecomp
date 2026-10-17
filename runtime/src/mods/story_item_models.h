// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
struct Cpu;
namespace story_item_models {
inline constexpr uint32_t kMemoryBytes=1024;
using Invoke=uint32_t(*)(Cpu*,uint32_t,std::initializer_list<uint32_t>);
bool declare(uint16_t key,const std::string& archive,unsigned frames,std::string& why,const std::string& asset_digest={});
size_t checkpoint();
void rollback(size_t) noexcept;
void configure(uint32_t memory,void(*original)(Cpu*,uint32_t),Invoke);
bool install(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
bool owns_archive(const std::string& name);
// Return true only when handled; foreign archives leave CPU untouched.
bool resource_table(Cpu*);
bool archive_wrapper(Cpu*);
bool backing_archive_wrapper(Cpu*);
void progress_loaded();
void request(uint16_t key); // successful Use only; no native calls
void tick(Cpu*); // one game logic tick, never Draw
void state_loaded(); // validates guest record on next tick, never replays Use
void reset();
namespace hooks {void hand(Cpu*);void draw(Cpu*);}
}
