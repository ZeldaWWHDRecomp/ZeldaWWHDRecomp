// Shared GameCube setting widgets; host callbacks remain in overlay.cpp.
#pragma once
#include "../mods/packages.h"
#include "imgui.h"

namespace overlay {
enum class GameSourceAction {None,Disc,Folder,Clear};
inline GameSourceAction game_source_widgets(const mods::packages::GameSourceView& source,bool focus) {
    // The installed-package picker also has a "Choose folder…" button in this window.
    ImGui::PushID("gamecube_source");
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.70f,0.78f,0.84f,1.0f));
    ImGui::TextWrapped("Some mods use files from the GameCube version of the game. They stay on your computer.");
    ImGui::PopStyleColor();
    ImGui::TextWrapped("%s",source.path.empty()?"No path selected":source.path.c_str());
    ImGui::TextWrapped("%s",source.result.c_str());
    GameSourceAction action=GameSourceAction::None;
    if(focus){ImGui::SetKeyboardFocusHere();ImGui::SetNavCursorVisible(true);}
    if(ImGui::Button("Choose disc image…"))action=GameSourceAction::Disc;
    ImGui::SameLine();if(ImGui::Button("Choose folder…"))action=GameSourceAction::Folder;
    ImGui::SameLine();ImGui::BeginDisabled(source.path.empty());
    if(ImGui::Button("Clear"))action=GameSourceAction::Clear;
    ImGui::EndDisabled();ImGui::PopID();
    return action;
}
}
