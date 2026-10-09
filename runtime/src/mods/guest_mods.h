// Mod SDK v2 runtime. Guest modules load only at startup, through the mod manager's trust flow.
#pragma once
#include <cstdint>
#include "guest_identity.h"
namespace guestmods {
bool hooks_built();  // marker registered by the running generated game code, never the setting
inline constexpr uint32_t kRegionStart=0x7F000000,kRegionSize=0x01000000;
std::vector<ModIdentity> enabled_mods();
void init(); // after dispatch::init, before guest threads start
void frame(uint64_t step); // logic-step clock exposed to guest mods
}
