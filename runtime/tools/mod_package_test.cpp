#include <cctype>
#include "mods/setup_run.h"
#include "mod_test_host.h"
#include "mods/packages.h"
#include "mods/mods.h"
#include "mods/required.h"
#include <algorithm>
#include "mods/content.h"
#include "mods/cemu_pack.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <iterator>
#include <string>
#ifdef _WIN32
#include <process.h>
#define wwhd_test_pid _getpid
#else
#include <unistd.h>
#define wwhd_test_pid getpid
#endif
namespace {
// A temporary folder that belongs to this process alone. ctest starts the --move-anim-startup cases (and
// others) at the same moment, and a steady_clock count is no unique name across processes: two of them
// shared one folder, and one read the other's profile (mod_move_anim_unset_missing found "sprint").
// The process ID tells processes apart; create_directory() claims the name, or a next one is tried.
std::filesystem::path unique_temp(const std::string& prefix) {
    namespace fs=std::filesystem;
    static unsigned serial=0;
    for(;;) {
        auto path=fs::temp_directory_path()/(prefix+std::to_string(wwhd_test_pid())+"-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial));
        if(fs::create_directory(path))return path;
    }
}
mods::packages::View view(const std::string& id) {for(auto v:mods::packages::list())if(v.id==id)return v;return {};}
// Second process on the same storage: a confirmed native package loads after a restart without asking.
int restart_check(const char* storage) {
    using namespace mods::packages;
    namespace fs=std::filesystem;
    env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",storage);env("WWHD_TEST_TRUST_NATIVE_MODS",nullptr);
    initialize();std::string error;
    assert(view("fixture").enabled && view("fixture").native_confirmed && unconfirmed_native("fixture").empty());
    frame(1);assert(view("fixture").active && view("fixture").status=="changed");
    assert(enable("fixture",false,error));frame(2);assert(!view("fixture").active);
    return 0;
}
}
int main(int argc, char** argv) {
    using namespace mods::packages;
    namespace fs=std::filesystem;
    if(argc==4&&std::string(argv[1])=="--quick-use-speed-startup") {
        // Numeric built-in profile options are separate from manifest option schemas.
        const std::string override=argv[2],saved=argv[3];
        auto root=fs::temp_directory_path()/("wwhd-quick-speed-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
        env("WWHD_NO_HOST_INPUT",nullptr);env("WWHD_MOD_MANAGER_DIR",root.string().c_str());
        env("WWHD_MOD_QUICK_USE_SPEED",override=="unset"?nullptr:override.c_str());
        // Simulate runtime startup parsing; invalid overrides retain vanilla but still own precedence.
        mods::set_quick_use_speed(1.f);
        if(override=="1.25")mods::set_quick_use_speed(1.25f);
        if(override=="1.5")mods::set_quick_use_speed(1.5f);
        auto database=mods::json::parse(R"({"format_version":1,"active":"Default","profiles":{"Default":{"builtin_options":{}}}})");
        auto& options=database["profiles"]["Default"]["builtin_options"];
        if(saved!="missing") {
            preferences["mod.quick-use.speed"]=saved=="legacy"?"1.25":saved;
            if(saved=="string")options["quick-use.speed"]=std::string("1.5");
            else if(saved!="legacy")options["quick-use.speed"]=saved=="invalid"?1.2:std::stod(saved);
        }
        database["profiles"]["Legacy"]["builtin_options"].type=mods::json::Value::Object;
        database["profiles"]["Invalid"]["builtin_options"]["quick-use.speed"]=1.25000001;
        std::ofstream(root/"profiles.json")<<mods::json::dump(database);
        const float expected=override!="unset"?(override=="1.25"?1.25f:override=="1.5"?1.5f:1.f):
            saved=="1.25"||saved=="legacy"?1.25f:saved=="1.5"?1.5f:1.f;
        mods::manager::load_saved();assert(mods::quick_use_speed()==expected);
        initialize();assert(mods::quick_use_speed()==expected);
        assert(!mods::quick_use()); // A speed choice never enables the mod.
        mods::set_quick_use_speed(1.5f);
        hostui::set("mod.quick-use.speed","1.500000");remember_option("quick-use.speed",1.5);
        std::ifstream file(root/"profiles.json");
        const std::string text{std::istreambuf_iterator<char>(file),{}};file.close();
        assert(mods::json::parse(text).get("profiles").get("Default").get("builtin_options").get("quick-use.speed").number==1.5);
        std::string error;
        assert(create_profile("Quick",error)); // Snapshot the getter, not just a remembered value.
        mods::set_quick_use_speed(1.25f);remember_option("quick-use.speed",1.25);
        if(override=="unset") {
            assert(select_profile("Quick",error)&&mods::quick_use_speed()==1.5f);
            assert(select_profile("Default",error)&&mods::quick_use_speed()==1.25f);
            // Legacy/malformed profile switches use vanilla rather than leaking the previous speed.
            assert(select_profile("Legacy",error)&&mods::quick_use_speed()==1.f);
            assert(select_profile("Quick",error)&&mods::quick_use_speed()==1.5f);
            assert(select_profile("Invalid",error)&&mods::quick_use_speed()==1.f);
        } else {
            assert(select_profile("Quick",error)&&mods::quick_use_speed()==1.25f);
        }
        fs::remove_all(root);
        std::cout<<"Quick-use speed startup override "<<override<<" survives saved choice "<<saved<<"\n";
        return 0;
    }
    if(argc==4&&std::string(argv[1])=="--quick-use-startup") {
        // Synthetic settings/profile fixtures: no game, native library or lock-on mod.
        const std::string override=argv[2],saved=argv[3];
        auto root=fs::temp_directory_path()/("wwhd-quick-use-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
        env("WWHD_NO_HOST_INPUT",nullptr);env("WWHD_MOD_MANAGER_DIR",root.string().c_str());
        env("WWHD_MOD_QUICK_USE",override=="unset"?nullptr:override.c_str());
        // Test host simulates the runtime module's environment-owned startup state.
        mods::set_quick_use(override=="1");
        auto database=mods::json::parse(R"({"format_version":1,"active":"Default","profiles":{"Default":{"builtins":{}}}})");
        if(saved!="missing") {
            preferences["mod.quick-use.enabled"]=saved;
            database["profiles"]["Default"]["builtins"]["quick-use"]=saved=="1";
        }
        std::ofstream(root/"profiles.json")<<mods::json::dump(database);
        const bool expected=override=="unset"?saved=="1":override=="1";
        mods::manager::load_saved();assert(mods::quick_use()==expected);
        initialize();assert(mods::quick_use()==expected);
        std::string error;
        assert(mods::manager::set_enabled("quick-use",true));
        std::ifstream file(root/"profiles.json");
        const std::string text{std::istreambuf_iterator<char>(file),{}};file.close();
        assert(mods::json::parse(text).get("profiles").get("Default").get("builtins").get("quick-use").boolean);
        assert(create_profile("Quick",error));
        assert(mods::manager::set_enabled("quick-use",false));
        assert(select_profile("Quick",error)&&mods::quick_use());
        assert(select_profile("Default",error)&&!mods::quick_use());
        // Generic settings packages can enable this built-in without another module.
        auto preset=root/"preset";fs::create_directories(preset);
        std::ofstream(preset/"manifest.json")<<R"({"format_version":1,"id":"quick-preset","name":"Quick preset","version":"1.0.0","game_id":"wwhd-usa","kind":"settings","settings":{"quick-use":true}})";
        assert(install(preset.string(),error));
        assert(enable("quick-preset",true,error));frame(1);assert(mods::quick_use()&&view("quick-preset").active);
        assert(enable("quick-preset",false,error));frame(2);assert(!mods::quick_use());
        assert(remove("quick-preset",error));
        fs::remove_all(root);
        std::cout<<"Quick-use startup override "<<override<<" survives saved toggle "<<saved<<"\n";
        return 0;
    }
    if(argc==2&&std::string(argv[1])=="--setup-acceptance") {
        auto root=unique_temp("wwhd-acceptance-");
        fs::create_directories(root/"source");env("WWHD_MOD_MANAGER_DIR",(root/"manager").string().c_str());env("WWHD_TEST_TRUST_NATIVE_MODS",nullptr);
        set_code_mod_support(true);initialize();std::string error;
        auto manifest=mods::json::parse(R"({"format_version":1,"id":"acceptance","name":"Acceptance fixture","version":"1.0.0","game_id":"wwhd-usa","kind":"guest","guest":{"api_version":1,"heap_size":0},"options":[{"id":"consent","name":"Consent","type":"bool","default":false},{"id":"colour","name":"Colour","type":"enum","default":"green","choices":["green","blue"]}],"setup":[{"id":"consent","title":"Prepare the fixture","type":"confirm","option":"consent"},{"id":"colour","title":"Choose a colour","type":"choice","option":"colour","choices":["green","blue"]},{"id":"build","title":"Build fixture","type":"build_guest_mod"}]})");
        std::ofstream(root/"source/manifest.json")<<mods::json::dump(manifest);
        {std::ofstream elf(root/"source/mod.elf",std::ios::binary);elf.write("\x7f" "ELF\x01\x02",6);}
        assert(install((root/"source").string(),error));auto identity=setup_identity("acceptance");
        assert(!begin_setup_run("acceptance",identity,{},error));
        assert(!view("acceptance").native_confirmed&&pending_setup_runs().empty());
        assert(!setup_steps("acceptance")[0].satisfied&&!setup_steps("acceptance")[1].satisfied);
        assert(!begin_setup_run("acceptance","stale snapshot",{{"colour","blue"}},error));
        assert(begin_setup_run("acceptance",identity,{{"colour","blue"}},error));
        assert(view("acceptance").native_confirmed&&setup_steps("acceptance")[0].satisfied&&setup_steps("acceptance")[1].satisfied);
        auto read_saved=[&]{std::ifstream f(root/"manager/profiles.json");return mods::json::parse(std::string{std::istreambuf_iterator<char>(f),{}});};
        auto saved=read_saved();auto trust=saved.get("native_trust");
        assert(trust.get("acceptance").string().size()==64);
        assert(saved.get("profiles").get("Default").get("config").get("acceptance").get("colour").string()=="blue");
        assert(begin_setup_run("acceptance",identity,{},error));assert(read_saved().get("native_trust")==trust);
        // Same version, changed package bytes: stale pending runs and stale dialogs cannot authorize it.
        std::ofstream(fs::path(directory())/"acceptance/helper.txt")<<"changed package inventory";
        assert(refresh(error)&&!view("acceptance").native_confirmed&&pending_setup_runs().empty());
        assert(!begin_setup_run("acceptance",identity,{},error));
        auto changed=setup_identity("acceptance");assert(begin_setup_run("acceptance",changed,{},error));
        assert(read_saved().get("native_trust")!=trust);
        // A version-only update also requires a new acknowledgement.
        manifest["version"]="1.0.1";std::ofstream(root/"source/manifest.json")<<mods::json::dump(manifest);
        assert(install((root/"source").string(),error)&&!view("acceptance").native_confirmed&&pending_setup_runs().empty());
        assert(!begin_setup_run("acceptance",changed,{},error));
        assert(begin_setup_run("acceptance",setup_identity("acceptance"),{},error));
        assert(save_setup_run("acceptance",false,error)&&pending_setup_runs().empty());
        assert(setup_steps("acceptance")[0].satisfied&&setup_steps("acceptance")[1].satisfied);
        // Declarative packages also bind approval to the exact manifest, even at the same version.
        manifest["id"]="settings-acceptance";manifest["kind"]="settings";manifest["settings"]["wall-climb"]=true;
        manifest["setup"].array.pop_back();
        std::ofstream(root/"source/manifest.json")<<mods::json::dump(manifest);
        assert(install((root/"source").string(),error));
        auto declarative=setup_identity("settings-acceptance");
        auto installed_manifest=fs::path(directory())/"settings-acceptance/manifest.json";
        manifest["setup"].array[0]["title"]="Changed preparation";
        std::ofstream(installed_manifest)<<mods::json::dump(manifest);
        assert(!begin_setup_run("settings-acceptance",declarative,{{"colour","blue"}},error));
        assert(refresh(error));
        assert(setup_identity("settings-acceptance")!=declarative);
        assert(!begin_setup_run("settings-acceptance",declarative,{{"colour","blue"}},error));
        assert(!setup_steps("settings-acceptance")[0].satisfied);
        assert(begin_setup_run("settings-acceptance",setup_identity("settings-acceptance"),{{"colour","blue"}},error));
        fs::remove_all(root);std::cout<<"Atomic setup trust/choices/cancel, stale snapshot, fingerprint and version checks passed\n";return 0;
    }
    if(argc==3&&std::string(argv[1])=="--game-source-startup") {
        namespace fs=std::filesystem;
        env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",argv[2]);set_code_mod_support(true);
        initialize();auto source=game_source().path;assert(!source.empty());
        auto moved=source+".moved";fs::rename(source,moved);
        bool inspected=false,loaded=false;
        start_guests([&](const GuestPackage&){inspected=true;return uint32_t(65536);},
                     [&](const GuestPackage&,uint32_t){loaded=true;});
        fs::rename(moved,source);
        assert(!inspected&&!loaded&&!view("gc-guest").active);return 0;
    }
    // manifest game IDs: "wwhd" (any region, manager 1.5) and "wwhd-usa" (older manifests) are this game
    if(argc==2&&std::string(argv[1])=="--game-id") {
        namespace fs=std::filesystem;
        auto root=unique_temp("wwhd-game-id-");
        env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",(root/"manager").string().c_str());
        initialize();std::string error;
        assert(mods::packages::game_id_supported("wwhd")&&mods::packages::game_id_supported("wwhd-usa"));
        assert(!mods::packages::game_id_supported("wwhd-eur")&&!mods::packages::game_id_supported("mkw")&&!mods::packages::game_id_supported(""));
        auto preset=[&](const std::string& id,const std::string& game,const std::string& extra="",const std::string& builtin="wall-climb"){
            auto dir=root/id;fs::create_directories(dir);
            std::ofstream(dir/"manifest.json")<<R"({"format_version":1,"id":")"+id+R"(","name":")"+id+R"(","version":"1.0.0","game_id":")"+game+R"(","kind":"settings","settings":{")"+builtin+R"(":true})"+extra+"}";
            return dir;
        };
        assert(install(preset("neutral","wwhd",R"(,"minimum_manager_version":"1.5.0")").string(),error));
        assert(install(preset("legacy","wwhd-usa","","quick-doors").string(),error));
        assert(!install(preset("other","wwhd-eur").string(),error)&&error.find("targets another game")!=std::string::npos);
        assert(view("neutral").compatible&&view("legacy").compatible&&view("other").id.empty());
        assert(enable("neutral",true,error)&&enable("legacy",true,error));
        // a content import written by an older build ("wwhd-usa", manager 1.1.0) still loads
        auto old=root/"old-import";fs::create_directories(old/"content"/"Common");
        std::ofstream(old/"content"/"Common"/"fixture.bin")<<"synthetic replacement";
        std::ofstream(old/"manifest.json")<<R"({"format_version":1,"id":"content.old-import","name":"old-import","version":"1.0.0","game_id":"wwhd-usa","kind":"content","minimum_manager_version":"1.1.0","content_dir":"content"})";
        assert(install(old.string(),error)&&view("content.old-import").compatible);
        // a new import is written with "wwhd" and requires 1.5.0 (older builds say "Requires mod manager 1.5.0")
        auto fresh=root/"fresh-import";fs::create_directories(fresh/"content"/"Common");
        std::ofstream(fresh/"content"/"Common"/"fixture.bin")<<"synthetic replacement";
        std::string id;assert(install(fresh.string(),error,&id));
        auto written=mods::json::parse(mods::catalogue::read_bounded(fs::path(directory())/id/"manifest.json",65536));
        assert(written.get("game_id").string()=="wwhd"&&written.get("minimum_manager_version").string()=="1.5.0");
        assert(view(id).compatible);
        fs::remove_all(root);
        std::cout<<"Game IDs: wwhd and wwhd-usa accepted, others refused, old and new imports load\n";return 0;
    }
    if(argc==2&&std::string(argv[1])=="--new-package") {
        // install_new (the world editor's new mod): a new package only, never one that is installed or whose
        // folder exists; the new mod's manifest (an empty actor list, the notebook dependency) installs and enables
        namespace fs=std::filesystem;
        auto root=unique_temp("wwhd-new-package-");
        env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",(root/"manager").string().c_str());
        initialize();std::string error,id;
        auto source=[&](const std::string& name,const std::string& text){auto dir=root/"sources"/name;fs::create_directories(dir);std::ofstream(dir/"manifest.json",std::ios::binary)<<text;return dir.string();};
        auto settings=[&](const std::string& pkg,const std::string& label,const std::string& builtin="wall-climb"){return R"({"format_version":1,"id":")"+pkg+R"(","name":")"+label+R"(","version":"1.0.0","game_id":"wwhd","kind":"settings","settings":{")"+builtin+R"(":true}})";};
        assert(install(source("notebook",settings("story-notebook","Notebook stand-in")),error)&&enable("story-notebook",true,error));
        assert(install(source("other",settings("other","Other","quick-doors")),error));if(!enable("other",true,error)){std::cerr<<error<<"\n";return 1;}
        auto slurp=[&](const fs::path& p){std::ifstream f(p,std::ios::binary);return std::string{std::istreambuf_iterator<char>(f),{}};};
        const auto other_before=slurp(fs::path(directory())/"other"/"manifest.json");
        // an installed ID: refused, the package stays as it was (install() would replace a disabled one)
        assert(!install_new(source("other-again",settings("other","Replaced","quick-doors")),error)&&error.find("installed already")!=std::string::npos);
        assert(enable("other",false,error));
        assert(!install_new(source("other-again",settings("other","Replaced","quick-doors")),error)&&view("other").name=="Other");
        assert(slurp(fs::path(directory())/"other"/"manifest.json")==other_before);
        // a folder of that ID in the mods folder without a package: refused, left alone
        fs::create_directories(fs::path(directory())/"stray");std::ofstream(fs::path(directory())/"stray"/"note.txt")<<"keep";
        assert(!install_new(source("stray",settings("stray","Stray")),error)&&slurp(fs::path(directory())/"stray"/"note.txt")=="keep");
        assert(enable("other",true,error));
        auto enabled_before=mods::json::parse(slurp(root/"manager"/"profiles.json")).get("profiles").get("Default").get("enabled");
        // the new mod: installed, enabled in the active profile, nothing else changed
        const std::string fresh=R"({"format_version":1,"id":"my-mod","name":"My mod","version":"0.1.0","author":"Me","game_id":"wwhd","minimum_manager_version":"1.5.0","kind":"content","dependencies":[{"id":"story-notebook","minimum_version":"0.0.0"}],"actors":[]})";
        if(!install_new(source("my-mod",fresh),error,&id)){std::cerr<<"new mod refused: "<<error<<"\n";return 1;}
        assert(id=="my-mod"&&view("my-mod").compatible&&!view("my-mod").enabled);
        assert(slurp(fs::path(directory())/"my-mod"/"manifest.json")==fresh);
        assert(enable("my-mod",true,error)&&view("my-mod").enabled&&view("other").enabled&&view("story-notebook").enabled);
        auto saved=mods::json::parse(slurp(root/"manager"/"profiles.json"));
        auto enabled_after=saved.get("profiles").get("Default").get("enabled");
        assert(enabled_after.get("my-mod").boolean);enabled_after.object.erase("my-mod");assert(enabled_after==enabled_before);
        assert(slurp(fs::path(directory())/"other"/"manifest.json")==other_before);
        assert(!install_new(source("my-mod",fresh),error));
        fs::remove_all(root);
        std::cout<<"New packages: installed IDs and existing folders refused, the editor's new mod installs and enables\n";return 0;
    }
    if(argc==2&&std::string(argv[1])=="--game-source") {
        namespace fs=std::filesystem;
        auto root=unique_temp("wwhd-gc-source-");
        fs::create_directories(root/"source");
        env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",(root/"manager").string().c_str());
        initialize();std::string error;
        std::ofstream(root/"source/manifest.json")<<R"({"format_version":1,"id":"gc-fixture","name":"GC fixture","version":"1.0.0","game_id":"wwhd-usa","kind":"settings","settings":{"wall-climb":true},"setup":[{"id":"game","type":"game_path","game":"gc_wind_waker","title":"GameCube game"}]})";
        assert(install((root/"source").string(),error));
        assert(game_source().path.empty()&&!game_source().valid);
        assert(!setup_steps("gc-fixture")[0].satisfied);
        assert(!view("gc-fixture").game_source_warning.empty());
        mods::catalogue::Entry source_entry;source_entry.id="gc-fixture";source_entry.setup={setup_steps("gc-fixture")[0].step};
        mods::catalogue::Entry dependent_entry;dependent_entry.id="catalogue-dependent";dependent_entry.dependencies={"gc-fixture"};
        mods::catalogue::Index index;index.entries={source_entry,dependent_entry};
        assert(!game_source_warning(dependent_entry,index).empty());
        assert(!enable("gc-fixture",true,error)&&error.find("GameCube")!=std::string::npos);
        assert(!enable_after_code_rebuild("gc-fixture",error));
        assert(!set_game_source("gc_wind_waker",(root/"missing.iso").string(),error));
        auto disc=root/"synthetic.iso";std::array<unsigned char,32> header{};
        std::copy_n("GZLE01",6,header.begin());header[28]=0xC2;header[29]=0x33;header[30]=0x9F;header[31]=0x3D;
        {std::ofstream out(disc,std::ios::binary);out.write(reinterpret_cast<char*>(header.data()),header.size());}
        assert(set_game_source("gc_wind_waker",disc.string(),error));
        {std::ifstream in(root/"manager/profiles.json");auto saved=mods::json::parse(std::string{std::istreambuf_iterator<char>(in),{}});
         assert(saved.get("game_sources").get("gc_wind_waker").string()==fs::canonical(disc).string());}
        assert(game_source().valid&&game_source().result.find("USA")!=std::string::npos);
        assert(game_source_warning(dependent_entry,index).empty());
        assert(setup_steps("gc-fixture")[0].satisfied&&view("gc-fixture").game_source_warning.empty());
        assert(enable("gc-fixture",true,error)&&create_profile("Copy",error));
        auto other=root/"other.iso";fs::copy_file(disc,other);
        assert(set_game_source("gc_wind_waker",other.string(),error));
        assert(!view("gc-fixture").enabled&&select_profile("Copy",error)&&!view("gc-fixture").enabled);
        assert(enable("gc-fixture",true,error));
        fs::create_directories(root/"second");
        std::ofstream(root/"second/manifest.json")<<R"({"format_version":1,"id":"gc-second","name":"GC second","version":"1.0.0","game_id":"wwhd-usa","kind":"settings","settings":{"direct-camera":true},"setup":[{"id":"game","type":"game_path","game":"gc_wind_waker","title":"GameCube game"}]})";
        assert(install((root/"second").string(),error)&&enable("gc-second",true,error));
        fs::create_directories(root/"dependent");
        std::ofstream(root/"dependent/manifest.json")<<R"({"format_version":1,"id":"gc-dependent","name":"GC dependent","version":"1.0.0","game_id":"wwhd-usa","kind":"settings","settings":{"quick-doors":true},"dependencies":[{"id":"gc-fixture"}]})";
        assert(install((root/"dependent").string(),error)&&enable("gc-dependent",true,error));
        assert(view("gc-dependent").game_source_warning.empty());
        fs::remove(other);assert(!game_source().valid&&!view("gc-fixture").game_source_warning.empty());
        assert(!select_profile("Copy",error)&&error.find("GameCube")!=std::string::npos);
        assert(!enable("gc-fixture",true,error));
        assert(!view("gc-dependent").game_source_warning.empty());
        assert(!enable("gc-dependent",true,error));
        assert(enable("gc-dependent",false,error));
        // Losing a shared copy must not trap multiple enabled mods in their on state.
        assert(enable("gc-fixture",false,error));
        assert(!view("gc-fixture").enabled&&view("gc-second").enabled);
        assert(enable("gc-second",false,error)&&!view("gc-second").enabled);
        assert(set_game_source("gc_wind_waker","",error)&&game_source().path.empty());
        assert(!setup_steps("gc-fixture")[0].satisfied&&!view("gc-fixture").enabled);
        {std::ifstream in(root/"manager/profiles.json");auto saved=mods::json::parse(std::string{std::istreambuf_iterator<char>(in),{}});
         assert(saved.get("game_sources").get("gc_wind_waker").string().empty());}
        // Guest preparation also refuses missing sources and loses readiness on change.
        fs::create_directories(root/"guest");
        std::ofstream(root/"guest/manifest.json")<<R"({"format_version":1,"id":"gc-guest","name":"GC guest","version":"1.0.0","game_id":"wwhd-usa","kind":"guest","guest":{"api_version":1,"elf":"mod.elf"},"setup":[{"id":"game","type":"game_path","game":"gc_wind_waker","title":"GameCube game"},{"id":"build","type":"build_guest_mod","title":"Build"}]})";
        {std::ofstream elf(root/"guest/mod.elf",std::ios::binary);elf.write("\x7f" "ELF\x01\x02",6);}
        set_code_mod_support(true);assert(install((root/"guest").string(),error)&&confirm_native("gc-guest",error));
        assert(!prepare_guest("gc-guest",error)&&error.find("GameCube")!=std::string::npos);
        set_guest_builder([](const GuestPackage&){return uint32_t(65536);},[&](const GuestPackage&,uint32_t){auto module=root/"synthetic-module";std::ofstream(module)<<"authored cache fixture";return GuestBuilt{module.string(),65536};});
        assert(set_game_source("gc_wind_waker",disc.string(),error)&&prepare_guest("gc-guest",error));
        assert(setup_steps("gc-guest")[1].satisfied);
        assert(enable("gc-dependent",true,error)&&create_profile("Dependent copy",error));
        assert(enable("gc-guest",true,error));
        auto startup=host::run_process({argv[0],"--game-source-startup",(root/"manager").string()});
        if(startup.code!=0)std::cerr<<startup.output;
        assert(startup.code==0);
        assert(set_game_source("gc_wind_waker","",error)&&!setup_steps("gc-guest")[1].satisfied);
        assert(fs::is_regular_file(root/"synthetic-module"));
        assert(!view("gc-dependent").enabled&&select_profile("Dependent copy",error)&&!view("gc-dependent").enabled);
        // Legacy per-region selections feed the shared copy and respect region requirements.
        mods::json::Value saved;saved["gc_usa"]=disc.string();mods::catalogue::Sources sources(saved);
        assert(sources.get("gc_wind_waker")==disc.string()&&sources.get("gc_eur").empty());
        assert(sources.set("gc_wind_waker","")&&sources.get("gc_usa").empty());
        fs::remove_all(root);std::cout<<"Shared GameCube source checks passed\n";return 0;
    }
    if(argc==3&&std::string(argv[1])=="--setup-code-mods") {
        namespace fs=std::filesystem;
        fs::path data=argv[2];
        std::ifstream active_file(data/"code-mods-active.json");
        auto active=mods::json::parse(std::string{std::istreambuf_iterator<char>(active_file),{}});
        assert(active.get("state").string()=="ready");
        std::ifstream settings_file(data/"user/settings.ini");std::string line,setting;
        while(std::getline(settings_file,line))if(line.rfind("code-mods=",0)==0)setting=line.substr(10);
        const bool supported=active.get("hooks").boolean&&setting=="1";
        set_code_mod_support(supported);
        auto root=data/"package-check";fs::remove_all(root);fs::create_directories(root/"source");
        env("WWHD_MOD_MANAGER_DIR",(root/"manager").string().c_str());
        env("WWHD_TEST_TRUST_NATIVE_MODS",nullptr);
        std::ofstream(root/"source/manifest.json")<<R"({"format_version":1,"id":"setup-guest","name":"Setup guest","version":"1.0.0","game_id":"wwhd-usa","kind":"guest","guest":{"api_version":1,"elf":"mod.elf"}})";
        {std::ofstream elf(root/"source/mod.elf",std::ios::binary);elf.write("\x7f" "ELF\x01\x02",6);}
        initialize();std::string error;
        assert(install((root/"source").string(),error));assert(confirm_native("setup-guest",error));
        assert(needs_code_mod_support("setup-guest")==!supported);
        assert(enable("setup-guest",true,error)==supported);
        assert(view("setup-guest").enabled==supported);
        if(supported)assert(error.empty());
        fs::remove_all(root);return 0;
    }

    namespace fs=std::filesystem;
    using namespace mods::packages;
    if(argc==3&&std::string(argv[1])=="--lock-on-startup") {
        auto root=unique_temp("wwhd-lock-on-");
        fs::create_directories(root);
        env("WWHD_NO_HOST_INPUT",nullptr);env("WWHD_MOD_MANAGER_DIR",root.string().c_str());
        const std::string mode=argv[2];
        const bool override=mode=="env", legacy=mode=="legacy", invalid=mode=="invalid";
        env("WWHD_MOD_LOCK_ON",override?"0":nullptr);
        env("WWHD_MOD_LOCK_ON_RANGE",override?"1":nullptr);
        env("WWHD_MOD_LOCK_ON_CYCLING",override?"1":nullptr);
        preferences["mod.lock-on.enabled"]="1";
        preferences["mod.lock-on.range"]="2";preferences["mod.lock-on.cycling"]="0";
        auto database=mods::json::parse(R"({"format_version":1,"active":"Default","profiles":{"Default":{"builtins":{"lock-on":true},"builtin_options":{}}}})");
        if (!legacy) {
            auto& options=database["profiles"]["Default"]["builtin_options"];
            options["lock-on.range"]=invalid?4.0:3.0;
            options["lock-on.cycling"]=invalid?mods::json::Value("invalid"):mods::json::Value(true);
        }
        std::ofstream(root/"profiles.json")<<mods::json::dump(database);
        mods::manager::load_saved();
        assert(mods::extended_lock_on()==!override);
        assert(mods::lock_on_range()==(override?1.f:2.f));
        assert(mods::lock_on_cycling()==override);
        initialize();
        assert(mods::extended_lock_on()==!override);
        assert(mods::lock_on_range()==(override||invalid?1.f:legacy?2.f:3.f));
        assert(mods::lock_on_cycling()==!legacy);
        std::string error;
        assert(select_profile("Default",error));
        assert(mods::lock_on_range()==(override||invalid||legacy?1.f:3.f));
        assert(mods::lock_on_cycling());
        fs::remove_all(root);return 0;
    }
    // The NPC catalogue's cache (guest_mods.cpp prepare_npc_catalogue) goes to the configured manager's folder:
    // a hidden test run that sets WWHD_MOD_MANAGER_DIR (or only WWHD_USER_DIR) never writes into the player's
    // folder. HOME points into the test folder too, so a wrong path cannot reach the real one.
    if(argc==3&&std::string(argv[1])=="--story-cache-dir") {
        const std::string how=argv[2];
        const auto root=fs::weakly_canonical(unique_temp("wwhd-story-cache-"));
        env("HOME",(root/"home").string().c_str());env("XDG_CONFIG_HOME",nullptr);env("APPDATA",(root/"appdata").string().c_str());
        fs::path expected;
        if(how=="manager") {  // the run scripts: a manager folder of their own, with or without WWHD_USER_DIR
            env("WWHD_NO_HOST_INPUT","1");env("WWHD_USER_DIR",nullptr);env("WWHD_MOD_MANAGER_DIR",(root/"manager").string().c_str());
            expected=root/"manager"/"story";
        } else {  // the player's game with --user-dir / WWHD_USER_DIR: the user folder's ModManager
            assert(how=="user");
            env("WWHD_NO_HOST_INPUT",nullptr);env("WWHD_MOD_MANAGER_DIR",nullptr);env("WWHD_USER_DIR",(root/"user").string().c_str());
            expected=root/"user"/"ModManager"/"story";
        }
        assert(mods::packages::story_cache_directory().empty());  // nothing before the manager is set up
        initialize();
        const fs::path cache=mods::packages::story_cache_directory();
        std::cout<<"NPC catalogue cache ("<<how<<"): "<<cache.string()<<"\n";
        assert(cache==expected);
        assert(!fs::exists(root/"home")&&!fs::exists(root/"appdata"));  // no default user folder was made
        fs::remove_all(root);return 0;
    }
    if(argc==4&&std::string(argv[1])=="--move-anim-startup") {
        auto root=unique_temp("wwhd-move-anim-");
        fs::create_directories(root);
        env("WWHD_NO_HOST_INPUT",nullptr);
        env("WWHD_MOD_MANAGER_DIR",root.string().c_str());
        const std::string override=argv[2],saved=argv[3];
        const bool legacy=saved=="legacy-dash";
        env("WWHD_MOD_MOVE_ANIM",override=="unset"?nullptr:override.c_str());
        mods::set_move_speed_anim(mods::move_anim_from_id(override));
        auto database=mods::json::parse(R"({"format_version":1,"active":"Default","profiles":{"Default":{"builtin_options":{}}}})");
        if(saved!="missing") {
            preferences["mod.move-speed.anim"]=legacy?"dash":saved;
            if(!legacy) database["profiles"]["Default"]["builtin_options"]["move-speed.anim"]=saved;
        }
        std::ofstream(root/"profiles.json")<<mods::json::dump(database);
        const auto expected=mods::move_anim_from_id(override=="unset"?(legacy?"dash":saved):override);
        mods::manager::load_saved();
        assert(mods::move_speed_anim()==expected);
        initialize();
        assert(mods::move_speed_anim()==expected);
        // The UI's string-valued option must update the active profile, not only settings.ini.
        remember_option("move-speed.anim",std::string("sprint"));
        std::ifstream saved_file(root/"profiles.json");
        const std::string text{std::istreambuf_iterator<char>(saved_file),{}};
        saved_file.close();
        assert(mods::json::parse(text).get("profiles").get("Default").get("builtin_options").get("move-speed.anim").string()=="sprint");
        if(override=="unset") {
            mods::set_move_speed_anim(mods::MoveAnim::kNative);
            std::string error;
            assert(select_profile("Default",error));
            assert(mods::move_speed_anim()==mods::MoveAnim::kSprint);
        }
        fs::remove_all(root);
        std::cout<<"Animation startup override "<<override<<" survives saved mode "<<saved<<"\n";
        return 0;
    }
    if(argc==3&&std::string(argv[1])=="--story-notebook") {  // tools/catalogue/test_story_notebook.py
        auto fixtures=fs::absolute(argv[2]);
        env("WWHD_MOD_MANAGER_DIR",(fixtures/"manager").string().c_str());env("WWHD_TEST_TRUST_NATIVE_MODS",nullptr);
        set_code_mod_support(true);initialize();
        // the start: what is installed only (no catalogue is given, nothing is fetched)
        assert(!mods::required::check_local());
        assert(mods::required::status().text.find("Install it from the Mods menu")!=std::string::npos);
        mods::required::Context c;
        c.port=mods::catalogue::Version::parse("0.2.13");c.work=fixtures/"work";
        c.fetch=[](const std::string&,const fs::path&,uint64_t){assert(!"a local catalogue needs no transport");};
        // offline: a clear message, nothing installed
        c.catalogue=(fixtures/"missing/index.json").string();
        assert(!mods::required::install(c));
        assert(mods::required::status().text.find("Try again from the Mods menu")!=std::string::npos&&!mods::required::status().installed);
        assert(std::none_of(list().begin(),list().end(),[](const View& v){return v.id=="story-notebook";}));
        // setup or the Mods menu: installed, confirmed and enabled; locked while the story system is on
        c.catalogue=(fixtures/"index.json").string();
        assert(mods::required::install(c)&&mods::required::status().installed&&mods::required::status().restart);
        assert(view("story-notebook").enabled&&view("story-notebook").native_confirmed);
        std::string error;
        assert(!enable("story-notebook",false,error)&&error.find("part of the story system")!=std::string::npos);
        assert(mods::required::check_local());  // the next start
        std::cout<<"Story notebook: offline message, install, confirm, enable, lock\n";
        return 0;
    }
    if(argc==4&&std::string(argv[1])=="--catalogue-pilots") {
        auto fixtures=fs::absolute(argv[2]),storage=fixtures/"manager";
        env("WWHD_MOD_MANAGER_DIR",storage.string().c_str());env("WWHD_TEST_TRUST_NATIVE_MODS",nullptr);
        initialize();std::string error;
        auto index=mods::catalogue::load((fixtures/"index.json").string(),{},fixtures/"unused");
        assert(index.index.entries.size()==2);
        for(const auto& entry:index.index.entries) {
            mods::catalogue::StagedPackage staged(entry,mods::catalogue::Version::parse("0.2.10"),"USA",platform_key(),fixtures/"work",index.fixture_root,{});
            assert(install(staged.path().string(),error));assert(!view(entry.id).enabled&&!view(entry.id).active);
        }
        assert(!view("catalogue-setup-pilot").native_confirmed);
        assert(set_game_source("gc_usa",(fixtures/"synthetic-gc-usa.iso").string(),error));
        assert(configure("catalogue-setup-pilot","colour","green",error));
        assert(configure("catalogue-setup-pilot","consent",true,error));
        auto config=mods::json::parse(R"({"format_version":1,"python":[],"compiler":["unused"],"builder":"unused","include":"unused"})");
        config["python"].array={mods::json::Value(argv[3])};
        auto path=fixtures/"guest-sdk.json";{std::ofstream output(path);output<<mods::json::dump(config);}
        env("WWHD_GUEST_BUILD_CONFIG",path.string().c_str());std::string output;
        assert(!run_setup_tool("catalogue-setup-pilot","prepare",error,output));
        assert(confirm_native("catalogue-setup-pilot",error));
        assert(run_setup_tool("catalogue-setup-pilot","prepare",error,output));
        for(const auto& step:setup_steps("catalogue-setup-pilot"))assert(step.satisfied);
        assert(fs::file_size(storage/"Data/catalogue-setup-pilot/pilot-prepared.txt")==31);
        fs::remove(fixtures/"synthetic-gc-usa.iso");assert(!setup_steps("catalogue-setup-pilot")[0].satisfied);
        assert(remove("catalogue-content-pilot",error)&&remove("catalogue-setup-pilot",error));
        std::cout<<"Catalogue pilots install/trust/source/options/real Python tool/remove passed\n";return 0;
    }
    // stands in for Python: the tool must be started with -B (no __pycache__ in the package)
    // python -B -c <bootstrap that puts the tool's folder on sys.path (#224)> tool data source
    if(argc==8&&std::string(argv[1])=="--setup-tool"&&std::string(argv[2])=="-B"&&std::string(argv[3])=="-c"&&
       std::string(argv[4]).find("sys.path.insert(0,os.path.dirname(os.path.abspath(s)))")!=std::string::npos) {
        argv+=3;
        std::ifstream input(argv[2]);std::string script{std::istreambuf_iterator<char>(input),{}};
        std::cout<<"Source: "<<argv[4]<<"\n";
        if(script.find("fail")!=std::string::npos||std::getenv("WWHD_TEST_SETUP_FAILURE")){
            std::cout<<"synthetic final failure\n";return 7;
        }
        assert(fs::equivalent(fs::current_path(),argv[3]));
        std::ofstream("result.bin")<<"synthetic prepared output";return 0;
    }
    if(argc==3&&std::string(argv[1])=="--guest-pending-check") {
        bool supported=std::string(argv[2])=="on";set_code_mod_support(supported);initialize();
        assert(view("guest-fixture").enabled==supported);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--guest-prepare") {
        auto root=unique_temp("wwhd-guest-prepare-");
        fs::create_directories(root/"source");auto storage=root/"manager";
        env("WWHD_MOD_MANAGER_DIR",storage.string().c_str());env("WWHD_TEST_TRUST_NATIVE_MODS",nullptr);
        auto manifest=mods::json::parse(R"({"format_version":1,"id":"prepare-guest","name":"Prepared guest","version":"1.0.0","game_id":"wwhd-usa","kind":"guest","guest":{"api_version":1,"heap_size":0},"setup":[{"id":"build","type":"build_guest_mod","title":"Build guest module"}]})");
        for(const auto* id:{"prepare-guest","reserved-guest"}) {
            auto source=storage/"Mods"/id;fs::create_directories(source);manifest["id"]=id;
            std::ofstream(source/"manifest.json")<<mods::json::dump(manifest);
            std::ofstream elf(source/"mod.elf",std::ios::binary);elf.write("\x7f" "ELF\x01\x02",6);
        }
        std::ofstream(storage/"profiles.json")<<R"({"format_version":1,"active":"Default","profiles":{"Default":{}},"guest_regions":{"reserved-guest":{"base":2130706432,"size":65536}}})";
        initialize();std::string error;int inspected=0,built=0;bool fail=true;uint32_t assigned=0;
        auto module=root/"synthetic.module";
        set_guest_builder([&](const GuestPackage& pkg){++inspected;assert(pkg.id=="prepare-guest");assert(!list().empty());std::string busy;assert(!configure(pkg.id,"unused",true,busy)&&busy.find("preparation")!=std::string::npos);assert(!select_profile("Default",busy)&&busy.find("preparation")!=std::string::npos);assert(!enable(pkg.id,true,busy)&&busy.find("operation")!=std::string::npos);return 131072u;},
            [&](const GuestPackage&,uint32_t base){++built;assigned=base;if(fail)throw std::runtime_error("synthetic compile failure");std::ofstream(module)<<"synthetic module";return GuestBuilt{module.string(),131072};});
        assert(!prepare_guest("prepare-guest",error)&&inspected==0);
        set_code_mod_support(true);assert(!prepare_guest("prepare-guest",error)&&inspected==0);
        assert(confirm_native("prepare-guest",error));
        assert(!prepare_guest("prepare-guest",error)&&error=="synthetic compile failure");
        assert(assigned==0x7F010000&&!setup_steps("prepare-guest")[0].satisfied);
        fail=false;assert(prepare_guest("prepare-guest",error)&&assigned==0x7F010000);
        assert(inspected==2&&built==2&&setup_steps("prepare-guest")[0].satisfied);
        int cache_checks=0;
        auto check_cache=[&](const mods::json::Value& requests) {
            ++cache_checks;assert(requests.array.size()==1);
            assert(requests.array[0].get("id").string()=="prepare-guest");
            assert(requests.array[0].get("base").number==assigned);
            return std::vector<std::string>{};
        };
        set_guest_builder({}, {}, check_cache);
        assert(cache_checks==1&&!setup_steps("prepare-guest")[0].satisfied);
        assert(!setup_steps("prepare-guest")[0].satisfied&&cache_checks==1);
        set_guest_builder({}, {}, [&](const mods::json::Value& requests) {
            ++cache_checks;assert(requests.array.size()==1);
            return std::vector<std::string>{"prepare-guest"};
        });
        assert(cache_checks==2&&setup_steps("prepare-guest")[0].satisfied);
        assert(!view("prepare-guest").active&&!view("prepare-guest").enabled);
        assert(enable("prepare-guest",true,error));frame(1);assert(!view("prepare-guest").active&&view("prepare-guest").pending_restart);
        fs::remove(module);assert(!setup_steps("prepare-guest")[0].satisfied);
        fs::remove_all(root);std::cout<<"guest preparation preserves assignments/trust/restart gate passed\n";return 0;
    }
    // One mod whose requirement can't be met (the GameCube game for mods is gone: the play setup's moved .rvz) is
    // skipped on its own with a message, and the mods that need it with it; every other mod starts (before,
    // the whole startup threw and no mod started). Cases: missing (the saved path is gone), unset (none
    // chosen), off (the GameCube mods switched off), met (a GameCube game is there: everything starts).
    if(argc==3&&std::string(argv[1])=="--startup-skip") {
        const std::string mode=argv[2];
        auto root=unique_temp("wwhd-startup-skip-");
        auto storage=root/"manager";
        env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",storage.string().c_str());
        env("WWHD_TEST_TRUST_NATIVE_MODS","gc-map,gc-addon,notebook,island");
        auto package=[&](const std::string& id,const std::string& extra){
            auto source=storage/"Mods"/id;fs::create_directories(source);
            std::ofstream(source/"manifest.json")<<R"({"format_version":1,"id":")"+id+R"(","version":"1.0.0","game_id":"wwhd-usa","kind":"guest","guest":{"api_version":1})"+extra+"}";
            std::ofstream elf(source/"mod.elf",std::ios::binary);elf.write("\x7f" "ELF\x01\x02",6);
        };
        package("gc-map",R"(,"name":"GC map","setup":[{"id":"game","type":"game_path","game":"gc_wind_waker","title":"GameCube game"}])");
        package("gc-addon",R"(,"name":"GC addon","dependencies":[{"id":"gc-map"}])");
        package("notebook",R"(,"name":"Notebook")");
        package("island",R"(,"name":"Island","dependencies":[{"id":"notebook"}])");
        fs::create_directories(storage/"Mods/tweak");
        std::ofstream(storage/"Mods/tweak/manifest.json")<<R"({"format_version":1,"id":"tweak","name":"Tweak","version":"1.0.0","game_id":"wwhd-usa","kind":"settings","settings":{"quick-doors":true}})";
        auto disc=root/"synthetic.iso";std::array<unsigned char,32> header{};
        std::copy_n("GZLE01",6,header.begin());header[28]=0xC2;header[29]=0x33;header[30]=0x9F;header[31]=0x3D;
        {std::ofstream out(disc,std::ios::binary);out.write(reinterpret_cast<char*>(header.data()),header.size());}
        const std::string gone=(root/"Wind Waker (USA).rvz").string();
        mods::json::Value profiles=mods::json::parse(R"({"format_version":1,"active":"Default","profiles":{"Default":{"enabled":{"gc-map":true,"gc-addon":true,"notebook":true,"island":true,"tweak":true}}}})");
        if(mode=="missing")profiles["game_sources"]["gc_wind_waker"]=gone;
        if(mode=="met"||mode=="off")profiles["game_sources"]["gc_wind_waker"]=fs::canonical(disc).string();
        if(mode=="off"){profiles["profiles"]["Default"]["enabled"]["gc-map"]=false;profiles["profiles"]["Default"]["enabled"]["gc-addon"]=false;}
        std::ofstream(storage/"profiles.json")<<mods::json::dump(profiles);
        set_code_mod_support(true);
        initialize();
        std::set<std::string> loaded;
        start_guests([&](const GuestPackage&){return uint32_t(65536);},[&](const GuestPackage& pkg,uint32_t){loaded.insert(pkg.id);});
        frame(1);  // settings mods load here
        assert(view("tweak").active&&view("tweak").reason.empty());
        assert(view("notebook").active&&view("island").active);
        const auto problems=startup_problems();
        if(mode=="met") {
            assert(loaded==(std::set<std::string>{"gc-map","gc-addon","notebook","island"}));
            assert(problems.empty()&&view("gc-map").active&&view("gc-addon").active&&view("gc-map").reason.empty());
        } else if(mode=="off") {
            assert(loaded==(std::set<std::string>{"notebook","island"}));
            assert(problems.empty()&&!view("gc-map").active&&view("gc-map").reason.empty());
        } else {
            const std::string fix=" Choose it in Settings → Mods → GameCube game for mods, or turn the mod off.";
            const std::string why=mode=="missing"?"GC map: the GameCube game for mods was not found at "+gone+"."+fix
                                                 :"GC map needs the GameCube version of the game, and no GameCube game for mods is chosen."+fix;
            assert(loaded==(std::set<std::string>{"notebook","island"}));
            assert(!view("gc-map").active&&!view("gc-addon").active);
            if(view("gc-map").reason!=why){std::cerr<<view("gc-map").reason<<"\n";return 1;}
            assert(view("gc-addon").reason=="GC addon needs GC map, which could not start: "+why);
            assert(problems==(std::vector<std::string>{view("gc-addon").reason,why}));
            // skipped, not switched off: the player's choices stay as they were
            assert(view("gc-map").enabled&&view("gc-addon").enabled);
            {std::ifstream in(storage/"profiles.json");auto saved=mods::json::parse(std::string{std::istreambuf_iterator<char>(in),{}});
             assert(saved.get("profiles").get("Default").get("enabled")==profiles.get("profiles").get("Default").get("enabled"));
             assert(saved.get("game_sources")==profiles.get("game_sources"));}
            // another frame (a re-plan) keeps the reasons and the rest running
            frame(2);assert(view("gc-map").reason==why&&view("tweak").active);
        }
        fs::remove_all(root);std::cout<<"startup skip ("<<mode<<"): the other mods started\n";return 0;
    }
    if(argc==2&&std::string(argv[1])=="--guest-startup") {
        auto root=unique_temp("wwhd-guest-startup-");
        auto storage=root/"manager";
        env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",storage.string().c_str());
        env("WWHD_TEST_TRUST_NATIVE_MODS","guest-a,guest-b,guest-c");
        for(const auto* id:{"guest-a","guest-b","guest-c"}) {
            auto source=storage/"Mods"/id;fs::create_directories(source);
            auto m=mods::json::parse(R"({"format_version":1,"id":"guest-a","name":"Fixture","version":"1.0.0","game_id":"wwhd-usa","kind":"guest","guest":{"api_version":1},"options":[{"id":"amount","name":"Amount","type":"number","min":1,"max":10,"default":3}]})");
            m["id"]=id;
            if(std::string(id)=="guest-b")m["dependencies"]=mods::json::parse(R"([{"id":"guest-a"}])");
            fs::create_directories(source/"content/Common");
            std::ofstream(source/"content/Common"/(std::string(id)+".txt"))<<"original fixture content";
            std::ofstream(source/"manifest.json")<<mods::json::dump(m);
            {std::ofstream elf(source/"mod.elf",std::ios::binary);elf.write("\x7f" "ELF\x01\x02",6);}
        }
        std::ofstream(storage/"profiles.json") << R"({"format_version":1,"active":"Default","profiles":{"Default":{"enabled":{"guest-a":true,"guest-b":true,"guest-c":true}}},"guest_regions":{"guest-a":{"base":2130771968,"size":65536},"guest-b":{"base":2130771968,"size":65536}}})";
        set_code_mod_support(true);
        initialize();std::string error;
        assert(mods::content::replacement("Common/guest-a.txt").empty());
        assert(view("guest-a").content_hashes.size()==1);
        assert(configure("guest-a","amount",5,error));
        int inspected=0,loaded=0;std::map<std::string,uint32_t> bases;
        auto inspect=[&](const GuestPackage& pkg){++inspected;assert(pkg.options.get("amount").number==3);return uint32_t(65536);};
        auto load=[&](const GuestPackage& pkg,uint32_t base){++loaded;bases[pkg.id]=base;
            // the guests' start-up code asks for the story cache folder while start_guests holds the manager
            // mutex (the NPC catalogue): it must answer, not wait on that mutex
            assert(fs::path(mods::packages::story_cache_directory())==storage/"story");
            if(pkg.id=="guest-c")throw std::runtime_error("synthetic compiler failure");};
        start_guests(inspect,load);
        assert(inspected==3&&loaded==3);
        assert(!mods::content::replacement("Common/guest-a.txt").empty());
        assert(!mods::content::replacement("Common/guest-b.txt").empty());
        assert(mods::content::replacement("Common/guest-c.txt").empty());
        assert(bases.at("guest-a")==0x7F010000); // valid persisted region retained
        assert(bases.at("guest-b")==0x7F000000); // duplicate saved reservation repaired
        assert(bases.at("guest-c")==0x7F020000);
        assert(view("guest-a").active&&view("guest-b").active&&!view("guest-c").active);
        assert(view("guest-c").reason=="synthetic compiler failure");
        assert(view("guest-a").pending_restart); // options changed after startup snapshot
        assert(enable("guest-b",false,error));
        frame(1);assert(view("guest-b").active&&view("guest-b").pending_restart);
        start_guests(inspect,load);assert(inspected==3&&loaded==3); // never live reload
        assert(!remove("guest-b",error));
        fs::remove_all(root);std::cout << "guest startup allocation/lifecycle passed\n";return 0;
    }
    if(argc==2&&std::string(argv[1])=="--guest-metadata") {
        auto root=unique_temp("wwhd-guest-metadata-");
        fs::create_directories(root/"source/content/Common");
        std::ofstream(root/"source/content/Common/fixture.txt")<<"original fixture content";
        env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",(root/"manager").string().c_str());
        env("WWHD_TEST_TRUST_NATIVE_MODS",nullptr);
        std::ofstream(root/"source/manifest.json") << R"({"format_version":1,"id":"guest-fixture","name":"Guest fixture","version":"1.0.0","game_id":"wwhd-usa","kind":"guest","guest":{"api_version":1,"elf":"mod.elf"},"options":[{"id":"amount","name":"Amount","type":"number","min":1,"max":10,"default":3}]})";
        {std::ofstream elf(root/"source/mod.elf",std::ios::binary);elf.write("\x7f" "ELF\x01\x02",6);}
        initialize();std::string error;
        assert(install((root/"source").string(),error));
        assert(view("guest-fixture").compatible&&view("guest-fixture").restart_required);
        assert(!view("guest-fixture").native_confirmed);
        {   // story quests naming an NPC the port's catalogue doesn't know: refused with the reason, never ignored
            auto quests=[&](const char* who){
                fs::remove_all(root/"quests");fs::create_directories(root/"quests");
                std::ofstream(root/"quests/manifest.json")<<R"({"format_version":1,"id":"quest-fixture","name":"Quest fixture","version":"1.0.0","game_id":"wwhd-usa","kind":"guest","guest":{"api_version":1,"elf":"mod.elf"},"quests_format":1,"quests":[{"id":"q","title":"Q","steps":[{"n":1,"text":"Go.","npc":{"who":")"<<who<<R"(","lines":["Hi."]}}]}]})";
                std::ofstream elf(root/"quests/mod.elf",std::ios::binary);elf.write("\x7f" "ELF\x01\x02",6);
            };
            quests("windfall-nobody");
            assert(!install((root/"quests").string(),error)&&error.find("no NPC named \"windfall-nobody\"")!=std::string::npos);
            quests("windfall-um1");
            assert(install((root/"quests").string(),error)&&view("quest-fixture").compatible);
            // every quest mod depends on the story system's required package, declared or not
            set_code_mod_support(true);
            const auto deps=view("quest-fixture").dependencies;
            assert(std::find(deps.begin(),deps.end(),"story-notebook>=0.0.0")!=deps.end());
            assert(!enable("quest-fixture",true,error)&&error.find("Needs the Story Notebook quest")!=std::string::npos);
        }
        {   // the Story Notebook's "notebook" block: its look is exempt from the HUD texture budget, with limits of its own
            static const char kPng[]="\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\x0d\x49\x44\x41\x54\x78\xda\x63\x64\x60\xf8\x5f\x0f\x00\x02\x87\x01\x80\xeb\x47\xba\x92\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82";
            auto png=[&](const fs::path& at){std::ofstream(at,std::ios::binary).write(kPng,sizeof kPng-1);};
            auto package=[&](const std::string& id,bool look){
                const auto dir=root/("look-"+id);fs::remove_all(dir);fs::create_directories(dir/"assets/layout");
                std::ofstream(dir/"manifest.json")<<R"({"format_version":1,"id":")"<<id<<R"(","name":"Look fixture","version":"1.0.0","game_id":"wwhd-usa","kind":"guest","guest":{"api_version":1,"elf":"mod.elf"})"
                    <<(look?R"(,"notebook":{"icon":"assets/icon.png","model":"assets/model.glb","layout":"assets/layout/page.json"})":"")<<"}";
                std::ofstream elf(dir/"mod.elf",std::ios::binary);elf.write("\x7f" "ELF\x01\x02",6);
                png(dir/"assets/icon.png");std::ofstream(dir/"assets/model.glb",std::ios::binary)<<"glTF synthetic";
                std::ofstream(dir/"assets/layout/page.json")<<"{}";
                for(int i=0;i<40;i++)png(dir/("assets/layout/art"+std::to_string(i)+".png"));  // more than the HUD budget's 32
                return dir;
            };
            assert(install(package("story-notebook",true).string(),error));
            assert(!install(package("hud-only",false).string(),error)&&error.find("32 HUD textures")!=std::string::npos);  // the HUD budget is unchanged
            assert(!install(package("look-elsewhere",true).string(),error)&&error.find("Only the Story Notebook package")!=std::string::npos);
            auto dir=package("story-notebook",true);std::ofstream(dir/"assets/model.glb",std::ios::binary)<<"not a model";
            assert(!install(dir.string(),error)&&error.find("binary glTF")!=std::string::npos);
            // part of the story system: it can't be switched off or removed while the story system is on
            assert(install(package("story-notebook",true).string(),error)&&confirm_native("story-notebook",error)&&enable("story-notebook",true,error));
            assert(!enable("story-notebook",false,error)&&error.find("part of the story system")!=std::string::npos);
            assert(!remove("story-notebook",error)&&error.find("part of the story system")!=std::string::npos);
            mods::set_story_system(false);
            assert(enable("story-notebook",false,error)&&remove("story-notebook",error));
            mods::set_story_system(true);
            assert(remove("quest-fixture",error));
            set_code_mod_support(false);
        }
        assert(unconfirmed_native("guest-fixture").size()==1);
        assert(!enable("guest-fixture",true,error));
        assert(confirm_native("guest-fixture",error));
        assert(needs_code_mod_support("guest-fixture"));
        assert(enable_after_code_rebuild("guest-fixture",error));
        assert(!view("guest-fixture").enabled); // queuing cannot enable the old running build
        assert(host::run_process({argv[0],"--guest-pending-check","off"}).code==0);
        assert(host::run_process({argv[0],"--guest-pending-check","on"}).code==0);
        assert(!enable("guest-fixture",true,error));
        assert(error.find("code-mod support")!=std::string::npos);
        set_code_mod_support(true);
        assert(!needs_code_mod_support("guest-fixture"));
        assert(enable("guest-fixture",true,error));
        assert(view("guest-fixture").pending_restart);
        assert(configure("guest-fixture","amount",4,error));
        assert(!configure("guest-fixture","amount",99,error));
        frame(1);assert(!view("guest-fixture").active); // never load a PowerPC ELF as a host library
        assert(enable("guest-fixture",false,error));
        std::ofstream(root/"source/content/Common/fixture.txt")<<"changed fixture content";
        assert(install((root/"source").string(),error));
        assert(!view("guest-fixture").native_confirmed); // content changes revoke package trust
        assert(confirm_native("guest-fixture",error));
        {std::ofstream elf(root/"source/mod.elf",std::ios::binary|std::ios::app);elf << "changed";}
        assert(install((root/"source").string(),error));
        assert(!view("guest-fixture").native_confirmed); // trust fingerprints the ELF, not its name
        fs::create_directories(root/"source/assets");
        std::ofstream(root/"source/assets/bad.png")<<"invalid synthetic PNG";
        assert(!install((root/"source").string(),error));
        assert(error.find("PNG")!=std::string::npos);
        fs::remove_all(root/"source/assets");
        fs::rename(root/"source/mod.elf",root/"source/saved.elf");
        assert(!install((root/"source").string(),error));
        assert(error.find("ELF is missing")!=std::string::npos);
        fs::rename(root/"source/saved.elf",root/"source/mod.elf");
        std::ifstream manifest_file(root/"source/manifest.json");
        std::string manifest_text((std::istreambuf_iterator<char>(manifest_file)),{});
        manifest_file.close();
        auto missing_content=mods::json::parse(manifest_text);
        missing_content["content_dir"]="missing";
        std::ofstream(root/"source/manifest.json")<<mods::json::dump(missing_content);
        assert(!install((root/"source").string(),error));
        std::ofstream(root/"source/manifest.json")<<manifest_text;
        assert(install((root/"source").string(),error));
        assert(confirm_native("guest-fixture",error));
        assert(enable("guest-fixture",true,error));
        fs::create_directories(root/"overlap/content/Common");
        std::ofstream(root/"overlap/content/Common/fixture.txt")<<"different original fixture";
        std::ofstream(root/"overlap/manifest.json")<<R"({"format_version":1,"id":"content-overlap","name":"Overlap","version":"1.0.0","game_id":"wwhd-usa","kind":"content","content_dir":"content"})";
        assert(install((root/"overlap").string(),error));
        assert(!enable("content-overlap",true,error));
        assert(error.find("Content file conflict")!=std::string::npos);
        assert(view("guest-fixture").enabled&&!view("content-overlap").enabled);
        assert(enable("guest-fixture",false,error));
        assert(enable("content-overlap",true,error));
        {std::ofstream(root/"source/manifest.json") << R"({"format_version":1,"id":"guest-fixture","name":"Guest fixture","version":"1.0.0","game_id":"wwhd-usa","kind":"guest","guest":{"api_version":2}})";}
        assert(!install((root/"source").string(),error));
        fs::remove_all(root);std::cout << "guest package metadata/trust passed\n";return 0;
    }
    if(argc == 3 && std::string(argv[1]) == "--restart") return restart_check(argv[2]);
    if(argc==3&&(std::string(argv[1])=="--cemu-conflicts-restart"||std::string(argv[1])=="--cemu-conflicts-legacy")){
        env("WWHD_MOD_MANAGER_DIR",argv[2]);mods::cemu::set_vulkan(true);initialize();
        assert(view("a").enabled&&view("a").active&&!view("a").pending_restart);
        assert(!view("b").enabled&&!view("b").active);
        assert(take_notices().size()==(std::string(argv[1])=="--cemu-conflicts-legacy"?1:0));
        mods::cemu::report_shader(1,2,false,true,{});
        assert(view("a").status.find("1 applied")!=std::string::npos);
        return 0;
    }
    if(argc==2&&std::string(argv[1])=="--cemu-conflicts"){
        auto root=unique_temp("wwhd-cemu-conflicts-");
        auto storage=root/"storage";
        auto make=[&](const std::string& id,const std::string& rules,int shader){
            auto folder=storage/"Mods"/id;fs::create_directories(folder);
            std::ofstream(folder/"manifest.json")<<"{\"format_version\":1,\"id\":\""<<id<<"\",\"name\":\""<<id<<"\",\"version\":\"1.0.0\",\"game_id\":\"wwhd-usa\",\"kind\":\"cemu\",\"cemu_dir\":\"\"}";
            std::ofstream(folder/"rules.txt")<<"[Definition]\nname=Test\ntitleIds=0005000010143500\nversion=4\n"<<rules;
            if(shader)std::ofstream(folder/(shader==1?"0000000000000001_0000000000000002_ps.txt":"0000000000000003_0000000000000004_ps.txt"))<<"#version 420\nvoid main(){}\n";
        };
        make("a","",1);make("b","",1);make("c","",2);make("d","",1);
        std::ofstream(storage/"Mods/d/0000000000000003_0000000000000004_ps.txt")<<"#version 420\nvoid main(){}\n";
        make("r1","[TextureRedefine]\nwidth=640\noverwriteWidth=1280\n",0);
        make("r2","[Preset]\nname=Separate\n$width=320\n[Preset]\nname=Overlap\n$width=640\n[TextureRedefine]\nwidth=$width\noverwriteWidth=1280\n",0);
        auto legacy=mods::json::parse(R"({"enabled":{"a":true,"b":true,"c":true},"enabled_since":{"a":2,"b":1},"enable_serial":2})");
        mods::json::Value db;db["format_version"]=1;db["active"]="Default";db["profiles"]["Default"]=legacy;
        db["profiles"]["Legacy"]=legacy;db["profiles"]["Legacy"]["enabled_since"]["a"]=0;
        std::ofstream(storage/"profiles.json")<<mods::json::dump(db);
        env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",storage.string().c_str());mods::cemu::set_vulkan(true);initialize();
        assert(!view("a").enabled&&view("b").enabled&&view("b").active&&view("c").active);
        auto messages=take_notices();assert(messages.size()==1&&messages[0]=="a was turned off: it conflicts with b");assert(take_notices().empty());
        assert(view("a").graphics_conflicts.size()==1&&view("a").graphics_conflicts[0].id=="b");
        std::string error;
        assert(!enable("a",true,error)); // Cancel / ordinary enable cannot mutate the profile.
        assert(view("b").enabled&&!view("a").enabled);
        assert(view("d").graphics_conflicts.size()==2);
        assert(enable("d",true,error,true));assert(view("d").enabled&&!view("b").enabled&&!view("c").enabled);
        assert(enable("b",true,error,true)&&enable("c",true,error));assert(!view("d").enabled);
        fs::create_directory(storage/"profiles.json.tmp"); // Failed save must leave both choices untouched.
        assert(!enable("a",true,error,true));assert(view("b").enabled&&!view("a").enabled);
        fs::remove(storage/"profiles.json.tmp");
        assert(enable("a",true,error,true));
        assert(view("a").enabled&&!view("b").enabled&&view("c").enabled);
        assert(!view("a").active&&view("a").pending_restart&&view("b").active&&view("b").pending_restart);
        assert(select_profile("Legacy",error)); // Actual active pack wins over this profile's history.
        assert(!view("a").enabled&&view("b").enabled&&take_notices().size()==1);
        assert(select_profile("Default",error));assert(view("a").enabled&&!view("b").enabled);
        assert(enable("r1",true,error)&&enable("r2",true,error)); // Independent rules coexist.
        assert(configure("r2","preset-0","Overlap",error)); // Imported / selected overlapping preset is repaired.
        assert(view("r1").enabled&&!view("r2").enabled&&take_notices().size()==1);
        assert(view("r2").graphics_conflicts.size()==1);
        assert(enable("r2",true,error,true));assert(!view("r1").enabled&&view("r2").enabled);
        std::ifstream saved(storage/"profiles.json");auto persisted=mods::json::parse(std::string(std::istreambuf_iterator<char>(saved),{}));
        saved.close();  // Windows: an open read handle makes the rewrite below fail silently
        assert(!persisted.get("profiles").get("Default").get("enabled").get("b").boolean);
        assert(host::run_process({argv[0],"--cemu-conflicts-restart",storage.string()}).code==0);
        persisted["profiles"]["Default"]["enabled"]["b"]=true;
        persisted["profiles"]["Default"].object.erase("enabled_since");
        {std::ofstream output(storage/"profiles.json");output<<mods::json::dump(persisted);}
        assert(host::run_process({argv[0],"--cemu-conflicts-legacy",storage.string()}).code==0);
        fs::remove_all(root);std::cout<<"Synthetic Cemu conflict previews, atomic switch, migration, profiles, presets and restart passed\n";return 0;
    }
    if(argc==2&&(std::string(argv[1])=="--cemu-startup"||std::string(argv[1])=="--cemu-backend")){
        bool backend=std::string(argv[1])=="--cemu-backend";
        auto root=unique_temp("wwhd-cemu-startup-");
        auto storage=root/"storage",pack=storage/"Mods"/"cemu.test";fs::create_directories(pack);
        std::ofstream(pack/"manifest.json")<<R"({"format_version":1,"id":"cemu.test","name":"Test","version":"1.0.0","game_id":"wwhd-usa","kind":"cemu","cemu_dir":""})";
        std::ofstream(pack/"rules.txt")<<"[Definition]\nname=Test\ntitleIds=0005000010143500\nversion=4\n[Preset]\nname=Normal\n$scale=1\n[Preset]\nname=Double\n$scale=2\n[TextureRedefine]\nwidth=1280\nheight=720\noverwriteWidth=1280*$scale\n";
        std::ofstream(storage/"profiles.json")<<R"({"format_version":1,"active":"Default","profiles":{"Default":{"enabled":{"cemu.test":true},"config":{"cemu.test":{"preset-0":"Double"}}}}})";
        if(backend){
            std::ofstream(pack/"0000000000000001_0000000000000002_ps.txt")<<"#version 420\nvoid main(){}\n";
            auto content=storage/"Mods"/"content.test";fs::create_directories(content/"content"/"Common");
            std::ofstream(content/"content"/"Common"/"test.bin")<<"synthetic content";
            std::ofstream(content/"manifest.json")<<R"({"format_version":1,"id":"content.test","name":"Content","version":"1.0.0","game_id":"wwhd-usa","kind":"content","content_dir":"content"})";
            std::ofstream(storage/"profiles.json")<<R"({"format_version":1,"active":"Default","profiles":{"Default":{"enabled":{"cemu.test":true,"content.test":true}}}})";
        }
        env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",storage.string().c_str());initialize();
        if(backend){
            assert(!mods::content::replacement("Common/test.bin").empty());
            for(const auto& view:list())if(view.id=="cemu.test")assert(!view.active&&!view.compatible&&view.enabled);
            std::string error;assert(enable("cemu.test",false,error));assert(remove("cemu.test",error));
            assert(!mods::content::replacement("Common/test.bin").empty());fs::remove_all(root);
            std::cout<<"Unavailable shader backend preserves content and permits disabling shader packs\n";return 0;
        }
        uint32_t width=0,height=0;assert(mods::cemu::texture_extent(1280,720,0x80e,1,4,width,height)&&width==2560&&height==720);
        std::string error;assert(list().at(0).active&&!list().at(0).pending_restart);
        assert(configure("cemu.test","preset-0","Normal",error));assert(list().at(0).pending_restart);
        assert(mods::cemu::texture_extent(1280,720,0x80e,1,4,width,height)&&width==2560);
        assert(enable("cemu.test",false,error));frame(100);assert(list().at(0).active&&list().at(0).pending_restart);
        assert(!remove("cemu.test",error));assert(!install(pack.string(),error));fs::remove_all(root);
        std::cout<<"Cemu startup presets and restart-only immutable lifecycle passed\n";return 0;
    }
    if(argc==3&&std::string(argv[1])=="--world-content-startup") {
        auto root=unique_temp("wwhd-world-content-test-");
        auto storage=root/"storage";auto pack=storage/"Mods"/"sdk-v3-windfall";
        fs::create_directories(pack.parent_path());fs::copy(fs::absolute(argv[2]),pack,fs::copy_options::recursive);
        auto notebook=storage/"Mods"/"story-notebook";fs::create_directories(notebook);
        std::ofstream(notebook/"manifest.json")<<R"({"format_version":1,"id":"story-notebook","name":"Notebook fixture","version":"1.0.0","game_id":"wwhd","kind":"guest","guest":{"api_version":1}})";
        {std::ofstream elf(notebook/"mod.elf",std::ios::binary);elf.write("\x7f" "ELF\x01\x02",6);}
        std::ofstream(storage/"profiles.json")<<R"({"format_version":1,"active":"Default","profiles":{"Default":{"enabled":{"sdk-v3-windfall":true,"story-notebook":true}}}})";
        env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",storage.string().c_str());env("WWHD_TEST_TRUST_NATIVE_MODS","story-notebook");
        set_code_mod_support(true);initialize();
        assert(view("sdk-v3-windfall").compatible&&view("sdk-v3-windfall").enabled&&view("sdk-v3-windfall").active);
        assert(view("story-notebook").compatible&&view("story-notebook").enabled);
        unsigned loaded=0;start_guests([](const GuestPackage&){return uint32_t(65536);},[&](const GuestPackage& pkg,uint32_t){assert(pkg.id=="story-notebook");++loaded;});
        assert(loaded==1&&view("story-notebook").active);
        // Other content dependencies retain their previous restriction.
        auto invalid=root/"invalid";fs::copy(pack,invalid,fs::copy_options::recursive);
        mods::json::Value bad;{std::ifstream input(invalid/"manifest.json");bad=mods::json::parse(std::string{std::istreambuf_iterator<char>(input),{}});}  // closed before rewriting and removing the file (Windows)
        bad["id"]="plain-content-dependency";bad.object.erase("actors");bad.object.erase("scenes");bad.object.erase("models");
        std::ofstream(invalid/"manifest.json")<<mods::json::dump(bad);std::string error;assert(!install(invalid.string(),error));
        bad["id"]="world-content-other-dependency";bad["actors"]=mods::json::parse(R"([{"id":"notice","actor":"sign","stage":"sea","room":11,"flag":"read","position":{"x":0,"y":0,"z":0}}])");
        bad["dependencies"].array.push_back(mods::json::parse(R"({"id":"other-content"})"));
        std::ofstream(invalid/"manifest.json")<<mods::json::dump(bad);assert(!install(invalid.string(),error));
        assert(error.find("only depend on the Story Notebook")!=std::string::npos);
        {   // story content without actors or scenes: a chart and its letter, no content folder, the notebook added
            auto chart=root/"chart-letter";fs::create_directories(chart);
            std::ofstream(chart/"manifest.json")<<R"({"format_version":1,"id":"chart-letter","name":"Old map","version":"1.0.0","game_id":"wwhd","kind":"content",
              "charts":[{"id":"old-map","name":"Old map","place":{"x":1759,"z":-202572}}],
              "letters":[{"id":"map-letter","sender":"A sailor","text":"Dear Link,\nan old map.","encloses":["chart:old-map"]}]})";
            assert(install(chart.string(),error)&&view("chart-letter").compatible);
            auto deps=view("chart-letter").dependencies;
            assert(deps.size()==1&&deps[0]=="story-notebook>=0.0.0");
            assert(enable("chart-letter",true,error)&&view("chart-letter").enabled);
            assert(enable("chart-letter",false,error));
            // an item on its own, and a quest on its own, the same way
            for(const char* section:{R"("items":[{"id":"key","name":"Key","description":"A key.","icon":"icons/key.png"}])",
                                     R"("quests_format":1,"quests":[{"id":"q","title":"Q","steps":[{"n":1,"text":"Go."}]}])"}){
                auto one=root/"story-one";fs::remove_all(one);fs::create_directories(one/"icons");
                std::ofstream(one/"manifest.json")<<R"({"format_version":1,"id":"story-one","name":"One","version":"1.0.0","game_id":"wwhd","kind":"content",)"<<section<<"}";
                assert(install(one.string(),error)&&view("story-one").compatible);
                assert(view("story-one").dependencies.size()==1);
                assert(remove("story-one",error));
            }
            // an empty actor list next to game files: the notebook dependency is accepted too
            auto empty=root/"empty-actors";fs::create_directories(empty/"content");std::ofstream(empty/"content"/"fixture.marker")<<"original fixture";
            std::ofstream(empty/"manifest.json")<<R"({"format_version":1,"id":"empty-actors","name":"Empty","version":"1.0.0","game_id":"wwhd","kind":"content","content_dir":"content","actors":[]})";
            assert(install(empty.string(),error)&&view("empty-actors").compatible);
            // the protection stays: plain game files can't depend on anything, story content on nothing but the notebook
            std::ofstream(empty/"manifest.json")<<R"({"format_version":1,"id":"plain-notebook","name":"Plain","version":"1.0.0","game_id":"wwhd","kind":"content","content_dir":"content","dependencies":[{"id":"story-notebook"}]})";
            assert(!install(empty.string(),error)&&error.find("only depend on the Story Notebook")!=std::string::npos);
            std::ofstream(chart/"manifest.json")<<R"({"format_version":1,"id":"chart-other","name":"Old map","version":"1.0.0","game_id":"wwhd","kind":"content","dependencies":[{"id":"guest-code"}],
              "charts":[{"id":"old-map","name":"Old map","place":{"x":1759,"z":-202572}}]})";
            assert(!install(chart.string(),error)&&error.find("only depend on the Story Notebook")!=std::string::npos);
            // a named content folder must still be there
            std::ofstream(chart/"manifest.json")<<R"({"format_version":1,"id":"chart-folder","name":"Old map","version":"1.0.0","game_id":"wwhd","kind":"content","content_dir":"missing",
              "charts":[{"id":"old-map","name":"Old map","place":{"x":1759,"z":-202572}}]})";
            assert(!install(chart.string(),error)&&error.find("Content folder is missing")!=std::string::npos);
            assert(remove("chart-letter",error)&&remove("empty-actors",error));
        }
        // Manifest-only owners must not install fake game replacements to carry scenes.
        auto scene_only=root/"scene-only";fs::create_directories(scene_only/"content");
        auto scene_manifest=mods::json::parse(R"({"format_version":1,"id":"scene-only","name":"Scene only","version":"1.0.0","game_id":"wwhd","kind":"content","content_dir":"content","scenes":[{"id":"test","trigger":{"type":"area","stage":"sea","room":11,"position":{"x":0,"y":0,"z":0},"radius":80},"steps":[{"type":"wait","frames":1},{"type":"end"}]}]})");
        std::ofstream(scene_only/"manifest.json")<<mods::json::dump(scene_manifest);
        assert(install(scene_only.string(),error)&&view("scene-only").compatible);
        assert(mods::content::index(scene_only/"content",true).empty());
        assert(mods::content::replacement("/vol/content/scene-only.marker").empty());
        scene_manifest["id"]="empty-content";scene_manifest.object.erase("scenes");
        std::ofstream(scene_only/"manifest.json")<<mods::json::dump(scene_manifest);
        assert(!install(scene_only.string(),error)&&error.find("contains no files")!=std::string::npos);
        scene_manifest["id"]="empty-scene-array";scene_manifest["scenes"]=mods::json::parse("[]");
        std::ofstream(scene_only/"manifest.json")<<mods::json::dump(scene_manifest);
        assert(!install(scene_only.string(),error)&&error.find("contains no files")!=std::string::npos);
        scene_manifest["id"]="malformed-scene";scene_manifest["scenes"]=mods::json::parse(R"([{"id":"bad"}])");
        std::ofstream(scene_only/"manifest.json")<<mods::json::dump(scene_manifest);
        assert(!install(scene_only.string(),error));
        // The game's own objects by reference: the package carries no game data (by name or by content).
        {
            auto objects=root/"objects";fs::create_directories(objects/"content");std::ofstream(objects/"content"/"objects.marker")<<"original fixture";
            auto om=mods::json::parse(R"({"format_version":1,"id":"objects","name":"Game objects","version":"1.0.0","game_id":"wwhd","kind":"content","content_dir":"content","requires":{"game_objects":1},"actors":[{"id":"palm","actor":"game:palm","stage":"sea","room":11,"position":{"x":0,"y":0,"z":0}}]})");
            std::ofstream(objects/"manifest.json")<<mods::json::dump(om);
            assert(install(objects.string(),error)&&view("objects").compatible);
            fs::create_directories(objects/"assets");
            for(const auto& [name,bytes]:std::vector<std::pair<std::string,std::string>>{{"Oyashi.bfres","x"},{"room.dzb","x"},{"tex.ftex","x"},{"Lwood.szs","x"},{"renamed.bin","Yaz0...."},{"data.dat","SARC...."},{"model.png","FRES...."}}){
                om["id"]="objects-"+std::to_string(name.size())+name.substr(0,3);for(char& ch:om["id"].text)if(ch=='.')ch='-';for(char& ch:om["id"].text)ch=char(std::tolower((unsigned char)ch));
                std::ofstream(objects/"manifest.json")<<mods::json::dump(om);
                {std::ofstream f(objects/"assets"/name,std::ios::binary);f<<bytes;}
                assert(!install(objects.string(),error)&&error.find("game data")!=std::string::npos&&error.find(name)!=std::string::npos);
                fs::remove(objects/"assets"/name);
            }
            // island buildings by reference: the same rule (no BFRES, SZS, DZB or other game data in the package)
            {
                auto pm=mods::json::parse(R"({"format_version":1,"id":"buildings","name":"Buildings","version":"1.0.0","game_id":"wwhd","kind":"content","content_dir":"content","requires":{"game_objects":2},"actors":[{"id":"house","actor":"piece:outset-house-small","stage":"sea","room":12,"position":{"x":0,"y":0,"z":0},"rotation":{"x":0,"y":90,"z":0}}]})");
                auto buildings=root/"buildings";fs::create_directories(buildings/"content");std::ofstream(buildings/"content"/"buildings.marker")<<"original fixture";
                std::ofstream(buildings/"manifest.json")<<mods::json::dump(pm);
                assert(install(buildings.string(),error)&&view("buildings").compatible);
                fs::create_directories(buildings/"assets");
                for(const auto& [name,bytes]:std::vector<std::pair<std::string,std::string>>{{"room.dzb","x"},{"Room44.bfres","x"},{"house.bin","FRES...."},{"sea_Room44.szs","x"}}){
                    pm["id"]="buildings-"+std::to_string(name.size());std::ofstream(buildings/"manifest.json")<<mods::json::dump(pm);
                    {std::ofstream f(buildings/"assets"/name,std::ios::binary);f<<bytes;}
                    assert(!install(buildings.string(),error)&&error.find("game data")!=std::string::npos&&error.find(name)!=std::string::npos);
                    fs::remove(buildings/"assets"/name);
                }
            }
            // a package without game objects keeps its own rules (a content mod may replace game files)
            std::ofstream(objects/"assets"/"Lwood.szs")<<"x";om["id"]="objects-none";om.object.erase("actors");om.object.erase("requires");
            std::ofstream(objects/"manifest.json")<<mods::json::dump(om);assert(install(objects.string(),error));
        }
        auto world_fixture=[&](const std::string& id,unsigned count){
            auto source=root/id;fs::create_directories(source/"content");std::ofstream(source/"content"/(id+".marker"))<<"original fixture";
            auto m=mods::json::parse(R"({"format_version":1,"id":"fixture","name":"World graph fixture","version":"1.0.0","game_id":"wwhd","kind":"content","content_dir":"content","actors":[]})");m["id"]=id;
            for(unsigned i=0;i<count;++i){auto actor=mods::json::parse(R"({"id":"resident","actor":"resident","stage":"sea","room":0,"position":{"x":0,"y":0,"z":0}})");actor["id"]="resident"+std::to_string(i);m["actors"].array.push_back(actor);}
            std::ofstream(source/"manifest.json")<<mods::json::dump(m);assert(install(source.string(),error));
        };
        world_fixture("graph-a",9);world_fixture("graph-b",8);
        assert(enable("graph-a",true,error));assert(!enable("graph-b",true,error)&&error.find("16 actors")!=std::string::npos);
        assert(view("graph-a").enabled&&!view("graph-b").enabled);
        {std::error_code ignored;fs::remove_all(root,ignored);}std::cout<<"Real world-content example starts with notebook dependency; content dependency policy and aggregate actor graph validated\n";return 0;
    }
    if(argc==2&&std::string(argv[1])=="--content-startup"){
        auto root=unique_temp("wwhd-content-test-");
        auto storage=root/"storage";auto pack=storage/"Mods"/"content.test";
        fs::create_directories(pack/"content"/"Common");
        std::ofstream(pack/"content"/"Common"/"fixture.bin")<<"synthetic replacement";
        fs::create_directories(pack/"content"/"Common"/"Pack");std::ofstream(pack/"content"/"Common"/"Pack"/"permanent_2d_EuEnglish.pack")<<"SARCsynthetic translation";
        std::ofstream(pack/"manifest.json")<<R"({"format_version":1,"id":"content.test","name":"Test","version":"1.0.0","game_id":"wwhd-usa","kind":"content","content_dir":"content"})";
        std::ofstream(storage/"profiles.json")<<R"({"format_version":1,"active":"Default","profiles":{"Default":{"enabled":{"content.test":true}}}})";
        env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",storage.string().c_str());
        assert(mods::content::replacement("/vol/content/Common/fixture.bin").empty());initialize();
        auto file=mods::content::replacement("/vol/content/common/FIXTURE.bin");assert(file==(pack/"content"/"Common"/"fixture.bin").string());
        assert(mods::content::replacement("Common/fixture.bin")==file);
        for(auto path:{"/vol/save/Common/fixture.bin","/vol/code/Common/fixture.bin","/vol/contentX/Common/fixture.bin","/vol/content/../Common/fixture.bin","Common/../Common/fixture.bin","Common\\fixture.bin"})assert(mods::content::replacement(path).empty());
        for(auto mode:{"w","a","r+","r+b","wb"})assert(mods::content::replacement("Common/fixture.bin",mode).empty());
        assert(mods::content::replacement("Common/absent.bin").empty());
        // a fan translation's language pack serves that language whatever region its file name has (exact names first)
        auto translation=(pack/"content"/"Common"/"Pack"/"permanent_2d_EuEnglish.pack").string();
        assert(mods::content::replacement("/vol/content/Common/Pack/permanent_2d_EuEnglish.pack")==translation);
        assert(mods::content::replacement("/vol/content/Common/Pack/permanent_2d_UsEnglish.pack")==translation);
        for(auto other:{"permanent_2d_UsFrench.pack","permanent_2d_EuGerman.pack","permanent_2d_JpJapanese.pack","permanent_3d.pack","permanent_2d_UsEnglish.pack.bak"})
            assert(mods::content::replacement(std::string("/vol/content/Common/Pack/")+other).empty());
        assert(mods::content::replacement("/vol/content/Common/Layout/permanent_2d_UsEnglish.pack").empty());
        // the European region reads its pack through the "local" device (Cafe/JP/Pack), no disc folder
        assert(mods::content::replacement("/vol/content/Cafe/JP/Pack/permanent_2d_EuEnglish.pack")==translation);
        assert(mods::content::replacement("/vol/content/Cafe/JP/Pack/permanent_2d_EuGerman.pack").empty());
        assert(mods::content::replacement("/vol/content/Cafe/JP/Packs/permanent_2d_EuEnglish.pack").empty());
        assert(mods::content::replacement("/vol/content/Common/Pack/permanent_2d_UsEnglish.pack","wb").empty());
        std::string error;assert(list().at(0).active&&list().at(0).restart_required);assert(enable("content.test",false,error));frame(100);
        assert(list().at(0).active&&!list().at(0).enabled);assert(mods::content::replacement("Common/fixture.bin")==file);
        assert(!remove("content.test",error));assert(!install(pack.string(),error));
        // Profile changes also retain the startup content snapshot.
        assert(create_profile("Other",error));assert(select_profile("Other",error));frame(101);assert(mods::content::replacement("Common/fixture.bin")==file);
        fs::remove_all(root);std::cout<<"Startup overrides, read-only routing, boundaries, and restart lifecycle passed\n";return 0;
    }
    assert(argc == 3 || argc == 4);
    env("WWHD_TEST_TRUST_NATIVE_MODS",nullptr);
    auto root=fs::path(argv[1])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    assert(!fs::exists(root));
    fs::create_directories(root);
    env("WWHD_NO_HOST_INPUT","1");
    env("WWHD_MOD_MANAGER_DIR",(root/"storage").string().c_str());
    initialize();
    std::string error;
    auto package=[&](const char* id, const char* extra, const char* setting="wall-climb") {
        auto path=root/id;fs::create_directories(path);
        std::ofstream(path/"manifest.json") << "{\"format_version\":1,\"id\":\"" << id
          << "\",\"name\":\"" << id << "\",\"version\":\"1.0.0\",\"game_id\":\"wwhd-usa\","
          << "\"kind\":\"settings\",\"settings\":{\"" << setting << "\":true}" << extra << "}";
        return path.string();
    };
    assert(install(package("climb-preset",""),error));
    assert(list().size()==1 && list()[0].id=="climb-preset");
    assert(list()[0].native_confirmed && unconfirmed_native("climb-preset").empty()); // settings presets never ask
    assert(enable("climb-preset",true,error));frame(1);assert(state[3] && list()[0].active);
    assert(!remove("climb-preset",error));
    assert(enable("climb-preset",false,error));frame(2);assert(!state[3]);
    mods::set_fast_forward_rate(3); mods::set_fast_forward_button(0x20); mods::set_fast_forward_mute(false);
    remember_option("fast-forward.rate", 3); remember_option("fast-forward.button", 0x20); remember_option("fast-forward.mute", 0);
    for (const char* mode : {"saved", "legacy", "invalid", "env"}) {
        auto result=host::run_process({argv[0],"--lock-on-startup",mode});
        if(result.code!=0)std::cerr<<result.output;
        assert(result.code==0);
    }
    assert(!mods::extended_lock_on() && mods::lock_on_range()==1.f && mods::lock_on_cycling());
    mods::set_lock_on_range(2.5f);mods::set_lock_on_cycling(false);
    assert(mods::manager::set_enabled("lock-on",true));
    assert(mods::manager::set_enabled("target-letterbox",false));
    remember_option("lock-on.range",2.5);remember_option("lock-on.cycling",0);
    mods::set_move_speed_bar(false);remember_option("move-speed.bar",0);  // "Show boost bar" off
    assert(mods::manager::set_enabled("camera-directed-items",true));
    assert(create_profile("Adventure",error));
    assert(mods::manager::set_enabled("camera-directed-items",false));
    assert(mods::manager::set_enabled("target-letterbox",true));
    mods::set_lock_on_range(1.f);mods::set_lock_on_cycling(true);
    assert(mods::manager::set_enabled("lock-on",false));
    remember_option("lock-on.range",1);remember_option("lock-on.cycling",1);
    mods::set_move_speed_bar(true);remember_option("move-speed.bar",1);
    mods::set_fast_forward_rate(4); mods::set_fast_forward_button(0x80); mods::set_fast_forward_mute(true);
    remember_option("fast-forward.rate", 4); remember_option("fast-forward.button", 0x80); remember_option("fast-forward.mute", 1);
    assert(enable("climb-preset",true,error));frame(20);assert(state[3]);
    assert(select_profile("Adventure",error));frame(21);assert(!state[3]);
    assert(!mods::letterbox_while_targeting());
    assert(mods::fast_forward_rate()==3 && mods::fast_forward_button()==0x20 && !mods::fast_forward_mute());
    assert(mods::camera_directed_items());
    assert(mods::extended_lock_on() && mods::lock_on_range()==2.5f && !mods::lock_on_cycling());
    assert(!mods::move_speed_bar());
    {std::ifstream saved_file(root/"storage/profiles.json");
     auto saved=mods::json::parse(std::string{std::istreambuf_iterator<char>(saved_file),{}});
     const auto& options=saved.get("profiles").get("Adventure").get("builtin_options");
     assert(options.get("lock-on.range").number==2.5);
     assert(options.get("lock-on.cycling").type==mods::json::Value::Bool && !options.get("lock-on.cycling").boolean);
     assert(options.get("move-speed.bar").type==mods::json::Value::Bool && !options.get("move-speed.bar").boolean);}
    assert(current_profile()=="Adventure");assert(!delete_profile("Adventure",error));
    assert(select_profile("Default",error));assert(mods::fast_forward_rate()==4 && mods::fast_forward_button()==0x80 && mods::fast_forward_mute());assert(mods::move_speed_bar());assert(delete_profile("Adventure",error));
    assert(!mods::extended_lock_on() && mods::lock_on_range()==1.f && mods::lock_on_cycling());
    assert(mods::letterbox_while_targeting());
    assert(!mods::camera_directed_items()); // Default profile preserves its independent off value.
    assert(install(package("missing-dep",",\"dependencies\":[{\"id\":\"absent\"}]"),error));
    assert(!enable("missing-dep",true,error));
    assert(remove("missing-dep",error));
    assert(install(package("cycle-a",",\"dependencies\":[{\"id\":\"cycle-b\"}]"),error));
    assert(install(package("cycle-b",",\"dependencies\":[{\"id\":\"cycle-a\"}]","quick-doors"),error));
    assert(!enable("cycle-a",true,error));assert(error.find("cycle")!=std::string::npos);
    assert(remove("cycle-a",error));assert(remove("cycle-b",error));
    assert(install(package("conflicting",",\"conflicts\":[\"climb-preset\"]","quick-doors"),error));
    assert(!enable("conflicting",true,error));assert(remove("conflicting",error));
    assert(install(package("typed",R"(,"options":[{"id":"toggle","name":"Toggle","type":"bool","default":false},{"id":"rate","name":"Rate","type":"number","min":1,"max":10,"default":2},{"id":"mode","name":"Mode","type":"enum","choices":["a","b"],"default":"a"}])","quick-doors"),error));
    assert(configure("typed","toggle",true,error));assert(!configure("typed","toggle",1,error));
    assert(configure("typed","rate",5,error));assert(!configure("typed","rate",11,error));
    assert(configure("typed","mode","b",error));assert(!configure("typed","mode","c",error));
    assert(install((root/"typed").string(),error));assert(remove("typed",error));
    auto bad=root/"bad.wwhdmod";std::ofstream(bad)<<"not a package";assert(!install(bad.string(),error));
    auto native=root/"native";fs::create_directories(native);
    fs::copy_file(argv[2],native/"fixture.dylib");
    std::ofstream(native/"manifest.json") << "{\"format_version\":1,\"id\":\"fixture\",\"name\":\"Fixture\",\"version\":\"1.0.0\",\"game_id\":\"wwhd-usa\",\"kind\":\"native\",\"abi_version\":1,\"binaries\":{\""
      << platform_key() << "\":\"fixture.dylib\"},\"options\":[{\"id\":\"label\",\"name\":\"Label\",\"type\":\"string\",\"default\":\"initial\"}]}";
    assert(install(native.string(),error));
    auto find=[] {return view("fixture");};
    auto storage=root/"storage";
    auto trusted=[&] {std::ifstream f(storage/"profiles.json");std::string text{std::istreambuf_iterator<char>(f),{}};
        auto value=mods::json::parse(text);return value.get("native_trust").get("fixture").string();};
    // Unconfirmed native code: enable() refuses and nothing loads until the player confirms.
    assert(!find().native_confirmed);
    auto pending=unconfirmed_native("fixture");assert(pending.size()==1 && pending[0].first=="fixture" && pending[0].second=="Fixture");
    assert(!enable("fixture",true,error));assert(error.find("native code")!=std::string::npos);
    frame(3);assert(!find().active && !find().enabled);
    // A settings preset that requires the native package names it in the confirmation.
    assert(install(package("needs-native",",\"dependencies\":[{\"id\":\"fixture\"}]","fast-scenes"),error));
    pending=unconfirmed_native("needs-native");assert(pending.size()==1 && pending[0].first=="fixture");
    assert(!enable("needs-native",true,error));assert(remove("needs-native",error));
    // Test aid: pre-confirmed only in isolated test runs, and never written to profiles.json.
    env("WWHD_TEST_TRUST_NATIVE_MODS","other,fixture");assert(find().native_confirmed && unconfirmed_native("fixture").empty());
    env("WWHD_TEST_TRUST_NATIVE_MODS",nullptr);assert(!find().native_confirmed && trusted().empty());
    assert(confirm_native("fixture",error));assert(trusted().size()==64);
    assert(find().native_confirmed && unconfirmed_native("fixture").empty());
    assert(!confirm_native("climb-preset",error));
    assert(enable("fixture",true,error));frame(6);
    assert(find().active && find().status=="initial");
    assert(configure("fixture","label","changed",error));frame(4);assert(find().status=="changed");
    assert(!configure("fixture","label",42,error));
    assert(enable("fixture",false,error));frame(5);assert(!find().active);
    // Confirmed: a new process loads it from the saved profile without asking again.
    assert(enable("fixture",true,error));frame(7);assert(find().active);
    std::string restart="\""+std::string(argv[0])+"\" --restart \""+storage.string()+"\"";
