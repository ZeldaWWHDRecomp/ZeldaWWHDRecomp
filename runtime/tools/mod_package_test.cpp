// Standalone host-side tests; no game files, player settings or guest code needed.
#include "mods/manager.h"
#include "mods/mods.h"
#include "mods/climb.h"
#include "overlay/hostui.h"
#include "platform/process.h"
#include "mods/catalogue_client.h"
#include "mods/catalogue_setup.h"
#include <cassert>
#include <cstdlib>
#include <map>
#include <string>

namespace {
bool state[6]{};
float speed = 1, sensitivity = .15f;
std::map<std::string,std::string> preferences;
int reads = 0, writes = 0;
void env(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value ? value : "");
#else
    if (value) setenv(key,value,1); else unsetenv(key);
#endif
}
}
namespace mods {
static bool ff_on = false, ff_mute = true;
static unsigned ff_rate = 2;
static uint32_t ff_button = 0x40;
bool fast_forward() { return ff_on; } void set_fast_forward(bool on) { ff_on = on; }
unsigned fast_forward_rate() { return ff_rate; }
void set_fast_forward_rate(unsigned r) { if (r >= 2 && r <= 4) ff_rate = r; }
uint32_t fast_forward_button() { return ff_button; }
void set_fast_forward_button(uint32_t b) { if (valid_fast_forward_button(b)) ff_button = b; }
bool fast_forward_mute() { return ff_mute; } void set_fast_forward_mute(bool on) { ff_mute = on; }

static bool move_on = false;
bool move_speed() { return move_on; } void set_move_speed(bool on) { move_on = on; }
float move_speed_factor() { return 1.5f; } void set_move_speed_factor(float) {}
uint32_t move_speed_button() { return 0x40000; } void set_move_speed_button(uint32_t) {}

bool direct_camera() { return state[0]; } void set_direct_camera(bool b) { state[0]=b; }
bool mouse_camera() { return state[1]; } void set_mouse_camera(bool b) { state[1]=b; }
bool first_person_wheel() { return state[2]; } void set_first_person_wheel(bool b) { state[2]=b; }
bool climb_enabled() { return state[3]; } void set_climb_enabled(bool b) { state[3]=b; }
bool quick_doors() { return state[4]; } void set_quick_doors(bool b) { state[4]=b; }
bool fast_scenes() { return state[5]; } void set_fast_scenes(bool b) { state[5]=b; }
float camera_speed() { return speed; }
void set_camera_speed(float f) { speed=f; }
float mouse_sensitivity() { return sensitivity; }
void set_mouse_sensitivity(float f) { sensitivity=f; }
}
namespace hostui {
bool get(const char* k, std::string& value) {
    ++reads; auto it=preferences.find(k);
    if(it==preferences.end()) return false;
    value=it->second;return true;
}
void set(const char* k, const std::string& value) { ++writes;preferences[k]=value; }
}
#include "mods/packages.h"
#include "mods/content.h"
#include "mods/cemu_pack.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
namespace {
mods::packages::View view(const std::string& id) {for(auto v:mods::packages::list())if(v.id==id)return v;return {};}
// Second process on the same storage: a confirmed native package loads after a restart without asking.
int restart_check(const char* storage) {
    using namespace mods::packages;
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
    if(argc==2&&std::string(argv[1])=="--game-source") {
        namespace fs=std::filesystem;
        auto root=fs::temp_directory_path()/("wwhd-gc-source-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root/"source");
        env("WWHD_NO_HOST_INPUT","1");env("WWHD_MOD_MANAGER_DIR",(root/"manager").string().c_str());
        initialize();std::string error;
        std::ofstream(root/"source/manifest.json")<<R"({"format_version":1,"id":"gc-fixture","name":"GC fixture","version":"1.0.0","game_id":"wwhd-usa","kind":"settings","settings":{"wall-climb":true},"setup":[{"id":"game","type":"game_path","game":"gc_wind_waker","title":"GameCube game"}]})";
        assert(install((root/"source").string(),error));
        assert(game_source().path.empty()&&!game_source().valid);
        assert(!setup_steps("gc-fixture")[0].satisfied);
        assert(!view("gc-fixture").game_source_warning.empty());
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
        assert(setup_steps("gc-fixture")[0].satisfied&&view("gc-fixture").game_source_warning.empty());
        assert(enable("gc-fixture",true,error)&&create_profile("Copy",error));
        auto other=root/"other.iso";fs::copy_file(disc,other);
        assert(set_game_source("gc_wind_waker",other.string(),error));
        assert(!view("gc-fixture").enabled&&select_profile("Copy",error)&&!view("gc-fixture").enabled);
        assert(enable("gc-fixture",true,error));
        fs::remove(other);assert(!game_source().valid&&!view("gc-fixture").game_source_warning.empty());
        assert(!select_profile("Copy",error)&&error.find("GameCube")!=std::string::npos);
        assert(!enable("gc-fixture",true,error));
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
        assert(set_game_source("gc_wind_waker","",error)&&!setup_steps("gc-guest")[1].satisfied);
        assert(fs::is_regular_file(root/"synthetic-module"));
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
    if(argc==5&&std::string(argv[1])=="--setup-tool") {
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
        auto root=fs::temp_directory_path()/("wwhd-guest-prepare-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
    if(argc==2&&std::string(argv[1])=="--guest-startup") {
        auto root=fs::temp_directory_path()/("wwhd-guest-startup-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
        auto root=fs::temp_directory_path()/("wwhd-guest-metadata-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
        auto root=fs::temp_directory_path()/("wwhd-cemu-conflicts-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
        auto root=fs::temp_directory_path()/("wwhd-cemu-startup-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
    if(argc==2&&std::string(argv[1])=="--content-startup"){
        auto root=fs::temp_directory_path()/("wwhd-content-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
    assert(create_profile("Adventure",error));
    mods::set_fast_forward_rate(4); mods::set_fast_forward_button(0x80); mods::set_fast_forward_mute(true);
    remember_option("fast-forward.rate", 4); remember_option("fast-forward.button", 0x80); remember_option("fast-forward.mute", 1);
    assert(enable("climb-preset",true,error));frame(20);assert(state[3]);
    assert(select_profile("Adventure",error));frame(21);assert(!state[3]);
    assert(mods::fast_forward_rate()==3 && mods::fast_forward_button()==0x20 && !mods::fast_forward_mute());
    assert(current_profile()=="Adventure");assert(!delete_profile("Adventure",error));
    assert(select_profile("Default",error));assert(mods::fast_forward_rate()==4 && mods::fast_forward_button()==0x80 && mods::fast_forward_mute());assert(delete_profile("Adventure",error));
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
    assert(remove("setup-fixture",error));
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
