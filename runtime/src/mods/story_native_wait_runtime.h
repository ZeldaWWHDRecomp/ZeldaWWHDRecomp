// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
struct Cpu;
namespace story_native_wait_runtime {
using LogicStep=uint64_t(*)();
// Called after committed mod startup, before guest threads. Retains eligible
// declared scene Sessions/arenas and their aggregate save gate. Public scene
// selection remains disabled while run/restore integration is being completed;
// the private pilot uses the same process-lifetime ownership storage.
void startup(bool stage_hooks_installed,LogicStep);
void logic(Cpu*);
void state_loaded();
}
