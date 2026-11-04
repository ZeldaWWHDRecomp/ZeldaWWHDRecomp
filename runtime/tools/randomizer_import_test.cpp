// Real process worker and pure command construction; no game, manager or assets.
#include "mods/randomizer_import.h"
#include <cassert>
#include <algorithm>
#include <iostream>
using namespace mods::randomizer_import;
static void touch(const fs::path& p,const std::string& text="fixture"){fs::create_directories(p.parent_path());std::ofstream(p)<<text;}
static Completion wait(Job& job){Completion out;for(unsigned n=0;n<1000;n++){if(job.take(out))return out;std::this_thread::sleep_for(std::chrono::milliseconds(2));}assert(false);return out;}
int main(int argc,char** argv){
    if(argc>1&&std::string(argv[1])=="--fail"){std::cout<<"fixture compiler backend unavailable";return 3;}
    if(argc>1&&std::string(argv[1])=="--child"){
        fs::path package=argv[2],cancel=argv[3];
        if(argc>4)for(unsigned n=0;n<500&&!fs::exists(cancel);n++)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        fs::create_directories(package);touch(package/"manifest.json","{}");
        mods::json::Value receipt;receipt["manager_action"]="import";receipt["package"]=package.string();std::cout<<mods::json::dump(receipt);return 0;
    }
    const auto root=fs::temp_directory_path()/("wwhd-import-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto release=root/fs::path(u8"rélease"),cache=root/"manager/Cache",original=root/"original",seed=root/"seed";
    const auto exe=fs::absolute(argv[0]);
    touch(release/"tools/guestmod/build_guest_mod.py");touch(release/"tools/core_randomizer.py");touch(release/"core-randomizer-resources.json");
    touch(original/"code/cking.rpx");fs::create_directories(original/"content");touch(seed/"code/cking.rpx");fs::create_directories(seed/"content");
    guestmods::BuildBridge bridge;bridge.builder=path_text(release/"tools/guestmod/build_guest_mod.py");bridge.python={exe.string()};bridge.compiler={exe.string(),"cc","literal;no-shell"};
    auto p=plan(bridge,cache,original.string(),seed.string(),"","profile-A");
    auto flag=[&](const char* value){auto i=std::find(p.command.begin(),p.command.end(),value);assert(i!=p.command.end()&&++i!=p.command.end());return *i;};
    assert(flag("--resources")== path_text(release/"core-randomizer-resources.json"));
    assert(flag("--original")==original.string()&&flag("--randomized")==seed.string());
    const auto cc=mods::json::parse(flag("--compiler-json"));assert(cc.array.size()==3&&cc.array[2].string()=="literal;no-shell");
    p.command={exe.string(),"--child",p.package.string(),p.cancel.string()};
    Job job;job.start(p);auto success=wait(job);assert(success.error.empty()&&!success.cancelled&&success.profile=="profile-A"&&fs::is_regular_file(success.package/"manifest.json"));assert(completion_action(success,"profile-A")==CompletionAction::Import);
    assert(completion_action(success,"profile-B")==CompletionAction::ProfileChanged);
    success.release();assert(!fs::exists(p.directory));
    auto cancelled=plan(bridge,cache,original.string(),seed.string(),exe.string(),"profile-A");
    cancelled.command={exe.string(),"--child",cancelled.package.string(),cancelled.cancel.string(),"wait"};
    touch(cancelled.progress,"{\"phase\":\"Building private core package\"}");
    job.start(cancelled);assert(job.progress()=="Building private core package…");job.cancel();assert(job.stopping());auto stopped=wait(job);assert(stopped.cancelled&&completion_action(stopped,"profile-A")==CompletionAction::Cancelled);stopped.release();assert(!fs::exists(cancelled.directory));
    auto failed=plan(bridge,cache,original.string(),seed.string(),"","profile-A");
    failed.command={exe.string(),"--fail"};job.start(failed);auto failure=wait(job);
    assert(completion_action(failure,"profile-A")==CompletionAction::Failed&&!fs::exists(failure.package/"manifest.json"));failure.release();
    auto cannot_start=plan(bridge,cache,original.string(),seed.string(),"","profile-A");
    bool start_failed=false;
    try{job.start(cannot_start,[](std::function<void()>)->std::thread{throw std::runtime_error("fixture thread failure");});}
    catch(...){start_failed=true;}
    assert(start_failed&&!job.busy()&&!fs::exists(cannot_start.directory));
    const auto unicode_compiler=root/fs::path(u8"compilér tools")/"clang.exe";touch(unicode_compiler);
    auto unicode_plan=plan(bridge,cache,original.string(),seed.string(),path_text(unicode_compiler),"profile-A");
    auto selected=std::find(unicode_plan.command.begin(),unicode_plan.command.end(),"--compiler-json");
    assert(selected!=unicode_plan.command.end());
    assert(mods::json::parse(*++selected).array.front().string()==path_text(unicode_compiler));
    fs::remove_all(unicode_plan.directory);
    // Bare/missing compiler paths and output/input overlap fail before creating a job.
    for(auto compiler:{std::string("clang"),(root/"missing-clang").string()}){bool rejected=false;try{plan(bridge,cache,original.string(),seed.string(),compiler,"p");}catch(...){rejected=true;}assert(rejected);}
    bool overlap=false;try{plan(bridge,original/"cache",original.string(),seed.string(),"","p");}catch(...){overlap=true;}assert(overlap&&!fs::exists(original/"cache"));
    fs::remove(release/"core-randomizer-resources.json");bool unavailable=false;try{plan(bridge,cache,original.string(),seed.string(),"","p");}catch(...){unavailable=true;}assert(unavailable);
    fs::remove_all(root);return 0;
}
