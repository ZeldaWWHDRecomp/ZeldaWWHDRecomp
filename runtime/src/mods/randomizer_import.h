// Desktop-only private build worker. Its completed directory is handed to the
// existing package installer on the UI thread; this worker never edits Mods or
// profiles and never enables code. Cancellation is cooperative between steps.
#pragma once
#include "guest_build.h"
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

namespace mods::randomizer_import {
namespace fs=std::filesystem;
inline fs::path path_from_text(const std::string& text){return fs::path(std::u8string(text.begin(),text.end()));}
inline std::string path_text(const fs::path& path){const auto text=path.u8string();return {text.begin(),text.end()};}
struct Plan {
    std::vector<std::string> command;
    fs::path directory,package,progress,cancel;
    std::string profile;
};
inline Plan plan(const guestmods::BuildBridge& bridge,const fs::path& cache,
                 const std::string& original,const std::string& seed,
                 const std::string& custom_compiler,const std::string& profile) {
    if(original.empty()||seed.empty())throw std::runtime_error("Choose both extracted game folders first.");
    const auto release=path_from_text(bridge.builder).parent_path().parent_path().parent_path();
    const auto worker=release/"tools/core_randomizer.py", resources=release/"core-randomizer-resources.json";
    if(!fs::is_regular_file(worker)||!fs::is_regular_file(resources))
        throw std::runtime_error("This release does not include the experimental randomizer builder. Update from a complete release.");
    auto compiler=custom_compiler.empty()?bridge.compiler:std::vector<std::string>{custom_compiler};
    if(compiler.empty()||!path_from_text(compiler.front()).is_absolute())
        throw std::runtime_error("Choose a PowerPC-capable Clang executable. This setup compiler command cannot be used directly for PowerPC builds.");
    if(!fs::is_regular_file(path_from_text(compiler.front())))
        throw std::runtime_error("The selected compiler file is unavailable. Choose a PowerPC-capable Clang executable.");
    const auto cache_path=fs::weakly_canonical(cache);
    auto contains=[](const fs::path& a,const fs::path& b){auto i=a.begin(),j=b.begin();for(;i!=a.end()&&j!=b.end()&&*i==*j;++i,++j){}return i==a.end();};
    for(const auto& input:{path_from_text(original),path_from_text(seed)}) {
        if(!fs::is_regular_file(input/"code/cking.rpx")||!fs::is_directory(input/"content"))
            throw std::runtime_error("Choose complete extracted game folders containing code/cking.rpx and content.");
        const auto path=fs::canonical(input);
        if(contains(path,cache_path)||contains(cache_path,path))
            throw std::runtime_error("The private build cache must be separate from both game folders.");
    }
    Plan p;p.profile=profile;
    fs::create_directories(cache);
    for(unsigned n=0;n<100;n++) {
        const auto nonce=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(n);
        p.directory=cache/("import-"+nonce);
        if(fs::create_directory(p.directory))break;
        p.directory.clear();
    }
    if(p.directory.empty())throw std::runtime_error("Cannot create a private randomizer build folder.");
    p.package=p.directory/"package";p.progress=p.directory/"progress.json";p.cancel=p.directory/"cancel";
    json::Value argv;argv.type=json::Value::Array;for(const auto& arg:compiler)argv.array.emplace_back(arg);
    p.command=bridge.tool_python();
    p.command.insert(p.command.end(),{path_text(worker),"--resources",path_text(resources),"--original",original,
        "--randomized",seed,"--output",path_text(p.package),"--compiler-json",json::dump(argv),
        "--progress-file",path_text(p.progress),"--cancel-file",path_text(p.cancel)});
    return p;
}
struct Completion {
    fs::path directory,package;
    std::string profile,error;
    bool cancelled=false;
    void release() {if(!directory.empty()){std::error_code ignored;fs::remove_all(directory,ignored);directory.clear();}}
};
enum class CompletionAction { Import, Cancelled, Failed, ProfileChanged };
inline CompletionAction completion_action(const Completion& result,const std::string& current_profile) {
    if(result.cancelled)return CompletionAction::Cancelled;
    if(!result.error.empty())return CompletionAction::Failed;
    if(result.profile!=current_profile)return CompletionAction::ProfileChanged;
    return CompletionAction::Import;
}
class Job {
    mutable std::mutex mutex;
    std::thread thread;
    Plan active;
    bool running=false,done=false,cancelled=false;
    std::string error;
public:
    ~Job(){cancel();if(thread.joinable())thread.join();std::error_code ignored;if(!active.directory.empty())fs::remove_all(active.directory,ignored);}
    bool busy() const {std::lock_guard g(mutex);return running||done;}
    bool stopping() const {std::lock_guard g(mutex);return cancelled;}
    using Launcher=std::function<std::thread(std::function<void()>)>;
    void start(Plan p,Launcher launch={}) {
        std::lock_guard g(mutex);
        if(running||done)throw std::runtime_error("A randomizer import is already in progress.");
        if(thread.joinable())thread.join();
        active=std::move(p);running=true;done=false;cancelled=false;error.clear();
        auto work=[this] {
            std::string failure;
            try{
                auto result=host::run_process(active.command);
                if(!result.error.empty())failure=result.error;
                else if(result.code!=0)failure=result.output.empty()?"The core build failed. Choose a PowerPC-capable compiler or check the game folders.":result.output;
                else {
                    auto receipt=json::parse(result.output);
                    if(receipt.get("manager_action").string()!="import"||receipt.get("package").string()!=path_text(active.package)||
                       !fs::is_regular_file(active.package/"manifest.json"))
                        failure="The builder did not return a complete private package.";
                }
            }catch(const std::exception& e){failure=e.what();}
            std::lock_guard g(mutex);error=std::move(failure);running=false;done=true;
        };
        try {
            thread=launch?launch(work):std::thread(work);
            if(!thread.joinable())throw std::runtime_error("Cannot start the randomizer build worker.");
        }catch(...) {
            const auto owned=active.directory;
            active={};running=false;done=false;cancelled=false;error.clear();
            std::error_code ignored;if(!owned.empty())fs::remove_all(owned,ignored);
            throw;
        }
    }
    void cancel() {
        std::lock_guard g(mutex);
        if(running||done){cancelled=true;if(!active.cancel.empty()){std::ofstream stream(active.cancel);stream<<"cancel\n";}}
    }
    std::string progress() const {
        fs::path path;
        {std::lock_guard g(mutex);if(cancelled)return "Cancelling after the current step…";path=active.progress;}
        try{
            if(!path.empty()&&fs::is_regular_file(path)&&fs::file_size(path)<4096){
                std::ifstream input(path);std::string text{std::istreambuf_iterator<char>(input),{}};
                auto phase=json::parse(text).get("phase").string();
                if(phase=="Checking PowerPC compiler"||phase=="Preparing and validating seed"||phase=="Building private core package"||phase=="Ready to import")return phase+"…";
            }
        }catch(...){}
        return "Starting private build…";
    }
    bool take(Completion& out) {
        std::lock_guard g(mutex);if(!done)return false;
        if(thread.joinable())thread.join();
        out={active.directory,active.package,active.profile,error,cancelled};
        active={};done=false;cancelled=false;error.clear();return true;
    }
};
} // namespace mods::randomizer_import
