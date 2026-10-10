// Production setup loop, also driven by the game-free offscreen integration fixture.
#pragma once
#include "setup_widgets.h"
#include "../mods/code_mods.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
namespace overlay::setupflow {
struct SetupWorker {
    std::mutex mutex;
    std::thread thread;
    bool running=false;
    std::string id,error,output;
    ~SetupWorker(){if(thread.joinable())thread.join();}
};
inline SetupWorker& setup_work(){static SetupWorker worker;return worker;}
inline bool start_setup(const std::string& id,const mods::catalogue::Step& step) {
    auto& setup_worker=setup_work();
    std::lock_guard guard(setup_worker.mutex);if(setup_worker.running)return false;
    if(setup_worker.thread.joinable())setup_worker.thread.join();
    setup_worker.running=true;setup_worker.id=id;setup_worker.error.clear();setup_worker.output.clear();
    setup_worker.thread=std::thread([id,step] {
        auto& setup_worker=setup_work();
        std::string error,output;
        if(step.type=="build_guest_mod")mods::packages::prepare_guest(id,error);
        else mods::packages::run_setup_tool(id,step.id,error,output);
        std::lock_guard guard(setup_worker.mutex);setup_worker.error=std::move(error);
        setup_worker.output=std::move(output);setup_worker.running=false;
        if(getenv("WWHD_NO_HOST_INPUT")&&getenv("WWHD_MOD_MANAGER_DIR")&&getenv("WWHD_TEST_MOD_SETUP"))fprintf(stderr,"[setup test] worker %s %s %s\n",
            id.c_str(),step.id.c_str(),setup_worker.error.empty()?"ready":"failed");
    });
    return true;
}
inline mods::setup::Run setup_run;
inline setupui::Confirmation setup_confirm;
inline double resume_notice_until=0;
inline std::string resume_name;
inline bool setup_open(const std::string& id) {
    auto steps=mods::packages::setup_steps(id);
    return std::any_of(steps.begin(),steps.end(),[](const auto& s){return !s.satisfied&&!s.step.optional;});
}
inline void request_setup(const mods::packages::View& mod) {
    if(!setup_run.id.empty()||!mod.game_source_warning.empty()||(mod.kind=="guest"&&mods::packages::platform_key().starts_with("android")))return;
    {auto& worker=setup_work();std::lock_guard guard(worker.mutex);if(worker.running)return;}
    auto steps=mods::packages::setup_steps(mod.id);
    if(mods::setup::offer(steps,!mod.game_source_warning.empty())==mods::setup::Action::Blocked)return;
    setup_confirm={mod.id,mod.name,std::move(steps),{},true};
    setup_confirm.native=mods::packages::unconfirmed_native(mod.id);
    setup_confirm.identity=mods::packages::setup_identity(mod.id);
    for(const auto& status:setup_confirm.steps)if(status.step.type=="choice"&&!status.satisfied&&!status.step.choices.empty())
        setup_confirm.choices[status.step.option]=status.step.choices.front();
}
inline bool enable_checkbox(const mods::packages::View& mod,bool& on) {
    ImGui::BeginDisabled(!on&&!mod.game_source_warning.empty());
    bool changed=ImGui::Checkbox("##package_enabled",&on);
    ImGui::EndDisabled();return changed;
}
inline bool handle_setup_enable(const mods::packages::View& mod,bool on) {
    if(!on||!setup_open(mod.id))return false;
    request_setup(mod);return true;
}
inline void stop_failed_setup() {
    auto& run=setup_run;
    if(run.id.empty()||run.error.empty()||run.stopped)return;
    // A failure must not turn into an unattended retry on the next launch.
    std::string save_error;
    if(!mods::packages::save_setup_run(run.id,false,save_error))run.error+=" (could not save stopped setup: "+save_error+")";
    run.stopped=true;
}
inline void setup_run_tick() {
    using namespace mods::packages;
    static bool resumed=false;
    if(!resumed){resumed=true;auto pending=pending_setup_runs();if(!pending.empty()) {
        setup_run.id=pending.front();setup_run.name=setup_run.id;
        for(const auto& mod:list())if(mod.id==setup_run.id)setup_run.name=mod.name;
        setup_run.started=ImGui::GetTime();setup_run.step="Continuing setup...";setup_run.profile=current_profile();setup_run.identity=setup_identity(setup_run.id);
        resume_name=setup_run.name;resume_notice_until=ImGui::GetTime()+4;
        fprintf(stderr,"Continuing setup of %s...\n",setup_run.name.c_str());
    }}
    auto& run=setup_run;
    struct StopOnError {~StopOnError(){stop_failed_setup();}} stop_on_error;
    if(run.id.empty()||!run.error.empty())return;
    auto pending=pending_setup_runs();
    if(run.profile!=current_profile()||std::find(pending.begin(),pending.end(),run.id)==pending.end()) {
        run.error="Setup changed or the active profile changed. Cancel and set up again.";return;
    }
    if(run.rebuilding) {
        auto status=mods::code::status();
        if(status.ready){std::string error;if(!mods::code::restart(error))run.error=error;}
        else if(!status.error.empty()){run.error=status.error;return;}
        return;
    }
    auto& worker=setup_work();
    if(run.waiting) {
        std::lock_guard guard(worker.mutex);if(worker.running)return;
        run.complete(worker.error,worker.output);
        if(!run.error.empty())return;
    }
    auto steps=setup_steps(run.id);
    auto action=run.next(steps,unconfirmed_native(run.id).empty(),needs_code_mod_support(run.id));
    if(action==mods::setup::Action::Rebuild) {
        run.rebuilding=true;mods::code::request(true);mods::code::begin();return;
    }
    if(action==mods::setup::Action::Step) {
        auto next=std::find_if(steps.begin(),steps.end(),[](const auto& s){return !s.satisfied&&!s.step.optional;});
        run.waiting=start_setup(run.id,next->step);return;
    }
    if(action!=mods::setup::Action::Enable)return;
    if(!enable(run.id,true,run.error))return;
    if(!save_setup_run(run.id,false,run.error))return;
    fprintf(stderr,"[setup run] enabled %s\n",run.id.c_str());run={};
}
inline bool setup_confirmation_dialog(std::string& error,bool back) {
    using namespace mods::packages;auto& c=setup_confirm;
    auto answer=setupui::confirmation(c,needs_code_mod_support(c.id),!unconfirmed_native(c.id).empty(),back);
    bool accept=answer==setupui::Answer::Accept;

    if(accept) {
        if(begin_setup_run(c.id,c.identity,c.choices,error))
            setup_run={c.id,c.name,"Starting setup...", "", "",false,false,ImGui::GetTime(),current_profile(),c.identity};
    }
    if(answer!=setupui::Answer::None)c={};
    return answer==setupui::Answer::Cancel;
}
inline void setup_run_controls(const mods::packages::View& mod) {
    auto& run=setup_run;
    if(run.id!=mod.id) {
        bool busy=false,same_worker=false;
        {auto& worker=setup_work();std::lock_guard guard(worker.mutex);busy=worker.running;same_worker=worker.id==mod.id;}
        if(busy&&same_worker)ImGui::TextUnformatted("Finishing the current step...");
        if(setup_open(mod.id)) {
            ImGui::BeginDisabled(busy||!run.id.empty()||!mod.game_source_warning.empty()||(mod.kind=="guest"&&mods::packages::platform_key().starts_with("android")));
            if(ImGui::Button("Set up"))request_setup(mod);
            ImGui::EndDisabled();
        }
        return;
    }
    if(ImGui::GetTime()<resume_notice_until)ImGui::Text("Continuing setup of %s...",resume_name.c_str());
    bool retry=false;
    bool cancel=setupui::progress(run,ImGui::GetTime(),retry);
    if(retry) {
        std::string error;
        if(run.profile!=mods::packages::current_profile()||run.identity!=mods::packages::setup_identity(run.id))
            run.error="Package or profile changed; cancel and confirm setup again.";
        else if(!mods::packages::save_setup_run(run.id,true,error))run.error=error;
        else {bool restart=run.rebuilding&&mods::code::status().ready;run.retry();run.rebuilding=restart;}
    }
    if(cancel) {
        if(run.rebuilding){mods::code::cancel();mods::code::dismiss();}
        // A running tool finishes safely; its receipt is retained.
        std::string error;if(!mods::packages::save_setup_run(run.id,false,error)){run.error=error;return;}run={};
    }
}
}
