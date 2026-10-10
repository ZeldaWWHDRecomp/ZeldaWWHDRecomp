// One-click sequence policy, shared by the overlay and headless fixtures.
#pragma once
#include "packages.h"
namespace mods::setup {
enum class Action { Idle, Blocked, Confirm, Step, Rebuild, Enable, Failed };
struct Run {
    std::string id,name,step,error,output;
    bool waiting=false,rebuilding=false;
    double started=0;
    std::string profile,identity;
    bool stopped=false;
    Action next(const std::vector<packages::SetupView>& steps,bool trusted,bool support_missing) {
        if(id.empty())return Action::Idle;
        if(!error.empty())return Action::Failed;
        if(!trusted){error="Package changed; cancel setup and confirm it again.";return Action::Failed;}
        if(waiting||rebuilding)return Action::Idle;
        for(const auto& status:steps)if(!status.satisfied&&!status.step.optional) {
            step=status.step.title;
            if(status.step.type=="game_path"){error="Required game source is unavailable.";return Action::Failed;}
            if(status.step.type=="confirm"||status.step.type=="choice") {
                error="Setup choice changed; cancel and set up again.";return Action::Failed;
            }
            if(status.step.type=="build_guest_mod"&&support_missing)return Action::Rebuild;
            return Action::Step;
        }
        if(support_missing){step="Rebuild game code";return Action::Rebuild;}
        step="Enable mod";return Action::Enable;
    }
    void complete(std::string failure,std::string details) {
        waiting=false;error=std::move(failure);output=std::move(details);
    }
    void retry(){error.clear();rebuilding=false;stopped=false;}
    void cancel(){*this={};}
};
inline Action offer(const std::vector<packages::SetupView>& steps,bool missing_source) {
    if(missing_source)return Action::Blocked;
    bool open=false;
    for(const auto& status:steps)if(!status.satisfied&&!status.step.optional) {
        if(status.step.type=="game_path")return Action::Blocked;
        open=true;
    }
    return open?Action::Confirm:Action::Enable;
}
}
