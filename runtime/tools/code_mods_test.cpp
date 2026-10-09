#include "mods/code_mods.h"
#include "mods/mod_json.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <map>
#include <functional>
#include <cstdlib>
namespace {
std::mutex settings_mutex;
std::map<std::string,std::string> settings;
std::string queued;
void env(const char* key,const std::string& value){
#ifdef _WIN32
    _putenv_s(key,value.c_str());
#else
    setenv(key,value.c_str(),1);
#endif
}
void clear_env(const char* key){
#ifdef _WIN32
    _putenv_s(key, "");
#else
    unsetenv(key);
#endif
}
}
namespace hostui {
bool get(const char* key,std::string& value){std::lock_guard lock(settings_mutex);value=settings[key];return !value.empty();}
void set(const char* key,const std::string& value){std::lock_guard lock(settings_mutex);settings[key]=value;}
void post(std::function<void()> fn){fn();}
}
namespace mods::packages {
bool enable_after_code_rebuild(const std::string& id,std::string&){queued=id;return true;}
}
int main(int argc,char** argv){
    assert(argc==2);
    namespace fs=std::filesystem;using namespace mods::code;
    clear_env("WWHD_CODE_MODS");assert(!enabled()); // absent preference starts off
    auto root=fs::temp_directory_path()/("wwhd-code-service-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    auto tool=root/"setup fixture.py";
    std::ofstream(tool)<<R"(import argparse,json,os
p=argparse.ArgumentParser()
p.add_argument('--data-dir');p.add_argument('--rebuild-code-mods',action='store_true')
p.add_argument('--code-mods');p.add_argument('--code-mods-status');p.add_argument('--code-mods-cancel');p.add_argument('--jobs')
a=p.parse_args()
assert a.rebuild_code_mods and a.jobs=='4'
with open(a.code_mods_status+'.tmp','w') as f: json.dump({'state':'ready','exe':'fixture executable','hooks':a.code_mods=='1'},f)
os.replace(a.code_mods_status+'.tmp',a.code_mods_status)
)";
    mods::json::Value config;config["format_version"]=2;
    config["python"].type=mods::json::Value::Array;config["python"].array.emplace_back(argv[1]);
    config["setup"]=tool.string();config["data_dir"]=root.string();
    auto path=root/"guest-sdk.json";std::ofstream(path)<<mods::json::dump(config);
    env("WWHD_GUEST_BUILD_CONFIG",path.string());env("WWHD_CODE_MODS","0");
    assert(!enabled());env("WWHD_CODE_MODS","1");assert(enabled());
    request(true,"heart-ticker");assert(status().requested);assert(!status().building);
    begin();
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(status().building&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto result=status();assert(result.ready&&!result.building&&result.error.empty());
    assert(result.exe=="fixture executable"&&queued=="heart-ticker");
    std::string saved;assert(hostui::get("code-mods",saved)&&saved=="1");
    clear_env("WWHD_CODE_MODS");assert(enabled()); // the persisted preference survives the override
    env("WWHD_CODE_MODS","0");assert(!enabled());
    assert(hostui::get("code-mods",saved)&&saved=="1"); // override never rewrites it
    clear_env("WWHD_CODE_MODS");
    dismiss();assert(!status().requested);
    request(false);dismiss();assert(!status().building); // declining never starts setup
    request(false);begin();
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(status().building&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(status().ready&&status().error.empty());
    assert(hostui::get("code-mods",saved)&&saved=="0"&&!enabled());
    env("WWHD_CODE_MODS","1");assert(enabled());
    assert(hostui::get("code-mods",saved)&&saved=="0");
    fs::remove_all(root);
}
