// Narrow game-free UI regression using production picker/build/install/review.
// Included by the existing software-rendered ImGui harness, never the runtime.
static int core_import_ui_flow(const fs::path& root,const std::string& mode) {
    namespace ui=overlay::modsui;using namespace mods::packages;
    const std::string id="twwhd-randomizer-core";
    set_code_mod_support(true);set_save_profile_support(true);initialize();
    {
        std::lock_guard g(mods::saves::mutex());
        mods::saves::store().log=[](const std::string&){};
        mods::saves::store().configure(root/"save",root/"backups");
    }
    set_guest_builder([](const GuestPackage&){return uint32_t(65536);},[&](const GuestPackage&,uint32_t){
        const auto file=root/"synthetic-native-cache";std::ofstream(file)<<"authored fixture";
        return GuestBuilt{file.string(),65536};
    });
    SDL_Surface* surface=SDL_CreateSurface(1000,2400,SDL_PIXELFORMAT_RGBA32);assert(surface);
    SDL_Renderer* renderer=SDL_CreateSoftwareRenderer(surface);assert(renderer);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1000,2400};io.DeltaTime=1.f/60;
    assert(ImGui_ImplSDLRenderer3_Init(renderer));
    ui::Host host;host.port_version=mods::catalogue::Version::parse("0.2.10");host.expand_all=true;host.mod_downloads=false;
    host.can_restart=[]{return true;};int restarts=0;host.restart=[&](bool){++restarts;};
    unsigned picks=0;
    host.choose_randomizer_source=[&](const char* title,bool folder,std::function<void(std::string)> chosen){
        std::string text=title;
        if(text.find("unmodified")!=std::string::npos){assert(folder&&picks==0);chosen((root/"original").string());}
        else if(text.find("randomized")!=std::string::npos){assert(folder&&picks==1);chosen((root/"seed").string());}
        else {assert(!folder&&picks==2);chosen(getenv("WWHD_UI_COMPILER"));}
        ++picks;
    };
    ui::go_area(ui::kMore);auto& s=ui::state();s.catalogue_initialized=true;
    Clicker click;bool started=false,review_checked=false,accepted=false,changed=false,cancelled=false,done=false;
    int review_at=-1;unsigned builds_seen=0;
    auto until=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    for(unsigned frame=0;frame<4000&&std::chrono::steady_clock::now()<until;frame++) {
        ImGui_ImplSDLRenderer3_NewFrame();ImGui::NewFrame();overlay::setupflow::setup_run_tick();ui::tick(host,true);
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1000,2400});
        ImGui::Begin("Settings",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);
        ui::draw(host,"");ImGui::End();
        const bool review=review_open();if(review&&review_at<0)review_at=int(frame);
        if(s.core_import.busy())++builds_seen;
        if(!click.busy()&&frame>3) {
            if(picks<3)click.click(picks==0?"Original USA game…##randomizer":picks==1?"Randomized game…##randomizer":"Choose PowerPC clang…##randomizer");
            else if(!started){click.click("Build and import##randomizer");started=true;}
            else if(s.core_import.busy()&&fs::exists(root/"worker-ready")) {
                if(mode=="core-cancel"&&!cancelled){click.click("Cancel after current step##randomizer");cancelled=true;}
                if(mode=="core-profile"&&!changed){
                    std::string error;assert(create_profile("Other profile",error)&&select_profile("Other profile",error));
                    std::ofstream(root/"profile-changed")<<"changed";changed=true;
                }
            }else if(review&&!accepted&&int(frame)>review_at+3){
                auto found=list();auto entry=std::find_if(found.begin(),found.end(),[&](const auto& v){return v.id==id;});
                assert(entry!=found.end()&&!entry->enabled&&!entry->active);
                assert(!unconfirmed_native(id).empty()&&!unacknowledged_experimental(id).empty());
                assert(s.review.plan.backup&&s.review.plan.restart&&!s.review.code_names.empty());
                review_checked=true;click.click("Switch on##review");accepted=true;
            }else if(s.job.flow.phase==ui::Phase::Done&&accepted&&!restarts) {
                assert(s.job.flow.restart_needed&&s.job.accepted.experimental);
                click.click("Restart now##flow");
            }
        }
        if(click.busy())click.step(io);
        ImGui::Render();SDL_SetRenderDrawColor(renderer,20,23,29,255);SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);SDL_RenderPresent(renderer);
        if(mode=="core-import"&&restarts){done=true;break;}
        if(mode!="core-import"&&started&&fs::exists(root/"worker-finished")&&!s.core_import.busy()) {
            assert(!review&&!accepted&&list().empty());
            if(mode=="core-cancel")assert(cancelled&&s.message.find("cancelled")!=std::string::npos);
            else assert(changed&&s.error.find("profile changed")!=std::string::npos);
            done=true;break;
        }
        if(!s.error.empty()&&mode=="core-import"){fprintf(stderr,"core UI failed: %s\n",s.error.c_str());assert(false);}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(done&&picks==3&&started&&builds_seen>0);
    if(mode=="core-import") {
        auto entries=list();auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto& v){return v.id==id;});
        assert(review_checked&&accepted&&restarts==1&&entry!=entries.end()&&entry->enabled&&!entry->active&&entry->pending_restart);
        unsigned backups=0;for(const auto& item:fs::directory_iterator(root/"backups"))if(item.is_directory())++backups;
        assert(backups==1);
    }else assert(restarts==0&&!review_checked);
    const auto cache=root/"manager/Cache/RandomizerImport";
    if(fs::exists(cache))assert(fs::is_empty(cache));
    fprintf(stderr,"[core-import-ui] %s: picker clicks, build handoff and approval/cancel gate passed\n",mode.c_str());
    ImGui_ImplSDLRenderer3_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);
    return 0;
}
