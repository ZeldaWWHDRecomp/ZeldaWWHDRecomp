#include "../exception_report.h"
#include "code_mods.h"
#include "guest_build.h"
#include "packages.h"
#include "../overlay/hostui.h"
#include "../platform/host.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstdlib>
#ifdef __ANDROID__
#include "guest_mods.h"
#include "../platform/android_code_mods.h"
#include <chrono>
#endif

namespace mods::code {
namespace {
namespace fs=std::filesystem;
std::mutex mutex;
Status state;
std::string pending_mod;
std::vector<std::string> launch_args;
fs::path install_dir;
fs::path status_path,cancel_path;
std::atomic<bool> stopping{false};  // the process exits: a follower of the phone's setup stops polling
struct Worker {
    std::thread thread;
    ~Worker(){if(thread.joinable()){stopping=true;if(!cancel_path.empty())std::ofstream(cancel_path)<<"cancel";thread.join();}}
} worker;
json::Value read(const fs::path& path) {
    if(!fs::is_regular_file(path)||fs::file_size(path)>1024*1024)exception_report::raise("Code-mod build configuration is unavailable; run setup again from a complete release folder");
    std::ifstream f(path);return json::parse(std::string{std::istreambuf_iterator<char>(f),{}});
}
}
namespace {
bool g_fixed_build=false;  // startup(..., true): the decomp version
void environment(const char* name,const std::string& value){
#ifdef _WIN32
    _putenv_s(name,value.c_str());
#else
    setenv(name,value.c_str(),1);
#endif
}
bool replace(const std::string& executable,std::string& error){
    if(launch_args.empty()){error="Restart command is unavailable";return false;}
    auto args=launch_args;args[0]=executable;
#ifdef _WIN32
    std::wstring command;
    for(const auto& a:args){if(!command.empty())command+=L' ';command+=host::process_quote(host::process_wide(a));}
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi)){error="Cannot restart the game";return false;}
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);std::_Exit(0);
#else
    std::vector<char*> argv;for(auto& a:args)argv.push_back(a.data());argv.push_back(nullptr);
    execv(executable.c_str(),argv.data());error="Cannot restart the game (error "+std::to_string(errno)+")";return false;
#endif
}
fs::path selected(){
    if(!fs::is_regular_file(install_dir/"code-mods-active.json"))return {};
    auto active=read(install_dir/"code-mods-active.json");
    if(active.get("state").string()!="ready")return {};
    if(const char* override=std::getenv("WWHD_CODE_MODS"))
        if((std::string(override)=="1")!=active.get("hooks").boolean)return {};
    auto exe=fs::path(active.get("exe").string());
    // code-builds/<first 16 hex digits of the fingerprint> (setup's code_mods.cache_name); releases up to
    // v0.2.12 used all 64 digits
    const auto fingerprint=active.get("fingerprint").string();
    auto cache=install_dir/"code-builds"/fingerprint.substr(0,16);
    if(!fs::is_directory(cache))cache=install_dir/"code-builds"/fingerprint;
    if(!fs::is_regular_file(exe)||fs::weakly_canonical(exe.parent_path())!=fs::weakly_canonical(cache/"bin"))return {};
    auto record=read(cache/"ready.json");
    if(record.get("fingerprint")!=active.get("fingerprint")||record.get("hooks")!=active.get("hooks"))return {};
    auto user=active.get("user_dir").string();if(!user.empty())environment("WWHD_USER_DIR",user);
    return exe;
}
}
void startup(int argc,char** argv,bool fixed_build){
    launch_args.assign(argv,argv+argc);
    g_fixed_build=fixed_build;
    if(const char* dir=std::getenv("WWHD_INSTALL_DIR"))install_dir=dir;
    else install_dir=fs::path(host::exe_dir()).parent_path();
    environment("WWHD_INSTALL_DIR",install_dir.string());
    auto config=install_dir/"guest-sdk.json";
    if(!std::getenv("WWHD_GUEST_BUILD_CONFIG")&&fs::is_regular_file(config))environment("WWHD_GUEST_BUILD_CONFIG",config.string());
    // the decomp version (setup's option, docs/decomp-build.md) is a build of its own, without code-mod
    // support: it is never replaced by the cached code-mod build of the normal game
    if(fixed_build)return;
    try {
        auto exe=selected();if(exe.empty())return;
        auto current=fs::path(host::exe_dir())/fs::path(argv[0]).filename();
        if(fs::exists(current)&&fs::equivalent(current,exe))return;
        std::string error;if(!replace(exe.string(),error))fprintf(stderr,"[code mods] %s; retaining previous build\n",error.c_str());
    }catch(const std::exception& e){fprintf(stderr,"[code mods] %s; retaining previous build\n",e.what());}
}
bool restart(std::string& error){
    auto failed=[&]{std::lock_guard guard(mutex);state.error=error;return false;};
#ifdef __ANDROID__
    // the app loads the new build when it starts again (WwhdActivity); it cannot relaunch itself
    error="Close and reopen the app to use the rebuilt game code";return failed();
#endif
    if(g_fixed_build){error="The decomp version has no code-mod support; start the normal game for code mods";return failed();}
    // The relaunched game uses the support the player just built: an explicit WWHD_CODE_MODS from the
    // launch (automated runs) would otherwise select the other build and stop a setup that resumes there.
    {std::lock_guard guard(mutex);if(state.ready&&std::getenv("WWHD_CODE_MODS"))environment("WWHD_CODE_MODS",state.target?"1":"0");}
    try {auto exe=selected();if(exe.empty()){error="No completed code-mod build is available";return failed();}if(!replace(exe.string(),error))return failed();return true;}
    catch(const std::exception& e){error=e.what();return failed();}
}

