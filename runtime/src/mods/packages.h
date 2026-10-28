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
// The manifest's game: "wwhd", any region (the build is checked where it matters: the title ID of
// Cemu/SDCafiine packs, the game build of code mods). Manifests written before 1.5 say "wwhd-usa" and keep
// loading. New manifests that say "wwhd" also require 1.5.0, so older builds explain what they need.
inline constexpr const char* kGameId="wwhd";
inline constexpr const char* kLegacyGameId="wwhd-usa";
inline bool game_id_supported(const std::string& id){return id==kGameId||id==kLegacyGameId;}
// The game ID native mods (SDK v1) receive in their host table: unchanged, so libraries comparing it keep working.
inline constexpr const char* kNativeHostGameId="wwhd-usa";
inline constexpr const char* kManagerVersion="1.5.0"; // 1.4: "experimental" flag; 1.5: game_id "wwhd"
struct Option {
    std::string id,name,description,type;
    // The item menu's settings page (game_settings.h): "menu": true shows the option there, in the Mods tab
    // under its mod (and its "section", when it has one); "live": true says the mod follows a change while the
    // game runs (otherwise the page says it applies at the next start). Optional; provisional until the SDK's
    // live options and manifest sections land (they replace these if they differ).
    std::string section;
    bool menu=false,live=false;
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
    bool experimental=false;    // untested: enabling asks first and backs up the saves
    std::vector<Conflict> graphics_conflicts; // currently enabled packs, before enabling
    std::vector<Option> options;
    std::vector<std::string> dependencies,conflicts;
    std::vector<std::string> setup_tools; // package-relative executables covered by native confirmation
    std::vector<std::pair<std::string,std::string>> content_hashes;
};
// Supplied from the running executable marker, never from a saved preference.
void set_code_mod_support(bool built);
bool has_code_mod_support(); // this executable was built with code-mod support
bool needs_code_mod_support(const std::string& id); // includes package dependencies
void initialize(); // metadata only; before the game starts
std::string directory();
std::string package_data_directory(const std::string& id); // configured manager Data/id, empty for unknown packages
// The configured manager's story/ folder (WWHD_MOD_MANAGER_DIR, else the user folder's ModManager): what the
// story packages build from the player's game files (the NPC catalogue). Empty before initialize().
std::string story_cache_directory();
std::vector<View> list();
bool install(const std::string& source,std::string& error,std::string* installed_id=nullptr); // directory or ZIP/.wwhdmod
// install() for a new package only (the world editor's new mod): refused when a package of that ID is
// installed or its folder exists, so an existing package is never replaced.
bool install_new(const std::string& source,std::string& error,std::string* installed_id=nullptr);
bool remove(const std::string& id,std::string& error); // also deletes the files its setup prepared
// Bytes on disk: the package and the files its setup prepared (what Remove frees).
uint64_t disk_use(const std::string& id);
bool enable_after_code_rebuild(const std::string& id,std::string& error); // requires the same trust confirmation
// switch_conflicts is the confirmed Switch action: disable conflicting Cemu packs atomically.
bool enable(const std::string& id,bool on,std::string& error,bool switch_conflicts=false); // refuses unconfirmed native code
void refresh_guest_startup(); // after a required package was installed or enabled at startup (required.h)
// Native packages that enabling `id` would newly turn on (itself and disabled dependencies) whose
// code the player has not confirmed yet, as {id, name}. Empty: enable() needs no confirmation.
std::vector<std::pair<std::string,std::string>> unconfirmed_native(const std::string& id);
// One-time player acknowledgement that a native package may run: remembered in profiles.json for
// this package ID and the SHA-256 of its full inventory, including its version (changes ask again).
bool confirm_native(const std::string& id,std::string& error);
// Experimental packages (manifest "experimental": true, or installed from a catalogue entry marked so)
// that enabling `id` would newly turn on and that the player has not confirmed for this enable, as
// {id, name}. enable(), enable_after_code_rebuild() and begin_setup_run() refuse while this is not empty.
std::vector<std::pair<std::string,std::string>> unacknowledged_experimental(const std::string& id);
// The player confirmed the experimental warning: backs up the save folder now (mods/save_backup.h),
// before anything is enabled, and records the confirmation for the next enable of these packages.
// A failed backup (disk full, copy error) returns false with the reason; nothing is enabled.
bool acknowledge_experimental(const std::string& id,std::string& error);
// Catalogue installs record the catalogue's flag (a package built before its manifest had it).
void mark_experimental(const std::string& id,bool on);
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
// The enabled mods that don't start because a requirement can't be met (the GameCube game for mods is gone,
// a dependency is missing, off or itself skipped), one message each saying what is missing and how to fix
// it; the rest of the mods start. The same message is each mod's reason in list(). Logged once per start
// (main.cpp) and shown once over the game (overlay/mods_tab.h startup_notice).
std::vector<std::string> startup_problems();
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
    std::string name;  // the manifest's display name (the story notebook names the quest's mod)
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
