#include "overlay/setup_test.h"
#include <cassert>
#include <fstream>
#include <chrono>
using namespace overlay::diagnostic;
int main() {
    auto directory=std::filesystem::temp_directory_path()/("wwhd-setup-test-plan-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    auto input=directory/"sources.json";
    std::ofstream(input)<<R"({"gc_wind_waker":"/private/player.rvz"})";
    auto path=input.string();
    assert(read_setup_plan(false,"manager","a",path.c_str()).id.empty());
    assert(read_setup_plan(true,nullptr,"a",path.c_str()).id.empty());
    assert(read_setup_plan(true,"manager","a",nullptr).id.empty());
    auto plan=read_setup_plan(true,"manager","a",path.c_str());
    assert(plan.id=="a"&&plan.error.empty()&&plan.sources.get("gc_wind_waker").string()=="/private/player.rvz");
    assert(!read_setup_plan(true,"manager","a","relative.json").error.empty());
    std::ofstream(input)<<R"({"gc_wind_waker":123})";
    assert(!read_setup_plan(true,"manager","a",path.c_str()).error.empty());
    std::ofstream(input)<<"[]";
    assert(!read_setup_plan(true,"manager","a",path.c_str()).error.empty());
    std::ofstream(input)<<"{}";
    assert(read_setup_plan(true,"manager","a",path.c_str()).error.empty()); // no-source guest mod
    std::ofstream(input)<<std::string(65537,' ');
    assert(!read_setup_plan(true,"manager","a",path.c_str()).error.empty());
    std::vector<mods::packages::SetupView> steps(3);
    steps[0].step.type="game_path";steps[1].step.type="run_tool";steps[2].step.type="build_guest_mod";
    assert(next_required_step(steps)==0&&!setup_ready(steps));
    steps[0].satisfied=true;assert(next_required_step(steps)==1);
    steps[1].step.optional=true;assert(next_required_step(steps)==2);
    steps[2].satisfied=true;assert(setup_ready(steps));
    steps[1].step.optional=false;assert(next_required_step(steps)==1&&!setup_ready(steps));
    std::filesystem::remove_all(directory);
}