const char* rebuild_duration(){
#ifdef __ANDROID__
    return "about as long as the first setup";
#else
    return "about 5 minutes";
#endif
}
bool enabled(){
#ifdef __ANDROID__
    // the phone's build is the choice: the setup builds the game code with or without mod support
    return guestmods::hooks_built();
#endif
    if(const char* value=std::getenv("WWHD_CODE_MODS"))return std::string(value)=="1";
    std::string value;return hostui::get("code-mods",value)&&value=="1";
}
void request(bool on,const std::string& mod){std::lock_guard guard(mutex);if(state.building)return;state={};state.requested=true;state.target=on;state.mod=mod;pending_mod=mod;fprintf(stderr,"[code mods] rebuild offer: support %s%s%s\n",on?"on":"off",mod.empty()?"":" for ",mod.c_str());}
void dismiss(){std::lock_guard guard(mutex);state.requested=false;}
Status status(){
    std::lock_guard guard(mutex);
    if(state.building&&!status_path.empty())try {
        auto v=read(status_path);state.stage=v.get("stage").string();
        state.done=unsigned(v.get("done").number);state.total=unsigned(v.get("total").number);
    }catch(...){} // atomic status may not have been published yet
    return state;
}
void cancel(){
    std::lock_guard guard(mutex);
#ifdef __ANDROID__
    state.building=false;return;  // stop following: the setup itself goes on (its screen can pause it)
#endif
    if(state.building&&!cancel_path.empty())std::ofstream(cancel_path)<<"cancel";
}
#ifdef __ANDROID__
// The phone's setup builds the game code (SetupService, its own process) while the game runs; this
// follows its progress until the new build is active, for the next start of the app.
void begin_android(){
    auto error=host::android::request_code_mod_build(state.target);
    if(!error.empty()){state.error=error;return;}
    state.building=true;state.error.clear();state.ready=false;state.stage="Starting setup";
    fprintf(stderr,"[code mods] setup rebuild requested: support %s\n",state.target?"on":"off");
    worker.thread=std::thread([target=state.target,mod=pending_mod]{
        while(!stopping) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            json::Value report;
            try{report=json::parse(host::android::code_mod_build_status());}catch(...){continue;}
            std::lock_guard lock(mutex);
            if(!state.building)return;  // dismissed
            const auto phase=report.get("state").string();
            state.stage=report.get("stage").string(state.stage);
            state.done=unsigned(report.get("done").number);state.total=unsigned(report.get("total").number);
            if(phase=="failed"){state.building=false;state.error=report.get("error").string("Setup failed; open setup for details");return;}
            if(phase!="ready")continue;
            state.building=false;state.ready=true;
            fprintf(stderr,"[code mods] setup rebuild ready (support %s); restart the app\n",target?"on":"off");
            if(!mod.empty()) {
                std::string why;
                if(!packages::enable_after_code_rebuild(mod,why))state.error="Game code rebuilt, but the mod could not be queued: "+why;
            }
            return;
        }
    });
}
#endif
void begin(){
    std::lock_guard guard(mutex);if(state.building||!state.requested)return;
    if(worker.thread.joinable())worker.thread.join();
#ifdef __ANDROID__
    begin_android();return;
#endif
    try {
        const char* override=std::getenv("WWHD_GUEST_BUILD_CONFIG");
        fs::path config=override?fs::path(override):fs::path("guest-sdk.json");
        auto cfg=read(config);auto base=fs::absolute(config).parent_path();
        // the same rules as the guest build bridge: $RELEASE/ (AppImage), relative paths, stale AppImage mounts
        const auto release=guestmods::release_root();
        auto resolve=[&](const std::string& name){auto p=guestmods::resolve_path(cfg.get(name.c_str()).string(),base,release);if(p.empty())exception_report::raise("Code-mod setup tools are missing; run setup again");return fs::path(p);};
        auto python=guestmods::BuildBridge::arguments(cfg.get("python"));
        python.front()=guestmods::resolve_path(python.front(),base,release,true);
        auto data=resolve("data_dir");auto setup=resolve("setup");
        status_path=data/"code-mods-status.json";cancel_path=data/"code-mods-cancel";
        std::error_code ec;fs::remove(status_path,ec);fs::remove(cancel_path,ec);
        python.insert(python.end(),{setup.string(),"--data-dir",data.string(),"--rebuild-code-mods","--code-mods",state.target?"1":"0","--code-mods-status",status_path.string(),"--code-mods-cancel",cancel_path.string(),"--jobs","4"});
        state.building=true;state.error.clear();state.ready=false;state.stage="Starting setup";
        bool target=state.target;auto mod=pending_mod;
        worker.thread=std::thread([command=std::move(python),target,mod]{
            auto result=host::run_process(command);
            std::lock_guard lock(mutex);state.building=false;
            try {
                auto report=read(status_path);
                if(result.code||!result.error.empty()||report.get("state").string()!="ready")
                    exception_report::raise(report.get("message").string(result.error.empty()?"Code-mod rebuild failed; see setup.log. Previous build retained.":result.error));
                state.ready=true;state.exe=report.get("exe").string();
                fprintf(stderr,"[code mods] rebuild ready; restart required\n");
                if(!mod.empty()) {
                    std::string error;
                    if(!packages::enable_after_code_rebuild(mod,error))exception_report::raise("Game code rebuilt, but the mod could not be queued: "+error);
                }
                hostui::post([target]{hostui::set("code-mods",target?"1":"0");});
            }catch(const std::exception& e){state.error=e.what();}
        });
    }catch(const std::exception& e){state.error=e.what();}
}
}
