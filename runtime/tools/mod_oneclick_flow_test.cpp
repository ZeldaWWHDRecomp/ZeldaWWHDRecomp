// The Mods tab as a player uses it (overlay/mods_tab.h), with the production setup loop and real workers,
// without a game or window: switch a mod on, answer the one review, watch the progress, end with the mod
// on. Driven by runtime/tools/test_mod_oneclick_flow.py with synthetic packages; clicks land on the
// rectangles the tab's own buttons reported in the previous frame.
#include <atomic>
#include "mod_test_host.h"
#include "overlay/mods_tab.h"
#include "overlay/extensions.h"
#include "overlay/quit_dialog.h"
#include "mods/catalogue_client.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
namespace fs=std::filesystem;
namespace hostui {void post(std::function<void()> fn){fn();}}
namespace {
mods::packages::View view(const std::string& id) {
    for(const auto& mod:mods::packages::list())if(mod.id==id)return mod;return {};
}
// One click on a labelled button: move there and press, then release (ImGui trickles the queued input).
struct Clicker {
    std::string target;int phase=0;
    bool step(ImGuiIO& io) {  // true when the click is complete
        auto& rects=overlay::modsui::state().rects;auto it=rects.find(target);
        if(target.empty()||it==rects.end())return false;
        auto c=it->second.GetCenter();
        if(phase==0){io.AddMousePosEvent(c.x,c.y);io.AddMouseButtonEvent(0,true);phase=1;return false;}
        io.AddMouseButtonEvent(0,false);phase=0;target.clear();return true;
    }
    void click(const std::string& name){if(target.empty()){target=name;phase=0;}}
    bool busy()const{return !target.empty();}
};
// the review is open (whatever ID stack the tab opened it from)
bool review_open() {
    for(const auto& popup:GImGui->OpenPopupStack)if(popup.Window&&strstr(popup.Window->Name,"###mods_review"))return true;
    return false;
}
// Two tabs as a port would add them (overlay/extensions.h): "test-platform" before Mods, with two sub-tabs
// (L / R move between them first) and the widgets a platform page has, and "test-extra" right after Mods.
void ext_tabs_for_test(int& area) {
    namespace ext=overlay::ext;
    ext::Tab platform;
    platform.id="test-platform";platform.name="Platform";
    platform.draw=[&area] {
        if(ImGui::BeginTabBar("platform_areas")) {
            for(int i=0;i<2;i++)if(ImGui::BeginTabItem(i?"Debug":"Picture",nullptr,area==i?ImGuiTabItemFlags_SetSelected:0))ImGui::EndTabItem();
            ImGui::EndTabBar();
        }
        static float exposure=1,gamma=1;static bool on=true;static char name[64]="profile";
        ImGui::PushID(area);  // the same labels on both sub-tabs: one ID per sub-tab
        ImGui::SeparatorText(area?"Debug":"Picture");
        ImGui::SliderFloat("Exposure",&exposure,.5f,1.5f);
        ImGui::SliderFloat("Gamma",&gamma,.5f,2.f);
        ImGui::Checkbox("Enabled",&on);
        text_entry::field("Name",name,sizeof name);
        for(int i=0;i<3;i++){ImGui::PushID(i);if(i)ImGui::SameLine();ImGui::Button("Apply");ImGui::PopID();}
        ImGui::PopID();
    };
    platform.shoulder=[&area](int direction) {
        const int next=area+direction;
        if(next<0||next>1)return false;
        area=next;return true;
    };
    ext::add_tab(platform,"mods");
    ext::add_tab({"test-extra","Extra",[]{ImGui::TextUnformatted("Extra");ImGui::Button("Go");},nullptr},"controls");
}
void set_env(const char* key,const std::string& value) {
#ifdef _WIN32
    _putenv_s(key,value.c_str());
#else
    setenv(key,value.c_str(),1);
#endif
}
}
int main(int argc,char** argv) {
    assert(argc==3);assert(getenv("WWHD_NO_AUDIO")&&getenv("WWHD_NO_HOST_INPUT"));
    assert(getenv("WWHD_MOD_MANAGER_DIR")&&getenv("WWHD_TEST_MOD_SETUP")&&getenv("WWHD_TEST_GAME_SOURCES"));
    using namespace mods::packages;using namespace overlay::setupflow;namespace ui=overlay::modsui;
    fs::path root=fs::absolute(argv[1]);std::string mode=argv[2],id=getenv("WWHD_TEST_MOD_SETUP");
    bool resumed=fs::weakly_canonical(fs::absolute(argv[0])).parent_path()==fs::weakly_canonical(root/"code-builds/synthetic/bin");
    // exp-*: an experimental mod (the review warns; switching it on backs up the saves once, first)
    bool experimental=mode.starts_with("exp-"),backup_fails=mode=="exp-backup-fails";
    bool missing=mode=="missing",broken=mode=="broken",catalogue=mode=="catalogue"||mode=="exp-catalogue"||backup_fails;
    bool tabs=mode=="tabs";  // each mod is listed in its own sub-tab
    bool update_off=mode=="update-off",update_on=mode=="update-on"||mode=="exp-update-on",ids=mode=="ids"||tabs,update=update_off||update_on||ids;
    if(experimental) {
        // the saves the backup copies; an earlier enable (exp-update-on) backs up elsewhere, so only the
        // backups of this flow are counted in <root>/save-backups
        fs::create_directories(root/"save");std::ofstream(root/"save"/"slot.bin")<<"synthetic save";
        std::lock_guard guard(mods::saves::mutex());
        mods::saves::store().log=[](const std::string&){};
        mods::saves::store().configure(root/"setup-save",root/"setup-backups");
        fs::create_directories(root/"setup-save");std::ofstream(root/"setup-save"/"slot.bin")<<"synthetic save";
    }
    bool cancel_mode=mode=="cancel"||mode=="cancel-rebuild",canceled=false;
    bool restart=mode=="restart"||mode=="cancel-rebuild";set_code_mod_support(!restart||resumed);initialize();
    auto plan=overlay::diagnostic::read_setup_plan(true,getenv("WWHD_MOD_MANAGER_DIR"),getenv("WWHD_TEST_MOD_SETUP"),getenv("WWHD_TEST_GAME_SOURCES"));
    assert(plan.id==id&&plan.error.empty());
    if(!resumed) {
        if(!catalogue) {  // already downloaded (switched on from the list); the catalogue mode downloads it
            // update modes: the older version is installed; the catalogue offers the newer one
            auto index=mods::catalogue::load((root/(update?"installed-index.json":"index.json")).string(),{},root/"unused");
            auto it=std::find_if(index.index.entries.begin(),index.index.entries.end(),[&](const auto& e){return e.id==id;});
            assert(it!=index.index.entries.end());
            mods::catalogue::StagedPackage staged(*it,mods::catalogue::Version::parse("0.2.10"),"USA",platform_key(),root/"stage",index.fixture_root,{});
            std::string error;assert(install(staged.path().string(),error));
            if(update_on) {  // on before the update; it must be on after it
                if(experimental)assert(acknowledge_experimental(id,error));
                assert(enable(id,true,error));
            }
        }
        mods::code::startup(argc,argv); // real service launch arguments, fake game-code rebuild output
    } else {
        assert(pending_setup_runs()==std::vector<std::string>{id});assert(!view(id).enabled);
        assert(unconfirmed_native(id).empty());
    }
    int builds=0;std::atomic<bool> cancel_released{false};
    set_guest_builder([](const GuestPackage&){return uint32_t(65536);},[&](const GuestPackage&,uint32_t){
        ++builds;std::this_thread::sleep_for(std::chrono::milliseconds(35));
        // cancel: the build lasts until the cancel has taken effect (ImGui trickles one input per frame)
        for(int i=0;mode=="cancel"&&!cancel_released&&i<5000;++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        auto module=root/"synthetic.module";std::ofstream(module)<<"synthetic guest cache";return GuestBuilt{module.string(),65536};
    });
    // the display: a desktop window, or a phone (WWHD_TEST_DISPLAY=WxH) for the captures
    int width=960,height=640;
    if(const char* d=getenv("WWHD_TEST_DISPLAY"))sscanf(d,"%dx%d",&width,&height);
    else if(ids)height=2400;  // tall enough that nothing in a view scrolls out of the sweep
    SDL_Surface* surface=SDL_CreateSurface(width,height,SDL_PIXELFORMAT_RGBA32);assert(surface);
    SDL_Renderer* renderer=SDL_CreateSoftwareRenderer(surface);assert(renderer);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(float(width),float(height));io.DeltaTime=1.f/60;
    io.ConfigFlags=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;io.BackendFlags|=ImGuiBackendFlags_HasGamepad;
    // the overlay's font where the test machine has it (WWHD_TEST_FONT=<ttf>), for captures that look like the game
    if(const char* font=getenv("WWHD_TEST_FONT"))if(fs::is_regular_file(font)){io.Fonts->AddFontFromFileTTF(font,17.0f);ImGui::GetStyle().FontSizeBase=17.0f;}
    // WWHD_TEST_ANDROID_STYLE=1: the overlay's Android touch style and near-full-screen Settings window (with
    // WWHD_TEST_DISPLAY at the logical size a phone gets from mobile_ui::scale, e.g. UI size Large)
    const bool android_style=getenv("WWHD_TEST_ANDROID_STYLE")!=nullptr;
    if(android_style){auto& st=ImGui::GetStyle();st.FramePadding=ImVec2(10,16);st.ItemInnerSpacing=ImVec2(10,8);st.ScrollbarSize=49;st.GrabMinSize=49;}
    // ids, tabs: tabs a port adds (overlay/extensions.h) behave as built-in ones; these two stand for them
    // (sub-tabs with L / R as Mods has, a text field, widgets with one label in two places)
    int platform_area=0;
    if(ids||tabs)ext_tabs_for_test(platform_area);
    // the rest of the Settings window around the tab: the settings tabs above (every tab of the overlay,
    // built-in and added); the page ends above the bottom row (Quit Game; on Android Close Settings too).
    // shown_tab: the tab drawn, "mods" or an added one
    std::string shown_tab="mods";
    auto draw_tab=[&](ui::Host& h) {
        if(ImGui::BeginTabBar("settings_tabs")){for(const auto& t:overlay::ext::tabs())
            if(ImGui::BeginTabItem(t.name.c_str(),nullptr,t.id==shown_tab?ImGuiTabItemFlags_SetSelected:0))ImGui::EndTabItem();ImGui::EndTabBar();}
        ImGui::BeginChild("page",ImVec2(0,-ImGui::GetFrameHeightWithSpacing()),ImGuiChildFlags_NavFlattened);
        if(shown_tab=="mods")ui::draw(h,"");
        else for(const auto& t:overlay::ext::tabs())if(t.id==shown_tab&&t.draw)t.draw();
        ImGui::EndChild();
        overlay::quit::bottom_row(android_style,nullptr);
    };
    auto window_size=[&]{return android_style?ImVec2(width*0.98f,height*0.97f):ImVec2(std::min(860.f,width*0.94f),std::min(620.f,height*0.92f));};
    assert(ImGui_ImplSDLRenderer3_Init(renderer));
    ui::Host host;
    host.port_version=mods::catalogue::Version::parse("0.2.10");
    host.can_restart=[]{return true;};
    host.restart=[](bool){};
    int picks=0;
    // the file picker: the player picks the synthetic disc (or closes the picker: "missing")
    host.choose_game_source=[&](bool,std::function<void(std::string)> chosen){
        ++picks;std::string path;
        if(!missing)for(const auto& [game,value]:plan.sources.object)path=value.string();
        chosen(path);
    };
    if(experimental) {
        std::lock_guard guard(mods::saves::mutex());
        // exp-backup-fails: the backups folder can't be created (a file is in its place)
        if(backup_fails)std::ofstream(root/"blocked")<<"not a folder";
        mods::saves::store().configure(root/"save",backup_fails?root/"blocked"/"save-backups":fs::path());
        hostui::set("mod.showExperimental","1");  // the list shows experimental mods
    }
    if(catalogue||update)set_env("WWHD_MOD_CATALOGUE",(root/"index.json").string());
    else set_env("WWHD_MOD_CATALOGUE",(root/"no-index.json").string());  // the list from the manager only
    int confirmations=0;bool accepted=resumed,saw_progress=false,saw_failure=false,retried=false,corrected=false,saw_done=false;
    int pad_pressed=0,review_since=0;bool switched=resumed,blue_seen=false;ImRect blue_last;int failure_frames=0;
    std::set<std::string> captured;
    auto capture=[&](const std::string& label) {
        if(!captured.insert(label).second)return;
        fs::path dir=getenv("WWHD_TEST_CAPTURE_DIR")?fs::path(getenv("WWHD_TEST_CAPTURE_DIR")):root/"captures";
        fs::create_directories(dir);
        auto name=std::string(getenv("WWHD_TEST_CAPTURE_PREFIX")?getenv("WWHD_TEST_CAPTURE_PREFIX"):"")+label+".bmp";
        assert(SDL_SaveBMP(surface,(dir/name).string().c_str()));
    };
    // ids: every view of the tab, with every section open, hovered all over: Dear ImGui reports two items with
    // one ID (it would show its red "conflicting ID" box in a debug build) when the hovered ID was submitted twice
    if(tabs) {
        auto& s=ui::state();
        // which mods a sub-tab lists: the rows it drew ("open_<id>") in one frame
        auto listed=[&](int area) {
            s.page.clear();ui::go_area(area);
            std::set<std::string> ids_seen;
            for(int i=0;i<3;i++) {
                s.rects.clear();
                ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0,0),ImGuiCond_Always);ImGui::SetNextWindowSize(ImVec2(float(width),float(height)));
                ImGui::Begin("Settings");draw_tab(host);ImGui::End();ImGui::Render();
            }
            for(const auto& [key,rect]:s.rects)if(key.starts_with("open_"))ids_seen.insert(key.substr(5));
            assert(s.area==area);
            return ids_seen;
        };
        for(int i=0;i<400&&!s.loader.snapshot().have_list;i++){listed(ui::kCommunity);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        auto builtin=listed(ui::kBuiltin),community=listed(ui::kCommunity);
        std::set<std::string> expected_builtin;
        for(const auto& e:mods::manager::entries())expected_builtin.insert(e.id);
        assert(builtin==expected_builtin);                                  // every built-in mod, nothing else
        assert(community==(std::set<std::string>{id,"available-fixture"}));  // the installed one and the downloadable one
        for(const auto& b:builtin)fprintf(stderr,"[tabs] Built-in: %s\n",b.c_str());
        for(const auto& c:community)fprintf(stderr,"[tabs] Community: %s\n",c.c_str());
        // the sub-tab is remembered for the session: More stays selected across frames and menu openings
        listed(ui::kMore);
        for(int i=0;i<3;i++){ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();ImGui::Begin("Settings");draw_tab(host);ImGui::End();ImGui::Render();}
        assert(s.area==ui::kMore);
        // Features::code_mods / mod_downloads / file_picker off (a platform without them, overlay/extensions.h):
        // More has no Code-mod support, Mod list or GameCube game section, Community no mods to download
        auto more_sections=[&] {
            s.rects.clear();
            ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0,0),ImGuiCond_Always);ImGui::SetNextWindowSize(ImVec2(float(width),float(height)));
            ImGui::Begin("Settings");draw_tab(host);ImGui::End();ImGui::Render();
            return std::set<std::string>{s.rects.contains("Code-mod support")?"code":"",s.rects.contains("Mod list")?"list":"",
                                         s.rects.contains("GameCube game for mods")?"gc":""};
        };
        assert((more_sections()==std::set<std::string>{"code","list","gc"}));
        host.code_mods=host.mod_downloads=host.file_picker=false;
        assert((more_sections()==std::set<std::string>{""}));
        assert((listed(ui::kCommunity)==std::set<std::string>{id}));  // the installed one only
        host.code_mods=host.mod_downloads=host.file_picker=true;
        listed(ui::kMore);
        fprintf(stderr,"[tabs] Features off: Code-mod support, Mod list, GameCube game and downloads left out\n");
        // controller L / R over the tab bar as the overlay does it (ext::shoulder with the Mods areas): through
        // an added tab's own sub-tabs, into Mods and its areas, on to the next added tab, and back
        namespace ext=overlay::ext;
        const int platform=ext::tab_index("test-platform"),mods_tab=ext::tab_index("mods"),extra=ext::tab_index("test-extra");
        const int mods_builtin=ext::builtin_tab(mods_tab);  // overlay.cpp kMods
        auto areas=[mods_builtin](int builtin,int direction){return builtin==mods_builtin&&ui::shoulder(direction);};
        assert(platform>=0&&mods_tab==platform+1&&extra==mods_tab+1);
        ui::go_area(ui::kBuiltin);platform_area=0;
        assert(ext::shoulder(platform,+1,areas)==-1&&platform_area==1);  // inside the added tab
        assert(ext::shoulder(platform,+1,areas)==mods_tab);              // its end: Mods
        assert(ext::shoulder(mods_tab,+1,areas)==-1&&s.area==ui::kCommunity);
        assert(ext::shoulder(mods_tab,+1,areas)==-1&&s.area==ui::kMore);
        assert(ext::shoulder(mods_tab,+1,areas)==extra);                 // Mods' end: the next tab
        assert(ext::shoulder(extra,-1,areas)==mods_tab&&s.area==ui::kMore);  // back: Mods remembers More
        assert(ext::shoulder(mods_tab,-1,areas)==-1&&ext::shoulder(mods_tab,-1,areas)==-1&&s.area==ui::kBuiltin);
        assert(ext::shoulder(mods_tab,-1,areas)==platform&&platform_area==1);
        assert(ext::shoulder(platform,-1,areas)==-1&&platform_area==0);
        assert(ext::shoulder(platform,-1,areas)==ext::next_tab(platform,-1));
        fprintf(stderr,"[tabs] L / R: added tabs and Mods areas in order\n");
        ImGui_ImplSDLRenderer3_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);
        return 0;
    }
    if(ids) {
        assert(io.ConfigDebugHighlightIdConflicts);
        host.expand_all=true;
        int conflicts=0;
        auto frame_once=[&](float mx,float my) {
            io.AddMousePosEvent(mx,my);
            ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();
            if(GImGui->DebugDrawIdConflictsId){++conflicts;return false;}
            ImGui::SetNextWindowPos(ImVec2(0,0),ImGuiCond_Always);ImGui::SetNextWindowSize(ImVec2(float(width),float(height)));
            ImGui::Begin("Settings",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);
            draw_tab(host);ImGui::End();ImGui::Render();
            return true;
        };
        auto sweep=[&](const std::string& view,const std::function<void()>& set) {
            // close what the previous view left open (the review)
            ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();
            if(!GImGui->OpenPopupStack.empty())ImGui::ClosePopupToLevel(0,true);
            ImGui::EndFrame();
            set();
            for(int i=0;i<4;i++)frame_once(-100,-100);  // let the view (and a popup) appear
            // Sweep down to the lowest item drawn, not the whole tall window: the tab's own area (a child that
            // fills the window, so its content end is where the items end) and any popup. Sweeping the whole
            // 2400-pixel window took ~220,000 frames: 20 s here, over 30 s on Windows CI.
            // With a modal popup open only the popup can be hovered: sweep just its rectangle.
            float top=2,bottom=0;
            for(auto* w:GImGui->Windows) {
                if(!w->Active)continue;
                if(w->Flags&ImGuiWindowFlags_Modal){top=w->Pos.y;bottom=w->Pos.y+w->Size.y;break;}
                if(strstr(w->Name,shown_tab=="mods"?"mods_area":"/page_"))bottom=std::max(bottom,w->DC.CursorMaxPos.y);
            }
            bottom=std::min(bottom+8.f,float(height));
            int before=conflicts;
            // rows and buttons are at least one text line (17 px) tall and 30 px wide: 8 x 24 px steps hit each
            for(float y=top;y<bottom;y+=8)for(float x=4;x<width;x+=24)
                if(!frame_once(x,y)){
                    fprintf(stderr,"ID conflict in %s at %.0f,%.0f (ID %08X)\n",view.c_str(),x,y,GImGui->DebugDrawIdConflictsId);
                    ImGui::EndFrame();
                }
            fprintf(stderr,"[ids] %s: %d conflicts (swept to y=%.0f)\n",view.c_str(),conflicts-before,bottom);
        };
        auto& s=ui::state();
        auto page=[&](const std::string& pid,bool builtin){return [&s,pid,builtin]{s.review={};s.job.flow={};ui::go_area(builtin?ui::kBuiltin:ui::kCommunity);s.page=pid;s.page_builtin=builtin;};};
        // wait for the catalogue list (local fixture) so the update and the downloadable mod show
        for(int i=0;i<400&&!s.loader.snapshot().have_list;i++){frame_once(-100,-100);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        assert(s.loader.snapshot().have_list);
        sweep("built-in list",page("",true));
        sweep("community list",page("",false));
        sweep("more",[&]{s.page.clear();ui::go_area(ui::kMore);});
        for(const auto& e:mods::manager::entries())sweep(std::string("page ")+e.id,page(e.id,true));
        sweep("page "+id,page(id,false));
        sweep("page available-fixture",page("available-fixture",false));
        sweep("review switch on",[&]{page(id,false)();ui::open_review_for_installed(view(id));});
        sweep("review update",[&]{page(id,false)();auto snap=s.loader.snapshot();auto v=view(id);
            ui::open_review_for_entry(*ui::catalogue_entry(snap,id),snap.catalogue.fixture_root,true,&v);});
        sweep("review install",[&]{page("available-fixture",false)();auto snap=s.loader.snapshot();
            ui::open_review_for_entry(*ui::catalogue_entry(snap,"available-fixture"),snap.catalogue.fixture_root,false);});
        // the progress view's states: working, failed (with its details open), done with a restart
        auto progress=[&](ui::Phase phase){return [&,phase]{page(id,false)();ui::Plan plan;plan.download=true;plan.prepare={"Prepare"};plan.build=true;plan.restart=true;
            s.job.flow=ui::Flow::make(id,"Sea minimap",plan);s.job.flow.accept(0);
            if(phase==ui::Phase::Failed)s.job.flow.fail("Preparing the mod's files failed.","synthetic details");
            if(phase==ui::Phase::Done)s.job.flow.finish(true,1);};};
        sweep("progress working",progress(ui::Phase::Working));
        sweep("progress failed",progress(ui::Phase::Failed));
        sweep("progress done",progress(ui::Phase::Done));
        s.job.flow={};
        // the tabs a port adds (overlay/extensions.h), each sub-tab of theirs
        int added=0;
        for(const auto& t:overlay::ext::tabs()) {
            if(t.builtin>=0)continue;
            ++added;
            for(int area=0;area<(t.shoulder?2:1);area++)sweep("tab "+t.id+" area "+std::to_string(area),[&,id=t.id,area]{shown_tab=id;platform_area=area;});
        }
        assert(added==2);
        shown_tab="mods";
        ImGui_ImplSDLRenderer3_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);
        fprintf(stderr,"[ids] %d conflicts in all views\n",conflicts);
        return conflicts?1:0;
    }
    ui::go_area(ui::kCommunity);  // packages and catalogue mods are listed under Community
    Clicker clicker;
    const std::string switch_label="##on_"+id;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(25);
    for(int frame=0;frame<6000&&std::chrono::steady_clock::now()<deadline;++frame) {
        assert(fs::space(root).available>10ull*1024*1024*1024);
        ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();setup_run_tick();ui::tick(host,true);
        // the settings window as the overlay sizes it
        ImGui::SetNextWindowPos(ImVec2(width*0.5f,height*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
        ImGui::SetNextWindowSize(window_size());
        ImGui::Begin("Settings",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);
        {auto& rects=ui::state().rects;blue_seen=rects.count("choice_colour_blue");if(blue_seen)blue_last=rects["choice_colour_blue"];rects.erase("choice_colour_blue");}
        bool had_review=review_open();
        host.back=false;
        draw_tab(host);
        bool has_review=review_open();
        ImGui::End();
        if(has_review&&!had_review){++confirmations;review_since=frame;}
        auto& s=ui::state();auto& f=s.job.flow;
        // the player's actions, one at a time
        if(!clicker.busy()&&frame>3) {
            if(!switched) {
                // the list shows the mod (catalogue mode: once the list is loaded); "checkbox" uses the page's switch
                if(update) {  // the mod's page offers Update once the catalogue is loaded
                    if(s.page.empty())clicker.click("open_"+id);else if(s.rects.count("Update##page")){clicker.click("Update##page");switched=true;}
                } else if(mode=="checkbox") {
                    if(s.page.empty())clicker.click("open_"+id);else if(s.rects.count("##on_page")){clicker.click("##on_page");switched=true;}
                } else if(s.rects.count(switch_label)){clicker.click(switch_label);switched=true;}
            } else if(has_review&&!accepted&&frame>8&&frame-review_since>=2) {
                // (a player taps once the review is in place: on its first frame it is not placed yet, and a tap
                // there would land outside it and close it, popup.h)
                auto& r=s.review;
                bool need_source=!r.source_game.empty()&&!ui::source_ready(r);
                // missing: first the controller's A on the review's default button, which is Cancel
                if(missing&&!pad_pressed){if(frame-review_since>=5){
                    // a controller player: the cursor is visible (any D-pad press shows it), A presses the focused button
                    io.AddKeyEvent(ImGuiKey_GamepadFaceDown,true);pad_pressed=frame;}
                    else if(frame-review_since>=2){io.AddMousePosEvent(-100,-100);io.ConfigNavCursorVisibleAlways=true;}}
                else if(missing&&confirmations<2){}
                else if(need_source&&picks==0)clicker.click("Choose disc image…##source");
                else if(mode=="choice"&&r.chosen["colour"]!="blue") {
                    // open the list, then pick blue in it
                    // the list is open when its item was drawn this frame at the same place as the last one
                    bool list_open=s.rects.count("choice_colour_blue")&&blue_seen&&blue_last.Min.x==s.rects["choice_colour_blue"].Min.x&&
                                   blue_last.Min.y==s.rects["choice_colour_blue"].Min.y;
                    if(list_open)clicker.click("choice_colour_blue");
                    else if(!s.rects.count("choice_colour_blue"))clicker.click("##choice_colour");
                } else {
                    std::string go=r.mode==ui::Review::SwitchOn?"Switch on##review":r.mode==ui::Review::Update?"Update##review":"Install##review";
                    // missing: the button is unavailable (the reason is next to it); the player cancels
                    clicker.click(missing&&frame>60?"Cancel##review":go);
                }
            } else if(f.phase==ui::Phase::Failed&&broken&&!corrected) {
                if(++failure_frames==4){fs::remove(root/"fail-tool");corrected=true;clicker.click("Try again##flow");}
            } else if(cancel_mode&&f.phase==ui::Phase::Working&&!canceled) {
                size_t c=f.current();
                bool at=c<f.steps.size()&&(mode=="cancel-rebuild"?setup_run.rebuilding:f.steps[c].kind==ui::StepKind::Build);
                if(at)clicker.click("Cancel##flow");
            }
        }
        if(pad_pressed&&frame==pad_pressed+1)io.AddKeyEvent(ImGuiKey_GamepadFaceDown,false);
        // ... it closed: switch the mod on again for the second review
        if(missing&&pad_pressed&&frame==pad_pressed+6){assert(!has_review&&confirmations==1);switched=false;io.ConfigNavCursorVisibleAlways=false;}
        if(clicker.busy()) {
            bool cancel_click=clicker.target=="Cancel##flow";
            if(clicker.step(io)&&cancel_click)canceled=true;
        }
        if(f.phase==ui::Phase::Working&&!accepted){accepted=true;assert(confirmations==1);std::ofstream(root/"consent-count")<<confirmations;}
        if(f.phase==ui::Phase::Working)saw_progress=true;
        if(f.phase==ui::Phase::Failed&&backup_fails) {
            // the reason (no "confirm the warning first"): the review was the confirmation
            assert(f.error.starts_with("Your saves couldn't be backed up")&&f.error.find("confirm")==std::string::npos);
            assert(!view(id).enabled&&pending_setup_runs().empty());
            saw_failure=true;break;
        }
        if(f.phase==ui::Phase::Failed) {
            fprintf(stderr,"flow failed: %s | %s\n",f.error.c_str(),f.details.c_str());
            saw_failure=true;assert(broken);assert(!view(id).enabled&&pending_setup_runs().empty());
            assert(f.details.find("synthetic deliberate failure")!=std::string::npos);
            assert(f.error=="Preparing the mod's files failed.");  // one plain sentence; the output is behind Show details
        }
        if(saw_failure&&corrected&&f.phase==ui::Phase::Working)retried=true;
        if(f.phase==ui::Phase::Done)saw_done=true;
        ImGui::Render();SDL_SetRenderDrawColor(renderer,20,23,29,255);SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);SDL_RenderPresent(renderer);
        if(frame==10&&!resumed&&!catalogue)capture("list");
        if(frame==30&&catalogue)capture("list");
        if(has_review&&frame>12)capture(mode=="choice"?"review-choice":"review");
        if(f.phase==ui::Phase::Working&&f.current()<f.steps.size()&&f.steps[f.current()].kind!=ui::StepKind::Download&&f.overall()>0.05)
            capture(resumed?"resume-progress":"progress");
        if(f.phase==ui::Phase::Failed&&failure_frames==2)capture("error");
        if(f.phase==ui::Phase::Done)capture("done");
        if(missing&&frame>80) {assert(f.phase==ui::Phase::Idle&&setup_run.id.empty()&&!has_review);break;}
        if(view(id).enabled&&setup_run.id.empty()&&saw_done&&captured.count("done"))break;
        if(update_off&&saw_done&&captured.count("done"))break;
        if(canceled&&f.phase==ui::Phase::Idle){cancel_released=true;auto& worker=setup_work();std::lock_guard guard(worker.mutex);if(!worker.running)break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if(experimental)assert(ui::state().job.accepted.experimental);  // the review showed the warning
    if(missing)assert(!view(id).enabled&&!accepted&&pending_setup_runs().empty()&&picks==1&&confirmations==2);
    else if(backup_fails)assert(saw_failure&&!view(id).enabled&&confirmations==1);
    else if(update) {
        // an update ends in the state before it: off stays off (prepared), on stays on
        assert(view(id).version=="1.1.0"&&view(id).enabled==update_on&&saw_done&&confirmations==1);
        assert(pending_setup_runs().empty()&&setup_run.id.empty());
        if(update_off) {
            assert(ui::state().job.flow.keep_off&&builds==1&&!ui::state().job.flow.restart_needed);
            for(const auto& step:setup_steps(id))assert(step.satisfied);
        } else assert(builds==0&&!ui::state().job.flow.keep_off);
    }
    else if(cancel_mode) {
        assert(canceled&&!view(id).enabled&&pending_setup_runs().empty());
        assert(setup_steps(id)[1].satisfied);assert(confirmations==1);  // the preparation stays done
        if(mode=="cancel-rebuild")assert(!mods::code::status().requested&&builds==0);
    }
    else {
        assert(view(id).enabled&&view(id).pending_restart&&setup_run.id.empty());assert(builds==1&&saw_progress&&saw_done);
        assert(confirmations==(resumed?0:1));assert(pending_setup_runs().empty());
        assert(ui::state().job.flow.restart_needed);  // "Restart now / Later" at the end
        if(broken)assert(saw_failure&&retried);
        if(restart)assert(resumed);
    }
    // the screens after the end, for the report (only with WWHD_TEST_CAPTURE_DIR)
    if(getenv("WWHD_TEST_CAPTURE_DIR")&&(mode=="normal"||mode=="catalogue")) {
        auto draw_frames=[&](int count,const std::string& target) {
            if(!target.empty())clicker.click(target);
            for(int i=0;i<count;i++) {
                ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();setup_run_tick();ui::tick(host,true);
                ImGui::SetNextWindowPos(ImVec2(width*0.5f,height*0.5f),ImGuiCond_Always,ImVec2(0.5f,0.5f));
                ImGui::SetNextWindowSize(window_size());
                ImGui::Begin("Settings",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);
                draw_tab(host);ImGui::End();
                if(clicker.busy())clicker.step(io);
                ImGui::Render();SDL_SetRenderDrawColor(renderer,20,23,29,255);SDL_RenderClear(renderer);
                ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);SDL_RenderPresent(renderer);
            }
        };
        draw_frames(6,"Later##flow");capture("page-on");
        draw_frames(6,ui::state().rects.count("< Community##back")?"< Community##back":"\xE2\x80\xB9 Community##back");capture("list-on");
        ui::shoulder(+1);draw_frames(4,"");capture("more");
    }
    std::cout<<"FLOW "<<id<<" "<<mode<<" enabled="<<view(id).enabled<<" confirmations="<<confirmations<<" resumed="<<resumed<<" failed="<<saw_failure<<"\n";
    ImGui_ImplSDLRenderer3_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);
    mods::json::Value result;result["enabled"]=view(id).enabled;result["resumed"]=resumed;result["confirmations"]=confirmations;
    std::ofstream(root/"flow-result.json.tmp")<<mods::json::dump(result);
    fs::rename(root/"flow-result.json.tmp",root/"flow-result.json");
}
