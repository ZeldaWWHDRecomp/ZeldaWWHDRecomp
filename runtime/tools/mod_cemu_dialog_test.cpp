// Real ImGui frames, synthetic pack names, no window/backend or game assets.
#include "overlay/graphics_switch.h"
#include "imgui_internal.h"
#include <cassert>
#include <iostream>
using namespace overlay;
int main(){
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(1000,700);io.DeltaTime=1.f/60;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags|=ImGuiBackendFlags_HasGamepad;
    io.ConfigNavCursorVisibleAlways=true;
    io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
    GraphicsSwitch choice{"new","New",{{"old","Old","both change the same shader"}},true};
    auto frame=[&](bool cancel=false){
        ImGui::NewFrame();ImGui::Begin("Mods");auto result=graphics_switch_dialog(choice,cancel);
        ImGui::End();ImGui::Render();return result;
    };
    auto press=[&](ImGuiKey key){io.AddKeyEvent(key,true);auto down=frame();io.AddKeyEvent(key,false);auto up=frame();return down==GraphicsSwitchAction::None?up:down;};
    frame();frame();
    auto* popup=ImGui::FindWindowByName("Switch graphics pack");assert(popup);
    assert(ImGui::GetCurrentContext()->NavId==popup->GetID("Cancel"));
    assert(press(ImGuiKey_Enter)==GraphicsSwitchAction::Cancel);
    choice.open=true;frame();frame();
    io.AddKeyEvent(ImGuiKey_LeftArrow,true);frame();
    io.AddKeyEvent(ImGuiKey_LeftArrow,false);frame();
    assert(press(ImGuiKey_Enter)==GraphicsSwitchAction::Switch);
    choice.open=true;frame();frame();
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft,true);frame();
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft,false);frame();
    assert(press(ImGuiKey_GamepadFaceDown)==GraphicsSwitchAction::Switch);
    choice.open=true;frame();frame();assert(frame(true)==GraphicsSwitchAction::Cancel);
    choice.open=true;frame();frame();
    popup=ImGui::FindWindowByName("Switch graphics pack");
    assert(ImGui::GetCurrentContext()->NavId==popup->GetID("Cancel"));
    // Buttons are the final line of the real popup; aim at the center of Switch.
    io.AddMousePosEvent(popup->DC.CursorStartPos.x+60,popup->DC.CursorPosPrevLine.y+ImGui::GetFrameHeight()/2);frame();
    io.AddMouseButtonEvent(0,true);frame();
    io.AddMouseButtonEvent(0,false);assert(frame()==GraphicsSwitchAction::Switch);
    ImGui::DestroyContext();std::cout<<"Cemu switch dialog mouse, keyboard and controller passed\n";
}
