#include "mods/setup_run.h"
#include <cassert>
#include <iostream>
using namespace mods::setup;
using mods::packages::SetupView;
int main() {
    std::vector<SetupView> steps(3);
    steps[0].step.type="game_path";steps[1].step.type="run_tool";steps[1].step.title="Prepare maps";
    steps[2].step.type="build_guest_mod";steps[2].step.title="Build minimap";
    assert(offer(steps,true)==Action::Blocked);
    assert(offer(steps,false)==Action::Blocked);steps[0].satisfied=true;
    assert(offer(steps,false)==Action::Confirm);
    Run run;assert(run.next(steps,true,false)==Action::Idle);
    run.id="minimap";assert(run.next(steps,true,false)==Action::Step&&run.step=="Prepare maps");
    run.waiting=true;assert(run.next(steps,true,false)==Action::Idle);
    run.complete("Broken tool","exit 7");assert(run.next(steps,true,false)==Action::Failed);
    run.retry();assert(run.next(steps,true,false)==Action::Step&&run.output=="exit 7");
    steps[1].satisfied=true;assert(run.next(steps,true,true)==Action::Rebuild);
    run.rebuilding=true;assert(run.next(steps,true,true)==Action::Idle);
    Run restarted;restarted.id=run.id;
    assert(restarted.next(steps,true,false)==Action::Step&&restarted.step=="Build minimap");
    steps[2].satisfied=true;assert(restarted.next(steps,true,false)==Action::Enable);
    assert(restarted.next(steps,false,false)==Action::Failed);
    restarted.cancel();assert(restarted.id.empty()&&steps[1].satisfied);
    steps[1].step.optional=true;steps[1].satisfied=false;
    assert(offer(steps,false)==Action::Enable);
    steps[1].step.optional=false;steps[1].step.type="choice";steps[1].step.choices={"green","blue"};
    assert(offer(steps,false)==Action::Confirm);
    Run choice;choice.id="choice";assert(choice.next(steps,true,false)==Action::Failed);
    choice.retry();steps[1].satisfied=true;assert(choice.next(steps,true,false)==Action::Enable);
    std::cout<<"Setup sequence, source gate, retry, cancel, trust and restart policy passed\n";
}
