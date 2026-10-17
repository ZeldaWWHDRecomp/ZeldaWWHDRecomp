// Exercise the actual hook runner with synthetic game bodies and a port wrapper.
#include "mods/guest_mods.cpp"
#include <cassert>
#include <vector>
#ifndef _WIN32
#include <sys/mman.h>
#endif

static std::vector<int> calls;
extern "C" void guest_hooks_body(Cpu* c);
extern "C" void guest_hooks_record_body() { calls.push_back(3); }
static void port(Cpu* c) {
    calls.push_back(1);
    c->r[3] *= 2; // stand-in for true-60 scaling before game code / replacements
    guest_hooks_body(c);
    calls.push_back(6);
}
static void entry(Cpu* c) { calls.push_back(2); assert(c->r[3] == 10); c->r[3] = 999; }
static void replacement(Cpu* c) {
    calls.push_back(7); assert(c->r[3] == 10);
    guestmods::call_original(c, 0x02000000);
    assert(c->mod_skip == 0);
}
static void ret_a(Cpu* c) { calls.push_back(4); assert(c->r[3] == 10); c->r[3] = 888; }
static void ret_b(Cpu* c) { calls.push_back(5); assert(c->r[3] == 10); c->r[3] = 777; }
extern "C" {
extern const char g_guest_build_name[] = "USA";
extern const RecompEntry g_recomp_funcs[] = {{0x02000000, port}};
extern const unsigned g_recomp_func_count = 1;
static const PpcFunc test_mod_bodies[] = {guest_hooks_body};
static uint8_t test_mod_flags[1]{};
volatile int g_core_preempt[3]{};
int g_ppc_trace = 0;
void ppc_trace_enter(uint32_t) {}
void ppc_dispatch(Cpu*) { std::abort(); }
void ppc_unimplemented(Cpu*, uint32_t, uint32_t) { std::abort(); }
void ppc_trap(Cpu*, uint32_t) { std::abort(); }
uint64_t ppc_timebase() { return 0; }
double ppc_fres(double x) { return x; }
double ppc_frsqrte(double x) { return x; }
void ppc_preempt(Cpu*) {}
}
namespace guestmods::settings {
std::optional<Value> read(const std::string& key) {
    if(key=="input.face_layout")return Value{std::string("labels"),7};
    if(key=="render.true60")return Value{true,8};
    if(key=="display.aspect")return Value{21.0/9,9};
    return {};
}
}
namespace input {PadState read() {return {};}}
namespace true60 {float dt() {return 0.5f;}}
namespace mem { std::string read_cstr(uint32_t) { return "option"; } uint32_t host_alloc(uint32_t,uint32_t) { return 0; } uint32_t runtime_alloc(uint32_t,uint32_t) {return 0;} }
namespace dispatch { void set(uint32_t, PpcFunc) {} }
namespace mods::packages {
std::vector<View> list(){return {};}
static bool in_startup_callback=false, inspected=false;
std::string directory() { assert(!in_startup_callback); return {}; }
void set_guest_builder(GuestInspect, GuestBuild, GuestCacheCheck) {}
void start_guests(const GuestInspect& inspect, const GuestLoad&) {
    // Real start_guests holds the manager mutex while invoking callbacks. Its
    // directory accessor cannot be called recursively from the build bridge.
    in_startup_callback=true;
    try { inspect(GuestPackage{}); assert(false); }
    catch(const std::runtime_error& e) {
        assert(std::string(e.what()).find("build tools are unavailable")!=std::string::npos);
        inspected=true;
    }
    in_startup_callback=false;
}
}
// the letter service has its own test (story_mail_test)
namespace story_mail {
int32_t send(const std::string&,const std::string&,const std::string&,const std::string&) {return kResultInvalid;}
bool imported() {return false;}
void set_text_buffers(uint32_t,uint32_t) {}
void set_notebook_model(const std::string&) {}
int32_t state(const std::string&,const std::string&) {return kResultNone;}
void note_import() {}
void set_game_calls(void (*)(Cpu*,uint32_t),uint32_t (*)(Cpu*,uint32_t,std::initializer_list<uint32_t>)) {}
void install(const std::function<std::string(uint32_t)>&,const std::function<void(uint32_t,void (*)(Cpu*))>&) {}
void state_loaded() {}
}
// and so has the chart service (story_chart_test)
namespace story_chart {
int32_t give(const std::string&,const std::string&) {return kResultInvalid;}
int32_t finish(const std::string&,const std::string&) {return kResultInvalid;}
int32_t state(const std::string&,const std::string&) {return kResultNone;}
int32_t mark(const std::string&,const std::string&,const std::string&,float,float,uint32_t,const std::string&) {return kResultInvalid;}
int32_t unmark(const std::string&,const std::string&,const std::string&) {return kResultInvalid;}
void note_import() {}
bool imported() {return false;}
void set_game_calls(void (*)(Cpu*,uint32_t),uint32_t (*)(Cpu*,uint32_t,std::initializer_list<uint32_t>)) {}
void install(const std::function<std::string(uint32_t)>&,const std::function<void(uint32_t,void (*)(Cpu*))>&) {}
void state_loaded() {}
size_t declared() {return 0;}
}
// and so have the mod treasure charts (their tests)
namespace story_treasure {
std::vector<Hook> chest_hooks() {return {};}
void set_game_calls(void (*)(Cpu*,uint32_t),uint32_t (*)(Cpu*,uint32_t,std::initializer_list<uint32_t>)) {}
void set_scratch_memory(uint32_t) {}
void install(const std::function<std::string(uint32_t)>&,const std::function<void(uint32_t,void (*)(Cpu*))>&) {}
}
namespace story_treasure_chest {
void state_loaded() {}
void set_item_get_next(void (*)(Cpu*),void (*)(Cpu*,uint32_t)) {}
namespace hooks { void exec_item_get(Cpu*) {} }
}
// World runtime has its own actor/scene tests; loader tests only exercise registration.
namespace actor_model_cache {
mod_actor::ModelRig rig_for(const std::string&){return {};}
std::vector<std::string> clips_for(const std::string&){return {};}
bool bounds_for(const std::string&,story_notebook::WorldPoint&,story_notebook::WorldPoint&){return false;}
std::string prepare(const std::string&,const story_notebook::ManifestModel&,const std::string&,const std::string&,const std::vector<std::string>&,const std::string&,const std::string&){return {}; }
}
namespace story_actors {
bool register_model(const std::string&,const std::string&,const std::string&,int,std::string&,story_notebook::WorldPoint,story_notebook::WorldPoint,bool,std::vector<std::string>,mod_actor::ModelRig){return true;}
bool register_flags(const std::string&,const std::vector<std::string>&,std::string&){return true;}
void service(Cpu*){}
bool load(const std::string&,const std::vector<story_notebook::ManifestActor>&,std::string&){return true;}
bool load(const std::string&,const std::vector<story_notebook::ManifestActor>&,std::string&,const std::vector<std::string>&){return true;}
bool declared(){return false;}
void set_game_calls(void (*)(Cpu*,uint32_t),uint32_t (*)(Cpu*,uint32_t,std::initializer_list<uint32_t>)){}
void set_scratch_memory(uint32_t){}
void set_scene_start(std::function<bool(const std::string&,const std::string&)>){}
void install(const std::function<std::string(uint32_t)>&,const std::function<void(uint32_t,void(*)(Cpu*))>&){}
void state_loaded(){}
void restore_talk_text(){}
uint32_t actor_address(Cpu*,const std::string&,const std::string&){return 0;}
bool particle_position(Cpu*,const std::string&,const std::string&,unsigned,story_notebook::WorldPoint&){return false;}
bool actions(const std::string&,const std::vector<story_notebook::StoryAction>&){return true;}
}
namespace story_scenes {
static MusicHook music_hook;
size_t checkpoint(){return 0;}
bool rollback(size_t) noexcept{return true;}
bool register_music_hook(const std::string&,MusicHook hook){music_hook=std::move(hook);return true;}
void set_place_resolver(ResolvePlace){}
bool validate(const std::vector<story_notebook::ManifestScene>&,std::string&){return true;}
void set_game_calls(uint32_t (*)(Cpu*,uint32_t,std::initializer_list<uint32_t>)){}
void set_scratch_memory(uint32_t){}
void set_callbacks(Resolve,Actions){}
void set_particle_resolver(ResolveParticle){}
bool declare(const std::string&,const std::vector<story_notebook::ManifestScene>&,std::string&){return true;}
bool request(const std::string&,const std::string&){return false;}
void tick(Cpu*,bool,unsigned){}
void install(void (*)(Cpu*,uint32_t),const std::function<std::string(uint32_t)>&,const std::function<void(uint32_t,void(*)(Cpu*))>&){}
void state_loaded(){}
}
namespace story_actor_textures {
size_t checkpoint(){return 0;}
bool rollback(size_t) noexcept{return true;}
uint32_t declare_png(const std::string&,std::span<const uint8_t>,std::string&){return 0;}
bool allocate_session(){return true;}
bool declared(){return false;}
}
namespace story_particles {
bool configure(Invoke,uint32_t,uint32_t,uint32_t){return true;}
bool burst(Cpu*,const Burst&){return false;}
static unsigned calls=0;static bool accept=true;static std::string effect;static Placement placement;static Options options;
bool burst_named(Cpu*,std::string_view e,const Placement&p,Options o){++calls;effect=e;placement=p;options=o;return accept;}
}
// Synthetic actor bridge: identities belong to one declared mod actor, and
// process changes invalidate previously captured identities.
namespace mod_actor {
static BehaviorSink sink;
static Identity actor{"first","sprite",11,101},other{"first","other",12,102};
static State current{};
static uint32_t handle=0,other_handle=0;
static unsigned paths_started=0;
static bool combat_available=true;static unsigned attacks_begun=0,attacks_cancelled=0,combat_quarantines=0;static bool attack_live=false,other_attack_live=false;static uint32_t quarantined_token=0;
static bool particle_identity=false;
static constexpr uint32_t address=0x7F030000,userdata=0x7F020000;
void set_behavior_sink(BehaviorSink value){sink=std::move(value);}
uint32_t declaration_token(const std::string&m,const std::string&id){return m==actor.mod&&id==actor.id?actor.token:m==other.mod&&id==other.id?other.token:0;}
bool identify(Cpu*,uint32_t a,Identity&out){if(a==address){out=actor;return true;}if(a==address+0x1000){out=other;return true;}return false;}
bool live(const Identity&i){return (i.mod==actor.mod&&i.id==actor.id&&i.token==actor.token&&i.process_id==actor.process_id)||(i.mod==other.mod&&i.id==other.id&&i.token==other.token&&i.process_id==other.process_id);}
uint32_t resolve(Cpu*,const Identity&i){return live(i)?(particle_identity?0x10008000:address)+(i.token==other.token?0x1000:0):0;}
bool snapshot(Cpu*,const Identity&i,State&out){if(!live(i))return false;out=current;return true;}
bool combat_snapshot(Cpu*,const Identity&i,CombatState&out){if(!live(i)||!combat_available)return false;out={};out.health=i.token==actor.token?7:3;out.max_health=10;out.phase=1;out.flags=1;out.last_damage_sequence=19;out.reward_status=0;out.native_drop_status=2;out.invulnerability_frames=9;out.reaction_frames=8;out.attack_active_frames=0;out.attack_cooldown_frames=21;return true;}
bool attack_begin(Cpu*,const Identity&i){if(!live(i)||!combat_available||current.scene_owned)return false;++attacks_begun;(i.token==other.token?other_attack_live:attack_live)=true;return true;}
bool attack_cancel(Cpu*,const Identity&i){if(!live(i)||!combat_available)return false;++attacks_cancelled;(i.token==other.token?other_attack_live:attack_live)=false;return true;}
void quarantine_combat(Cpu*,const Identity&i){assert(live(i));++combat_quarantines;quarantined_token=i.token;(i.token==other.token?other_attack_live:attack_live)=false;}
bool set_position(Cpu*,const Identity&i,story_notebook::WorldPoint p){if(!live(i)||current.scene_owned)return false;current.position=p;return true;}
bool set_rotation(Cpu*,const Identity&i,story_notebook::WorldPoint p){if(!live(i)||current.scene_owned)return false;current.rotation=p;return true;}
bool set_collision(Cpu*,const Identity&i,float r,float h,bool e,bool p){if(!live(i)||current.scene_owned)return false;current.collision_radius=r;current.collision_height=h;current.collision_enabled=e;current.pushable=p;return true;}
bool has_animation(Cpu*,const Identity&i,const std::string&clip){return live(i)&&clip=="wave";}
bool set_animation(Cpu*c,const Identity&i,const std::string&clip,float,bool){if(!has_animation(c,i,clip))return false;current.animation=clip;return true;}
uint32_t hit_event_address(Cpu*,const Identity&i){return live(i)?userdata+256:0;}
uint32_t userdata_address(Cpu*,const Identity&i){return live(i)?userdata+(i.token==other.token?0x1000:0):0;}
uint32_t behavior_handle(Cpu*,const Identity&i){return live(i)?(i.token==other.token?other_handle:handle):0;}
bool set_behavior_handle(Cpu*,const Identity&i,uint32_t value){if(!live(i))return false;if(i.token==other.token)other_handle=value;else handle=value;return true;}
bool set_hit_notifications(const std::string&m,const std::string&id,bool){return declaration_token(m,id)!=0;}
bool start_path(Cpu*,const Identity&i,const std::vector<story_notebook::WorldPoint>&p,float speed,bool){if(!live(i)||current.scene_owned||p.size()<2||p.size()>16||!std::isfinite(speed)||speed<=0||speed>1500)return false;for(const auto&point:p)if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z))return false;++paths_started;return true;}
bool stop_path(Cpu*,const Identity&i){return live(i);}
}
// and so has the story notebook (story_notebook_test)
namespace config { std::string game_dir; }
namespace story_notebook {
bool place_position(const std::string&,const std::string&,float&x,float&z){x=20;z=30;return true;}
void logic_step(uint64_t) {}
void set_look(const std::string&,const std::string&) {}
bool load_local_npc_catalogue(const std::string&) {return false;}
std::vector<std::pair<std::string,std::string>> npc_recipes() {return {};}
int32_t define(const std::string&,const std::string&,const std::string&,const std::string&,const std::string&,const std::string&) {return kResultInvalid;}
int32_t step(const std::string&,const std::string&,uint32_t,const std::string&,const std::string&) {return kResultInvalid;}
int32_t place(const std::string&,const std::string&,float,float) {return kResultInvalid;}
int32_t no_place(const std::string&,const std::string&) {return kResultInvalid;}
int32_t known(const std::string&,const std::string&,const std::string&) {return kResultInvalid;}
int32_t portrait(const std::string&,const std::string&,const std::string&,const std::string&) {return kResultInvalid;}
int32_t done(const std::string&,const std::string&) {return kResultInvalid;}
int32_t state(const std::string&,const std::string&) {return kResultInvalid;}
void note_import() {}
void set_game_calls(void (*)(Cpu*,uint32_t),uint32_t (*)(Cpu*,uint32_t,std::initializer_list<uint32_t>)) {}
void install(const std::function<std::string(uint32_t)>&,const std::function<void(uint32_t,void (*)(Cpu*))>&) {}
void install_sail(const std::function<std::string(uint32_t)>&,const std::function<void(uint32_t,void (*)(Cpu*))>&) {}
void state_loaded() {}
int32_t carry(const std::string&,const std::string&,bool) {return kResultInvalid;}
bool declare(const std::string&,const std::string&,const mods::json::Value&) {return true;}
int32_t talk(const std::string&,const std::string&,uint32_t,const std::string&,const std::string&,uint32_t) {return kResultInvalid;}
int32_t set_flag(const std::string&,const std::string&,uint32_t,bool) {return kResultInvalid;}
int32_t flag(const std::string&,const std::string&,uint32_t) {return kResultInvalid;}
bool imported() {return false;}
void set_game_ui(const GameUi*) {}
const GameUi& game_ui() {static GameUi none;return none;}
}
// and so have the mod items (story_items_test) and the item icon archive (item_icons_test)
namespace story_items {
void set_model_presenter(std::function<void(uint16_t)>) {}
bool define(const std::string&,const std::string&,const story_notebook::ManifestItem&,std::string&) {return false;}
std::vector<Item> installed() {return {};}
void set_frame_source(int (*)(uint16_t)) {}
int32_t give(const std::string&,const std::string&,uint32_t,const char*,bool) {return kResultInvalid;}
int32_t take(const std::string&,const std::string&,uint32_t) {return kResultInvalid;}
int32_t count(const std::string&,const std::string&) {return kResultInvalid;}
int32_t used(const std::string&,const std::string&) {return kResultInvalid;}
void set_presenter(ShowInfo) {}
void set_use_calls(bool (*)(const char*),int32_t (*)(const std::string&,const std::string&,uint32_t)) {}
void note_import() {}
bool imported() {return false;}
void install(const std::function<std::string(uint32_t)>&,const std::function<void(uint32_t,void (*)(Cpu*))>&) {}
void set_game_calls(void (*)(Cpu*,uint32_t),uint32_t (*)(Cpu*,uint32_t,std::initializer_list<uint32_t>)) {}
namespace hooks { void exec_item_get(Cpu*) {} }
}
namespace item_icons {
void set_items(const std::vector<Item>&,const std::filesystem::path&) {}
int frame(uint16_t) {return -1;}
}
namespace story_mail { void set_item_get_next(void (*)(Cpu*)) {} }
namespace story_notebook {
int32_t reach(const std::string&,const std::string&,uint32_t) {return kResultInvalid;}
void set_icon_open(const std::string&) {}
void set_layout_description(const std::string&,const std::string&) {}
}
// and the story layouts, the notebook's page layout, the bag's extra pages and the item buttons (their tests)
namespace story_lyt {
void set_game_calls(void (*)(Cpu*,uint32_t),uint32_t (*)(Cpu*,uint32_t,std::initializer_list<uint32_t>)) {}
void install(const std::function<void(uint32_t,void (*)(Cpu*))>&) {}
}
namespace story_lyt_screen {
void set_game_calls(void (*)(Cpu*,uint32_t),uint32_t (*)(Cpu*,uint32_t,std::initializer_list<uint32_t>)) {}
void install(const std::function<std::string(uint32_t)>&,const std::function<void(uint32_t,void (*)(Cpu*))>&) {}
void state_loaded() {}
void service(Cpu*) {}
}
namespace story_notebook_layout { bool configure() {return false;} }
namespace story_bag {
bool configure(const std::filesystem::path&) {return false;}
void set_after_assign(void (*)(Cpu*)) {}
void set_game_calls(void (*)(Cpu*,uint32_t),uint32_t (*)(Cpu*,uint32_t,std::initializer_list<uint32_t>)) {}
void state_loaded() {}
}
namespace story_item_buttons {
void set_game_calls(void (*)(Cpu*,uint32_t),uint32_t (*)(Cpu*,uint32_t,std::initializer_list<uint32_t>)) {}
void refresh_strip(Cpu*) {}
void install(const std::function<std::string(uint32_t)>&,const std::function<void(uint32_t,void (*)(Cpu*))>&) {}
}
// Item presentation has its own native-routing/lifetime tests; this fixture isolates guest hooks.
namespace story_item_models {
bool declare(uint16_t,const std::string&,unsigned,std::string&,const std::string&) {return true;}
size_t checkpoint() {return 0;}
void rollback(size_t) noexcept {}
void configure(uint32_t,void(*)(Cpu*,uint32_t),Invoke) {}
bool install(const std::function<std::string(uint32_t)>&,const std::function<void(uint32_t,void(*)(Cpu*))>&) {return true;}
void request(uint16_t) {}
void tick(Cpu*) {}
void state_loaded() {}
void progress_loaded() {}
}
// and so has the story progress store (story_notebook_test)
namespace story_store {
void set_progress_loaded(void(*)()) {}
void set_memory(uint32_t) {}
void set_game_calls(void (*)(Cpu*,uint32_t)) {}
void install(const std::function<std::string(uint32_t)>&,const std::function<void(uint32_t,void (*)(Cpu*))>&) {}
}
static unsigned hud_calls=0;
struct ActorCall {uint32_t function=0;std::vector<uint32_t> args;double dt=0;};
static std::vector<ActorCall> actor_calls;
static bool actor_callback_throws=false;
static bool actor_draw_mutation=false;
static uint32_t actor_draw_result=99;
static uint32_t actor_callback_result=1;
static uint32_t actor_nested_handle=0,actor_nested_result=99;
static bool actor_nested_active=false;
static uint32_t actor_nested_attack_result=99,actor_draw_combat_query=99,actor_draw_attack_result=99;

