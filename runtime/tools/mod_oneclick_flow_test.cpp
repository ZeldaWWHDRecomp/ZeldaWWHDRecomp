// Exercise the production overlay setup loop and real workers without a game or window.
#include <atomic>
#include "mod_test_host.h"
#include "overlay/setup_flow.h"
#include "overlay/setup_test.h"
#include "mods/catalogue_client.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
namespace hostui {void post(std::function<void()> fn){fn();}}
namespace {
mods::packages::View view(const std::string& id) {
    for(const auto& mod:mods::packages::list())if(mod.id==id)return mod;return {};
}
}
int main(int argc,char** argv) {
    assert(argc==3);assert(getenv("WWHD_NO_AUDIO")&&getenv("WWHD_NO_HOST_INPUT"));
    assert(getenv("WWHD_MOD_MANAGER_DIR")&&getenv("WWHD_TEST_CATALOGUE_INSTALL")&&getenv("WWHD_TEST_MOD_SETUP")&&getenv("WWHD_TEST_GAME_SOURCES"));
    using namespace mods::packages;using namespace overlay::setupflow;
    fs::path root=fs::absolute(argv[1]);std::string mode=argv[2],id=getenv("WWHD_TEST_MOD_SETUP");
    bool resumed=fs::weakly_canonical(fs::absolute(argv[0])).parent_path()==fs::weakly_canonical(root/"code-builds/synthetic/bin");
    bool missing=mode=="missing",broken=mode=="broken";
    bool cancel_mode=mode=="cancel"||mode=="cancel-rebuild",cancel_clicked=false,canceled=false;int cancel_frames=0;
    bool restart=mode=="restart"||mode=="cancel-rebuild";set_code_mod_support(!restart||resumed);initialize();
    if(!resumed) {
        auto catalogue=mods::catalogue::load((root/"index.json").string(),{},root/"unused");
        auto it=std::find_if(catalogue.index.entries.begin(),catalogue.index.entries.end(),[&](const auto& e){return e.id==getenv("WWHD_TEST_CATALOGUE_INSTALL");});
        assert(it!=catalogue.index.entries.end());
        mods::catalogue::StagedPackage staged(*it,mods::catalogue::Version::parse("0.2.10"),"USA",platform_key(),root/"stage",catalogue.fixture_root,{});
        std::string error;assert(install(staged.path().string(),error));
        auto plan=overlay::diagnostic::read_setup_plan(true,getenv("WWHD_MOD_MANAGER_DIR"),getenv("WWHD_TEST_MOD_SETUP"),getenv("WWHD_TEST_GAME_SOURCES"));
        assert(plan.id==id&&plan.error.empty());
        if(!missing)for(const auto& [game,path]:plan.sources.object)assert(set_game_source(game,path.string(),error));
        mods::code::startup(argc,argv); // real service launch arguments, fake game-code rebuild output
    } else {
        assert(pending_setup_runs()==std::vector<std::string>{id});assert(!view(id).enabled);
        assert(unconfirmed_native(id).empty());
    }
    int builds=0;std::atomic<bool> cancel_released{false};
    set_guest_builder([](const GuestPackage&){return uint32_t(65536);},[&](const GuestPackage&,uint32_t){
        ++builds;std::this_thread::sleep_for(std::chrono::milliseconds(35));
        // cancel: the build lasts until the Cancel click is complete (press and release), so a slow
        // runner can't finish it before the click lands (seen on Windows CI)
        for(int i=0;mode=="cancel"&&!cancel_released&&i<5000;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        auto module=root/"synthetic.module";std::ofstream(module)<<"synthetic guest cache";return GuestBuilt{module.string(),65536};
    });
    SDL_Surface* surface=SDL_CreateSurface(960,640,SDL_PIXELFORMAT_RGBA32);assert(surface);
    SDL_Renderer* renderer=SDL_CreateSoftwareRenderer(surface);assert(renderer);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(960,640);io.DeltaTime=1.f/60;
    io.ConfigFlags=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;io.BackendFlags|=ImGuiBackendFlags_HasGamepad;
    assert(ImGui_ImplSDLRenderer3_Init(renderer));std::string error;
    const char* window_name=mode=="catalogue"?"Catalogue entry":"Installed package";
    int confirmations=0;bool clicked=resumed,accepted=resumed,saw_progress=false,saw_failure=false,retried=false;
    bool error_capture=false,dialog_capture=false,progress_capture=false,details_capture=false,corrected=false;
    bool accept_mouse_down=false,setup_mouse_down=false,choice_mouse_down=false;int error_frames=0,choice_frames=0;
    auto capture=[&](const std::string& label) {
        auto dir=root/"captures";fs::create_directories(dir);
        assert(SDL_SaveBMP(surface,(dir/(label+".bmp")).string().c_str()));
    };
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    for(int frame=0;frame<4000&&std::chrono::steady_clock::now()<deadline;++frame) {
        assert(fs::space(root).available>10ull*1024*1024*1024);
        ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();setup_run_tick();
        ImGui::SetNextWindowPos(ImVec2(100,80),ImGuiCond_Always);ImGui::SetNextWindowSize(ImVec2(760,480));
        ImGui::Begin(window_name);ImGui::Text("%s",view(id).name.c_str());
        auto trigger_position=ImGui::GetCursorScreenPos();
        if(mode=="checkbox") {
            bool on=view(id).enabled;
            if(enable_checkbox(view(id),on)&&!handle_setup_enable(view(id),on))enable(id,on,error);
        } else setup_run_controls(view(id));
        bool had_dialog=ImGui::IsPopupOpen("Set up mod");
        auto last_id=setup_confirm.id;
        setup_confirmation_dialog(error,false);
        bool has_dialog=ImGui::IsPopupOpen("Set up mod");
        if(has_dialog&&!had_dialog)++confirmations;
        auto* popup=ImGui::FindWindowByName("Set up mod");
        // Drive the actual shared Set up button and modal Continue via pointer events.
        if(!clicked&&frame>2) {
            auto* window=ImGui::FindWindowByName(window_name);
            io.AddMousePosEvent(trigger_position.x+(mode=="checkbox"?9:25),trigger_position.y+ImGui::GetFrameHeight()/2);
            io.AddMouseButtonEvent(0,!setup_mouse_down);setup_mouse_down=!setup_mouse_down;
            if(!setup_confirm.id.empty()){clicked=true;io.AddMouseButtonEvent(0,false);}
        }
        bool choice_ready=mode!="choice"||setup_confirm.choices["colour"]=="blue";
        if(has_dialog&&popup&&frame>8&&!accepted&&!choice_ready) {
            auto* combo=ImGui::FindWindowByName("##Combo_00");
            if(combo&&combo->Active&&combo->Hidden){io.AddMouseButtonEvent(0,false);choice_mouse_down=false;}
            else {
                if(combo&&combo->Active)io.AddMousePosEvent(combo->Pos.x+30,combo->Pos.y+ImGui::GetStyle().WindowPadding.y+ImGui::GetTextLineHeightWithSpacing()+ImGui::GetTextLineHeight()/2);
                else io.AddMousePosEvent(popup->Pos.x+80,popup->Pos.y+ImGui::GetFrameHeight()+ImGui::GetStyle().WindowPadding.y+3*ImGui::GetTextLineHeightWithSpacing()+ImGui::GetFrameHeight()/2);
                io.AddMouseButtonEvent(0,!choice_mouse_down);choice_mouse_down=!choice_mouse_down;
            }
        }
        if(has_dialog&&popup&&frame>8&&!accepted&&choice_ready) {
            io.AddMousePosEvent(popup->Pos.x+40,popup->Pos.y+popup->Size.y-ImGui::GetStyle().WindowPadding.y-ImGui::GetFrameHeight()/2);
            io.AddMouseButtonEvent(0,!accept_mouse_down);accept_mouse_down=!accept_mouse_down;
        }
        if(!last_id.empty()&&setup_confirm.id.empty()&&!setup_run.id.empty()) {
            accepted=true;io.AddMouseButtonEvent(0,false);
            assert(unconfirmed_native(id).empty());
            std::ofstream(root/"consent-count")<<confirmations;
        }
        if(!setup_run.id.empty())saw_progress=true;
        if(!setup_run.error.empty()) {
            saw_failure=true;assert(broken);assert(!view(id).enabled&&pending_setup_runs().empty());
            assert(setup_run.output.find("synthetic deliberate failure")!=std::string::npos);
            ++error_frames;
            auto* window=ImGui::FindWindowByName(window_name);
            auto row=ImGui::GetTextLineHeightWithSpacing();
            auto top=window->Pos.y+ImGui::GetFrameHeight()+ImGui::GetStyle().WindowPadding.y;
            if(error_frames==3||error_frames==4) {
                io.AddMousePosEvent(window->Pos.x+50,top+3*row+5);
                io.AddMouseButtonEvent(0,error_frames==3);
            }
            if(error_frames==7||error_frames==8) {
                assert(window->StateStorage.GetInt(window->GetID("Show details"),0));
                fs::remove(root/"fail-tool");corrected=true;
                io.AddMousePosEvent(window->Pos.x+35,top+5*row+ImGui::GetFrameHeight()/2);
                io.AddMouseButtonEvent(0,error_frames==7);
            }
        }
        if(saw_failure&&corrected&&setup_run.error.empty())retried=true;
        if(cancel_mode&&(mode=="cancel-rebuild"?setup_run.rebuilding:setup_run.step=="Build the minimap")) {
            ++cancel_frames;auto* window=ImGui::FindWindowByName(window_name);
            auto top=window->Pos.y+ImGui::GetFrameHeight()+ImGui::GetStyle().WindowPadding.y;
            io.AddMousePosEvent(window->Pos.x+40,top+2*ImGui::GetTextLineHeightWithSpacing()+ImGui::GetFrameHeight()/2);
            io.AddMouseButtonEvent(0,cancel_frames==1);cancel_clicked=true;
        }
        if(cancel_frames>=2)cancel_released=true;
        if(cancel_clicked&&setup_run.id.empty())canceled=true;
        ImGui::End();ImGui::Render();SDL_SetRenderDrawColor(renderer,20,23,29,255);SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);SDL_RenderPresent(renderer);
        if(has_dialog&&popup&&!popup->Hidden&&frame>=7&&!dialog_capture){capture("dialog");dialog_capture=true;}
        if((setup_run.waiting||setup_run.rebuilding)&&setup_run.error.empty()&&!progress_capture){capture(resumed?"resume-progress":"progress");progress_capture=true;}
        if(!setup_run.error.empty()&&!error_capture){capture("error");error_capture=true;}
        if(error_frames==6&&!details_capture){capture("error-details");details_capture=true;}
        if(mode=="choice"&&has_dialog&&choice_ready&&++choice_frames==2)capture("dialog-choice");
        if(missing&&frame>8) {assert(setup_confirm.id.empty()&&setup_run.id.empty()&&confirmations==0);break;}
        if(view(id).enabled&&setup_run.id.empty())break;
        if(canceled){auto& worker=setup_work();std::lock_guard guard(worker.mutex);if(!worker.running)break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if(missing)assert(!view(id).enabled&&!accepted&&pending_setup_runs().empty());
    else if(cancel_mode) {
        assert(canceled&&!view(id).enabled&&pending_setup_runs().empty());
        assert(setup_steps(id)[1].satisfied);assert(confirmations==1);
        if(mode=="cancel-rebuild")assert(!mods::code::status().requested&&builds==0);
    }
    else {
        assert(view(id).enabled&&view(id).pending_restart&&setup_run.id.empty()&&error.empty());assert(builds==1&&saw_progress);
        assert(confirmations==(resumed?0:1));assert(pending_setup_runs().empty());
        if(broken)assert(saw_failure&&retried&&details_capture);
        if(restart)assert(resumed);
    }
    std::cout<<"FLOW "<<id<<" "<<mode<<" enabled="<<view(id).enabled<<" confirmations="<<confirmations<<" resumed="<<resumed<<" failed="<<saw_failure<<"\n";
    ImGui_ImplSDLRenderer3_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);
    mods::json::Value result;result["enabled"]=view(id).enabled;result["resumed"]=resumed;result["confirmations"]=confirmations;
    std::ofstream(root/"flow-result.json.tmp")<<mods::json::dump(result);
    fs::rename(root/"flow-result.json.tmp",root/"flow-result.json");
}
