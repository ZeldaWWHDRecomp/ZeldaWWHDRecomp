#pragma once
#include "mod_json.h"
#include "catalogue_schema.h"
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
namespace mods::packages {
inline constexpr const char* kGameId="wwhd-usa";
inline constexpr const char* kManagerVersion="1.3.0";
struct Option {
    std::string id,name,description,type;
    json::Value value,default_value;
    double minimum=0,maximum=1,step=1;
    std::vector<std::string> choices;
};
struct View {
    std::string id,name,version,author,description,kind,reason,status;
    bool enabled=false,active=false,compatible=false,restart_required=false,pending_restart=false;
    bool native_confirmed=true; // false: code the player has not confirmed (native library or guest ELF)
    std::vector<Option> options;
    std::vector<std::string> dependencies,conflicts;
    std::vector<std::string> setup_tools; // package-relative executables covered by native confirmation
};
// Supplied from the running executable marker, never from a saved preference.
void set_code_mod_support(bool built);
bool needs_code_mod_support(const std::string& id); // includes package dependencies
void initialize(); // metadata only; before the game starts
std::string directory();
std::vector<View> list();
bool install(const std::string& source,std::string& error,std::string* installed_id=nullptr); // directory or ZIP/.wwhdmod
bool remove(const std::string& id,std::string& error);
bool enable_after_code_rebuild(const std::string& id,std::string& error); // requires the same trust confirmation
bool enable(const std::string& id,bool on,std::string& error); // refuses unconfirmed native code
// Native packages that enabling `id` would newly turn on (itself and disabled dependencies) whose
// code the player has not confirmed yet, as {id, name}. Empty: enable() needs no confirmation.
std::vector<std::pair<std::string,std::string>> unconfirmed_native(const std::string& id);
// One-time player acknowledgement that a native package may run: remembered in profiles.json for
// this package ID and the SHA-256 of its current platform library or guest ELF (changed code asks again).
bool confirm_native(const std::string& id,std::string& error);
bool configure(const std::string& id,const std::string& option,const json::Value& value,std::string& error);
struct SetupView {catalogue::Step step;bool satisfied=false;};
std::vector<SetupView> setup_steps(const std::string& id);
bool set_game_source(const std::string& game,const std::string& path,std::string& error);
// Run on a worker thread after native confirmation; never holds the manager lock while running.
bool run_setup_tool(const std::string& id,const std::string& step,std::string& error,std::string& last_output);
void disable_all();
std::vector<std::string> profiles();
std::string current_profile();
bool create_profile(const std::string& name,std::string& error);
bool select_profile(const std::string& name,std::string& error);
bool delete_profile(const std::string& name,std::string& error);
void remember_builtin(const std::string& id,bool on);
void remember_option(const std::string& id,double value);
using ReadMemory=int(*)(uint32_t,void*,size_t);
using WriteMemory=int(*)(uint32_t,const void*,size_t);
void set_memory_access(ReadMemory read,WriteMemory write);
// Frozen at initialize(): later profile/enable/config changes take effect at the next launch.
struct GuestPackage {
    std::string id, version, path, data_path, fingerprint;
    json::Value options;
    uint32_t heap_size=256*1024;
};
using GuestInspect = std::function<uint32_t(const GuestPackage&)>; // reserved bytes, 64 KiB aligned
using GuestLoad = std::function<void(const GuestPackage&, uint32_t base)>;
// After dispatch/memory init, before guest threads. Build/load errors remain visible in list().
void start_guests(const GuestInspect& inspect, const GuestLoad& load);
void frame(uint64_t step); // actual load/configure/unload and callbacks: game thread only
std::string platform_key();
bool refresh(std::string& error);
}
