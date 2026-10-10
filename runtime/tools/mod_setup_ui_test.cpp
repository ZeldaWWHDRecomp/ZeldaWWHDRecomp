// Offscreen production widgets; no window, audio, game or native code is loaded.
#include "overlay/setup_widgets.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <filesystem>
#include <iostream>
int main(int argc,char** argv) {
    SDL_Surface* surface=SDL_CreateSurface(960,640,SDL_PIXELFORMAT_RGBA32);assert(surface);
    SDL_Renderer* renderer=SDL_CreateSoftwareRenderer(surface);assert(renderer);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.ConfigFlags=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags|=ImGuiBackendFlags_HasGamepad;io.DisplaySize=ImVec2(960,640);io.DeltaTime=1.f/60;
    assert(ImGui_ImplSDLRenderer3_Init(renderer));
    using namespace overlay::setupui;
    Confirmation c;c.id="gc-sea-minimap";c.name="Sea minimap";c.open=true;
    mods::packages::SetupView tool,build;tool.step.type="run_tool";tool.step.title="Prepare island maps from your GameCube game";
    build.step.type="build_guest_mod";build.step.title="Build the minimap for your installation";c.steps={tool,build};
    auto capture=[&](const char* name) {
        if(argc<2)return;
        std::filesystem::create_directories(argv[1]);auto path=std::filesystem::path(argv[1])/(std::string(name)+".bmp");
        assert(SDL_SaveBMP(surface,path.string().c_str()));
    };
    auto frame=[&](bool show_dialog,mods::setup::Run* run=nullptr) {
        ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(100,80),ImGuiCond_Always);ImGui::SetNextWindowSize(ImVec2(760,480));
        ImGui::Begin("Mod setup");Answer answer=Answer::None;
        if(show_dialog)answer=confirmation(c,true,true,false);
        if(run){bool retry=false;progress(*run,12,retry);}
        ImGui::End();ImGui::Render();SDL_SetRenderDrawColor(renderer,20,23,29,255);SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);SDL_RenderPresent(renderer);return answer;
    };
    for(int i=0;i<4;i++)assert(frame(true)==Answer::None);capture("minimap-dialog");
    // Mouse accepts the actual Continue button.
    auto* popup=ImGui::FindWindowByName("Set up mod");assert(popup);
    io.AddMousePosEvent(popup->Pos.x+40,popup->Pos.y+popup->Size.y-ImGui::GetStyle().WindowPadding.y-ImGui::GetFrameHeight()/2);
    io.AddMouseButtonEvent(0,true);frame(true);io.AddMouseButtonEvent(0,false);assert(frame(true)==Answer::Accept);
    // Keyboard and gamepad activate the safe default (Cancel).
    for(auto key:{ImGuiKey_Enter,ImGuiKey_GamepadFaceDown}) {
        io.AddMousePosEvent(-100,-100);c.open=true;for(int i=0;i<5;i++)frame(true);
        if(key==ImGuiKey_Enter){io.AddKeyEvent(ImGuiKey_Tab,true);frame(true);io.AddKeyEvent(ImGuiKey_Tab,false);frame(true);}

        io.AddKeyEvent(key,true);auto answer=frame(true);io.AddKeyEvent(key,false);
        auto released=frame(true);assert(answer==Answer::Cancel||released==Answer::Cancel);
    }
    mods::setup::Run run;run.id=c.id;run.step="Preparing island maps…";frame(false,&run);capture("minimap-progress");
    run.error="Preparation tool failed (exit 7).";run.output="synthetic final failure";frame(false,&run);capture("minimap-error");
    c.name="Heart ticker";c.id="heart-ticker";c.steps={build};c.steps[0].step.title="Build the heart ticker for your installation";c.open=true;
    for(int i=0;i<4;i++)frame(true);capture("heart-ticker-dialog");
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown,true);frame(true);io.AddKeyEvent(ImGuiKey_GamepadFaceDown,false);frame(true);
    run.error.clear();run.output.clear();run.step="Building the heart ticker…";frame(false,&run);capture("heart-ticker-progress");
    run.rebuilding=true;frame(false,&run);capture("restart-progress");
    ImGui_ImplSDLRenderer3_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);
    std::cout<<"Production setup dialog mouse/keyboard/controller and progress/error frames passed\n";
}