#ifdef _WIN32
    restart="\""+restart+"\"";  // cmd.exe /c drops the outer quotes of a line that starts with one
#endif
    assert(std::system(restart.c_str())==0);
    // That process disabled it; this one keeps its own view until told, so follow the saved state.
    assert(enable("fixture",false,error));frame(8);assert(!find().active);
    // A changed library under the same ID asks again, including when a profile switch would load it.
    assert(create_profile("Native",error));assert(select_profile("Native",error));
    assert(enable("fixture",true,error));frame(9);assert(find().active);
    assert(select_profile("Default",error));frame(10);assert(!find().active);
    auto before=trusted();
    std::ofstream(native/"fixture.dylib",std::ios::binary|std::ios::app) << "changed build";
    assert(install(native.string(),error));assert(!find().native_confirmed && trusted()==before);
    assert(select_profile("Native",error));frame(11);
    assert(!find().active && !find().enabled && find().reason.find("native code you have not confirmed")!=std::string::npos);
    assert(unconfirmed_native("fixture").size()==1 && !enable("fixture",true,error));
    assert(confirm_native("fixture",error));assert(trusted()!=before);
    assert(enable("fixture",true,error));frame(12);assert(find().active && find().reason.empty());
    assert(enable("fixture",false,error));frame(13);assert(select_profile("Default",error));assert(delete_profile("Native",error));
    // Removing forgets the confirmation; reinstalling asks again.
    assert(remove("fixture",error));assert(trusted().empty());
    assert(install(native.string(),error));assert(!find().native_confirmed);
    assert(remove("fixture",error));assert(enable("climb-preset",false,error));frame(30);assert(remove("climb-preset",error));
    assert(list().empty());
    // A settings/content package can ship a preparation tool: the same confirmation
    // covers the whole package, so changed imported helpers also invalidate trust.
    auto toolpkg=root/"SetupFixture";fs::create_directories(toolpkg/"tools");
    std::ofstream(toolpkg/"manifest.json")<<R"({"format_version":1,"id":"setup-fixture","name":"Setup fixture","version":"1.0.0","game_id":"wwhd-usa","kind":"settings","settings":{"wall-climb":true},"setup":[{"id":"prepare","type":"run_tool","title":"Prepare fixture","tool":"tools/prepare.py","arguments":["{data}","{game:gc_usa}"],"outputs":["result.bin"]}]})";
    std::ofstream(toolpkg/"tools"/"prepare.py")<<"# synthetic preparation tool\n";
    std::ofstream(toolpkg/"tools"/"helper.py")<<"# synthetic helper\n";
    assert(install(toolpkg.string(),error));assert(!view("setup-fixture").native_confirmed);
    assert(view("setup-fixture").setup_tools==std::vector<std::string>{"tools/prepare.py"});
    auto setup_confirmation=unconfirmed_native("setup-fixture");
    assert(setup_confirmation.size()==1&&setup_confirmation[0].second.find("tools/prepare.py")!=std::string::npos);
    assert(!enable("setup-fixture",true,error));assert(confirm_native("setup-fixture",error));
    assert(view("setup-fixture").native_confirmed);
    std::string output;assert(!run_setup_tool("setup-fixture","prepare",error,output)); // missing source
    auto disc=fs::absolute(root/"synthetic.iso");std::array<unsigned char,32> disc_header{};
    std::copy_n("GZLE01",6,disc_header.begin());disc_header[28]=0xC2;disc_header[29]=0x33;disc_header[30]=0x9F;disc_header[31]=0x3D;
    {std::ofstream out(disc,std::ios::binary);out.write(reinterpret_cast<char*>(disc_header.data()),disc_header.size());}
    assert(!set_game_source("gc_eur",disc.string(),error)&&set_game_source("gc_usa",disc.string(),error));
    auto build_config=fs::absolute(root/"setup-tools.json");
    auto build_json=mods::json::parse(R"({"format_version":1,"python":[],"compiler":["unused"],"builder":"unused","include":"unused"})");
    build_json["python"].array={mods::json::Value(fs::absolute(argv[0]).string()),mods::json::Value("--setup-tool")};
    {std::ofstream config(build_config);config<<mods::json::dump(build_json);}
    env("WWHD_GUEST_BUILD_CONFIG",build_config.string().c_str());
    assert(run_setup_tool("setup-fixture","prepare",error,output));
    assert(output.find(disc.string())==std::string::npos&&output.find("[game source]")!=std::string::npos);
    assert(setup_steps("setup-fixture")[0].satisfied);
    assert(set_game_source("gc_wind_waker","",error));
    assert(fs::is_regular_file(fs::path(directory()).parent_path()/"Data/setup-fixture/result.bin"));
    assert(!setup_steps("setup-fixture")[0].satisfied);
    assert(set_game_source("gc_wind_waker",disc.string(),error));
    assert(!setup_steps("setup-fixture")[0].satisfied); // returning to the old copy cannot revive a receipt
    assert(run_setup_tool("setup-fixture","prepare",error,output));
    auto other_disc=disc.parent_path()/"other-synthetic.iso";fs::copy_file(disc,other_disc);
    assert(set_game_source("gc_usa",other_disc.string(),error));
    assert(!setup_steps("setup-fixture")[0].satisfied); // changed source selection
    assert(run_setup_tool("setup-fixture","prepare",error,output));
    assert(setup_steps("setup-fixture")[0].satisfied);
    env("WWHD_TEST_SETUP_FAILURE","1");
    assert(!run_setup_tool("setup-fixture","prepare",error,output));
    env("WWHD_TEST_SETUP_FAILURE",nullptr);
    assert(!setup_steps("setup-fixture")[0].satisfied); // old result.bin still exists
    assert(run_setup_tool("setup-fixture","prepare",error,output));
    fs::rename(other_disc,other_disc.string()+".moved");
    assert(!setup_steps("setup-fixture")[0].satisfied); // source moved after success
    fs::rename(other_disc.string()+".moved",other_disc);
    std::ofstream(toolpkg/"tools"/"helper.py",std::ios::app)<<"# changed helper\n";
    assert(install(toolpkg.string(),error));assert(!view("setup-fixture").native_confirmed);
    assert(!setup_steps("setup-fixture")[0].satisfied&&!run_setup_tool("setup-fixture","prepare",error,output));
    std::ofstream(toolpkg/"tools"/"prepare.py",std::ios::app)<<"# fail\n";
    assert(install(toolpkg.string(),error)&&confirm_native("setup-fixture",error));
    assert(!run_setup_tool("setup-fixture","prepare",error,output)&&output.find("synthetic final failure")!=std::string::npos);
    assert(!setup_steps("setup-fixture")[0].satisfied);env("WWHD_GUEST_BUILD_CONFIG",nullptr);
    // Remove also deletes what its setup prepared, and says how much that is beforehand
    auto prepared=fs::path(directory()).parent_path()/"Data/setup-fixture";
    assert(fs::is_regular_file(prepared/"result.bin")&&disk_use("setup-fixture")>fs::file_size(prepared/"result.bin"));
    assert(remove("setup-fixture",error));
    assert(!fs::exists(prepared)&&disk_use("setup-fixture")==0);
    if(argc == 4) {
        assert(install(argv[3],error));
        auto id=list().at(0).id;
        if(list()[0].kind=="native") {assert(!enable(id,true,error));assert(confirm_native(id,error));}
        assert(enable(id,true,error));frame(40);assert(list()[0].active);
        assert(configure(id,"label","ZIP works",error));frame(41);
        assert(list()[0].status.starts_with("ZIP works"));
        assert(enable(id,false,error));frame(42);assert(remove(id,error));
    }
    auto graphics=root/"CemuResolution";fs::create_directories(graphics);
    std::ofstream(graphics/"rules.txt")<<"[Definition]\nname=Resolution\ntitleIds=0005000010143500\nversion=4\n[Preset]\nname=Normal\n$scale=1\n[Preset]\nname=Double\n$scale=2\n[TextureRedefine]\nwidth=1280\nheight=720\noverwriteWidth=1280*$scale\noverwriteHeight=720*$scale\n";
    assert(install(graphics.string(),error));auto graphicsView=list().at(0);
    assert(graphicsView.kind=="cemu"&&graphicsView.restart_required&&graphicsView.options.size()==1);
    assert(graphicsView.native_confirmed&&unconfirmed_native(graphicsView.id).empty()); // no native code: never asks
    assert(configure(graphicsView.id,"preset-0","Double",error));
    assert(!configure(graphicsView.id,"preset-0","Unknown",error));
    assert(enable(graphicsView.id,true,error));frame(45);
    assert(!list().at(0).active&&list().at(0).pending_restart);
    assert(enable(graphicsView.id,false,error));assert(remove(graphicsView.id,error));
    std::ofstream(graphics/"0000000000000001_0000000000000002_ps.txt")<<"#version 420\nvoid main(){}\n";
    assert(install(graphics.string(),error));assert(!list().at(0).compatible);
    assert(!enable(list().at(0).id,true,error));assert(remove(list().at(0).id,error));
    auto legacy=root/"LegacyModel";fs::create_directories(legacy/"content"/"Object");
    std::ofstream(legacy/"content"/"Object"/"test.arc")<<"synthetic model archive";
    assert(install(legacy.string(),error));auto imported=list().at(0);assert(imported.id=="content.legacymodel"&&imported.restart_required&&!imported.active);assert(imported.native_confirmed&&unconfirmed_native(imported.id).empty());
    assert(enable(imported.id,true,error));frame(50);assert(!list().at(0).active); // waits for restart
    auto second=root/"OtherModel";fs::create_directories(second/"content"/"Object");std::ofstream(second/"content"/"Object"/"test.arc")<<"synthetic conflicting archive";
    assert(install(second.string(),error));assert(!enable("content.othermodel",true,error));assert(error.find("Content file conflict")!=std::string::npos);
    assert(enable(imported.id,false,error));frame(51);assert(remove(imported.id,error));assert(remove("content.othermodel",error));
    auto loose=root/"permanent_3d.pack";std::ofstream(loose)<<"SARCsynthetic-fixture";
    assert(install(loose.string(),error));assert(list().at(0).id=="content.permanent_3d");assert(remove(list().at(0).id,error));
    auto loose_folder=root/"LooseModel";fs::create_directories(loose_folder);std::ofstream(loose_folder/"permanent_3d.pack")<<"SARCsynthetic-fixture";
    assert(install(loose_folder.string(),error));assert(remove(list().at(0).id,error));
    std::ofstream(loose_folder/"unknown.pack")<<"SARCsynthetic-fixture";assert(!install(loose_folder.string(),error));
    // A fan translation as loose files (any region's language pack name, a layout the installed game has once,
    // a read-me): the pack goes to Common/Pack, the layout to its game path, the read-me is not used.
    auto game=root/"game";fs::create_directories(game/"content"/"Common"/"Layout");fs::create_directories(game/"content"/"Common"/"Object");
    std::ofstream(game/"content"/"Common"/"Layout"/"Title_00.szs")<<"original";std::ofstream(game/"content"/"Common"/"Object"/"Twice.szs")<<"a";
    fs::create_directories(game/"content"/"Common"/"Stage");std::ofstream(game/"content"/"Common"/"Stage"/"Twice.szs")<<"b";
    mods::content::set_game_root(game);
    auto translation=root/"FanTranslation";fs::create_directories(translation/"inner");
    std::ofstream(translation/"inner"/"permanent_2d_EuEnglish.pack")<<"SARCsynthetic translation";std::ofstream(translation/"Title_00.szs")<<"Yaz0logo";
    std::ofstream(translation/"readme.txt")<<"text";
    assert(install(translation.string(),error));{auto v=list().at(0);assert(v.id=="content.fantranslation");
        assert(v.description.find("Common/Pack/permanent_2d_EuEnglish.pack")!=std::string::npos&&v.description.find("Common/Layout/Title_00.szs")!=std::string::npos);
        assert(v.description.find("Not used: readme.txt")!=std::string::npos);assert(remove(v.id,error));}
    std::ofstream(translation/"Twice.szs")<<"ambiguous";assert(!install(translation.string(),error));fs::remove(translation/"Twice.szs");
    auto single=root/"permanent_2d_JpJapanese.pack";std::ofstream(single)<<"SARCsynthetic";assert(install(single.string(),error));assert(remove(list().at(0).id,error));
    // the content folder of a mod selected on its own: named after the mod
    fs::create_directories(translation/"content"/"Common"/"Pack");fs::rename(translation/"inner"/"permanent_2d_EuEnglish.pack",translation/"content"/"Common"/"Pack"/"permanent_2d_EuEnglish.pack");
    assert(install((translation/"content").string(),error));assert(list().at(0).id=="content.fantranslation");assert(remove(list().at(0).id,error));
    mods::content::set_game_root({});
    auto invalid=root/"CodeMod";fs::create_directories(invalid/"content");std::ofstream(invalid/"content"/"dummy")<<"fixture";std::ofstream(invalid/"patches.txt")<<"code";assert(!install(invalid.string(),error));
    fs::remove(invalid/"patches.txt");fs::create_directories(invalid/"graphicPacks"/"Patch");std::ofstream(invalid/"graphicPacks"/"Patch"/"rules.txt")<<"[Definition]\ntitleIds = 0005000010143500\n";
    std::ofstream(invalid/"graphicPacks"/"Patch"/"patch_code.asm")<<"[Code]\nmoduleMatches = 0x475BD29F\n";assert(!install(invalid.string(),error));assert(error.find("code patch")!=std::string::npos);
    fs::remove_all(invalid/"graphicPacks");std::ofstream(invalid/"rules.txt")<<"[Definition]\ntitleIds = 0005000010143600\n";assert(!install(invalid.string(),error));
    std::ofstream(invalid/"rules.txt",std::ios::trunc)<<"[Definition]\ntitleIds = 0005000010143500\n[TextureRedefine]\n";assert(!install(invalid.string(),error));
    fs::remove(invalid/"rules.txt");std::ofstream(invalid/"content"/".deleted_dummy")<<"";assert(!install(invalid.string(),error));
    auto duplicate=root/"Duplicate";fs::create_directories(duplicate/"content"/"Object");fs::create_directories(duplicate/"content"/"object");
    std::ofstream(duplicate/"content"/"Object"/"A.bin")<<"fixture";std::ofstream(duplicate/"content"/"object"/"a.bin")<<"fixture";
    // A case-sensitive volume can represent the conflict; a case-insensitive volume collapses it.
    if(std::distance(fs::directory_iterator(duplicate/"content"),fs::directory_iterator{})==2)assert(!install(duplicate.string(),error));
    std::cout << "Package install, settings, profiles, dependencies, native load/config/unload passed\n";
}
