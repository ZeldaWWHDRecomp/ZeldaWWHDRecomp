// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
struct Cpu;
namespace story_native_restore_resume {class Boundary;}
namespace story_native_event_game_thread {
// The logic Execute hook is the only production source allowed to establish
// this binding. A ticket binds later native admission to the same live guest
// OSThread, host thread, Cpu and load epoch. It is not an event-list lease.
struct Ticket {uint64_t epoch=0;uint32_t guest=0;bool operator==(const Ticket&)const=default;};
bool observe_logic(const Cpu*) noexcept;
bool verify(const Cpu*,Ticket) noexcept;
Ticket ticket(const Cpu*) noexcept;
void state_loaded() noexcept;
// Epoch data alone grants no authority. Only an actual synchronous post-load
// frame Boundary can establish the first new ticket before native execution.
uint64_t current_load_epoch() noexcept;
bool reissue_after_restore(const Cpu*,const story_native_restore_resume::Boundary&) noexcept;
}