uint32_t guest_call(Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args) {
    if(fn==GC(0x025200D4)){c->r[10]=0xDEAD;c->f[7].ps0=456;c->lr=0x123;return 0x10000000;}
    if(fn>=0x7F000090&&fn<=0x7F0000D0){
        actor_calls.push_back({fn,std::vector<uint32_t>(args),c->f[1].ps0});
        if(actor_draw_mutation&&fn==0x7F0000B0){c->pc=fn;c->r[3]=*args.begin();c->f[1].ps0=999;guestmods::svc_actor_set_position(c);actor_draw_result=c->r[3];c->r[3]=*args.begin();guestmods::svc_actor_attack_begin(c);actor_draw_attack_result=c->r[3];const uint32_t out=0x7F000880;st32(out,64);st32(out+4,1);c->r[3]=*args.begin();c->r[4]=out;guestmods::svc_actor_combat_get(c);actor_draw_combat_query=c->r[3];}
        if(actor_nested_handle&&actor_nested_active&&fn==0x7F0000A0){c->pc=fn;c->r[3]=actor_nested_handle;c->f[1].ps0=777;guestmods::svc_actor_set_position(c);actor_nested_result=c->r[3];c->r[3]=actor_nested_handle;guestmods::svc_actor_attack_begin(c);actor_nested_attack_result=c->r[3];}
        else if(actor_nested_handle&&!actor_nested_active&&(fn==0x7F0000B0||fn==0x7F0000A0)){
            actor_nested_active=true;assert(guestmods::actor_dispatch(c,actor_nested_handle,mod_actor::behavior::Event::Execute));actor_nested_active=false;
        }
        c->pc=fn;c->r[10]=0xBAD;c->f[7].ps0=1234;c->lr=0xABCD;
        if(actor_callback_throws)throw std::runtime_error("synthetic actor callback failure");
        return actor_callback_result;
    }
    assert(fn==0x7F000080&&args.size()==1);++hud_calls;
    auto list=*args.begin();c->pc=fn;c->r[3]=list;c->r[4]=0x7F000600;
    guestmods::svc_hud_emit(c);assert(c->r[3]==1);
    c->r[10]=0xBAD;return 0;
}
void log_msg(const char*, ...) {}
[[noreturn]] void fatal(const char*, ...) { std::abort(); }
int main() {
    assert(!guestmods::hooks_built());
    ppc_mod_register(1, test_mod_flags, test_mod_bodies);
    assert(guestmods::hooks_built());
    const char* missing="__wwhd_nonexistent_guest_build_config_for_test__.json";
    assert(!std::filesystem::exists(missing));
#ifdef _WIN32
    _putenv_s("WWHD_GUEST_BUILD_CONFIG",missing);
#else
    setenv("WWHD_GUEST_BUILD_CONFIG",missing,1);
#endif
    guestmods::init();
    assert(mods::packages::inspected);
    Cpu c{};
    c.r[3] = 5; port(&c);
    assert((calls == std::vector<int>{1, 3, 6})); assert(c.r[3] == 11);
    auto& chain = guestmods::g_chains[0x02000000];
    chain.addr = 0x02000000; chain.ordinal = 0;
    chain.entry = {entry}; chain.ret = {ret_b, ret_a};
    g_mod_hook_flags[0] = 1;
    calls.clear(); c.r[3] = 5; port(&c);
    assert((calls == std::vector<int>{1, 2, 3, 5, 4, 6})); assert(c.r[3] == 11);
    chain.replace = replacement;
    calls.clear(); c.r[3] = 5; port(&c);
    assert((calls == std::vector<int>{1, 2, 7, 3, 5, 4, 6})); assert(c.r[3] == 11);
    // ORIGINAL bypasses both mods and the port wrapper, and consumes the skip token.
    calls.clear(); c.r[3] = 20; guestmods::call_original(&c, 0x02000000);
    assert((calls == std::vector<int>{3})); assert(c.r[3] == 21); assert(c.mod_skip == 0);
    // Map only synthetic mod data for string/input/heap service checks.
    constexpr uint32_t data_base=0x7F000000;
#ifdef _WIN32
    void* data=VirtualAlloc(mem::ptr(data_base),0x30000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    void* data=mmap(mem::ptr(data_base),0x30000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
#endif
    assert(data==mem::ptr(data_base));
    // Typed services select the owning mod by its translated import callsite.
    WWHDGuestModuleV1 module{};module.mem_base=0x7F000000;module.mem_size=4096;
    guestmods::Loaded mod;mod.id="first";mod.m=&module;mod.options["option"]=42;
    guestmods::g_loaded.push_back(std::move(mod));c.pc=0x7F000100;c.r[3]=1;c.r[4]=9;
    guestmods::svc_config_int(&c);assert(c.r[3]==42);
    guestmods::g_loaded[0].options["option"]=true;c.r[3]=1;
    guestmods::svc_config_bool(&c);assert(c.r[3]==1);
    c.r[3]=1;c.r[4]=9;guestmods::svc_config_int(&c);assert(c.r[3]==9); // wrong type uses fallback
    guestmods::g_loaded[0].options["option"]=2.5;c.r[3]=1;c.f[1].ps0=8;
    guestmods::svc_config_float(&c);assert(c.f[1].ps0==2.5);
    c.r[3]=1;c.r[4]=9;guestmods::svc_config_int(&c);assert(c.r[3]==9); // non-integral numeric option
    guestmods::svc_logic_dt(&c);assert(c.f[1].ps0==1.0/60.0);
    guestmods::frame(0x100000002ull);guestmods::svc_logic_step(&c);assert(c.r[3]==1&&c.r[4]==2);
    guestmods::g_loaded[0].region_size=0x10000;
    auto read_setting=[&](const char* key,uint32_t type,uint32_t destination,uint32_t capacity) {
        std::strcpy(reinterpret_cast<char*>(mem::ptr(data_base+0x400)),key);
        c.r[3]=data_base+0x400;c.r[4]=type;c.r[5]=destination;c.r[6]=capacity;
        guestmods::svc_setting_get(&c);return c.r[3];
    };
    auto target=data_base+0x500;
    assert(read_setting("input.face_layout",1,target,32)==7);
    assert(std::string(reinterpret_cast<char*>(mem::ptr(target)))=="labels");
    assert(read_setting("input.face_layout",1,0,0)==7);
    assert(read_setting("input.face_layout",2,target,32)==0);
    assert(read_setting("unknown",1,target,32)==0);
    assert(read_setting("input.face_layout",1,target,2)==0&&ld8(target)=='l');
    assert(read_setting("input.face_layout",1,data_base+0xFFFF,32)==0);
    assert(read_setting("render.true60",2,target,4)==4&&ld32(target)==1);
    assert(read_setting("display.aspect",4,target,8)==8&&ldf64(target)==21.0/9);
    std::strcpy(reinterpret_cast<char*>(mem::ptr(data_base+0x400)),"input.face_layout");
    c.r[3]=data_base+0x400;guestmods::svc_setting_changed(&c);assert(c.r[3]==0&&c.r[4]==7);
    WWHDGuestModuleV1 second{};second.mem_base=0x7F010000;second.mem_size=4096;
    guestmods::Loaded another;another.id="second";another.m=&second;another.options["option"]=84;
    guestmods::g_loaded.push_back(std::move(another));c.pc=0x7F010100;c.r[3]=1;
    guestmods::svc_config_int(&c);assert(c.r[3]==84);
    c.pc=0x7F000100;c.r[3]=1;c.f[1].ps0=9;
    guestmods::g_loaded[0].options["option"]="text";
    guestmods::svc_config_float(&c);assert(c.f[1].ps0==9);
    c.r[3]=1;c.r[4]=data_base+0x200;c.r[5]=3;
    guestmods::svc_config_string(&c);assert(c.r[3]==2);assert(std::string(reinterpret_cast<char*>(mem::ptr(data_base+0x200)))=="te");
    guestmods::g_loaded[0].heap=std::make_unique<guestmods::Heap>(mem::ptr(data_base+0x1000),data_base+0x1000,4096);
    guestmods::g_loaded[0].heap->initialize();c.r[3]=32;guestmods::svc_malloc(&c);
    assert(c.r[3]==data_base+0x1010);guestmods::svc_free(&c);
    c.r[3]=data_base+0x300;guestmods::svc_input(&c);assert(ld32(data_base+0x300)==0);
    // Packed guest element decoding and automatic per-step recording/publication.
    WWHDGuestFunc functions[]={{data_base+0x80,nullptr},{data_base+0x90,nullptr},{data_base+0xA0,nullptr},{data_base+0xB0,nullptr},{data_base+0xC0,nullptr},{data_base+0xD0,nullptr}};module.funcs=functions;module.func_count=6;
    uint32_t element=data_base+0x600;memset(mem::ptr(element),0,72);
    st32(element,0);stf32(element+20,20);stf32(element+24,30);stf32(element+32,1);
    stf32(element+48,1);stf32(element+52,1);st32(element+56,0x12345678);
    c.r[3]=data_base+0x84;c.r[4]=2;guestmods::svc_hud_register(&c);assert(c.r[3]==0);
    c.r[3]=data_base+0x80;c.r[4]=2;guestmods::svc_hud_register(&c);assert(c.r[3]==1);
    Cpu saved=c;guestmods::draw_frame(&c,4);assert(hud_calls==1&&memcmp(&saved,&c,sizeof c)==0);
    auto tv=guestmods::hud::store().snapshot(0),drc=guestmods::hud::store().snapshot(1);
    assert(tv.size()==1&&drc.size()==1&&tv[0]==drc[0]);
    assert(tv[0]->commands[0].rgba==0x12345678&&tv[0]->commands[0].w==20);
    guestmods::draw_frame(&c,4);assert(hud_calls==1);
    guestmods::draw_frame(&c,5);assert(hud_calls==2);
    auto epoch=guestmods::hud::store().state_generation();guestmods::state_loaded();
    assert(!guestmods::hud::store().active()&&guestmods::hud::store().state_generation()!=epoch);
    guestmods::draw_frame(&c,5);assert(hud_calls==3); // rebuilt even if restored clock repeats
    c.r[3]=0;c.r[4]=2;guestmods::svc_hud_register(&c);assert(c.r[3]==1);
    guestmods::draw_frame(&c,6);assert(hud_calls==3&&!guestmods::hud::store().active());
    // Public actor services use exact owner metadata, packed BE tables and live
    // process identities. Callback CPU mutations must never escape dispatch.
    const uint32_t actor_id=data_base+0x700,table=data_base+0x740,state=data_base+0x800;
    std::strcpy(reinterpret_cast<char*>(mem::ptr(actor_id)),"sprite");
    auto reg=[&](uint32_t pointer){c.pc=data_base+0x100;c.r[3]=actor_id;c.r[4]=pointer;guestmods::svc_actor_register(&c);return c.r[3];};
    st32(table,32);st32(table+4,1);st32(table+8,256);
    for(unsigned i=0;i<5;++i)st32(table+12+i*4,data_base+0x90+i*0x10);
    assert(reg(data_base+0x10000)==0); // foreign table
    assert(reg(table+1)==0); // alignment
    st32(table,31);assert(reg(table)==0);st32(table,32);
    st32(table+4,2);assert(reg(table)==0);st32(table+4,1);
    st32(table+8,257);assert(reg(table)==0);st32(table+8,256);
    st32(table+16,data_base+0x10090);assert(reg(table)==0); // foreign callback
    st32(table+16,data_base+0xA1);assert(reg(table)==0); // interior, not exact function
    st32(table+16,data_base+0xA0);assert(reg(table)==1);
    memset(mem::ptr(mod_actor::userdata),0xCC,256);
    Cpu actor_saved=c;guestmods::behavior_sink(&c,mod_actor::address,mod_actor::BehaviorEvent::CreateNew,nullptr);
    assert(memcmp(&actor_saved,&c,sizeof c)==0&&actor_calls.size()==1);
    const uint32_t first_handle=mod_actor::handle;
    assert(first_handle&&actor_calls.back().function==data_base+0x90&&actor_calls.back().args==std::vector<uint32_t>({first_handle,mod_actor::userdata,0}));
    assert(ld32(mod_actor::userdata)==0);

    // Exercise actual public particle service ABI against captured native boundary.
    {
#ifdef _WIN32
        void* particle_mem=VirtualAlloc(mem::ptr(0x10000000),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
        void* particle_mem=mmap(mem::ptr(0x10000000),0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
#endif
        assert(particle_mem==mem::ptr(0x10000000));
        st32(0x10005B34,0x10007000);st16(0x1000700E,0xA8);st32(0x10007004,55);st8(0x10007326,11);st8(0x10008326,11);
        mod_actor::particle_identity=true;mod_actor::current.position={12,34,56};
        const uint32_t name=data_base+0x900;std::strcpy(reinterpret_cast<char*>(mem::ptr(name)),"auction-sparkle");
        auto request=[&](bool actor,uint32_t owner_pc,uint32_t handle,uint32_t string){
            c.pc=owner_pc;c.r[3]=actor?handle:string;c.r[4]=actor?string:177;c.r[5]=188;
            c.f[1].ps0=actor?1.25:101;c.f[2].ps0=202;c.f[3].ps0=303;c.f[4].ps0=2.5;
            Cpu expected=c;if(actor)guestmods::svc_actor_particle(&c);else guestmods::svc_particle_at(&c);
            const uint32_t result=c.r[3];expected.r[3]=result;assert(!memcmp(&expected,&c,sizeof c));return result;
        };
        g_logic_step.store(100);assert(request(true,data_base+0x100,first_handle,name)==1);
        assert(story_particles::calls==1&&story_particles::effect=="auction-sparkle"&&story_particles::placement.room==11);
        assert(story_particles::placement.position.x==12&&story_particles::placement.position.y==34&&story_particles::placement.position.z==56&&story_particles::options.scale==1.25&&story_particles::options.alpha==188);
        assert(request(false,data_base+0x100,0,name)==1);assert(story_particles::placement.position.x==101&&story_particles::placement.position.y==202&&story_particles::placement.position.z==303&&story_particles::options.scale==2.5&&story_particles::options.alpha==177);
        assert(!request(true,data_base+0x10100,first_handle,name));assert(!request(true,data_base+0x100,first_handle+999,name));assert(!request(false,data_base+0x100,0,data_base+0x10000));
        guestmods::g_actor_draw_callback=true;assert(!request(true,data_base+0x100,first_handle,name));assert(!request(false,data_base+0x100,0,name));guestmods::g_actor_draw_callback=false;
        st8(0x10008326,12);assert(!request(true,data_base+0x100,first_handle,name));st8(0x10008326,11);
        st16(0x1000700E,0x175);assert(!request(false,data_base+0x100,0,name));st16(0x1000700E,0xA8);
        assert(story_particles::calls==2);assert(request(false,data_base+0x100,0,name));assert(request(false,data_base+0x100,0,name));assert(!request(false,data_base+0x100,0,name));assert(story_particles::calls==4);
        g_logic_step.store(101);story_particles::accept=false;assert(!request(false,data_base+0x100,0,name));assert(story_particles::calls==5);story_particles::accept=true;
        const auto pid=mod_actor::actor.process_id;++mod_actor::actor.process_id;assert(!request(true,data_base+0x100,first_handle,name));mod_actor::actor.process_id=pid;
        mod_actor::particle_identity=false;
    }
    c.pc=data_base+0x100;c.r[3]=actor_id;guestmods::svc_actor_find(&c);assert(c.r[3]==first_handle);
    c.r[3]=first_handle;guestmods::svc_actor_userdata(&c);assert(c.r[3]==mod_actor::userdata);
    c.pc=data_base+0x10100;c.r[3]=first_handle;guestmods::svc_actor_userdata(&c);assert(c.r[3]==0);
    c.pc=data_base+0x100;
    auto get=[&](uint32_t out){c.r[3]=first_handle;c.r[4]=out;guestmods::svc_actor_get(&c);return c.r[3];};
    st32(state,80);st32(state+4,1);assert(get(state)==1);
    st32(mod_actor::userdata,80);st32(mod_actor::userdata+4,1);assert(get(mod_actor::userdata)==1);
    assert(get(data_base+0x10000)==0&&get(mod_actor::userdata+240)==0&&get(0xFFFFFFF0)==0);
    const uint32_t combat_out=data_base+0x880;
    auto combat_get=[&](uint32_t handle,uint32_t out){c.r[3]=handle;c.r[4]=out;guestmods::svc_actor_combat_get(&c);return c.r[3];};
    auto initialize_combat=[&](){std::memset(mem::ptr(combat_out),0xA5,64);st32(combat_out,64);st32(combat_out+4,1);};
    initialize_combat();assert(combat_get(first_handle,combat_out)==1&&ld32(combat_out+8)==7&&ld32(combat_out+12)==10&&ld32(combat_out+24)==19&&ld32(combat_out+48)==21&&ld32(combat_out+52)==0&&ld32(combat_out+60)==0);
    initialize_combat();std::array<uint8_t,64> untouched{};std::memcpy(untouched.data(),mem::ptr(combat_out),64);mod_actor::combat_available=false;assert(!combat_get(first_handle,combat_out)&&!std::memcmp(untouched.data(),mem::ptr(combat_out),64));mod_actor::combat_available=true;
    for(unsigned bad_size:{0u,63u,65u}){initialize_combat();st32(combat_out,bad_size);std::memcpy(untouched.data(),mem::ptr(combat_out),64);assert(!combat_get(first_handle,combat_out)&&!std::memcmp(untouched.data(),mem::ptr(combat_out),64));}
    initialize_combat();st32(combat_out+4,2);std::memcpy(untouched.data(),mem::ptr(combat_out),64);assert(!combat_get(first_handle,combat_out)&&!std::memcmp(untouched.data(),mem::ptr(combat_out),64));
    initialize_combat();assert(!combat_get(first_handle,data_base+0x10000)&&!combat_get(first_handle,mod_actor::userdata+240)&&!combat_get(first_handle,0xFFFFFFF0)&&!combat_get(first_handle,combat_out+1));
    c.pc=data_base+0x10100;assert(!combat_get(first_handle,combat_out));c.r[3]=first_handle;guestmods::svc_actor_attack_begin(&c);assert(c.r[3]==0);c.pc=data_base+0x100;
    c.r[3]=first_handle;guestmods::svc_actor_attack_begin(&c);assert(c.r[3]==1&&mod_actor::attacks_begun==1);c.r[3]=first_handle;guestmods::svc_actor_attack_cancel(&c);assert(c.r[3]==1&&mod_actor::attacks_cancelled==1);
    mod_actor::current.scene_owned=true;c.r[3]=first_handle;guestmods::svc_actor_attack_begin(&c);assert(c.r[3]==0);mod_actor::current.scene_owned=false;
    c.r[3]=first_handle;c.f[1].ps0=1;c.f[2].ps0=2;c.f[3].ps0=3;guestmods::svc_actor_set_position(&c);assert(c.r[3]==1&&mod_actor::current.position.x==1);
    c.r[3]=first_handle;c.f[1].ps0=10;c.f[2].ps0=20;c.f[3].ps0=30;guestmods::svc_actor_set_rotation(&c);assert(c.r[3]==1&&mod_actor::current.rotation.x==10&&mod_actor::current.rotation.z==30);
    c.r[3]=first_handle;c.r[4]=3;c.f[1].ps0=24;c.f[2].ps0=80;guestmods::svc_actor_set_collision(&c);assert(c.r[3]==1&&mod_actor::current.collision_radius==24&&mod_actor::current.collision_height==80&&mod_actor::current.pushable);
    c.r[3]=first_handle;c.r[4]=4;guestmods::svc_actor_set_collision(&c);assert(c.r[3]==0);
    std::strcpy(reinterpret_cast<char*>(mem::ptr(state)),"wave");c.r[3]=first_handle;c.r[4]=state;c.r[5]=1;c.f[1].ps0=.5;guestmods::svc_actor_set_animation(&c);assert(c.r[3]==1&&mod_actor::current.animation=="wave");
    mod_actor::current.scene_owned=true;c.r[3]=first_handle;guestmods::svc_actor_set_position(&c);assert(c.r[3]==0);mod_actor::current.scene_owned=false;
    actor_saved=c;guestmods::behavior_sink(&c,mod_actor::address,mod_actor::BehaviorEvent::Execute,nullptr);
    assert(memcmp(&actor_saved,&c,sizeof c)==0&&actor_calls.back().dt==1.0/30.0);
    actor_draw_mutation=true;guestmods::behavior_sink(&c,mod_actor::address,mod_actor::BehaviorEvent::Draw,nullptr);actor_draw_mutation=false;
    assert(actor_draw_result==0&&actor_draw_attack_result==0&&actor_draw_combat_query==1&&mod_actor::current.position.x==1&&memcmp(&actor_saved,&c,sizeof c)==0);
    for(unsigned phase=0;phase<3;++phase){guestmods::behavior_sink(&c,mod_actor::address,phase==0?mod_actor::BehaviorEvent::TalkBegin:phase==1?mod_actor::BehaviorEvent::TalkEnd:mod_actor::BehaviorEvent::TalkCancel,nullptr);assert(actor_calls.back().function==data_base+0xC0&&actor_calls.back().args[2]==phase);}
    mod_actor::Hit hit;hit.sequence=9;hit.attack_type=0x80;hit.attack_power=3;hit.source_kind=1;hit.position={4,5,6};hit.reaction={7,8,9};
    guestmods::behavior_sink(&c,mod_actor::address,mod_actor::BehaviorEvent::Hit,&hit);
    const uint32_t packet=mod_actor::userdata+256;
    assert(actor_calls.back().function==data_base+0xD0&&actor_calls.back().args[2]==packet&&ld32(packet)==64&&ld32(packet+4)==1&&ld32(packet+8)==9&&ld32(packet+24)==0&&u32_as_f32(ld32(packet+28))==4&&ld32(packet+60)==0);
    // Packed paths accept own static data and live userdata only.
    auto walk=[&](uint32_t pointer,uint32_t count,uint32_t loop,float speed){c.pc=data_base+0x100;c.r[3]=first_handle;c.r[4]=pointer;c.r[5]=count;c.r[6]=loop;c.f[1].ps0=speed;guestmods::svc_actor_walk(&c);return c.r[3];};
    for(unsigned i=0;i<6;++i){stf32(state+i*4,float(i));stf32(mod_actor::userdata+i*4,float(i));}
    assert(walk(state,2,0,100)==1&&walk(mod_actor::userdata,2,1,100)==1);
    assert(walk(data_base+0x10000,2,0,100)==0&&walk(mod_actor::userdata+240,2,0,100)==0&&walk(state,17,0,100)==0&&walk(state,2,2,100)==0&&walk(state,2,0,1501)==0);
    stf32(state,std::numeric_limits<float>::quiet_NaN());assert(walk(state,2,0,100)==0);
    c.pc=data_base+0x100;c.r[3]=data_base+0x10090;guestmods::svc_scene_music_register(&c);assert(c.r[3]==0);
    c.r[3]=data_base+0xD0;guestmods::svc_scene_music_register(&c);assert(c.r[3]==1&&story_scenes::music_hook);
    actor_saved=c;assert(story_scenes::music_hook(&c,"harbor",story_scenes::MusicPhase::Start,12,story_scenes::StopReason::Finished));
    assert(memcmp(&actor_saved,&c,sizeof c)==0&&actor_calls.back().args.size()==4&&actor_calls.back().args[1]==0&&actor_calls.back().args[2]==12);
    actor_callback_result=0;assert(!story_scenes::music_hook(&c,"harbor",story_scenes::MusicPhase::Stop,7,story_scenes::StopReason::Finished));actor_callback_result=1;assert(memcmp(&actor_saved,&c,sizeof c)==0);
    actor_callback_throws=true;assert(!story_scenes::music_hook(&c,"harbor",story_scenes::MusicPhase::Restore,0,story_scenes::StopReason::Aborted));actor_callback_throws=false;assert(memcmp(&actor_saved,&c,sizeof c)==0);
    // Registry permits cross-actor nesting: an outer Draw must keep its
    // mutation guard through inner Execute, while ordinary Execute may mutate.
    std::strcpy(reinterpret_cast<char*>(mem::ptr(actor_id)),"other");assert(reg(table)==1);
    guestmods::behavior_sink(&c,mod_actor::address+0x1000,mod_actor::BehaviorEvent::CreateNew,nullptr);
    std::strcpy(reinterpret_cast<char*>(mem::ptr(actor_id)),"sprite");
    actor_nested_handle=mod_actor::other_handle;assert(actor_nested_handle&&actor_nested_handle!=first_handle);actor_saved=c;
    guestmods::behavior_sink(&c,mod_actor::address,mod_actor::BehaviorEvent::Draw,nullptr);
    assert(actor_nested_result==0&&actor_nested_attack_result==0&&mod_actor::current.position.x==1&&!guestmods::g_actor_draw_callback&&memcmp(&actor_saved,&c,sizeof c)==0);
    guestmods::behavior_sink(&c,mod_actor::address,mod_actor::BehaviorEvent::Execute,nullptr);
    assert(actor_nested_result==1&&actor_nested_attack_result==1&&mod_actor::current.position.x==777&&!guestmods::g_actor_draw_callback&&memcmp(&actor_saved,&c,sizeof c)==0);
    initialize_combat();c.pc=data_base+0x100;assert(combat_get(actor_nested_handle,combat_out)==1&&ld32(combat_out+8)==3); // Exact other actor state, not a cached first binding.
    actor_nested_handle=0;mod_actor::current.position.x=1;
    // State restore invalidates old handles while preserving native userdata.
    st32(mod_actor::userdata,0x12345678);guestmods::state_loaded();
    c.pc=data_base+0x100;c.r[3]=first_handle;guestmods::svc_actor_userdata(&c);assert(c.r[3]==0);
    guestmods::behavior_sink(&c,mod_actor::address,mod_actor::BehaviorEvent::CreateRestored,nullptr);
    const uint32_t restored_handle=mod_actor::handle;assert(restored_handle!=first_handle&&ld32(mod_actor::userdata)==0x12345678&&actor_calls.back().args[2]==1);
    initialize_combat();std::memcpy(untouched.data(),mem::ptr(combat_out),64);assert(!combat_get(first_handle,combat_out)&&!std::memcmp(untouched.data(),mem::ptr(combat_out),64));
    mod_actor::actor.process_id+=2;c.r[3]=restored_handle;guestmods::svc_actor_userdata(&c);assert(c.r[3]==0);
    guestmods::behavior_sink(&c,mod_actor::address,mod_actor::BehaviorEvent::CreateNew,nullptr);assert(ld32(mod_actor::userdata)==0&&mod_actor::handle!=restored_handle);
    assert(mod_actor::combat_quarantines==0); // Healthy callbacks and denied Draw mutations never quarantine.
    c.pc=data_base+0x100;c.r[3]=mod_actor::handle;guestmods::svc_actor_attack_begin(&c);assert(c.r[3]==1&&mod_actor::attack_live);
    actor_callback_throws=true;actor_saved=c;guestmods::behavior_sink(&c,mod_actor::address,mod_actor::BehaviorEvent::Execute,nullptr);actor_callback_throws=false;
    assert(memcmp(&actor_saved,&c,sizeof c)==0&&mod_actor::combat_quarantines==1&&mod_actor::quarantined_token==mod_actor::actor.token&&!mod_actor::attack_live);const size_t quarantined_count=actor_calls.size();
    initialize_combat();std::memcpy(untouched.data(),mem::ptr(combat_out),64);c.pc=data_base+0x100;assert(!combat_get(mod_actor::handle,combat_out)&&!std::memcmp(untouched.data(),mem::ptr(combat_out),64));c.r[3]=mod_actor::handle;guestmods::svc_actor_attack_begin(&c);assert(c.r[3]==0);
    guestmods::behavior_sink(&c,mod_actor::address,mod_actor::BehaviorEvent::Execute,nullptr);assert(actor_calls.size()==quarantined_count);
    assert(mod_actor::combat_quarantines==1); // Already quarantined callbacks do not re-enter or issue work.
    guestmods::behavior_sink(&c,mod_actor::address+0x1000,mod_actor::BehaviorEvent::CreateRestored,nullptr);
    c.pc=data_base+0x100;c.r[3]=mod_actor::other_handle;guestmods::svc_actor_attack_begin(&c);assert(c.r[3]==1&&mod_actor::other_attack_live);
    actor_saved=c;actor_callback_throws=true;guestmods::behavior_sink(&c,mod_actor::address+0x1000,mod_actor::BehaviorEvent::Draw,nullptr);actor_callback_throws=false;
    assert(!memcmp(&actor_saved,&c,sizeof c)&&mod_actor::combat_quarantines==2&&mod_actor::quarantined_token==mod_actor::other.token&&!mod_actor::other_attack_live&&!guestmods::g_actor_draw_callback);

#ifdef _WIN32
    VirtualFree(data,0,MEM_RELEASE);
#else
    munmap(data,0x30000);
#endif
    std::puts("guest hook ordering and original passed");
}
