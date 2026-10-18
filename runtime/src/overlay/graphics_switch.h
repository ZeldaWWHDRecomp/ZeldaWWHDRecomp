#pragma once
#include "../mods/packages.h"
#include "imgui.h"
#include "popup.h"

namespace overlay {
struct GraphicsSwitch {
    std::string id,name;
    std::vector<mods::packages::Conflict> conflicts;
    bool open=false;
};
enum class GraphicsSwitchAction {None,Switch,Cancel};
// Same ImGui navigation path as other overlay dialogs. The host supplies controller B.
inline GraphicsSwitchAction graphics_switch_dialog(GraphicsSwitch& choice,bool controller_cancel) {
    const bool open=choice.open;choice.open=false;
    if(!popup::begin("Switch graphics pack",open))return GraphicsSwitchAction::None;
    ImGui::PushTextWrapPos(popup::wrap_pos(28));
    for(const auto& conflict:choice.conflicts)ImGui::TextWrapped("%s conflicts with %s (%s).",choice.name.c_str(),conflict.name.c_str(),conflict.reason.c_str());
    ImGui::TextWrapped("Switch to %s?",choice.name.c_str());
    for(const auto& conflict:choice.conflicts)ImGui::TextWrapped("%s will be turned off after restart.",conflict.name.c_str());
    ImGui::PopTextWrapPos();
    auto action=GraphicsSwitchAction::None;
    if(ImGui::Button("Switch",ImVec2(120,0)))action=GraphicsSwitchAction::Switch;
    ImGui::SameLine();
    if(ImGui::Button("Cancel",ImVec2(120,0)))action=GraphicsSwitchAction::Cancel;
    ImGui::SetItemDefaultFocus();
    if(popup::dismissed(controller_cancel))action=GraphicsSwitchAction::Cancel;  // the X, a tap outside, Esc or B
    if(action!=GraphicsSwitchAction::None)ImGui::CloseCurrentPopup();
    popup::end();
    return action;
}
}
