// Mod SDK v2 runtime. Guest modules load only at startup, through the mod manager's trust flow.
#pragma once
#include <cstdint>
#include "guest_identity.h"
#include "mod_json.h"
#include <string>
struct Cpu;
namespace guestmods {
bool hooks_built();  // marker registered by the running generated game code, never the setting
inline constexpr uint32_t kRegionStart=0x7F000000,kRegionSize=0x01000000;
std::vector<ModIdentity> enabled_mods();
void init(); // after dispatch::init, before guest threads start
// Called for each loaded package with world content or quests (the world editor's inspector); set before init.
using PackageObserver=void(*)(const std::string& mod,const std::string& name,const std::string& dir,const mods::json::Value& manifest);
void set_package_observer(PackageObserver observer);
// World editor: prepares an imported model of `mod` (its manifest entry) for the game; `archive` names it.
bool editor_prepare_model(const std::string& mod,const std::string& package_dir,const std::string& data_dir,const mods::json::Value& model,std::string& archive,std::string& why);
void logic_service(Cpu* cpu); // once per game logic pass, never interpolation-only
void frame(uint64_t step); // original full-step clock
void draw_frame(Cpu* cpu,uint64_t executed_step); // once per actual logic pass, including true60
void state_loaded(); // discard host HUD lists and texture handles after a full state load
void service(Cpu* cpu); // at the frame boundary (main thread, other threads idle): story layouts' builds after a load
}
