// Shared ImGui controls: the production overlay and offscreen tests use the same widgets.
#pragma once
#include "../mods/packages.h"
#include "../mods/setup_run.h"
#include "imgui.h"
#include <map>
namespace overlay::setupui {
struct Confirmation {
    std::string id,name;
    std::vector<mods::packages::SetupView> steps;
    std::map<std::string,std::string> choices;
    bool open=false;
    std::vector<std::pair<std::string,std::string>> native;
    std::string identity;
};
enum class Answer {None,Accept,Cancel};
inline Answer confirmation(Confirmation& c,bool needs_support,bool untrusted,bool back) {
    if(c.open){ImGui::OpenPopup("Set up mod");c.open=false;}
    if(!ImGui::BeginPopupModal("Set up mod",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return Answer::None;
    ImGui::PushTextWrapPos(ImGui::GetFontSize()*32);
    ImGui::TextWrapped("%s will now:",c.name.c_str());
    for(const auto& status:c.steps)if(!status.satisfied&&!status.step.optional) {
        const auto& step=status.step;
        ImGui::TextWrapped("%s%s",step.title.c_str(),step.type=="run_tool"?" (runs the mod's own tool)":"");
        if(step.type=="choice") {
            auto& value=c.choices[step.option];
            ImGui::SetNextItemWidth(ImGui::GetFontSize()*24);
            if(ImGui::BeginCombo(("##"+step.option).c_str(),value.c_str())) {
                for(const auto& choice:step.choices)if(ImGui::Selectable(choice.c_str(),choice==value))value=choice;
                ImGui::EndCombo();
            }
        }
    }
    if(needs_support)ImGui::TextWrapped("The game code is rebuilt once with mod support (a few minutes, can cost some performance), then the game restarts.");
    if(untrusted)for(const auto& [id,name]:c.native)ImGui::TextWrapped("Runs code from %s",name.c_str());
    if(untrusted)ImGui::TextWrapped("This package contains code that runs with the game's full permissions. Only continue if you trust its source. Changed packages ask again.");
    ImGui::PopTextWrapPos();
    bool accept=ImGui::Button("Continue",ImVec2(120,0));ImGui::SameLine();
    bool cancel=ImGui::Button("Cancel",ImVec2(120,0));ImGui::SetItemDefaultFocus();
    if(back){cancel=true;accept=false;}
    auto answer=accept?Answer::Accept:cancel?Answer::Cancel:Answer::None;
    if(answer!=Answer::None)ImGui::CloseCurrentPopup();
    ImGui::EndPopup();return answer;
}
inline bool progress(mods::setup::Run& run,double now,bool& retry) {
    ImGui::TextWrapped("%s · %.0f seconds",run.rebuilding?"Rebuilding game code...":run.step.c_str(),now-run.started);
    retry=false;
    if(!run.error.empty()) {
        ImGui::TextWrapped("%s",run.error.c_str());
        if(ImGui::TreeNode("Show details")){ImGui::TextWrapped("%s",run.output.empty()?run.error.c_str():run.output.c_str());ImGui::TreePop();}
        retry=ImGui::Button("Try again");
    }
    return ImGui::Button("Cancel setup");
}
}
