#pragma once
#include "mod_json.h"
#include "catalogue_schema.h"
#include <cstdint>
#include <functional>
#include <map>
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
struct Conflict {std::string id,name,reason;};
struct View {
    std::string id,name,version,author,description,kind,reason,status;
    bool enabled=false,active=false,compatible=false,restart_required=false,pending_restart=false;
    std::string game_source_warning;
    bool native_confirmed=true; // false: code the player has not confirmed (native library or guest ELF)
    std::vector<Conflict> graphics_conflicts; // currently enabled packs, before enabling
    std::vector<Option> options;
    std::vector<std::string> dependencies,conflicts;
    std::vector<std::string> setup_tools; // package-relative executables covered by native confirmation
    std::vector<std::pair<std::string,std::string>> content_hashes;
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
// switch_conflicts is the confirmed Switch action: disable conflicting Cemu packs atomically.
bool enable(const std::string& id,bool on,std::string& error,bool switch_conflicts=false); // refuses unconfirmed native code
// Native packages that enabling `id` would newly turn on (itself and disabled dependencies) whose
// code the player has not confirmed yet, as {id, name}. Empty: enable() needs no confirmation.
std::vector<std::pair<std::string,std::string>> unconfirmed_native(const std::string& id);
// One-time player acknowledgement that a native package may run: remembered in profiles.json for
// this package ID and the SHA-256 of its full inventory, including its version (changes ask again).
bool confirm_native(const std::string& id,std::string& error);
bool configure(const std::string& id,const std::string& option,const json::Value& value,std::string& error);
struct SetupView {catalogue::Step step;bool satisfied=false;};
std::vector<SetupView> setup_steps(const std::string& id);
// Snapshot shown by the setup dialog. Acceptance atomically saves trust, choices and run intent.
std::string setup_identity(const std::string& id);
bool begin_setup_run(const std::string& id,const std::string& identity,
    const std::map<std::string,std::string>& choices,std::string& error);
// Pending setup is scoped to the current profile and exact package trust fingerprint.
bool save_setup_run(const std::string& id,bool pending,std::string& error);
std::vector<std::string> pending_setup_runs();
struct GameSourceView {std::string path,result;bool valid=false;};
GameSourceView game_source();
std::string game_source_warning(const std::vector<catalogue::Step>& steps);
std::string game_source_warning(const catalogue::Entry& entry,const catalogue::Index& index);
bool set_game_source(const std::string& game,const std::string& path,std::string& error);
// Run on a worker thread after native confirmation; never holds the manager lock while running.
bool run_setup_tool(const std::string& id,const std::string& step,std::string& error,std::string& last_output);
// Notices are consumed once by the Mods tab.
std::vector<std::string> take_notices();
void disable_all();
std::vector<std::string> profiles();
std::string current_profile();
bool create_profile(const std::string& name,std::string& error);
bool select_profile(const std::string& name,std::string& error);
bool delete_profile(const std::string& name,std::string& error);
void remember_builtin(const std::string& id,bool on);
void remember_option(const std::string& id,double value);
void remember_option(const std::string& id,const std::string& value);
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
struct GuestBuilt {std::string module;uint32_t allocation_size=0;};
using GuestBuild = std::function<GuestBuilt(const GuestPackage&,uint32_t base)>;
using GuestCacheCheck = std::function<std::vector<std::string>(const json::Value& requests)>;
void set_guest_builder(GuestInspect inspect,GuestBuild build,GuestCacheCheck check = {});
bool prepare_guest(const std::string& id,std::string& error); // builds the cached module; activation requires restart
// After dispatch/memory init, before guest threads. Build/load errors remain visible in list().
void start_guests(const GuestInspect& inspect, const GuestLoad& load);
void frame(uint64_t step); // actual load/configure/unload and callbacks: game thread only
std::string platform_key();
bool refresh(std::string& error);
}
