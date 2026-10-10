// Actual setting widgets beside the installed-package picker, with real ImGui input.
#include "overlay/game_source_widgets.h"
#include "imgui_internal.h"
#include <cassert>
#include <iostream>
#include <vector>
int main() {
    using namespace overlay;
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.DisplaySize={1000,700};io.DeltaTime=1.f/60;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags|=ImGuiBackendFlags_HasGamepad|ImGuiBackendFlags_RendererHasTextures;
    io.ConfigNavCursorVisibleAlways=true;
    mods::packages::GameSourceView source{"synthetic source","Recognised GameCube Wind Waker · USA",true};
    std::vector<GameSourceAction> actions;int package_picks=0;bool focus=true;ImVec2 clear;
    auto frame=[&]{
        ImGui::NewFrame();ImGui::SetNextWindowPos({40,40});ImGui::SetNextWindowSize({900,600});
        ImGui::Begin("Mods");
        auto action=game_source_widgets(source,focus);focus=false;
        if(action!=GameSourceAction::None)actions.push_back(action);
        auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();clear={(a.x+b.x)/2,(a.y+b.y)/2};
        ImGui::SeparatorText("Installed packages");
        if(ImGui::Button("Choose folder…"))++package_picks;
        ImGui::End();ImGui::Render();
    };
    auto press=[&](ImGuiKey key){io.AddKeyEvent(key,true);frame();frame();io.AddKeyEvent(key,false);frame();frame();};
    frame();frame();
    press(ImGuiKey_RightArrow);press(ImGuiKey_Enter);
    assert(actions==std::vector<GameSourceAction>{GameSourceAction::Folder}&&package_picks==0);
    press(ImGuiKey_GamepadDpadLeft);press(ImGuiKey_GamepadFaceDown);
    assert(actions.size()==2&&actions.back()==GameSourceAction::Disc&&package_picks==0);
    auto click_clear=[&]{io.AddMousePosEvent(clear.x,clear.y);frame();frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();frame();};
    click_clear();assert(actions.size()==3&&actions.back()==GameSourceAction::Clear);
    source.path.clear();frame();click_clear();assert(actions.size()==3&&package_picks==0);
    ImGui::DestroyContext();std::cout<<"GameCube setting mouse/keyboard/controller and picker isolation passed\n";
}
