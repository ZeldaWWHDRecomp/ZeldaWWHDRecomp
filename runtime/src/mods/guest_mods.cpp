#include <cmath>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <span>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
// Guest mods (Mod SDK v2 prototype, docs/mod-sdk-v2.md): PowerPC mods translated to C on install and
// compiled into a module by the player's own compiler (tools/guestmod/). They hook or replace game
// functions at runtime through the per-function flag check that recomp.py --mod-hooks puts at the
// start of every game function body (PPC_MOD_HOOK in ppc.h).
//
// The mod manager builds and loads its frozen, trusted guest set before guest code runs.
#include "../editor/editor_game.h"
#include "story_native_canonical_code_id.h"
#include "../exception_report.h"
#include "guest_mods.h"
#include "../model_probe.h"
#include "story_native_refresh_dispatch_audit.h"
#include "story_native_manager_cancel_dispatch_audit.h"
#include "story_native_cancel_dispatch_lifetime.h"
#include "story_native_stage_owner_observer.h"
#include "story_native_event_dispatch.h"
#include "story_native_outer_return.h"
#include "../native_quiescence.h"
#include "story_native_event_game_thread.h"
#include "story_native_stage_owner_hooks.h"
#include "story_native_setinfo_diagnostic.h"
#include "story_native_wait_runtime.h"
#include "story_native_scene_request.h"
#include <atomic>
#include <mutex>
#include "guest_validation.h"
#include "guest_build.h"
#include "packages.h"
#include "guest_heap.h"
#include "guest_files.h"
#include "guest_settings.h"
#include "guest_hud.h"
#include "guest_live.h"
#include "map_hud.h"
#include "../aspect.h"
#include "../interp.h"
#include "../mic.h"
#include "../fixed_seed.h"
#include "../gfx/renderer.h"
#include "../overlay/hostui.h"
#include "game_layouts.h"
#include <fstream>
#include <filesystem>
#include "guest_audio.h"
#include "story_notebook_game.h"
#include "guest_png.h"
#include "mod_archive.h"
#include "story_mail.h"
#include "item_icons.h"
#include "story_lyt_screen.h"
#include "story_notebook_layout.h"
#include "story_mail_book.h"
#include "story_chart.h"
#include "story_treasure.h"
#include "story_actors.h"
#include "story_scene_needs.h"
#include "story_room_paths.h"
#include "story_resident_routes.h"
#include "story_resident_walk_files.h"
#include "story_game_pieces.h"
#include "story_kit_models.h"
#include "mod_stages.h"
#include "mod_actor.h"
#include "mod_actor_behavior_core.h"
#include "actor_model_cache.h"
#include "story_scenes.h"
#include "story_notebook.h"
#include "story_items.h"
#include "story_item_models.h"
#include "story_item_get_icon.h"
#include "story_actor_textures.h"
#include "story_prop_textures.h"
#include "story_props.h"
#include "story_particles.h"
#include "mod_hash.h"
#include "story_item_buttons.h"
#include "story_bag.h"
#include "story_manifest.h"
#include "story_store.h"
#include "story_takeover.h"
#include "story_treasure_chest.h"
#include "mod_json.h"
#include "input.h"
#include "true60.h"
#include "guest_addr.h"
#include <atomic>
#include <limits>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "recomp_table.h"
#include "runtime.h"
#include "../platform/host.h"
#include "wwhd_guest_abi.h"

namespace {std::recursive_mutex&native_hook_mutex(){static std::recursive_mutex mutex;return mutex;}
std::atomic<bool> native_hooks_sealed{false},native_hooks_fault{false},guest_init_started{false};
uint8_t*sealed_mod_flags=nullptr;const PpcFunc*sealed_mod_bodies=nullptr;unsigned sealed_mod_count=0;}
extern "C" {
unsigned g_mod_hook_count = 0;  // initialized only by a hook-enabled game-code build
uint8_t* g_mod_hook_flags = nullptr;
const PpcFunc* g_mod_bodies = nullptr;
void ppc_mod_register(unsigned count, uint8_t* flags, const PpcFunc* bodies) {
    std::lock_guard mutation(native_hook_mutex());
    if(native_hooks_sealed.load(std::memory_order_acquire)){native_hooks_fault=true;return;}
#ifndef WWHD_GUEST_HOOKS_STARTUP_FIXTURE
    if(g_mod_hook_count){native_hooks_fault=true;return;}
#endif
    g_mod_hook_count = count; g_mod_hook_flags = flags; g_mod_bodies = bodies;
}
extern volatile int g_core_preempt[3];
double ppc_fres(double);
double ppc_frsqrte(double);
}

// Cpu::mod_skip uses former padding: save states (which store sizeof(Cpu) bytes) stay compatible
static_assert(sizeof(void*) != 8 || sizeof(Cpu) == 752, "Cpu layout changed");

namespace story_lyt {
void set_game_calls(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>));
void install(const std::function<void(uint32_t, void (*)(Cpu*))>& replace);
}
namespace guestmods {
// This sole production issuer brackets the final committed startup call. It
// exposes no callback or request-controlled way to reopen allocation admission.
class NativeSceneRequestIssuer final {
public:
 static void startup(bool installed,story_native_wait_runtime::LogicStep step){
  story_native_scene_request::StartupWindow window;
  story_native_wait_runtime::startup(installed,step);
 }
};
namespace {
void call_original(Cpu*,uint32_t);

struct Chain {
    uint32_t addr = 0, ordinal = 0;
    PpcFunc replace = nullptr;
    std::string replace_mod;
    std::vector<PpcFunc> entry, ret;
};
std::unordered_map<uint32_t, Chain> g_chains;  // built before the game starts, read-only afterwards
struct Loaded {
    std::string path,id,version,package_path,data_path,name;
    uint32_t hud_callback=0,hud_screen=0;
    uint64_t hud_step=0;
    bool hud_called=false;
    const WWHDGuestModuleV1* m=nullptr;
    uint32_t region_size=0;
    mods::json::Value options;  // the start values; live options change here while the game runs (g_options_mutex)
    std::vector<std::pair<std::string,std::string>> mic_options;  // its options with "permission": "microphone"
    uint32_t input_filter=0, touch_callback=0, scratch=0;  // live features (guest_live.h): callbacks, a 32-byte exchange area
    std::unique_ptr<Heap> heap;
    std::unique_ptr<Files> files;
};
std::vector<Loaded> g_loaded;
std::atomic<uint64_t> g_logic_step{0};
std::atomic<bool> g_hud_callbacks{false};
bool g_world_scenes=false;
bool g_native_scenes=false;  // native scene playback can run (set once in init): the game-thread binding
bool g_particle_import=false;  // a loaded code mod imports wwhd_actor_particle or wwhd_particle_at
std::mutex g_hud_mutex;
thread_local bool g_map_hud_draw=false;
uint32_t map_hud_owner(const Loaded& mod){return uint32_t(&mod-g_loaded.data())+1;}
Loaded& owner(Cpu* c) {
    for(auto& mod:g_loaded)
        if(c->pc>=mod.m->mem_base&&c->pc-mod.m->mem_base<mod.m->mem_size)return mod;
    fatal("[guestmods] host service called outside a loaded mod: %08X",c->pc);
}

int ordinal_of(uint32_t addr) {
    const RecompEntry* b = g_recomp_funcs;
    const RecompEntry* e = g_recomp_funcs + g_recomp_func_count;
    const RecompEntry* it = std::lower_bound(b, e, addr, [](const RecompEntry& r, uint32_t a) { return r.addr < a; });
    return it != e && it->addr == addr ? (int)(it - b) : -1;
}

// SetInfo/teardown stay native pass-through while the event-resource owner
// receipt is being proved. Installing all eleven flags ensures the same original
// wrapper covers calls from recompiled game code and guestmods::call_original.
// No observer, receipt, or native scene route is enabled by these flags.
// Bulk heap destruction outside these entry points remains unproven.
bool install_stage_owner_pass_through() {
    std::lock_guard mutation(native_hook_mutex());
    if(native_hooks_sealed.load())return false;
    static bool installed=false;
    if(installed)return true;
    if(!g_mod_hook_count||!g_mod_hook_flags||!g_mod_bodies)return false;
    const std::array<uint32_t,11> addresses={
        story_native_stage_owner_hooks::SetInfo(),
        story_native_stage_owner_hooks::DeleteInfo(),
        story_native_stage_owner_hooks::FreePackEntry(),
        story_native_stage_owner_hooks::UnloadArchive(),
        story_native_stage_owner_hooks::ManagerDisposer(),
        story_native_stage_owner_hooks::ArchiveHolderDtor(),
        story_native_stage_owner_hooks::PackHolderDtor(),
        story_native_stage_owner_hooks::StageHeapDtor(),
        story_native_stage_owner_hooks::ArchiveResourceDtor(),
        story_native_stage_owner_hooks::SarcResourceDtor(),
        story_native_stage_owner_hooks::ArrayDelete()};
    std::array<unsigned,11> ordinals{};
    for(unsigned n=0;n<addresses.size();++n){
        const uint32_t addr=addresses[n];
        if(!addr)return false;
        const int i=ordinal_of(addr);
        if(i<0||unsigned(i)>=g_mod_hook_count||!g_mod_bodies[i]||g_mod_hook_flags[i]||g_chains.contains(addr))return false;
        for(unsigned k=0;k<n;++k)if(addresses[k]==addr)return false;
        ordinals[n]=unsigned(i);
    }
    g_chains.reserve(g_chains.size()+addresses.size());
    unsigned inserted=0;
    try{
        for(unsigned n=0;n<addresses.size();++n){
            Chain chain;chain.addr=addresses[n];chain.ordinal=ordinals[n];
            if(!g_chains.emplace(addresses[n],std::move(chain)).second)break;
            ++inserted;
        }
    }catch(...){for(unsigned n=0;n<inserted;++n)g_chains.erase(addresses[n]);throw;}
    if(inserted!=addresses.size()){
        for(unsigned n=0;n<inserted;++n)g_chains.erase(addresses[n]);
        return false;
    }
    for(unsigned n=0;n<addresses.size();++n)g_mod_hook_flags[ordinals[n]]=1;
    installed=true;
    return true;
}

namespace {
struct NativePipelineEvidence {
 Cpu* cpu{};uint32_t function{};unsigned delegates=0,started=0,completed=0;bool delegating=false,invalid=false;
 std::optional<story_native_event_depth::FrameWitness>witness;
 NativePipelineEvidence*previous=nullptr;
};
thread_local NativePipelineEvidence* native_pipeline=nullptr;
struct PipelinePin {NativePipelineEvidence& evidence;explicit PipelinePin(NativePipelineEvidence&e):evidence(e){e.previous=native_pipeline;native_pipeline=&e;}~PipelinePin(){native_pipeline=evidence.previous;}};
struct WaitRouteFrame {Cpu*cpu;uint32_t function,receiver;bool next=false;unsigned started=0,completed=0;WaitRouteFrame*previous;};
thread_local WaitRouteFrame*wait_route_frame=nullptr;
void original_started(Cpu*c,uint32_t fn){
 if(auto*f=wait_route_frame;f&&f->cpu==c&&f->function==fn)++f->started;
 if(auto*e=native_pipeline;e&&e->function==fn){if(e->cpu!=c||!e->delegating)e->invalid=true;++e->started;}
}
void original_completed(Cpu*c,uint32_t fn){
 if(auto*f=wait_route_frame;f&&f->cpu==c&&f->function==fn)++f->completed;
 if(auto*e=native_pipeline;e&&e->function==fn){if(e->cpu!=c||!e->delegating)e->invalid=true;++e->completed;}
}

const std::array<uint32_t,kNativeWaitRouteCount>& native_wait_addresses() {
    static const std::array<uint32_t,kNativeWaitRouteCount> addresses={
        GC_OPTIONAL(0x022C36D0),GC_OPTIONAL(0x024FFA3C),GC_OPTIONAL(0x02544374),
        GC_OPTIONAL(0x022C2958),GC_OPTIONAL(0x0259F858),GC_OPTIONAL(0x024FF164),
        GC_OPTIONAL(0x02543D80),GC_OPTIONAL(0x02541664),GC_OPTIONAL(0x02542D88),
        GC_OPTIONAL(0x02543280),GC_OPTIONAL(0x0259FA00),GC_OPTIONAL(0x0259FA64),
        GC_OPTIONAL(0x0254465C),GC_OPTIONAL(0x02542C9C),GC_OPTIONAL(0x024FE3E8),
        GC_OPTIONAL(0x0253EFA8),GC_OPTIONAL(0x0253FF34),
        GC_OPTIONAL(0x02543C6C),GC_OPTIONAL(0x02542A38),
        GC_OPTIONAL(0x025A0984),GC_OPTIONAL(0x025A0B3C)};
    return addresses;
}
bool world_actor_overlap(size_t n) {return n==0||n==3||n==8||n==18;}
NativeWaitDispatch native_wait_dispatch=nullptr;
std::array<PpcFunc,kNativeWaitRouteCount> native_wait_prior{};
// Recorded only by this module's own story_actors::install callback. A guest
// replacement can choose the same display name, but cannot become this owner.
std::array<PpcFunc,kNativeWaitRouteCount> native_wait_known_world_actor{};
PpcFunc native_wait_known_notebook_people=nullptr;
bool known_native_wait_owner(size_t n,PpcFunc owner,const std::string& name){
    return (world_actor_overlap(n)&&native_wait_known_world_actor[n]&&
            owner==native_wait_known_world_actor[n]&&name=="the port's world actors")||
           (n==0&&native_wait_known_notebook_people&&owner==native_wait_known_notebook_people&&
            name=="the port's story notebook");
}
bool native_wait_installed=false;
void run_native_wait_route(Cpu* c,size_t n) {
    const auto route=native_wait_dispatch;
    if(!route)fatal("[guestmods] native WAIT route called before installation");
    const uint32_t address=native_wait_addresses()[n];
    bool next_called=false;
    WaitRouteFrame frame{c,address,c->r[3],false,0,0,wait_route_frame};wait_route_frame=&frame;
    struct Pop{WaitRouteFrame*previous;~Pop(){wait_route_frame=previous;}}pop{frame.previous};
    const NativeWaitNext next=[&] {
        if(next_called)return;
        next_called=true;frame.next=true;
        NativePipelineEvidence*e=native_pipeline;
        const bool proving=e&&e->cpu==c&&e->function==address&&n==1&&!native_wait_prior[n];
        if(proving){++e->delegates;e->delegating=true;}
        struct EndDelegate{NativePipelineEvidence*e;~EndDelegate(){if(e)e->delegating=false;}}end{proving?e:nullptr};
        if(auto prior=native_wait_prior[n])prior(c);
        else call_original(c,address);
    };
    if(route(c,address,next)==NativeWaitDecision::Delegate)next();
}
template<size_t N> void native_wait_trampoline(Cpu* c) {run_native_wait_route(c,N);}
const std::array<PpcFunc,kNativeWaitRouteCount> native_wait_trampolines={
    native_wait_trampoline<0>,native_wait_trampoline<1>,native_wait_trampoline<2>,
    native_wait_trampoline<3>,native_wait_trampoline<4>,native_wait_trampoline<5>,
    native_wait_trampoline<6>,native_wait_trampoline<7>,native_wait_trampoline<8>,
    native_wait_trampoline<9>,native_wait_trampoline<10>,native_wait_trampoline<11>,
    native_wait_trampoline<12>,native_wait_trampoline<13>,native_wait_trampoline<14>,
    native_wait_trampoline<15>,native_wait_trampoline<16>,native_wait_trampoline<17>,
    native_wait_trampoline<18>,native_wait_trampoline<19>,native_wait_trampoline<20>};
}
NativeWaitRouteCoverage native_wait_route_coverage_impl();
bool install_native_wait_routes_impl(NativeWaitDispatch route) {
    std::lock_guard mutation(native_hook_mutex());
    if(native_hooks_sealed.load())return false;
    if(native_wait_installed)
        return route && native_wait_dispatch==route &&
               native_wait_route_coverage_impl().certified;
    if(!route||!hooks_built())return false;
    const auto& addresses=native_wait_addresses();
    std::array<unsigned,kNativeWaitRouteCount> ordinals{};
    std::array<PpcFunc,kNativeWaitRouteCount> prior{};
    const auto refuse=[&](size_t n,const char* reason){
        LOG("[native_wait] route refused index=%u address=%08X reason=%s",
            unsigned(n),addresses[n],reason);
        return false;
    };
    for(size_t n=0;n<addresses.size();++n) {
        const auto address=addresses[n];
        if(!address)return refuse(n,"mapping");
        const int ordinal=ordinal_of(address);
        if(ordinal<0||unsigned(ordinal)>=g_mod_hook_count||!g_mod_bodies[ordinal])return refuse(n,"native body");
        for(size_t k=0;k<n;++k)
            if(addresses[k]==address||ordinals[k]==unsigned(ordinal))return false;
        ordinals[n]=unsigned(ordinal);
        const auto it=g_chains.find(address);
        const bool flagged=g_mod_hook_flags[ordinal]!=0;
        if((it==g_chains.end())!=!flagged)return refuse(n,"flag/chain");
        if(it==g_chains.end())continue;
        const Chain& chain=it->second;
        if(chain.addr!=address||chain.ordinal!=unsigned(ordinal))return refuse(n,"ordinal");
        if(chain.replace) {
            if(!known_native_wait_owner(n,chain.replace,chain.replace_mod)){
                LOG("[native_wait] replacement owner=%s known=%u matches=%u",
                    chain.replace_mod.c_str(),unsigned(bool(native_wait_known_world_actor[n])),
                    unsigned(chain.replace==native_wait_known_world_actor[n]));
                return refuse(n,"replacement owner");
            }
            prior[n]=chain.replace;
        }
    }
    // Stage a complete copy first. Allocation or string failures leave every
    // flag and chain untouched; the final swap precedes enabling all flags.
    try {
        auto staged=g_chains;
        staged.reserve(staged.size()+addresses.size());
        for(size_t n=0;n<addresses.size();++n) {
            Chain& chain=staged[addresses[n]];
            chain.addr=addresses[n];chain.ordinal=ordinals[n];
            chain.replace=native_wait_trampolines[n];
            chain.replace_mod="the port's native WAIT route";
        }
        g_chains.swap(staged);
    } catch (...) {
        return false;
    }
    native_wait_prior=prior;
    native_wait_dispatch=route;
    for(size_t n=0;n<addresses.size();++n)g_mod_hook_flags[ordinals[n]]=1;
    native_wait_installed=true;
    return true;
}
bool routes_owned_impl() {return native_wait_route_coverage_impl().certified;}
NativeWaitRouteCoverage native_wait_route_coverage_impl() {
    NativeWaitRouteCoverage result{uint32_t(kNativeWaitRouteCount),0,0,false};
    if(!native_wait_installed||!native_wait_dispatch||!hooks_built())return result;
    const auto& addresses=native_wait_addresses();
    for(size_t n=0;n<addresses.size();++n) {
        const int ordinal=ordinal_of(addresses[n]);
        if(ordinal<0||unsigned(ordinal)>=g_mod_hook_count||
           !g_mod_bodies[ordinal]||!g_mod_hook_flags[ordinal])return result;
        const auto it=g_chains.find(addresses[n]);
        if(it==g_chains.end()||it->second.addr!=addresses[n]||
           it->second.ordinal!=unsigned(ordinal)||
           it->second.replace!=native_wait_trampolines[n])return result;
        if(native_wait_prior[n]&&
           !((world_actor_overlap(n)&&native_wait_prior[n]==native_wait_known_world_actor[n])||
             (n==0&&native_wait_prior[n]==native_wait_known_notebook_people)))return result;
        ++result.installed;
        if(native_wait_prior[n]){
            if(n==0&&native_wait_prior[n]==native_wait_known_notebook_people)++result.notebook_composed;
            else ++result.world_actors_composed;
        }
    }
    result.certified=result.installed==result.requested;
    if(result.certified)for(size_t n=0;n<3;++n)
        result.outer_owner[n]=uint64_t(reinterpret_cast<uintptr_t>(native_wait_trampolines[n]));
    return result;
}

namespace {
std::atomic<NativeEventObserver> g_native_event_observer{nullptr};
std::atomic<NativeOuterReturnCallback> g_native_outer_return_callback{nullptr};
bool native_event_outer(uint32_t fn){return fn==GC(0x022C36D0)||fn==GC(0x024FFA3C)||fn==GC(0x02544374);}
 }
} // private implementation namespace
class NativeOuterReturnIssuer {
public:
 static void completed(Cpu*c,story_native_event_depth::Tracker&t,const NativePipelineEvidence&e){
  const auto callback=g_native_outer_return_callback.load(std::memory_order_acquire);
  const int ordinal=ordinal_of(e.function);
  if(ordinal<0||dispatch::lookup(e.function)!=g_recomp_funcs[ordinal].fn)return;
  if(!callback||!native_hooks_sealed.load()||native_hooks_fault.load()||!dispatch::native_text_sealed()||g_mod_hook_flags!=sealed_mod_flags||g_mod_bodies!=sealed_mod_bodies||g_mod_hook_count!=sealed_mod_count||e.cpu!=c||e.function!=GC(0x024FFA3C)||e.invalid||e.delegates!=1||e.started!=1||e.completed!=1||!e.witness||!e.witness->valid()||!e.witness->returned(t)||!native_wait_route_coverage_impl().certified)return;
  story_native_outer_return::OuterReturn token(c,t,*e.witness);
  if(token.current(c,e.witness->expected()))callback(c,token);
 }
};
namespace {
namespace {
template<class Next>void observed_native(Cpu*c,uint32_t fn,bool original,Next&&next){
 auto observer=g_native_event_observer.load(std::memory_order_acquire);
 if(!observer||!native_event_outer(fn)){next();return;}
 story_native_event_depth::Tracker*tracker=nullptr;story_native_event_depth::Call call;
 if(!observer(c,fn,tracker,call)){next();return;}
 if(!tracker){if(call.authority==story_native_event_depth::Authority::Foreign)next();else c->r[3]=1;return;}
 if(call.function!=fn){
  if(call.authority==story_native_event_depth::Authority::Foreign){auto invalid=call;invalid.authority=story_native_event_depth::Authority::InvalidOwned;auto fault=tracker->enter(invalid);next();return;}
  call.authority=story_native_event_depth::Authority::InvalidOwned;
 }
 NativePipelineEvidence evidence;evidence.cpu=c;evidence.function=fn;
 const auto body=[&](){if(!original&&call.authority==story_native_event_depth::Authority::Owned&&fn==GC(0x024FFA3C))evidence.witness=tracker->current_owned_frame();next();return c->r[3];};
 std::optional<PipelinePin>pin;if(!original)pin.emplace(evidence);
 const auto result=original?story_native_event_dispatch::original(*tracker,call,body):story_native_event_dispatch::dispatcher(*tracker,call,body);
 if(!result.executed)c->r[3]=1; // exact invalid owned consumer is denied; foreign always delegates
 pin.reset();
 if(!original&&result.executed&&result.observed)NativeOuterReturnIssuer::completed(c,*tracker,evidence);
}
}
void call_original(Cpu* c, uint32_t func) {
    int i = ordinal_of(func);
    if (i < 0) fatal("[guestmods] call_original: %08X is not a game function", func);
    observed_native(c,func,true,[&]{
        if (g_mod_hook_flags[i]) c->mod_skip = func;  // consumed by the body check
        story_native_stage_owner_hooks::invoke_original(c,func,[&]{original_started(c,func);g_mod_bodies[i](c);original_completed(c,func);});
    });
}

PackageObserver g_package_observer=nullptr;
// Declarative world content is shared by guest and data-only content packages.
bool prop_texture_context_valid(const story_prop_textures::Context& context){
    if(context.actor<mem::kMem2Start||context.actor>mem::kMem2End-story_props::kClassBytes)return false;
    uint32_t token=0,epoch=0,generation=0,begin=0,end=0;
    return story_actors::prop_texture_identity(context.actor,context.pid,token,epoch,generation)&&
           token&&epoch==context.epoch&&generation==context.generation&&
           story_props::texture_heap(context.actor,begin,end)&&
           begin==context.private_begin&&end==context.private_end&&
           ld32(context.actor+0x790)==context.type;
}
bool prop_texture_binding_valid(const story_actor_textures::Binding& binding){
    if(binding.actor<mem::kMem2Start||binding.actor>mem::kMem2End-story_props::kClassBytes||
       !binding.model||binding.model!=ld32(binding.actor+0x3B4))return false;
    story_prop_textures::Context context{binding.actor,binding.pid,binding.epoch,binding.generation,
        ld32(binding.actor+0x790),binding.private_begin,binding.private_end};
    return prop_texture_context_valid(context)&&binding.no_pattern_animation;
}
// models a content package's start-up couldn't prepare (mod, words), for the editor's status
std::mutex g_world_model_notes_mutex;
std::vector<std::pair<std::string,std::string>> g_world_model_notes;
void load_world(const std::string& mod,const mods::json::Value& manifest,const std::string& package_dir,const std::string& data_dir) {
    mod_actor::configure_world_objects(config::game_dir);
    // Mod stages (mod_stages.h): built from the player's game files into the stage cache (or taken from it) and
    // served to the game; a stage the player's files can't make is left out with a line in the log
    if(manifest.get("stages").type!=mods::json::Value::Null){
        std::vector<std::string> python;std::string tool;
        try{const auto bridge=BuildBridge::installed(std::string(),false);python=bridge.tool_python();tool=(std::filesystem::path(bridge.builder).parent_path().parent_path()/"story_mods"/"stage_builder.py").string();}
        catch(const std::exception& e){LOG("[mod-stages] %s: the stage builder is unavailable: %s",mod.c_str(),e.what());}
        mod_stages::declare(mod,manifest,package_dir,data_dir,python,tool,config::game_dir);
    }
    struct ItemModelBatch {size_t checkpoint=story_item_models::checkpoint();bool committed=false;~ItemModelBatch(){if(!committed)story_item_models::rollback(checkpoint);}} item_model_batch;
    struct TextureBatch {size_t checkpoint=story_actor_textures::checkpoint();bool committed=false;~TextureBatch(){if(!committed)story_actor_textures::rollback(checkpoint);}} texture_batch;
    struct SceneBatch {size_t checkpoint=story_scenes::checkpoint();bool committed=false;~SceneBatch(){if(!committed)story_scenes::rollback(checkpoint);}} scene_batch;
    std::vector<story_notebook::ManifestActor> actors;
    std::vector<story_notebook::ManifestScene> scenes;
    std::string why;
    if(!story_notebook::parse_actors(manifest,actors,why)||!story_notebook::parse_scenes(manifest,scenes,why))
        exception_report::raise("World manifest "+mod+": "+why);
    // the mod's room paths: declared now, written to their records once the record memory is there
    {std::vector<story_room_path::Path> paths;
     if(!story_notebook::parse_room_paths(manifest,paths,why)||!story_room_paths::declare(mod,paths,why))exception_report::raise("World paths "+mod+": "+why);}
    for(auto& actor:actors)if(!actor.texture.empty()) {
        namespace fs=std::filesystem;
        if(!mods::archive::relative_path(actor.texture))exception_report::raise("World texture "+mod+":"+actor.id+": invalid path");
        fs::path path=package_dir;
        if(!fs::is_directory(path)||fs::is_symlink(path))exception_report::raise("World texture "+mod+":"+actor.id+": package unavailable");
        for(const auto& part:fs::path(actor.texture)){path/=part;if(fs::is_symlink(path))exception_report::raise("World texture "+mod+":"+actor.id+": symlink path denied");}
        if(!fs::is_regular_file(path)||fs::file_size(path)>2u*1024u*1024u)exception_report::raise("World texture "+mod+":"+actor.id+": PNG missing or oversized");
        std::ifstream file(path,std::ios::binary);std::vector<uint8_t> bytes(size_t(fs::file_size(path)));
        if(!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))exception_report::raise("World texture "+mod+":"+actor.id+": PNG read failed");
        actor.texture_digest=mods::hash::sha256_text(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
        actor.texture_token=story_actor_textures::declare_png(mod+":"+actor.id+":"+actor.actor+":"+actor.texture,bytes,why);
        if(!actor.texture_token)exception_report::raise("World texture "+mod+":"+actor.id+": "+why);
    }
    std::vector<story_notebook::ManifestModel> models;
    if(!story_notebook::parse_models(manifest,models,why))exception_report::raise("World models "+mod+": "+why);
    std::vector<story_notebook::ManifestItem> model_items;
    if(!story_notebook::parse_items(manifest,model_items,why))exception_report::raise("Item models "+mod+": "+why);
    for(const auto& item:model_items)if(!item.held_model.empty()&&std::none_of(models.begin(),models.end(),[&](const auto&m){return m.id==item.held_model;}))exception_report::raise("Unknown held model "+mod+":"+item.held_model);
    // Game pieces ("actor": "piece:<id>"): each set is built once from the player's room archive into the
    // player's model cache, then each piece becomes a model and collision of this mod's placements. A piece
    // the player's game files can't make (another game version) is left out with a line in the log, like a
    // game object the game doesn't have; the rest of the mod works.
    for(const auto& id:story_game_pieces::used(actors)) {
        std::string failure;
        try {
            const auto bridge=BuildBridge::installed(std::string(),false);
            const auto tool=std::filesystem::path(bridge.builder).parent_path().parent_path()/"story_mods"/"game_piece_model.py";
            const auto set=story_game_pieces::build(id,story_game_pieces::cache_directory(data_dir),bridge.tool_python(),tool.string(),config::game_dir);
            if(!story_game_pieces::register_for_mod(mod,*set,id,false,why))failure=why;
        } catch(const std::exception& e) {failure=e.what();}
        if(failure.empty())continue;
        LOG("[game_pieces] %s: piece:%s is left out: %s",mod.c_str(),id.c_str(),failure.c_str());
        actors.erase(std::remove_if(actors.begin(),actors.end(),[&](const auto& a){return a.piece==id;}),actors.end());
    }
    // a model that can't be prepared (its conversion failed: another tools version, a model the converter refuses) is
    // left out with its reason in the log and the editor's status; its actors stay out of the game ("not ready"), the
    // rest of the mod and the game go on
    std::vector<std::string> failed_models;
    for(const auto& model:models) {
        const bool actor_uses=std::any_of(actors.begin(),actors.end(),[&](const auto& actor){return actor.kind=="mod"&&actor.model==model.id;});
        const bool item_uses=std::any_of(model_items.begin(),model_items.end(),[&](const auto& item){return item.held_model==model.id;});
        const bool collision_uses=std::any_of(actors.begin(),actors.end(),[&](const auto& actor){return actor.collision_mesh==model.id;});
        const bool visual_uses=std::any_of(actors.begin(),actors.end(),[&](const auto& actor){return actor.distant.model==model.id||actor.shoreline.model==model.id;});
        const bool shoreline_uses=std::any_of(actors.begin(),actors.end(),[&](const auto& actor){return actor.shoreline.model==model.id;});
        // the shoreline's material: coast foam ("") or a water, river, waterfall or lava surface; one per model
        std::string shoreline_material;
        for(const auto& actor:actors)if(actor.shoreline.model==model.id){shoreline_material=actor.shoreline.material;break;}
        if(std::any_of(actors.begin(),actors.end(),[&](const auto& actor){return actor.shoreline.model==model.id&&actor.shoreline.material!=shoreline_material;}))exception_report::raise("A shoreline model is either coast foam or one kind of water surface");
        const bool distant_uses=std::any_of(actors.begin(),actors.end(),[&](const auto& actor){return actor.distant.model==model.id;});
        if(shoreline_uses&&(actor_uses||item_uses||collision_uses||distant_uses))exception_report::raise("A shoreline model must be dedicated to shoreline use");
        if(!actor_uses&&!item_uses&&!collision_uses&&!visual_uses)continue;
        // a kit model (story_kit_models.h): built from the player's game files like a game piece, drawn with the
        // background light of its room; only static world objects place it (the manifest's rules)
        if(model.kind=="kit") {
            if(item_uses||shoreline_uses)exception_report::raise("A kit model is placed as a world object or a distant model; it can't be held or a shoreline");
            try {
                const auto bridge=BuildBridge::installed(std::string());
                const auto tool=std::filesystem::path(bridge.builder).parent_path().parent_path()/"story_mods"/"kit_model.py";
                // only a distant model: its game textures at the size of the game's own distant views
                const bool distant_only=distant_uses&&!actor_uses&&!collision_uses;
                const auto set=story_kit_models::build(mod,model,package_dir,story_kit_models::cache_directory(data_dir),bridge.tool_python(),tool.string(),config::game_dir,distant_only);
                if(distant_uses&&set->pieces[0].triangles>1024)throw std::runtime_error("a distant model may have 1 to 1024 triangles");
                if(!story_kit_models::register_for_mod(mod,model.id,*set,false,why))throw std::runtime_error(why);
            } catch(const std::exception& e) {
                LOG("[world] %s: model %s is not ready and its actors are left out: %s",mod.c_str(),model.id.c_str(),e.what());
                {std::lock_guard guard(g_world_model_notes_mutex);g_world_model_notes.push_back({mod,"Model "+model.id+" is not ready: "+e.what()});}
                actor_model_cache::note_failure(mod,model.id,e.what());
                failed_models.push_back(model.id);
            }
            continue;
        }
        if(visual_uses&&model.kind!="static")exception_report::raise("Distant and shoreline models require static kind");
        if(item_uses&&model.kind!="static")exception_report::raise("Held item models currently require static kind; skinned needs a separate Morf lifetime");
        try {
            const auto bridge=BuildBridge::installed(std::string(),false);
            const auto tool=std::filesystem::path(bridge.builder).parent_path().parent_path()/"story_mods"/(model.kind=="skinned"?"skinned_actor_model.py":"actor_model.py");
            const auto archive=actor_model_cache::prepare(mod,model,package_dir,data_dir,bridge.tool_python(),tool.string(),config::game_dir,collision_uses,shoreline_uses,shoreline_material);
            if(visual_uses&&(actor_model_cache::triangles_for(archive)==0||actor_model_cache::triangles_for(archive)>1024))exception_report::raise("Distant and shoreline models must contain 1..1024 render triangles");
            if(collision_uses&&!mod_actor::register_collision(mod,model.id,actor_model_cache::collision_for(archive)))exception_report::raise("Prepared collision unavailable");
            story_notebook::WorldPoint minimum,maximum;
            if(!actor_model_cache::bounds_for(archive,minimum,maximum))exception_report::raise("Prepared model bounds unavailable");
            for(const auto& item:model_items)if(item.held_model==model.id&&!story_item_models::declare(story_items::item_key(mod,item.id),archive,item.held_frames,why,mods::hash::sha256_file(actor_model_cache::archive_for("/vol/content/Common/Object/"+archive+".szs"))))exception_report::raise("Item model "+mod+": "+why);
            if((actor_uses||visual_uses)&&!story_actors::register_model(mod,model.id,archive,1,why,minimum,maximum,model.kind=="skinned",actor_model_cache::clips_for(archive),actor_model_cache::rig_for(archive)))exception_report::raise("World model "+mod+": "+why);
        } catch(const std::exception& e) {
            LOG("[world] %s: model %s is not ready and its actors are left out: %s",mod.c_str(),model.id.c_str(),e.what());
            {std::lock_guard guard(g_world_model_notes_mutex);g_world_model_notes.push_back({mod,"Model "+model.id+" is not ready: "+e.what()});}
            actor_model_cache::note_failure(mod,model.id,e.what());
            failed_models.push_back(model.id);
        }
    }
    if(!failed_models.empty()) {
        auto failed=[&](const std::string& id){return !id.empty()&&std::find(failed_models.begin(),failed_models.end(),id)!=failed_models.end();};
        actors.erase(std::remove_if(actors.begin(),actors.end(),[&](const auto& a){
            const bool out=(a.kind=="mod"&&failed(a.model))||failed(a.collision_mesh)||failed(a.shoreline.model)||failed(a.distant.model);
            if(out)LOG("[world] %s: %s is left out (its model is not ready)",mod.c_str(),a.id.c_str());
            return out;}),actors.end());
    }
    std::vector<std::string> flags;
    for(const auto& scene:scenes)for(const auto& step:scene.steps)for(const auto& action:step.then)
        if(action.kind==story_notebook::StoryAction::Kind::Flag)flags.push_back(action.id);
    if(!story_scenes::declare(mod,scenes,why)||
       !story_actors::load(mod,actors,why,flags))
        exception_report::raise("World content "+mod+": "+why);
    item_model_batch.committed=true;
    texture_batch.committed=true;
    scene_batch.committed=true;
    g_world_scenes|=!scenes.empty();
    if(g_package_observer)g_package_observer(mod,manifest.get("name").string(mod),package_dir,manifest);  // the world editor's inspector
    if(!actors.empty()||!scenes.empty()) {
        // wordless scenes run without the story system's required package (story_scene_needs.h)
        if((!actors.empty()||story_scene_needs::needs_story(manifest))&&!story_scene_needs::story_off().count(mod)){story_notebook::note_import();story_mail::note_import();}
        LOG("[world] %s: %zu actors, %zu scenes",mod.c_str(),actors.size(),scenes.size());
    }
}

// ---- host services (imports of guest mods by name; arguments in r3..r10 / f1..f8, result in r3 / f1)
std::string cstr(uint32_t a) { return a ? mem::read_cstr(a) : std::string("(null)"); }
void svc_log(Cpu* c) { LOG("[guestmod:%s] %s",owner(c).id.c_str(),cstr(c->r[3]).c_str()); }
void svc_log_int(Cpu* c) { LOG("[guestmod:%s] %s %d",owner(c).id.c_str(),cstr(c->r[3]).c_str(),(int32_t)c->r[4]); }
void svc_log_hex(Cpu* c) { LOG("[guestmod:%s] %s %08X",owner(c).id.c_str(),cstr(c->r[3]).c_str(),c->r[4]); }
void svc_log_float(Cpu* c) { LOG("[guestmod:%s] %s %g",owner(c).id.c_str(),cstr(c->r[3]).c_str(),c->f[1].ps0); }
void svc_memcpy(Cpu* c) { memmove(mem::ptr(c->r[3]), mem::ptr(c->r[4]), c->r[5]); }
void svc_memset(Cpu* c) { memset(mem::ptr(c->r[3]), (int)(c->r[4] & 0xFF), c->r[5]); }
// ---- live options, input filters, touches (guest_live.h; QoL track M0)
std::mutex g_options_mutex;  // Loaded::options (the main thread changes live ones), g_revisions
guestlive::Revisions g_revisions;
std::mutex g_touch_mutex;    // g_touch, g_touch_queue
guestlive::Touch g_touch;
std::vector<std::pair<int,guestlive::Event>> g_touch_queue;  // (mod index, event) for the next pad read
mods::json::Value option(Cpu* c) {std::lock_guard lock(g_options_mutex);return owner(c).options.get(cstr(c->r[3]));}
void svc_config_changed(Cpu* c) {
    auto& mod=owner(c);uint64_t revision;
    {std::lock_guard lock(g_options_mutex);revision=g_revisions.revision(mod.id,cstr(c->r[3]));}
    c->r[3]=uint32_t(revision>>32);c->r[4]=uint32_t(revision);
}
bool own_function(const Loaded& mod,uint32_t fn) {
    for(uint32_t i=0;i<mod.m->func_count;++i)if(mod.m->funcs[i].addr==fn)return true;
    return false;
}
bool exchange_area(Loaded& mod) {
    if(!mod.scratch)mod.scratch=mod.heap->allocate(32);
    return mod.scratch!=0;
}
void svc_input_filter_register(Cpu* c) {
    auto& mod=owner(c);uint32_t fn=c->r[3];c->r[3]=0;
    if(fn&&(!own_function(mod,fn)||!exchange_area(mod))){LOG("[guestmod:%s] input filter refused: the callback must be the mod's own",mod.id.c_str());return;}
    mod.input_filter=fn;c->r[3]=1;
}
void svc_touch_register(Cpu* c) {
    auto& mod=owner(c);uint32_t fn=c->r[3];c->r[3]=0;
    if(fn&&(!own_function(mod,fn)||!exchange_area(mod))){LOG("[guestmod:%s] touch callback refused: the callback must be the mod's own",mod.id.c_str());return;}
    mod.touch_callback=fn;
    if(!fn){std::lock_guard lock(g_touch_mutex);g_touch.drop(int(&mod-g_loaded.data()));}
    c->r[3]=1;
}
void svc_touch_claim(Cpu* c) {
    auto& mod=owner(c);const int slot=int32_t(c->r[3]);c->r[3]=0;
    if(!mod.touch_callback||slot<0||slot>=guestlive::kSlots)return;
    const guestlive::Rect r{float(c->f[1].ps0),float(c->f[2].ps0),float(c->f[3].ps0),float(c->f[4].ps0)};
    std::lock_guard lock(g_touch_mutex);
    g_touch.claim(int(&mod-g_loaded.data()),slot,r);c->r[3]=1;
}
// wwhd_screen_point(world xyz, out xy): a point of the world where the drawn camera shows it, in layout units (guest_live.h);
// the camera_draw camera (interp::last_camera: eye +0xDC, center +0xE8, up +0xF4, fovy +0xD4) and the picture's aspect
// and FOV as drawn (aspect::layout_scale). 1 when in front of the camera, 0 otherwise (out unchanged).
void svc_project(Cpu* c) {
    const uint32_t in=c->r[3],out=c->r[4];c->r[3]=0;
    const uint32_t cam=interp::last_camera();
    if(!in||!out||cam<0x10000000u||cam>=0x50000000u)return;
    auto v3=[](uint32_t a){return guestlive::V3{float(ldf32(a)),float(ldf32(a+4)),float(ldf32(a+8))};};
    guestlive::View view;
    if(!view.set(v3(cam+0xDC),v3(cam+0xE8),v3(cam+0xF4),aspect::layout_scale(float(ldf32(cam+0xD4)))))return;
    float x,y;
    if(!view.project(v3(in),x,y))return;
    stf32(out,x);stf32(out+4,y);c->r[3]=1;
}
// ---- the microphone (mic.h): only a mod with a "permission": "microphone" option that is on now may open it
int mod_index(const Loaded& mod) { return int(&mod - g_loaded.data()); }
bool mic_option_on(const Loaded& mod) {
    std::lock_guard lock(g_options_mutex);
    for (const auto& [id, name] : mod.mic_options) {
        const auto v = mod.options.get(id);
        if (v.type == mods::json::Value::Bool && v.boolean) return true;
    }
    return false;
}
void svc_mic_open(Cpu* c) {
    auto& mod = owner(c);
    if (!mic_option_on(mod)) {
        LOG("[guestmod:%s] microphone refused: no microphone option of this mod is on", mod.id.c_str());
        c->r[3] = uint32_t(mic::kUnavailable);
        return;
    }
    mic::want(mod_index(mod), true);
    c->r[3] = uint32_t(mic::wanted(mod_index(mod)) && mic::state() == mic::kClosed ? mic::kAsking : mic::state());
}
void svc_mic_close(Cpu* c) { mic::want(mod_index(owner(c)), false); c->r[3] = 0; }
void svc_mic_state(Cpu* c) { c->r[3] = uint32_t(mic::wanted(mod_index(owner(c))) ? mic::state() : mic::kClosed); }
// wwhd_mic_pitch(f32 out[3]): hz (0 none), level, clarity of the newest window; returns the window count (u64)
void svc_mic_pitch(Cpu* c) {
    const uint32_t out = c->r[3];
    mic::Pitch p;
    uint64_t serial = 0;
    if (!mic::wanted(mod_index(owner(c))) || mic::state() != mic::kOpen || !mic::latest(p, serial)) p = {}, serial = 0;
    if (out) stf32(out, p.hz), stf32(out + 4, p.level), stf32(out + 8, p.clarity);
    c->r[3] = uint32_t(serial >> 32), c->r[4] = uint32_t(serial);
}
void svc_config_int(Cpu* c) {
    const auto v=option(c);
    if(v.type==mods::json::Value::Number&&v.number>=INT32_MIN&&v.number<=INT32_MAX&&std::floor(v.number)==v.number)c->r[3]=uint32_t(int32_t(v.number));
    else c->r[3]=c->r[4];
}
void svc_config_bool(Cpu* c) {const auto v=option(c);c->r[3]=v.type==mods::json::Value::Bool?v.boolean:!!c->r[4];}
void svc_config_float(Cpu* c) {const auto v=option(c);if(v.type==mods::json::Value::Number)c->f[1].ps0=v.number;}
void svc_config_string(Cpu* c) {
    const auto v=option(c);uint32_t dst=c->r[4],size=c->r[5];
    if(v.type!=mods::json::Value::String||!dst||!size||size>1024*1024){c->r[3]=0;return;}
    uint32_t n=uint32_t(std::min<size_t>(v.text.size(),size-1));
    memcpy(mem::ptr(dst),v.text.data(),n);mem::ptr(dst)[n]=0;c->r[3]=n;
}
void svc_malloc(Cpu* c) {c->r[3]=owner(c).heap->allocate(c->r[3]);}
void svc_free(Cpu* c) {if(!owner(c).heap->release(c->r[3]))LOG("[guestmod:%s] invalid heap free",owner(c).id.c_str());}
void svc_file_read(Cpu* c) {if((!c->r[4]&&c->r[5])||uint64_t(c->r[4])+c->r[5]>0x100000000ull){c->r[3]=UINT32_MAX;return;}c->r[3]=uint32_t(owner(c).files->read(cstr(c->r[3]),mem::ptr(c->r[4]),c->r[5]));}
void svc_file_write(Cpu* c) {if((!c->r[4]&&c->r[5])||uint64_t(c->r[4])+c->r[5]>0x100000000ull){c->r[3]=UINT32_MAX;return;}c->r[3]=uint32_t(owner(c).files->write(cstr(c->r[3]),mem::ptr(c->r[4]),c->r[5]));}
void svc_input(Cpu* c) {
    if(!c->r[3])return;
    const auto p=input::read();uint32_t a=c->r[3];
    st32(a,p.buttons);stf32(a+4,p.lx);stf32(a+8,p.ly);stf32(a+12,p.rx);stf32(a+16,p.ry);
    st32(a+20,p.touch);stf32(a+24,p.tx);stf32(a+28,p.ty);
}
void svc_logic_dt(Cpu* c) {c->f[1].ps0=double(true60::dt())/30.0;}
void svc_logic_step(Cpu* c) {uint64_t step=g_logic_step.load(std::memory_order_relaxed);c->r[3]=uint32_t(step>>32);c->r[4]=uint32_t(step);}
bool setting_buffer(const Loaded& mod,uint32_t address,uint32_t bytes) {
    return address>=mod.m->mem_base&&uint64_t(address)+bytes<=uint64_t(mod.m->mem_base)+mod.region_size;
}
std::string setting_key(const Loaded& mod,uint32_t address) {
    std::string key;
    for(uint32_t i=0;i<64;++i) {
        if(!setting_buffer(mod,address+i,1))return {};
        char byte=char(ld8(address+i));if(!byte)return key;
        if(byte<32||byte>126)return {};key+=byte;
    }
    return {};
}
void query_dungeon_room(Cpu* c, uint32_t address);
// "layout-pane:<Layout>/<pane>" (structured type 6): 64 bytes, see wwhd_game_pane in wwhd_guest.h;
// "layout-texture:<Layout>/<texture>" (same type and size): flags 4, the texture's width and height
void query_layout_pane(Cpu* c, const std::string& key, uint32_t address) {
    auto& mod = owner(c);
    c->r[3] = 0;
    if (!setting_buffer(mod, address, 64)) return;
    if (key.rfind("layout-texture:", 0) == 0) {
        uint32_t w = 0, h = 0;
        if (!game_layouts::texture_size(key.substr(15), w, h)) return;
        for (uint32_t i = 0; i < 64; i += 4) st32(address + i, 0);
        st32(address, 1); st32(address + 4, 4);
        stf32(address + 16, float(w)); stf32(address + 20, float(h));
        c->r[3] = 64;
        return;
    }
    game_layouts::Pane pane;
    if (!game_layouts::pane(key.substr(12), pane)) return;
    const uint32_t flags = (pane.visible ? 1u : 0u) | (pane.kind == "pic1" ? 2u : 0u);
    st32(address, 1); st32(address + 4, flags);
    stf32(address + 8, pane.x); stf32(address + 12, pane.y);
    stf32(address + 16, pane.width); stf32(address + 20, pane.height);
    stf32(address + 24, pane.rotation); st32(address + 28, pane.alpha);
    for (uint32_t i = 0; i < 32; ++i) st8(address + 32 + i, i < pane.texture.size() && i < 31 ? uint8_t(pane.texture[i]) : 0);
    c->r[3] = 64;
}
void svc_setting_get(Cpu* c) {
    auto& mod=owner(c);auto key=setting_key(mod,c->r[3]);
    uint32_t expected=c->r[4],destination=c->r[5],capacity=c->r[6];
    if (key == "mod-stage.current-room-map" && expected == 5) {
        c->r[3] = 0;
        if (!destination && !capacity) { c->r[3] = 808; return; }
        if (capacity >= 808) query_dungeon_room(c, destination);
        return;
    }
    if ((key.rfind("layout-pane:", 0) == 0 || key.rfind("layout-texture:", 0) == 0) && expected == 6) {  // the game's layout art (game_layouts.h)
        c->r[3] = 0;
        if (!destination && !capacity) { c->r[3] = 64; return; }
        if (capacity >= 64) query_layout_pane(c, key, destination);
        return;
    }
    if(key=="native-map-hud.v1"&&expected==7){
        c->r[3]=0;
        if(capacity<36||!setting_buffer(mod,destination,36)||ld32(destination)!=36||ld32(destination+4)!=1)return;
        uint32_t action=ld32(destination+8),argument=ld32(destination+12),id=map_hud_owner(mod);
        int result=-2;maphud::Rect rect{};
        switch(action){
        case 1:result=maphud::compasses(c,id,argument,g_map_hud_draw);break;
        case 2:result=maphud::rectangle(id,rect);if(result==1){st32(destination+20,f32_as_u32(rect.x));st32(destination+24,f32_as_u32(rect.y));st32(destination+28,f32_as_u32(rect.w));st32(destination+32,f32_as_u32(rect.h));}break;
        case 3:result=maphud::selected(c,g_map_hud_draw);break;
        case 4:result=maphud::select(c,id,argument,g_map_hud_draw);break;
        case 5:result=maphud::cancel(c,id,g_map_hud_draw);break;
        }
        st32(destination+16,uint32_t(result));c->r[3]=36;return;
    }
    if(key=="hud.boat-compasses.version"||key=="map.chart-selection.version"){
        c->r[3]=0;if(expected!=3)return;
        if(!destination&&!capacity){c->r[3]=4;return;}
        if(capacity<4||!setting_buffer(mod,destination,4))return;
        st32(destination,maphud::capability());c->r[3]=4;return;
    }
    auto value=settings::read(key);c->r[3]=0;
    if(!value||value->type()!=expected)return;
    uint32_t bytes=value->type()==settings::String?uint32_t(std::get<std::string>(value->data).size()+1):value->type()==settings::Number?8:4;
    if(!destination&&!capacity){c->r[3]=bytes;return;}
    if(capacity<bytes||!setting_buffer(mod,destination,bytes))return;
    switch(value->type()) {
        case settings::String:memcpy(mem::ptr(destination),std::get<std::string>(value->data).c_str(),bytes);break;
        case settings::Boolean:st32(destination,std::get<bool>(value->data));break;
        case settings::Unsigned:st32(destination,std::get<uint32_t>(value->data));break;
        case settings::Number:stf64(destination,std::get<double>(value->data));break;
        default:return;
    }
    c->r[3]=bytes;
}
void svc_setting_changed(Cpu* c) {
    auto& mod=owner(c);auto value=settings::read(setting_key(mod,c->r[3]));
    uint64_t revision=value?value->revision:0;c->r[3]=uint32_t(revision>>32);c->r[4]=uint32_t(revision);
}
// PCM is signed 16-bit big-endian in the guest, copied before returning.
void svc_audio_epoch(Cpu* c) {auto v=pcm::store().epoch();c->r[3]=uint32_t(v>>32);c->r[4]=uint32_t(v);}
void svc_audio_open(Cpu* c) {
    auto& mod=owner(c);c->r[3]=uint32_t(pcm::store().open(mod.id,c->r[3],c->r[4]));
    if(getenv("WWHD_AUDIO_STREAM_TRACE"))LOG("[guestpcm:%s] open %d epoch %llu",mod.id.c_str(),int32_t(c->r[3]),(unsigned long long)pcm::store().epoch());
}
void svc_audio_available(Cpu* c) {c->r[3]=uint32_t(pcm::store().available(owner(c).id,c->r[3]));}
void svc_audio_close(Cpu* c) {
    auto& mod=owner(c);uint32_t handle=c->r[3];c->r[3]=uint32_t(pcm::store().close(mod.id,handle));
    if(getenv("WWHD_AUDIO_STREAM_TRACE"))LOG("[guestpcm:%s] close %u result %d",mod.id.c_str(),handle,int32_t(c->r[3]));
}
void svc_audio_submit(Cpu* c) {
    auto& mod=owner(c);uint32_t h=c->r[3],a=c->r[4],frames=c->r[5],channels=c->r[6];
    c->r[3]=uint32_t(pcm::kInvalid);
    if(frames>pcm::kMaxSubmit||(channels!=1&&channels!=2)||!setting_buffer(mod,a,frames*channels*2))return;
    int16_t samples[pcm::kMaxSubmit*2];
    for(uint32_t i=0;i<frames*channels;++i)samples[i]=int16_t(ld16(a+i*2));
    c->r[3]=uint32_t(pcm::store().submit(mod.id,h,samples,frames,channels));
    if(getenv("WWHD_AUDIO_STREAM_TRACE"))LOG("[guestpcm:%s] submit %u requested %u accepted %d",mod.id.c_str(),h,frames,int32_t(c->r[3]));
}
// HUD services use packed big-endian guest structures, never host struct casts.
std::string hud_path(const Loaded& mod,uint32_t address) {
    std::string path;
    for(uint32_t i=0;i<512;++i){
        if(!setting_buffer(mod,address+i,1))return {};
        char ch=char(ld8(address+i));if(!ch)return path;path+=ch;
    }
    return {};
}
void svc_boat_compasses(Cpu* c){auto& m=owner(c);c->r[3]=maphud::compasses(c,map_hud_owner(m),c->r[3],g_map_hud_draw);}
void svc_boat_compasses_rect(Cpu* c){
    auto& m=owner(c);uint32_t out=c->r[3];c->r[3]=uint32_t(-2);
    if(!setting_buffer(m,out,24)||ld32(out)!=24||ld32(out+4)!=1)return;
    maphud::Rect r{};int result=maphud::rectangle(map_hud_owner(m),r);
    if(result==1){st32(out+8,f32_as_u32(r.x));st32(out+12,f32_as_u32(r.y));st32(out+16,f32_as_u32(r.w));st32(out+20,f32_as_u32(r.h));}
    c->r[3]=uint32_t(result);
}
void svc_map_chart_selected(Cpu* c){c->r[3]=maphud::selected(c,g_map_hud_draw);}
void svc_map_chart_select(Cpu* c){auto& m=owner(c);c->r[3]=maphud::select(c,map_hud_owner(m),c->r[3],g_map_hud_draw);}
void svc_map_chart_cancel(Cpu* c){auto& m=owner(c);c->r[3]=maphud::cancel(c,map_hud_owner(m),g_map_hud_draw);}
void svc_hud_register(Cpu* c) {
    auto& mod=owner(c);uint32_t callback=c->r[3],screen=c->r[4];c->r[3]=0;
    // Map/UI hooks may call HUD services: never acquire their mutex while
    // holding g_hud_mutex (native event dispatch takes them in the other order).
    if(!callback&&screen<=2)maphud::revoke(map_hud_owner(mod));
    std::lock_guard lock(g_hud_mutex);
    if(screen>2){hud::store().note(mod.id,"HUD registration failed: invalid screen");return;}
    if(callback) {
        bool found=false;for(uint32_t i=0;i<mod.m->func_count;++i)found|=mod.m->funcs[i].addr==callback;
        if(!found){hud::store().note(mod.id,"HUD registration failed: callback must belong to this mod");return;}
    }
    if(mod.hud_callback!=callback||mod.hud_screen!=screen)mod.hud_called=false;
    mod.hud_callback=callback;mod.hud_screen=screen;
    if(!callback)hud::store().drop(mod.id);
    bool any=false;for(const auto& loaded:g_loaded)any|=loaded.hud_callback!=0;
    g_hud_callbacks.store(any,std::memory_order_release);c->r[3]=1;
}
void svc_hud_texture(Cpu* c) {
    auto& mod=owner(c);uint32_t source=c->r[3];auto path=hud_path(mod,c->r[4]);c->r[3]=0;
    if(source==2) {  // the game's own layout art (game_layouts.h), read from the installed 2D pack
        game_layouts::Image image;std::string error;
        if(!game_layouts::texture(path,image,error)){hud::store().note(mod.id,"HUD game texture load failed: "+error);return;}
        c->r[3]=hud::store().create_image(mod.id,image.width,image.height,image.width*4,image.rgba.data(),image.rgba.size());
        if(!c->r[3])hud::store().note(mod.id,"HUD game texture load failed: per-mod image quota exceeded");
        return;
    }
    if(source>1||path.empty()){hud::store().note(mod.id,"HUD PNG load failed: invalid source or path");return;}
    // Package textures are deliberately restricted to the two artwork folders.
    if(source==0&&!path.starts_with("assets/")&&!path.starts_with("textures/")){hud::store().note(mod.id,"HUD PNG load failed: package images must be in assets/ or textures/");return;}
    try {
        auto pixels=hud::load_png(source?mod.data_path:mod.package_path,path);
        c->r[3]=hud::store().create_image(mod.id,pixels.width,pixels.height,pixels.width*4,pixels.rgba.data(),pixels.rgba.size());
        if(!c->r[3])hud::store().note(mod.id,"HUD PNG load failed: per-mod image quota exceeded");
    }catch(const std::exception&){hud::store().note(mod.id,"HUD PNG load failed: missing, invalid or oversized texture");}
}
// Optional read-only view of the port's existing mod-stage map.
void query_dungeon_room(Cpu* c, uint32_t address) {
    auto& mod = owner(c);
    c->r[3] = 0;
    if (!setting_buffer(mod, address, 808)) return;
    const uint32_t cached = ld32(address + 8);
    mod_stages::CornerMap room;
    if (!mod_stages::corner_map(room)) return;
    uint32_t image = 0;
    if (cached != room.revision) {
        image = hud::store().create_image(mod.id, room.width, room.height, room.width * 4,
                                          room.rgba->data(), room.rgba->size());
        if (!image) return;
    }
    st32(address, 1); st32(address + 4, image); st32(address + 8, room.revision);
    st32(address + 12, uint32_t(room.floor));
    for (unsigned i = 0; i < 4; ++i) stf32(address + 16 + i * 4, room.bounds[i]);
    const uint32_t count = uint32_t(std::min<size_t>(room.markers.size(), 64));
    st32(address + 32, count); st32(address + 36, 0);
    for (uint32_t i = 0; i < count; ++i) {
        stf32(address + 40 + i * 12, room.markers[i].x);
        stf32(address + 44 + i * 12, room.markers[i].z);
        st32(address + 48 + i * 12, room.markers[i].kind);
    }
    c->r[3] = 808;
}
void svc_hud_release(Cpu* c) {c->r[3]=hud::store().release(owner(c).id,c->r[3]);}
void svc_hud_epoch(Cpu* c) {auto epoch=hud::store().state_generation();c->r[3]=uint32_t(epoch>>32);c->r[4]=uint32_t(epoch);}
void svc_hud_record(Cpu* c,bool clip_only) {
    auto& mod=owner(c);uint32_t list=c->r[3],a=c->r[4];c->r[3]=0;
    if(!setting_buffer(mod,a,72)){hud::store().fail(mod.id,list,"HUD draw list dropped: invalid element buffer");return;}
    hud::Command command;
    command.kind=hud::Command::Kind(ld32(a));
    bool clip=command.kind==hud::Command::ClipPush||command.kind==hud::Command::ClipPop;
    if(clip!=clip_only){hud::store().fail(mod.id,list,"HUD draw list dropped: wrong recording service");return;}
    command.anchor=hud::Anchor(ld32(a+4));command.blend=hud::Blend(ld32(a+8));
    command.x=ldf32(a+12);command.y=ldf32(a+16);command.w=ldf32(a+20);command.h=ldf32(a+24);
    command.size=ldf32(a+28);command.thickness=ldf32(a+32);command.rotation=ldf32(a+36);
    command.u0=ldf32(a+40);command.v0=ldf32(a+44);command.u1=ldf32(a+48);command.v1=ldf32(a+52);
    command.rgba=ld32(a+56);uint32_t image=ld32(a+60),text=ld32(a+64),bytes=ld32(a+68);
    if(command.kind==hud::Command::Text) {
        if(bytes>hud::kMaxText||!setting_buffer(mod,text,bytes)){hud::store().fail(mod.id,list,"HUD draw list dropped: invalid text buffer");return;}
        command.text.assign(reinterpret_cast<const char*>(mem::ptr(text)),bytes);
    }
    c->r[3]=command.kind==hud::Command::Picture?hud::store().picture(mod.id,list,image,std::move(command)):
              hud::store().append(mod.id,list,std::move(command));
}
void svc_hud_emit(Cpu* c) {svc_hud_record(c,false);}
void svc_hud_clip(Cpu* c) {svc_hud_record(c,true);}
// Letters (story_mail.h). Strings must lie in the calling mod's memory; too long or unterminated
// strings make the call fail with WWHD_MAIL_INVALID.
bool mod_string(const Loaded& mod,uint32_t address,uint32_t limit,std::string& out) {
    out.clear();
    for(uint32_t i=0;i<=limit;++i){
        if(!setting_buffer(mod,address+i,1))return false;
        char ch=char(ld8(address+i));if(!ch)return true;out+=ch;
    }
    return false;
}
void svc_mail_send(Cpu* c) {
    auto& mod=owner(c);std::string id,sender,text;
    if(!mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)||!mod_string(mod,c->r[4],story_mail::kMaxSenderBytes,sender)||
       !mod_string(mod,c->r[5],story_mail::kMaxTextBytes,text)){c->r[3]=uint32_t(story_mail::kResultInvalid);return;}
    c->r[3]=uint32_t(story_mail::send(mod.id,id,sender,text));
}
void svc_mail_state(Cpu* c) {
    auto& mod=owner(c);std::string id;
    c->r[3]=uint32_t(mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)?story_mail::state(mod.id,id):story_mail::kResultInvalid);
}
// Mod charts (story_chart.h). IDs and labels must lie in the calling mod's memory.
void svc_chart_give(Cpu* c) {
    auto& mod=owner(c);std::string id;
    c->r[3]=uint32_t(mod_string(mod,c->r[3],story_chart::kMaxIdBytes,id)?story_chart::give(mod.id,id):story_chart::kResultInvalid);
}
void svc_chart_finish(Cpu* c) {
    auto& mod=owner(c);std::string id;
    c->r[3]=uint32_t(mod_string(mod,c->r[3],story_chart::kMaxIdBytes,id)?story_chart::finish(mod.id,id):story_chart::kResultInvalid);
}
void svc_chart_state(Cpu* c) {
    auto& mod=owner(c);std::string id;
    c->r[3]=uint32_t(mod_string(mod,c->r[3],story_chart::kMaxIdBytes,id)?story_chart::state(mod.id,id):story_chart::kResultInvalid);
}
// wwhd_chart_mark(chart, marker, x, z, icon, label): r3, r4, f1, f2, r5, r6; a null label is empty
void svc_chart_mark(Cpu* c) {
    auto& mod=owner(c);std::string chart,marker,label;
    if(!mod_string(mod,c->r[3],story_chart::kMaxIdBytes,chart)||!mod_string(mod,c->r[4],story_chart::kMaxIdBytes,marker)||
       (c->r[6]&&!mod_string(mod,c->r[6],story_chart::kMaxLabelBytes,label))){c->r[3]=uint32_t(story_chart::kResultInvalid);return;}
    c->r[3]=uint32_t(story_chart::mark(mod.id,chart,marker,float(c->f[1].ps0),float(c->f[2].ps0),c->r[5],label));
}
// wwhd_chart_unmark(chart, marker): a null marker removes all markers of the chart
void svc_chart_unmark(Cpu* c) {
    auto& mod=owner(c);std::string chart,marker;
    if(!mod_string(mod,c->r[3],story_chart::kMaxIdBytes,chart)||(c->r[4]&&!mod_string(mod,c->r[4],story_chart::kMaxIdBytes,marker))){
        c->r[3]=uint32_t(story_chart::kResultInvalid);return;}
    c->r[3]=uint32_t(story_chart::unmark(mod.id,chart,marker));
}
// The story notebook (story_notebook.h). Texts must lie in the calling mod's memory; null texts are empty.
bool quest_text(const Loaded& mod,uint32_t address,uint32_t limit,std::string& out) {
    out.clear();return !address||mod_string(mod,address,limit,out);
}
void quest_result(Cpu* c,int32_t r) {c->r[3]=uint32_t(r);}
// wwhd_quest_define(quest, title, reward, condition)
void svc_quest_define(Cpu* c) {
    auto& mod=owner(c);std::string id,title,reward,condition;
    if(!mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)||!mod_string(mod,c->r[4],story_notebook::kMaxTitleBytes,title)||
       !quest_text(mod,c->r[5],story_notebook::kMaxRewardBytes,reward)||!quest_text(mod,c->r[6],story_notebook::kMaxConditionBytes,condition))
        return quest_result(c,story_notebook::kResultInvalid);
    quest_result(c,story_notebook::define(mod.id,mod.name,id,title,reward,condition));
}
// wwhd_quest_step(quest, step, next, where)
void svc_quest_step(Cpu* c) {
    auto& mod=owner(c);std::string id,next,where;
    if(!mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)||!quest_text(mod,c->r[5],story_notebook::kMaxNextBytes,next)||
       !quest_text(mod,c->r[6],story_notebook::kMaxWhereBytes,where))
        return quest_result(c,story_notebook::kResultInvalid);
    quest_result(c,story_notebook::step(mod.id,id,c->r[4],next,where));
}
// wwhd_quest_place(quest, x, z): r3, f1, f2
void svc_quest_place(Cpu* c) {
    auto& mod=owner(c);std::string id;
    quest_result(c,mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)?story_notebook::place(mod.id,id,float(c->f[1].ps0),float(c->f[2].ps0))
                                                                     :story_notebook::kResultInvalid);
}
void svc_quest_no_place(Cpu* c) {
    auto& mod=owner(c);std::string id;
    quest_result(c,mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)?story_notebook::no_place(mod.id,id):story_notebook::kResultInvalid);
}
void svc_quest_known(Cpu* c) {
    auto& mod=owner(c);std::string id,text;
    quest_result(c,mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)&&quest_text(mod,c->r[4],story_notebook::kMaxKnownBytes,text)
                       ?story_notebook::known(mod.id,id,text):story_notebook::kResultInvalid);
}
// wwhd_quest_portrait(quest, path): a PNG of the package (assets/ or textures/)
void svc_quest_portrait(Cpu* c) {
    auto& mod=owner(c);std::string id,path;
    quest_result(c,mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)&&mod_string(mod,c->r[4],story_notebook::kMaxPortraitBytes,path)
                       ?story_notebook::portrait(mod.id,id,path,mod.package_path):story_notebook::kResultInvalid);
}
void svc_quest_done(Cpu* c) {
    auto& mod=owner(c);std::string id;
    quest_result(c,mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)?story_notebook::done(mod.id,id):story_notebook::kResultInvalid);
}
// wwhd_quest_carry(quest, carried)
void svc_quest_carry(Cpu* c) {
    auto& mod=owner(c);std::string id;
    quest_result(c,mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)?story_notebook::carry(mod.id,id,c->r[4]!=0):story_notebook::kResultInvalid);
}
// wwhd_quest_talk(quest, step, resident, lines, reward)
void svc_quest_talk(Cpu* c) {
    auto& mod=owner(c);std::string id,resident,lines;
    if(!mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)||!mod_string(mod,c->r[5],48,resident)||!mod_string(mod,c->r[6],story_notebook::kMaxLinesBytes,lines))
        return quest_result(c,story_notebook::kResultInvalid);
    quest_result(c,story_notebook::talk(mod.id,id,c->r[4],resident,lines,c->r[7]));
}
void svc_quest_state(Cpu* c) {
    auto& mod=owner(c);std::string id;
    quest_result(c,mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)?story_notebook::state(mod.id,id):story_notebook::kResultInvalid);
}
// wwhd_quest_set_flag(quest, n, on), wwhd_quest_flag(quest, n)
void svc_quest_set_flag(Cpu* c) {
    auto& mod=owner(c);std::string id;
    quest_result(c,mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)?story_notebook::set_flag(mod.id,id,c->r[4],c->r[5]!=0):story_notebook::kResultInvalid);
}
void svc_quest_flag(Cpu* c) {
    auto& mod=owner(c);std::string id;
    quest_result(c,mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)?story_notebook::flag(mod.id,id,c->r[4]):story_notebook::kResultInvalid);
}
// Mod items (story_items.h): IDs must lie in the calling mod's memory; counts 1..254
void svc_item_give(Cpu* c) {
    auto& mod=owner(c);std::string id;
    c->r[3]=uint32_t(mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)?story_items::give(mod.id,id,c->r[4],"its mod"):story_items::kResultInvalid);
}
void svc_item_take(Cpu* c) {
    auto& mod=owner(c);std::string id;
    c->r[3]=uint32_t(mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)?story_items::take(mod.id,id,c->r[4]):story_items::kResultInvalid);
}
void svc_item_count(Cpu* c) {
    auto& mod=owner(c);std::string id;
    c->r[3]=uint32_t(mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)?story_items::count(mod.id,id):story_items::kResultInvalid);
}
void svc_item_used(Cpu* c) {
    auto& mod=owner(c);std::string id;
    c->r[3]=uint32_t(mod_string(mod,c->r[3],story_mail::kMaxIdBytes,id)?story_items::used(mod.id,id):story_items::kResultInvalid);
}
#include "guest_actor_services.inc"
const std::unordered_map<std::string, PpcFunc> kServices = {
    {"wwhd_actor_register",svc_actor_register},{"wwhd_actor_find",svc_actor_find},{"wwhd_actor_get",svc_actor_get},
    {"wwhd_actor_combat_get",svc_actor_combat_get},{"wwhd_actor_attack_begin",svc_actor_attack_begin},{"wwhd_actor_attack_cancel",svc_actor_attack_cancel},
    {"wwhd_actor_userdata",svc_actor_userdata},{"wwhd_actor_set_position",svc_actor_set_position},{"wwhd_actor_set_rotation",svc_actor_set_rotation},
    {"wwhd_actor_set_collision",svc_actor_set_collision},{"wwhd_actor_set_animation",svc_actor_set_animation},
    {"wwhd_actor_walk",svc_actor_walk},{"wwhd_actor_walk_quest",svc_actor_walk_quest},{"wwhd_actor_stop",svc_actor_stop},
    {"wwhd_actor_particle",svc_actor_particle},{"wwhd_particle_at",svc_particle_at},
    {"wwhd_scene_music_register",svc_scene_music_register},
    {"wwhd_audio_epoch",svc_audio_epoch},{"wwhd_audio_open",svc_audio_open},
    {"wwhd_audio_available",svc_audio_available},{"wwhd_audio_submit",svc_audio_submit},{"wwhd_audio_close",svc_audio_close},
    {"wwhd_log", svc_log},       {"wwhd_log_int", svc_log_int}, {"wwhd_log_hex", svc_log_hex},
    {"wwhd_log_float", svc_log_float}, {"wwhd_config_int", svc_config_int},
    {"wwhd_config_bool",svc_config_bool},{"wwhd_config_changed",svc_config_changed},
    {"wwhd_input_filter_register",svc_input_filter_register},{"wwhd_touch_register",svc_touch_register},{"wwhd_touch_claim",svc_touch_claim},{"wwhd_screen_point",svc_project},{"wwhd_mic_open",svc_mic_open},{"wwhd_mic_close",svc_mic_close},{"wwhd_mic_state",svc_mic_state},{"wwhd_mic_pitch",svc_mic_pitch},{"wwhd_config_float",svc_config_float},{"wwhd_config_string",svc_config_string},
    {"wwhd_malloc",svc_malloc},{"wwhd_free",svc_free},{"wwhd_input_read",svc_input},
    {"wwhd_file_read",svc_file_read},{"wwhd_file_write",svc_file_write},
    {"wwhd_hud_boat_compasses",svc_boat_compasses},{"wwhd_hud_boat_compasses_rect",svc_boat_compasses_rect},
    {"wwhd_map_chart_selected",svc_map_chart_selected},{"wwhd_map_chart_select",svc_map_chart_select},{"wwhd_map_chart_cancel",svc_map_chart_cancel},
    {"wwhd_hud_register",svc_hud_register},{"wwhd_hud_texture",svc_hud_texture},
    {"wwhd_hud_clip",svc_hud_clip},{"wwhd_hud_release",svc_hud_release},{"wwhd_hud_epoch",svc_hud_epoch},{"wwhd_hud_emit",svc_hud_emit},
    {"wwhd_setting_get",svc_setting_get},{"wwhd_setting_changed",svc_setting_changed},
    {"wwhd_logic_dt",svc_logic_dt},{"wwhd_logic_step",svc_logic_step},
    {"wwhd_mail_send",svc_mail_send},{"wwhd_mail_state",svc_mail_state},
    {"wwhd_chart_give",svc_chart_give},{"wwhd_chart_finish",svc_chart_finish},{"wwhd_chart_state",svc_chart_state},
    {"wwhd_chart_mark",svc_chart_mark},{"wwhd_chart_unmark",svc_chart_unmark},
    {"wwhd_quest_define",svc_quest_define},{"wwhd_quest_step",svc_quest_step},{"wwhd_quest_place",svc_quest_place},
    {"wwhd_quest_no_place",svc_quest_no_place},{"wwhd_quest_known",svc_quest_known},{"wwhd_quest_portrait",svc_quest_portrait},
    {"wwhd_quest_done",svc_quest_done},{"wwhd_quest_state",svc_quest_state},
    {"wwhd_quest_carry",svc_quest_carry},{"wwhd_quest_talk",svc_quest_talk},
    {"wwhd_quest_set_flag",svc_quest_set_flag},{"wwhd_quest_flag",svc_quest_flag},
    {"wwhd_item_give",svc_item_give},{"wwhd_item_take",svc_item_take},{"wwhd_item_count",svc_item_count},{"wwhd_item_used",svc_item_used},
    {"memcpy", svc_memcpy},      {"memmove", svc_memcpy},       {"memset", svc_memset},
};
// the first host service a module being loaded asked for and this port doesn't have (its load error names it)
std::string g_missing_service;
PpcFunc service(const char* name) {
    auto it = kServices.find(name);
    if (it == kServices.end()) {
        if (g_missing_service.empty()) g_missing_service = name ? name : "?";
        return nullptr;
    }
    if (!strcmp(name, "wwhd_actor_particle") || !strcmp(name, "wwhd_particle_at")) g_particle_import = true;  // particle scratch
    if (!strncmp(name, "wwhd_mail_", 10)) story_mail::note_import();  // the postbox hooks are needed
    if (!strncmp(name, "wwhd_chart_", 11)) {  // the sea chart hooks are needed, and the chart list's captions
        story_chart::note_import();
        story_mail::note_import();
    }
    if (!strncmp(name, "wwhd_quest_", 11)) {  // the notebook's item menu entry, and its marker on the sea chart
        story_notebook::note_import();
        story_chart::note_import();
        story_mail::note_import();  // residents' lines go through the message hooks
    }
    return it->second;
}

const WWHDGuestHostV1 kHost = {
    sizeof(WWHDGuestHostV1), WWHD_GUEST_ABI_VERSION,
    ppc_dispatch, ppc_unimplemented, ppc_trap, ppc_timebase, ppc_fres, ppc_frsqrte, ppc_preempt,
    g_core_preempt, call_original, service,
};

// The NPC catalogue's names and spots come from the player's own game files: generated once by
// tools/story_mods/npc_catalogue.py (next to the guest builder) into the mod manager's story folder
// (packages::story_cache_directory: it follows WWHD_MOD_MANAGER_DIR and WWHD_USER_DIR, so a test run that
// isolates its manager never writes into the player's folder), for the compiled-in recipes
// (story_npc_catalogue.h); generated again when the recipes change.
bool g_story_package=false;
std::string g_notebook_icon,g_icon_cache;  // the notebook's icon and the built archives' cache (item_icons)
std::string g_story_data;  // the Story Notebook package's data folder
void prepare_npc_catalogue() {
    static bool done=false;
    if(done)return;
    done=true;
    std::string recipes;for(const auto& r:story_notebook::npc_recipes())recipes+=r.second;
    char tag[17];snprintf(tag,sizeof tag,"%016llx",(unsigned long long)std::hash<std::string>{}(recipes));
    const char* forced=getenv("WWHD_NPC_CATALOGUE");
    const std::string cache=mods::packages::story_cache_directory();
    if(cache.empty()&&!(forced&&*forced)){LOG("[notebook] NPC catalogue: the mod manager has no folder (no names from the game)");return;}
    const std::filesystem::path dir=cache.empty()?std::filesystem::path(forced).parent_path():std::filesystem::path(cache);
    const std::filesystem::path file=forced&&*forced?std::filesystem::path(forced):dir/("npc_catalogue-"+std::string(tag)+".json");
    std::error_code ec;
    if(!std::filesystem::is_regular_file(file,ec)) {
        try {
            const auto bridge=BuildBridge::installed(std::string(),false);
            const auto tool=std::filesystem::path(bridge.builder).parent_path().parent_path()/"story_mods"/"npc_catalogue.py";
            std::filesystem::create_directories(dir/"recipes",ec);
            auto command=bridge.tool_python();command.push_back(tool.string());command.insert(command.end(),{"--game",config::game_dir,"--out",file.string(),"--json"});
            for(const auto& [place,text]:story_notebook::npc_recipes()) {
                const auto recipe=dir/"recipes"/(place+".recipe.json");
                std::ofstream(recipe,std::ios::binary)<<text;
                command.insert(command.end(),{"--recipe",recipe.string()});
            }
            const auto process=host::run_process(command);
            LOG("[notebook] NPC catalogue generated from the game files: %s",process.code==0?"ok":(process.output+process.error).c_str());
        } catch(const std::exception& e) {LOG("[notebook] NPC catalogue not generated: %s",e.what());}
    }
    if(!story_notebook::load_local_npc_catalogue(file.string()))
        LOG("[notebook] NPC catalogue: no names from the game yet (residents show their description and have no spot)");
}

// The story notebook's game model, built from the Story Notebook package's .glb with the player's own
// stand-in item archive (tools/story_mods/notebook_model.py, next to the guest builder) into the
// package's data folder; built again when the .glb changes. Empty when it can't be built (the game's
// stand-in model shows).
std::string notebook_model(const std::filesystem::path& glb,const std::string& data_dir) {
    std::error_code ec;
    if(data_dir.empty()||!std::filesystem::is_regular_file(glb,ec))return {};
    std::ifstream in(glb,std::ios::binary);std::string bytes{std::istreambuf_iterator<char>(in),{}};
    char tag[17];snprintf(tag,sizeof tag,"%016llx",(unsigned long long)std::hash<std::string>{}(bytes));
    const auto out=std::filesystem::path(data_dir)/("notebook_model-"+std::string(tag)+".szs");
    if(std::filesystem::is_regular_file(out,ec))return out.string();
    try {
        const auto bridge=BuildBridge::installed(std::string(),false);
        const auto tool=std::filesystem::path(bridge.builder).parent_path().parent_path()/"story_mods"/"notebook_model.py";
        std::filesystem::create_directories(data_dir,ec);
        auto command=bridge.tool_python();command.push_back(tool.string());
        command.insert(command.end(),{"--game",config::game_dir,"--glb",glb.string(),"--out",out.string()});
        const auto process=host::run_process(command);
        LOG("[notebook] the notebook's model built from %s: %s",glb.filename().string().c_str(),process.code==0?"ok":(process.output+process.error).c_str());
    } catch(const std::exception& e) {LOG("[notebook] the notebook's model not built: %s",e.what());}
    return std::filesystem::is_regular_file(out,ec)?out.string():std::string();
}

bool load_one(const std::string& path, std::string& err,const mods::packages::GuestPackage& pkg,uint32_t base,uint32_t reserved) {
#ifdef _WIN32
    void* lib = (void*)LoadLibraryA(path.c_str());
    auto init = lib ? (WWHDGuestInitV1)(void*)GetProcAddress((HMODULE)lib, WWHD_GUEST_INIT_SYMBOL) : nullptr;
#else
    void* lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    auto init = lib ? (WWHDGuestInitV1)dlsym(lib, WWHD_GUEST_INIT_SYMBOL) : nullptr;
#endif
    if (!lib) { err = "cannot load the module"; return false; }
    struct LibraryGuard {
        void* handle;
        ~LibraryGuard() {
            if (!handle) return;
#ifdef _WIN32
            FreeLibrary((HMODULE)handle);
#else
            dlclose(handle);
#endif
        }
    } guard{lib};
    if (!init) { err = "not a guest mod module (no " WWHD_GUEST_INIT_SYMBOL ")"; return false; }
    g_missing_service.clear();
    const WWHDGuestModuleV1* m = init(&kHost);
    if (!m && !g_missing_service.empty()) {  // (a service of a newer port: rebuilding doesn't help)
        err = "this mod needs a newer version of the game: it uses " + g_missing_service + ", which this version doesn't have";
        return false;
    }
    if (!m || m->size < sizeof(WWHDGuestModuleV1) || m->abi_version != WWHD_GUEST_ABI_VERSION) {
        err = "module ABI does not match this game version: rebuild the mod";
        return false;
    }
    if(base<0x7F000000||base>=0x80000000||reserved>0x80000000-base||m->mem_base!=base||m->mem_size>reserved||pkg.heap_size>reserved-m->mem_size){err="module does not fit its assigned guest region";return false;}
    if (!validate_module(*m, pkg.id,
            [](uint32_t addr) { return ordinal_of(addr) >= 0; },
            [](uint32_t addr) {
                auto it = g_chains.find(addr);
                return it == g_chains.end() ? std::string() : it->second.replace_mod;
            }, err)) return false;
    for (auto& o : g_loaded)
        if (m->mem_base < o.m->mem_base + o.region_size && o.m->mem_base < m->mem_base + reserved) {
            err = "module memory overlaps " + o.path;
            return false;
        }
    Loaded loaded;
    loaded.path=path;loaded.package_path=pkg.path;loaded.data_path=pkg.data_path;loaded.id=pkg.id;loaded.name=pkg.name;loaded.version=pkg.version;loaded.m=m;loaded.region_size=reserved;loaded.options=pkg.options;loaded.mic_options=pkg.mic_options;
    {std::lock_guard lock(g_options_mutex);for(const auto& [key,value]:pkg.options.object)g_revisions.declare(pkg.id,key);}
    loaded.files=std::make_unique<Files>(pkg.data_path);
    uint32_t heap_base=(m->mem_base+m->mem_size+15)&~15u;
    loaded.heap=std::make_unique<Heap>(mem::ptr(heap_base),heap_base,pkg.heap_size);
    memset(mem::ptr(m->mem_base), 0, reserved);
    loaded.heap->initialize();
    if (m->image_size) memcpy(mem::ptr(m->mem_base), m->image, m->image_size);
    for (uint32_t i = 0; i < m->func_count; i++) dispatch::set(m->funcs[i].addr, m->funcs[i].fn);
    for (uint32_t i = 0; i < m->hook_count; i++) {
        const WWHDGuestHook& h = m->hooks[i];
        Chain& ch = g_chains[h.target];
        ch.addr = h.target;
        ch.ordinal = (uint32_t)ordinal_of(h.target);
        if (h.kind == WWHD_GUEST_REPLACE) { ch.replace = h.fn; ch.replace_mod = pkg.id; }
        else if (h.kind == WWHD_GUEST_HOOK_ENTRY) ch.entry.push_back(h.fn);
        else ch.ret.insert(ch.ret.begin(), h.fn);  // return hooks run in reverse load order
        g_mod_hook_flags[ch.ordinal] = 1;
        LOG("[guestmods] %s %08X", h.kind == WWHD_GUEST_REPLACE ? "replace" : h.kind == WWHD_GUEST_HOOK_ENTRY ? "entry hook" : "return hook",
            h.target);
    }
    // story quests declared in the manifest (story_notebook.h): known without running the mod's code.
    // The story system runs only with its required package, the Story Notebook (it brings the notebook
    // and its look); without it no mod's quests are declared.
    try {
        std::ifstream manifest_file(std::filesystem::path(pkg.path)/"manifest.json",std::ios::binary);
        std::string manifest_text{std::istreambuf_iterator<char>(manifest_file),{}};
        const auto manifest=mods::json::parse(manifest_text);
        if(loaded.id==story_notebook::kNotebookPackage) {
            g_story_package=true;
            const auto& look=manifest.get("notebook");
            const std::string icon=look.get("icon").string(),model=look.get("model").string();
            story_notebook::set_look(icon.empty()?std::string():(std::filesystem::path(pkg.path)/icon).string(),
                                     model.empty()?std::string():notebook_model(std::filesystem::path(pkg.path)/model,pkg.data_path));
            // the page as a game layout (story_notebook_layout.h): its description in the package
            const std::string icon_open=look.get("icon_open").string();
            story_notebook::set_icon_open(icon_open.empty()?std::string():(std::filesystem::path(pkg.path)/icon_open).string());
            const std::string layout=look.get("layout").string();
            story_notebook::set_layout_description(layout.empty()?std::string():(std::filesystem::path(pkg.path)/layout).string(),pkg.data_path);
            // the item icons (item_icons.h): the notebook's own frame (key 0) first; the mod items follow once
            // every package is loaded (the notebook is their dependency, so it loads before them)
            g_notebook_icon=icon.empty()?std::string():(std::filesystem::path(pkg.path)/icon).string();
            g_icon_cache=pkg.data_path;
            g_story_data=pkg.data_path;  // the Delivery Bag's extra pages build their layout there (story_bag.h)
        }
        if(manifest_text.find("\"quests\"")!=std::string::npos||manifest_text.find("\"charts\"")!=std::string::npos||manifest_text.find("\"letters\"")!=std::string::npos) {
            prepare_npc_catalogue();
            story_notebook::declare(loaded.id,loaded.name,manifest);
        }
        // the mod's items (story_items.h): kept in the Delivery Bag, owned in the save's story block
        std::vector<story_notebook::ManifestItem> items;std::string why;
        if(!story_notebook::parse_items(manifest,items,why))LOG("[items] %s: the manifest's items are not used: %s",loaded.id.c_str(),why.c_str());
        for(const auto& item:items)
            if(!story_items::define(loaded.id,pkg.path,item,why))LOG("[items] %s: %s",loaded.id.c_str(),why.c_str());
        if(!items.empty()){
            story_items::note_import();story_notebook::note_import();story_mail::note_import();
            LOG("[items] %s: %zu item(s) from the manifest",loaded.id.c_str(),items.size());
        }
        load_world(loaded.id,manifest,pkg.path,pkg.data_path);
    } catch(const std::exception& e) {LOG("[guestmods] %s: manifest quests not read: %s",loaded.id.c_str(),e.what());}
    g_loaded.push_back(std::move(loaded));
    guard.handle = nullptr; // module functions remain resident for the process lifetime
    LOG("[guestmods] loaded %s (translator %s): %u functions, %u hooks, guest memory %08X-%08X", path.c_str(),
        m->translator, m->func_count, m->hook_count, m->mem_base, m->mem_base + m->mem_size);
    return true;
}

struct Args {
    uint32_t r[8];
    double f[8][2];
    void save(const Cpu* c) {
        for (int i = 0; i < 8; i++) { r[i] = c->r[3 + i]; f[i][0] = c->f[1 + i].ps0; f[i][1] = c->f[1 + i].ps1; }
    }
    void load(Cpu* c) const {
        for (int i = 0; i < 8; i++) { c->r[3 + i] = r[i]; c->f[1 + i].ps0 = f[i][0]; c->f[1 + i].ps1 = f[i][1]; }
    }
};

}  // namespace

bool install_native_wait_routes(NativeWaitDispatch route) {
    return install_native_wait_routes_impl(route);
}
bool routes_owned() {return routes_owned_impl();}
NativeWaitRouteCoverage native_wait_route_coverage() {
    return native_wait_route_coverage_impl();
}

namespace {
bool native_restore_coverage_current(NativeEventObserver observer,NativeOuterReturnCallback outer,const std::array<uint64_t,3>&owners){
 std::lock_guard lock(native_hook_mutex());
 if(!observer||!outer||g_native_event_observer.load(std::memory_order_acquire)!=observer||g_native_outer_return_callback.load(std::memory_order_acquire)!=outer||!native_hooks_sealed.load()||native_hooks_fault.load()||!dispatch::native_text_sealed()||g_mod_hook_flags!=sealed_mod_flags||g_mod_bodies!=sealed_mod_bodies||g_mod_hook_count!=sealed_mod_count)return false;
 const auto routes=native_wait_route_coverage_impl();return routes.certified&&routes.outer_owner==owners&&owners[0]&&owners[1]&&owners[2];
}
}
class NativeRestoreTrackerIssuer final {
public:
 static std::unique_ptr<story_native_event_depth::Tracker>acquire(const story_native_restore_resume::Boundary&boundary,Cpu*cpu){try{
  std::lock_guard lock(native_hook_mutex());
  const auto observer=g_native_event_observer.load(std::memory_order_acquire);
  const auto outer=g_native_outer_return_callback.load(std::memory_order_acquire);
  const auto owners=native_wait_route_coverage_impl().outer_owner;
  const auto current=[observer,outer,owners]{return native_restore_coverage_current(observer,outer,owners);};
  if(!cpu||!current())return {};
  story_native_event_depth::Coverage coverage{{owners[0],owners[0],true},{owners[1],owners[1],true},{owners[2],owners[2],true},true};
  return story_native_event_depth::Tracker::restore_provisional(boundary,cpu,coverage,current);
 }catch(...){return {};}}
};
std::unique_ptr<story_native_event_depth::Tracker>acquire_native_restore_tracker(const story_native_restore_resume::Boundary&boundary,Cpu*cpu){return NativeRestoreTrackerIssuer::acquire(boundary,cpu);}

bool register_native_event_observer(NativeEventObserver observer){
 if(!observer)return false;NativeEventObserver empty=nullptr;
 return g_native_event_observer.compare_exchange_strong(empty,observer,std::memory_order_release,std::memory_order_relaxed);
}

// The end of init(): the hook registry and the game-code dispatch table take no more start-up writes.
static void seal_startup() {
#ifndef WWHD_GUEST_HOOKS_STARTUP_FIXTURE
    sealed_mod_flags=g_mod_hook_flags;sealed_mod_bodies=g_mod_bodies;sealed_mod_count=g_mod_hook_count;
    // A safety seal, kept for every start: after this, the game-code dispatch table (dispatch::set on
    // the .text range) takes no more writes. Nothing writes it after start-up (the recompiled table is
    // built in dispatch::init, guest modules load above), so without native mods it changes nothing
    // the game does; the native scene and hook checks require it (dispatch::native_text_sealed).
    if(!dispatch::seal_native_text())native_hooks_fault=true;
    native_hooks_sealed.store(true,std::memory_order_release);
#endif
}

void init() {
    std::lock_guard mutation(native_hook_mutex());
    if(guest_init_started.exchange(true)||native_hooks_sealed.load()){native_hooks_fault=true;return;}
    namespace packages=mods::packages;
    // live options (guest_live.h): a change of a "live" option reaches the running mod
    packages::set_option_observer([](const std::string& id,const std::string& option,const mods::json::Value& value,bool live) {
        if(!live)return;
        std::lock_guard lock(g_options_mutex);
        for(auto& mod:g_loaded)
            if(mod.id==id&&g_revisions.bump(id,option)){
                mod.options[option]=value;
                LOG("[guestmod:%s] live option %s changed (revision %llu)",id.c_str(),option.c_str(),(unsigned long long)g_revisions.revision(id,option));
            }
    });
    // start_guests invokes callbacks under the package-manager mutex. Resolve its
    // directory before entering those callbacks to avoid recursively locking it.
#ifdef __ANDROID__
    const auto cache=host::android::guest_module_cache();
#else
    const auto cache=(std::filesystem::path(packages::directory()).parent_path()/"GuestBuild").string();
#endif
    auto inspect=[cache](const packages::GuestPackage& pkg) {
        if(!g_mod_hook_count)exception_report::raise("Guest mods require code-mod support; rebuild and restart first");
        auto result=BuildBridge::installed(cache).run(pkg.path,0x7F000000,true,g_guest_build_name);
        if(result.get("elf_sha256").string()!=pkg.fingerprint)exception_report::raise("Guest ELF changed; review its trust confirmation again");
        const auto& size=result.get("allocation_size");
        if(size.type!=mods::json::Value::Number||size.number<=0||size.number>0x1000000||std::floor(size.number)!=size.number)
            exception_report::raise("Invalid guest module memory requirement");
        uint32_t module_bytes=uint32_t(size.number);
        if(pkg.heap_size>0x1000000-module_bytes)exception_report::raise("Guest module and heap exceed the mod region");
        return (module_bytes+pkg.heap_size+0xFFFF)&~0xFFFFu;
    };
    auto build=[cache](const packages::GuestPackage& pkg,uint32_t base) {
        if(!g_mod_hook_count)exception_report::raise("Guest mods require code-mod support; rebuild and restart first");
        auto result=BuildBridge::installed(cache).run(pkg.path,base,false,g_guest_build_name);
        if(result.get("elf_sha256").string()!=pkg.fingerprint)exception_report::raise("Guest ELF changed; review its trust confirmation again");
        const auto& memory=result.get("allocation_size");
        if(memory.type!=mods::json::Value::Number||memory.number<=0||memory.number>0x1000000||std::floor(memory.number)!=memory.number||pkg.heap_size>0x1000000-uint32_t(memory.number))
            exception_report::raise("Invalid guest module allocation size");
        uint32_t reserved=(uint32_t(memory.number)+pkg.heap_size+0xFFFF)&~0xFFFFu;
        return packages::GuestBuilt{result.get("module").string(),reserved};
    };
    packages::set_guest_builder(inspect,build,[cache](const mods::json::Value& requests) {
        auto result=BuildBridge::installed(cache).check_cached(requests,g_guest_build_name);
        const auto& valid=result.get("valid");
        if(valid.type!=mods::json::Value::Array)exception_report::raise("Invalid guest cache check result");
        std::vector<std::string> ids;for(const auto& id:valid.array) {
            if(id.type!=mods::json::Value::String)exception_report::raise("Invalid guest cache check ID");
            ids.push_back(id.text);
        }
        return ids;
    });
    packages::start_guests(inspect,[build](const packages::GuestPackage& pkg,uint32_t base) {
        auto result=build(pkg,base);std::string error;
        if(result.module.empty()||!load_one(result.module,error,pkg,base,result.allocation_size))
            exception_report::raise(error.empty()?"Guest builder returned no module":error);
    });
    // code mods the missing compiler stopped (issue #266): the Mods tab says it next to each, and shows this
    // line once over the game (overlay/mods_tab.h startup_notice)
    if(const auto notice=compiler_notice_for(packages::list());!notice.empty())LOG("[code mods] %s",notice.c_str());
    // Content packages have no ELF; their enabled startup snapshot still declares world data.
    for(const auto& pkg:packages::list()) if(pkg.kind=="content"&&pkg.active&&pkg.compatible) {
        std::ifstream file(std::filesystem::path(packages::directory())/pkg.id/"manifest.json",std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(file),{}};
        const auto manifest=mods::json::parse(text);
        const auto path=std::filesystem::path(packages::directory())/pkg.id;
        // the Story Notebook as an optional dependency: without it, the package's stages and wordless scenes play
        // and its story content stays out (no story import, so the story system doesn't go inactive for it)
        const bool story_off=!g_story_package&&story_scene_needs::story_optional(manifest);
        if(story_off){
            story_scene_needs::story_off().insert(pkg.id);
            LOG("[world] %s: the Story Notebook isn't installed: its quests and letters wait for it; its stages and scenes play",pkg.id.c_str());
            load_world(pkg.id,manifest,path.string(),packages::package_data_directory(pkg.id));
            continue;
        }
        if(manifest.get("quests").type!=mods::json::Value::Null||manifest.get("charts").type!=mods::json::Value::Null||
           manifest.get("letters").type!=mods::json::Value::Null) {
            prepare_npc_catalogue();
            story_notebook::declare(pkg.id,pkg.name,manifest);
        }
        std::vector<story_notebook::ManifestItem> items;std::string why;
        if(!story_notebook::parse_items(manifest,items,why))exception_report::raise("World items "+pkg.id+": "+why);
        for(const auto& item:items)if(!story_items::define(pkg.id,path.string(),item,why))
            exception_report::raise("World items "+pkg.id+": "+why);
        if(!items.empty()){story_items::note_import();story_notebook::note_import();story_mail::note_import();}
        load_world(pkg.id,manifest,path.string(),packages::package_data_directory(pkg.id));
    }
    // The world editor (--editor, #270): mod items and chests that hold them work while the game runs. The item
    // hooks and the chest item hook are there from the start even when no package declared an item or such a
    // chest, so an item or a chest added in the editor needs no restart (story_actors no longer leaves the
    // chest out). Inert until something uses them; only with the Story Notebook (items need it).
    if(editor::launch_mode()&&g_story_package){
        story_items::note_import();story_notebook::note_import();story_mail::note_import();story_treasure_chest::note_placed_chests();
    }
    // The item icons (item_icons.h): the notebook (key 0), then the mod items in declaration order (frames
    // 56...), built into the game's 2D pack before the game opens it; the mod items draw from those frames.
    // (test aid WWHD_TEST_ITEM_ICONS=a.png,b.png adds pictures as more items)
    if(g_story_package) {
        std::vector<item_icons::Item> icons;
        if(!g_notebook_icon.empty())icons.push_back({0,g_notebook_icon});
        for(const auto& item:story_items::installed())icons.push_back({item.key,item.icon_path});
        if(const char* extra=std::getenv("WWHD_TEST_ITEM_ICONS")) {
            std::string list=extra;uint16_t key=0xF000;
            for(size_t a=0;a<list.size();){size_t b=list.find(',',a);if(b==std::string::npos)b=list.size();
                if(b>a)icons.push_back({key++,list.substr(a,b-a)});a=b+1;}
        }
        // the world editor: spare frames for items added while the game runs (item_icons.h, live icons)
        item_icons::set_items(icons,g_icon_cache,editor::launch_mode()?item_icons::kSpareFrames:0);
        story_items::set_frame_source(item_icons::frame);
    }
    // Every service below claims game functions in the hook-flag table (g_mod_hook_flags), which only game
    // code translated with recomp.py --mod-hooks registers (setup builds it so when code mods are chosen).
    // Without it no game function can be replaced: those services stay off and the game runs as it is; the
    // packages' data above is still read (the editor opens and saves mods), the guest packages refused.
    if(!hooks_built()) {
        LOG("[guestmods] no code-mod support in this game code (translated without --mod-hooks): letters, charts, notebook, "
            "mod items, world actors, scenes%s are off",editor::launch_mode()?" and the editor's live edits":"");
        seal_startup();
        return;
    }
    // The story system runs only with its required package, the Story Notebook (packages.cpp makes every
    // quest mod depend on it); without it: no letters, takeovers, chart marks or notebook, the game as it is.
    if((story_mail::imported()||story_chart::imported()||story_notebook::imported())&&!g_story_package) {
        LOG("[guestmods] the story system is inactive: the Story Notebook package (%s) isn't installed",story_notebook::kNotebookPackage.c_str());
        return;
    }
    const char* setinfo_probe=std::getenv("WWHD_TEST_NATIVE_SETINFO_PROBE");
    const bool probe_setinfo=setinfo_probe&&std::strcmp(setinfo_probe,"1")==0;
    const bool stage_pass_through=(g_world_scenes||probe_setinfo)&&install_stage_owner_pass_through();
    if(g_world_scenes&&!stage_pass_through)
        LOG("[world] native event resource pass-through hooks unavailable; scenes retain fallback");
    if(probe_setinfo){
        if(stage_pass_through&&story_native_setinfo_diagnostic::enable())
            LOG("[native_setinfo_probe] enabled");
        else LOG("[native_setinfo_probe] unavailable");
    }
    // Letters from mods: host replacements of the postbox and message functions, only when a loaded
    // mod uses the letter services (story_mail.h).
    if(story_mail::imported())story_mail::set_text_buffers(mem::host_alloc(8192,16),8192);
    // Story progress (story_store.h): its areas in guest memory, so full save states carry them; saved
    // with the game's saves in the story block of cking.sav. Without a story mod the file's block is
    // still kept as it is.
    // (mod stages keep where a Quest Log was saved in one there too: mod_stages.h)
    const bool story=story_mail::imported()||story_chart::imported()||story_notebook::imported()||story_items::imported()||mod_stages::declared();
    if(story){uint32_t areas=mem::runtime_alloc(story_store::kMemoryBytes,16);memset(mem::ptr(areas),0,story_store::kMemoryBytes);story_store::set_memory(areas);}
    // developer override of the notebook's model archive (the Story Notebook package's setup step builds it)
    if(const char* model=getenv("WWHD_NOTEBOOK_MODEL");model&&*model)story_mail::set_notebook_model(model);
    story_mail::set_game_calls(call_original,[](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);});
    story_mail::install([](uint32_t addr) {
        auto it=g_chains.find(addr);return it==g_chains.end()?std::string():it->second.replace_mod;
    },[](uint32_t addr,PpcFunc fn) {
        int ordinal=ordinal_of(addr);
        if(ordinal<0)fatal("[guestmods] letters: %08X is not a game function",addr);
        Chain& ch=g_chains[addr];
        ch.addr=addr;ch.ordinal=uint32_t(ordinal);ch.replace=fn;ch.replace_mod="the port's letter service";
        g_mod_hook_flags[ordinal]=1;
    });
    // Mod charts: host replacements of the sea chart menu's story marks, only when a loaded mod uses
    // the chart services (story_chart.h).
    story_chart::set_game_calls(call_original,[](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);});
    if(story_chart::imported()){
        story_chart::set_entry_memory(mem::host_alloc(story_chart::kMaxCharts*0x28,16),story_chart::kMaxCharts*0x28);
        story_chart::set_caption_text([](uint32_t number,const std::u16string* text){if(text)story_mail::caption(number,text);else story_mail::uncaption();});
    }
    story_chart::install([](uint32_t addr) {
        auto it=g_chains.find(addr);return it==g_chains.end()?std::string():it->second.replace_mod;
    },[](uint32_t addr,PpcFunc fn) {
        int ordinal=ordinal_of(addr);
        if(ordinal<0)fatal("[guestmods] charts: %08X is not a game function",addr);
        Chain& ch=g_chains[addr];
        ch.addr=addr;ch.ordinal=uint32_t(ordinal);ch.replace=fn;ch.replace_mod="the port's chart service";
        g_mod_hook_flags[ordinal]=1;
    });
    // Mod treasure charts (story_treasure.h): the game's salvage point, light ring, crane and chest for a
    // declared mod chart, only when a loaded mod declares charts.
    if(story_chart::declared()){
        story_treasure::set_game_calls(call_original,[](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);});
        story_treasure::set_scratch_memory(mem::host_alloc(story_treasure::kScratchBytes,16));
        story_treasure::install([](uint32_t addr) {
            auto it=g_chains.find(addr);return it==g_chains.end()?std::string():it->second.replace_mod;
        },[](uint32_t addr,PpcFunc fn) {
            int ordinal=ordinal_of(addr);
            if(ordinal<0)fatal("[guestmods] treasure charts: %08X is not a game function",addr);
            Chain& ch=g_chains[addr];
            ch.addr=addr;ch.ordinal=uint32_t(ordinal);ch.replace=fn;ch.replace_mod="the port's treasure charts";
            g_mod_hook_flags[ordinal]=1;
        });
    }
    // Placed chests (SDK v3 "chest") that hold a mod's item: the chest item hook of the treasure charts, on its
    // own when no chart needs it (story_treasure_chest.h). Without such a chest nothing is installed.
    story_treasure_chest::set_placed_chest(story_actors::placed_chest_item);
    if(story_treasure_chest::placed_chests()){
        bool ready=story_treasure::installed();
        if(!ready){
            story_treasure::set_game_calls(call_original,[](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);});
            ready=true;
            for(const auto& [addr,fn]:story_treasure::chest_hooks()){
                auto it=g_chains.find(addr);
                if(it!=g_chains.end()&&!it->second.replace_mod.empty()){LOG("[treasure] %s replaces %08X: chests of mod items are left out",it->second.replace_mod.c_str(),addr);ready=false;continue;}
                int ordinal=ordinal_of(addr);
                if(ordinal<0)fatal("[guestmods] placed chests: %08X is not a game function",addr);
                Chain& ch=g_chains[addr];
                ch.addr=addr;ch.ordinal=uint32_t(ordinal);ch.replace=fn;ch.replace_mod="the port's placed chests";
                g_mod_hook_flags[ordinal]=1;
            }
        }
        story_treasure_chest::set_item_hook_ready(ready);
        if(ready)LOG("[treasure] placed chests can hold mod items");
    }
    // The story notebook: the item menu's Delivery Bag shows it and opens its page, only when a loaded
    // mod uses the quest services (story_notebook.h).
    story_notebook::set_game_ui(&story_notebook::game_ui());
    story_notebook::set_game_calls(call_original,[](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);});
    story_notebook::install([](uint32_t addr) {
        auto it=g_chains.find(addr);return it==g_chains.end()?std::string():it->second.replace_mod;
    },[](uint32_t addr,PpcFunc fn) {
        int ordinal=ordinal_of(addr);
        if(ordinal<0)fatal("[guestmods] notebook: %08X is not a game function",addr);
        Chain& ch=g_chains[addr];
        ch.addr=addr;ch.ordinal=uint32_t(ordinal);ch.replace=fn;ch.replace_mod="the port's story notebook";
        if(addr==native_wait_addresses()[0])native_wait_known_notebook_people=fn;
        g_mod_hook_flags[ordinal]=1;
    });
    // World actors: with declared actors, and in the --editor launch mode always (the editor places
    // residents, props, switches, doors and enemies live even when no package declares one at start-up).
    // Without --editor and without declarations nothing here runs or allocates.
    const bool world_actors=story_actors::declared()||editor::launch_mode();
    if(world_actors) {
        if(story_actor_textures::declared()){
            if(!story_actor_textures::configure([](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);},
                 [](const story_actor_textures::Texture&){return false;},prop_texture_binding_valid)||
               !story_prop_textures::configure(prop_texture_context_valid))
                exception_report::raise("Owned prop texture binding unavailable");
            story_props::set_texture_apply([](Cpu* c,uint32_t prop_token,uint32_t actor,uint32_t pid,uint32_t texture_token,uint32_t begin,uint32_t end,bool ready_only){
                uint32_t owner=0,epoch=0,generation=0;
                if(!texture_token||!story_actors::prop_texture_identity(actor,pid,owner,epoch,generation)||owner!=prop_token)return false;
                const uint32_t type=ld32(actor+0x790);
                const char* name=type==0?"Txm_tsubo02":type==3?"k_taru04":type==15?"k_taru02":nullptr;
                if(!name)return false;
                return story_prop_textures::apply(c,{actor,pid,epoch,generation,type,begin,end},4,{{name,texture_token}},ready_only);
            });
        }
        mod_actor::set_behavior_sink(behavior_sink);
        story_actors::set_game_calls(call_original,[](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);});
        story_actors::set_scratch_memory(mem::runtime_alloc(story_actors::kScratchBytes,16));
        // walking residents' route records: only with a declared route (or the editor, which adds them live)
        if(story_actors::resident_routes_declared()||editor::launch_mode())story_resident_routes::set_memory(mem::runtime_alloc(story_resident_route::kMemoryBytes,16));
        // the mod paths' records: only with a declared path (or the editor, which adds them live)
        if(story_room_paths::declared()||editor::launch_mode())story_room_paths::set_memory(mem::runtime_alloc(story_room_path::kMemoryBytes,16));
        // the borrowed walks of the villagers without one (Ub, Uw, Sa), built from the player's own files:
        // the ones a declared route needs (every one for the editor, which adds routes live)
        {std::vector<unsigned> kinds;for(unsigned k=0;k<story_resident_types::kBorrowedWalkCount;++k)if(editor::launch_mode()||story_actors::borrowed_walk_declared(k))kinds.push_back(k);
         if(!kinds.empty()&&story_resident_routes::memory()){story_resident_routes::set_clip_memory(mem::runtime_alloc(story_resident_routes::kClipMemoryBytes,16));
          for(unsigned k:kinds){const auto&w=story_resident_types::kBorrowedWalks[k];story_resident_walk_files::Clip clip;std::string why;
           if(!story_resident_walk_files::build(config::game_dir,k,clip,why))LOG("[actors] the borrowed walk %s (%s's) is not available: %s; those villagers stand",w.name,w.lender,why.c_str());
           else if(!story_resident_routes::set_clip(k,clip.fska))LOG("[actors] the borrowed walk %s does not fit its memory (%zu bytes); those villagers stand",w.name,clip.fska.size());
           else LOG("[actors] borrowed walk %s: %s's %s on %s, %u frames, stride %.1f, %zu bytes",w.name,w.lender,w.clip,w.target,clip.frames,clip.stride,clip.fska.size());}}}
        story_actors::set_scene_start(story_scenes::request);
        story_actors::set_stage_enter(mod_stages::enter_stage);
        mod_stages::set_story_actions(story_actors::actions);
        if(editor::launch_mode())story_actors::editor_live_kinds();  // --editor: native props, switches, doors and enemies placed live
        story_actors::install([](uint32_t addr) {
            auto it=g_chains.find(addr);return it==g_chains.end()?std::string():it->second.replace_mod;
        },[](uint32_t addr,PpcFunc fn) {
            int ordinal=ordinal_of(addr);
            if(ordinal<0)fatal("[guestmods] actors: %08X is not a game function",addr);
            Chain& ch=g_chains[addr];ch.addr=addr;ch.ordinal=uint32_t(ordinal);ch.replace=fn;ch.replace_mod="the port's world actors";
            const auto& wait_addresses=native_wait_addresses();
            for(size_t n=0;n<wait_addresses.size();++n)
                if(world_actor_overlap(n)&&addr==wait_addresses[n])native_wait_known_world_actor[n]=fn;
            g_mod_hook_flags[ordinal]=1;
        });
    }
    if(story_actor_textures::declared()&&!story_actor_textures::allocate_session())exception_report::raise("Owned prop texture snapshot cache allocation failed");
    // 8152/8153 have native visual and finite-emitter proof. Door debris remains a
    // private probe until a loaded bank and independent expiry proof admit it.
    // Its guest scratch only when something can ask for a burst: a code mod importing a particle service,
    // declared actors or scenes (their particle steps), or a private particle probe. Without them no
    // runtime_alloc, so a start without mods keeps the guest runtime layout (and its full save states)
    // as before the particles; burst() refuses while unconfigured.
    const bool door_probe=std::getenv("WWHD_TEST_PARTICLE_DOOR_DEBRIS_TICK")!=nullptr;
    const bool particle_probe=door_probe||std::getenv("WWHD_TEST_PARTICLE_AUCTION_SPARKLE_TICK")||
        std::getenv("WWHD_TEST_PARTICLE_BEACON_SPARKLE_TICK");
    if(g_particle_import||world_actors||g_world_scenes||particle_probe) {
        const uint32_t particle_scratch=mem::runtime_alloc(story_particles::kScratchBytes,16);
        const uint32_t particle_mask=story_particles::kAuctionSparkleMask|story_particles::kBeaconSparkleMask|
            (door_probe?story_particles::kDoorDebrisMask:0);
        if(!story_particles::configure([](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);},particle_scratch,story_particles::kScratchBytes,particle_mask))
            exception_report::raise("Curated particle scratch unavailable");
    }
    story_scenes::set_game_calls([](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);});
    if(g_world_scenes)story_scenes::set_scratch_memory(mem::runtime_alloc(story_scenes::kScratchBytes,16));
    // Mod demos (story_demo.h): their guest record and data, only when a declared scene has a demo step.
    if(g_world_scenes&&story_scenes::has_demo_steps()&&story_scenes::demo_arena_bytes())story_scenes::configure_demo(mem::runtime_alloc(story_scenes::demo_arena_bytes(),32),[](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);},call_original);
    story_scenes::set_callbacks(story_actors::actor_address,story_actors::actions);
    story_scenes::set_particle_resolver(story_actors::particle_position);
    story_scenes::set_place_resolver([](Cpu*,const std::string& mod,const std::string& quest)->std::optional<story_notebook::WorldPoint>{
        float x=0,z=0;if(!story_notebook::place_position(mod,quest,x,z))return {};return story_notebook::WorldPoint{x,0,z};
    });
    const auto scenes_replaced_by=[](uint32_t addr) {
        auto it=g_chains.find(addr);return it==g_chains.end()?std::string():it->second.replace_mod;
    };
    const auto scenes_replace=[](uint32_t addr,PpcFunc fn) {
        int ordinal=ordinal_of(addr);
        if(ordinal<0)fatal("[guestmods] scenes: %08X is not a game function",addr);
        Chain& ch=g_chains[addr];ch.addr=addr;ch.ordinal=uint32_t(ordinal);ch.replace=fn;ch.replace_mod="the port's scene player";
        g_mod_hook_flags[ordinal]=1;
    };
    story_scenes::install(call_original,scenes_replaced_by,scenes_replace);
    // The King of Red Lions' one-time line for the tracked quest (story_sail.h): the boat's execute,
    // checkForceMessage and procTalk_init, only when a declared quest step has a sail_line.
    story_notebook::install_sail([](uint32_t addr) {
        auto it=g_chains.find(addr);return it==g_chains.end()?std::string():it->second.replace_mod;
    },[](uint32_t addr,PpcFunc fn) {
        int ordinal=ordinal_of(addr);
        if(ordinal<0)fatal("[guestmods] notebook: %08X is not a game function",addr);
        Chain& ch=g_chains[addr];
        ch.addr=addr;ch.ordinal=uint32_t(ordinal);ch.replace=fn;ch.replace_mod="the port's story notebook (King of Red Lions)";
        g_mod_hook_flags[ordinal]=1;
    });
    // Story layouts (story_lyt_screen.h): a layout of our own with the item screen. Its client: the layout
    // test harness (WWHD_STORY_LYT, story_lyt_proto.cpp); nothing is hooked without one.
    if(story_notebook::imported()||g_story_package)story_notebook_layout::configure();  // the notebook's page layout (first client)
    // the Delivery Bag's extra pages for mod items (story_bag.h): a second layout with the item screen
    story_bag::set_game_calls(call_original,[](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);});
    story_bag::set_after_assign(story_item_buttons::refresh_strip);  // X/Y on a bag page: the item menu's strip shows it at once
    if(g_story_package&&!g_story_data.empty())story_bag::configure(std::filesystem::path(g_story_data)/"bag_page");
    story_lyt::set_game_calls(call_original,[](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);});
    story_lyt::install([](uint32_t,PpcFunc){});
    story_lyt_screen::set_game_calls(call_original,[](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);});
    story_lyt_screen::install([](uint32_t addr) {
        auto it=g_chains.find(addr);return it==g_chains.end()?std::string():it->second.replace_mod;
    },[](uint32_t addr,PpcFunc fn) {
        int ordinal=ordinal_of(addr);
        if(ordinal<0)fatal("[guestmods] story layout: %08X is not a game function",addr);
        Chain& ch=g_chains[addr];
        ch.addr=addr;ch.ordinal=uint32_t(ordinal);ch.replace=fn;ch.replace_mod="the port's story layout";
        g_mod_hook_flags[ordinal]=1;
    });
    // Mod items (story_items.h): the game's item tables answer for the port's item numbers, only when a
    // loaded mod declares items.
    story_items::set_game_calls(call_original,[](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);});
    if(editor::launch_mode())item_icons::set_game_calls([](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);},story_lyt_screen::safe_string("BtnItemIcon_00"));
    story_items::set_presenter(story_notebook::show_info);
    if(story_items::imported())story_mail::set_item_get_next(story_items::hooks::exec_item_get);
    // a mod treasure chart's chest (story_treasure_chest.h): its stand-in's execItemGet, before the mod items'
    // pool numbers; inert unless a chest of a mod item is pending
    story_treasure_chest::set_item_get_next(story_items::imported()?story_items::hooks::exec_item_get:nullptr,call_original);
    if(!story_treasure::chest_hooks().empty())story_mail::set_item_get_next(story_treasure_chest::hooks::exec_item_get);
    story_items::set_use_calls([](const char* how){return story_notebook::open_page(how);},story_notebook::reach);
    story_items::install([](uint32_t addr) {
        auto it=g_chains.find(addr);return it==g_chains.end()?std::string():it->second.replace_mod;
    },[](uint32_t addr,PpcFunc fn) {
        int ordinal=ordinal_of(addr);
        if(ordinal<0)fatal("[guestmods] items: %08X is not a game function",addr);
        Chain& ch=g_chains[addr];
        ch.addr=addr;ch.ordinal=uint32_t(ordinal);ch.replace=fn;ch.replace_mod="the port's mod items";
        g_mod_hook_flags[ordinal]=1;
    });
    // Mod items and the notebook on X/Y/R (story_item_buttons.h): the item use in play and the HUD's
    // button read, with the story notebook's item menu hooks.
    // held item models (story_item_models.h): their guest record only when a loaded item declares a
    // held model; without one nothing is allocated or hooked (install() keeps nothing to do)
    if(story_item_models::declared())
        story_item_models::configure(mem::runtime_alloc(story_item_models::kMemoryBytes,16),call_original,[](Cpu*c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);});
    story_items::set_model_presenter(story_item_models::request);
    // items with "hold_up": the game's item-get pose with the held model and the item's get message
    story_items::set_hold_up_presenter(story_item_models::hold_up,story_item_models::chest_hold_up);
    story_items::set_get_text(story_mail::item_get_text);
    story_item_models::set_text(story_items::arm_get_text);
    if(!story_item_models::install([](uint32_t fn){auto it=g_chains.find(fn);return it==g_chains.end()?std::string():it->second.replace_mod;},[](uint32_t fn,PpcFunc hook){int i=ordinal_of(fn);if(i<0)fatal("item model hook missing");auto&ch=g_chains[fn];ch.addr=fn;ch.ordinal=uint32_t(i);ch.replace=hook;ch.replace_mod="the port's item models";g_mod_hook_flags[i]=1;}))exception_report::raise("Item model hook conflict");
    // a mod item's get moment from a chest (story_item_get_icon.h): its icon over Link's head in the place of
    // the stand-in's card; the demo item's draw hook also asks the held models first. With mod items, chests
    // of them, or in the editor launch mode (an item added there gets its chest at once).
    story_item_get_icon::configure(call_original,[]{
        const editor::CameraView v=editor::camera_view();story_item_get_icon::Camera c;c.valid=v.valid;
        for(int i=0;i<3;i++)c.eye[i]=v.eye[i],c.center[i]=v.center[i],c.up[i]=v.up[i];
        c.fovy=v.fovy;c.aspect=v.aspect;for(int i=0;i<16;i++)c.proj[i]=v.proj[i];return c;
    },item_icons::picture);
    story_treasure_chest::set_icon_presenter([](uint16_t key,const std::string& png,bool on){if(on)story_item_get_icon::show(key,png);else story_item_get_icon::hide();});
    if(story_items::imported()||story_treasure_chest::placed_chests()||story_item_models::declared()||editor::launch_mode())
        story_item_get_icon::install([](uint32_t fn){auto it=g_chains.find(fn);return it==g_chains.end()?std::string():it->second.replace_mod;},[](uint32_t fn,PpcFunc hook){int i=ordinal_of(fn);if(i<0)fatal("[guestmods] item get: %08X is not a game function",fn);auto&ch=g_chains[fn];ch.addr=fn;ch.ordinal=uint32_t(i);ch.replace=hook;ch.replace_mod="the port's item get";g_mod_hook_flags[i]=1;});
    story_item_buttons::set_game_calls(call_original,[](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);});
    story_item_buttons::install([](uint32_t addr) {
        auto it=g_chains.find(addr);return it==g_chains.end()?std::string():it->second.replace_mod;
    },[](uint32_t addr,PpcFunc fn) {
        int ordinal=ordinal_of(addr);
        if(ordinal<0)fatal("[guestmods] item buttons: %08X is not a game function",addr);
        Chain& ch=g_chains[addr];
        ch.addr=addr;ch.ordinal=uint32_t(ordinal);ch.replace=fn;ch.replace_mod="the port's item buttons";
        g_mod_hook_flags[ordinal]=1;
    });
    story_store::set_game_calls(call_original);
    story_store::set_progress_loaded(story_item_models::progress_loaded);
    story_store::install([](uint32_t addr) {
        auto it=g_chains.find(addr);return it==g_chains.end()?std::string():it->second.replace_mod;
    },[](uint32_t addr,PpcFunc fn) {
        int ordinal=ordinal_of(addr);
        if(ordinal<0)fatal("[guestmods] story store: %08X is not a game function",addr);
        Chain& ch=g_chains[addr];
        ch.addr=addr;ch.ordinal=uint32_t(ordinal);ch.replace=fn;ch.replace_mod="the port's story progress";
        g_mod_hook_flags[ordinal]=1;
    });
    if(getenv("WWHD_GUEST_MODS"))LOG("[guestmods] WWHD_GUEST_MODS is retired; install and trust guest packages in the mod manager");
    // The world editor's scene previews: in the --editor launch mode the scene player (its scratch, the
    // demo memory, the camera trim, choice and demo hooks) is there even when no package declared a scene
    // at start-up, so a new, unsaved scene plays at once (Play scene / Play step) instead of after a
    // restart. Its runtime objects (1 KiB scratch, the demo memory) are part of full save states: a full
    // state from an --editor start of an older build without them is refused ("laid out differently");
    // portable states and starts without --editor are not affected. Without --editor nothing here runs
    // or allocates.
    if(editor::launch_mode()) {
        if(!story_scenes::scratch_memory())story_scenes::set_scratch_memory(mem::runtime_alloc(story_scenes::kScratchBytes,16));
        if(!(g_world_scenes&&story_scenes::has_demo_steps())&&story_scenes::demo_arena_bytes())story_scenes::configure_demo(mem::runtime_alloc(story_scenes::demo_arena_bytes(),32),[](Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){return guest_call(c,fn,args);},call_original);
        story_scenes::enable_editor_previews();
        story_scenes::install(call_original,scenes_replaced_by,scenes_replace);
        LOG("[editor] scene previews: ready (new scenes play without a restart)");
    }
    // Native scene playback (story_native_wait_runtime.h): only with declared world scenes or the private
    // native-wait test; otherwise no declaration catalogue, session or log line.
    g_native_scenes=g_world_scenes||std::getenv("WWHD_TEST_NATIVE_WAIT");
    if(g_native_scenes)
        NativeSceneRequestIssuer::startup(stage_pass_through,[]() -> uint64_t {
            return g_logic_step.load(std::memory_order_relaxed);
        });
    if(!g_loaded.empty())maphud::install(call_original,[](uint32_t fn,bool replacing){
        auto it=g_chains.find(fn);return ordinal_of(fn)>=0&&(!replacing||it==g_chains.end()||!it->second.replace);
    },[](uint32_t fn,PpcFunc hook){
        int ordinal=ordinal_of(fn);auto& ch=g_chains[fn];ch.addr=fn;ch.ordinal=uint32_t(ordinal);
        ch.replace=hook;ch.replace_mod="the port's map/HUD bridge";g_mod_hook_flags[ordinal]=1;
    });
    seal_startup();
}

std::vector<std::string> world_model_notes(const std::string& mod){
    std::lock_guard guard(g_world_model_notes_mutex);std::vector<std::string> out;
    for(const auto&[m,words]:g_world_model_notes)if(m==mod)out.push_back(words);
    return out;
}
void set_package_observer(PackageObserver observer){g_package_observer=observer;}

// The world editor's model import (any thread but the game's: it runs the model tool). The archive is
// prepared as load_world prepares a model at start-up; the caller registers it on the game thread.
bool editor_prepare_model(const std::string& mod,const std::string& package_dir,const std::string& data_dir,const mods::json::Value& entry,std::string& archive,std::string& why){
    story_notebook::ManifestModel model;model.id=entry.get("id").string();model.source=entry.get("source").string();model.kind=entry.get("kind").string("static");
    if(model.id.empty()||model.source.empty())return why="the model has no id or source",false;
    if(model.kind=="kit")return why="a kit model is prepared with editor_build_kit_model",false;
    const bool collision=entry.get("collision").boolean;
    if(collision&&model.kind!="static")return why="collision requires a static model",false;
    // a shoreline (preparation option, never a manifest field): "foam" or a water surface's material
    const auto& shore=entry.get("shoreline");const bool shoreline=shore.type==mods::json::Value::String;
    if(shoreline&&(collision||model.kind!="static"||(shore.text!="foam"&&!story_notebook::water_surface_material(shore.text))))return why="a shoreline is a collision-free static model with the material foam, water, pond, river, waterfall or lava",false;
    try {
        const auto bridge=BuildBridge::installed(std::string(),false);
        const auto tool=std::filesystem::path(bridge.builder).parent_path().parent_path()/"story_mods"/(model.kind=="skinned"?"skinned_actor_model.py":"actor_model.py");
        // "generation" (the editor's replace_model): an archive of its own for a file replaced while the game runs
        const unsigned generation=entry.get("generation").type==mods::json::Value::Number&&entry.get("generation").number>0&&entry.get("generation").number<1e6?unsigned(entry.get("generation").number):0u;
        archive=actor_model_cache::prepare(mod,model,package_dir,data_dir,bridge.tool_python(),tool.string(),config::game_dir,collision,shoreline,shoreline?shore.text:std::string(),generation);
        return true;
    } catch(const std::exception& e) {return why=e.what(),false;}
}

std::shared_ptr<const story_game_pieces::BuiltSet> editor_build_kit_model(const std::string& mod,const std::string& package_dir,const std::string& data_dir,const mods::json::Value& entry){
    // only the declaration: the editor's request may carry preparation options ("replace" after Replace from file, Watch
    // file or an import under a removed model's id), which a declaration doesn't take (issue #281)
    mods::json::Value manifest;manifest["models"].type=mods::json::Value::Array;manifest["models"].array.push_back(story_notebook::model_declaration(entry));
    std::vector<story_notebook::ManifestModel> models;std::string why;
    if(!story_notebook::parse_models(manifest,models,why))throw std::runtime_error(why);
    if(models.size()!=1||models[0].kind!="kit")throw std::runtime_error("not a kit model");
    const auto bridge=BuildBridge::installed(std::string());
    const auto tool=std::filesystem::path(bridge.builder).parent_path().parent_path()/"story_mods"/"kit_model.py";
    return story_kit_models::build(mod,models[0],package_dir,story_kit_models::cache_directory(data_dir),bridge.tool_python(),tool.string(),config::game_dir);
}

std::shared_ptr<const story_game_pieces::BuiltSet> editor_build_piece(const std::string& id,const std::string& cache_dir){
    const auto bridge=BuildBridge::installed(std::string(),false);
    const auto tool=std::filesystem::path(bridge.builder).parent_path().parent_path()/"story_mods"/"game_piece_model.py";
    return story_game_pieces::build(id,cache_dir,bridge.tool_python(),tool.string(),config::game_dir);
}

std::vector<ModIdentity> enabled_mods() {
    std::vector<ModIdentity> mods;for(const auto& mod:g_loaded)mods.push_back({mod.id,mod.version});return mods;
}

namespace {
bool refresh_dispatch_current()noexcept{try{
 std::lock_guard readback(native_hook_mutex());
 using namespace story_native_refresh_dispatch;
 detail::View view;
 view.hooks_sealed=native_hooks_sealed.load(std::memory_order_acquire)&&!native_hooks_fault.load();
 view.text_sealed=dispatch::native_text_sealed();view.probe_quiet=model_probe::native_refresh_quiescent();
 view.registration_exact=g_mod_hook_count==g_recomp_func_count&&g_mod_hook_count==sealed_mod_count&&g_mod_hook_flags&&g_mod_bodies&&g_mod_hook_flags==sealed_mod_flags&&g_mod_bodies==sealed_mod_bodies;
 view.route=[](uint32_t raw,detail::Route&out){const auto address=GC(raw);const int i=ordinal_of(address);if(i<0||unsigned(i)>=g_mod_hook_count)return false;out={g_recomp_funcs[i].fn,dispatch::lookup(address),g_mod_hook_flags[i]!=0,g_chains.find(address)!=g_chains.end()};return true;};
 return detail::audit(view);
}catch(...){return false;}}
}
std::optional<story_native_refresh_dispatch::Guard>acquire_native_people_refresh_dispatch(){
 if(!refresh_dispatch_current())return std::nullopt;
 return story_native_refresh_dispatch::Guard([]{return refresh_dispatch_current();});
}

namespace {
bool cancel_dispatch_current()noexcept{try{
 std::lock_guard readback(native_hook_mutex());
 if(!native_hooks_sealed.load(std::memory_order_acquire)||native_hooks_fault.load()||!dispatch::native_text_sealed()||
    g_mod_hook_count!=g_recomp_func_count||g_mod_hook_count!=sealed_mod_count||!g_mod_hook_flags||!g_mod_bodies||g_mod_hook_flags!=sealed_mod_flags||g_mod_bodies!=sealed_mod_bodies||!native_wait_route_coverage_impl().certified)return false;
 for(auto raw:story_native_manager_cancel_dispatch::required_functions()){
  const auto address=GC(raw);const int i=ordinal_of(address);
  if(i<0||unsigned(i)>=g_mod_hook_count||dispatch::lookup(address)!=g_recomp_funcs[i].fn)return false;
  auto chain=g_chains.find(address);
  if(raw==story_native_code_identity::CANONICAL_CODE_ID(0x0254465C).value()||raw==story_native_code_identity::CANONICAL_CODE_ID(0x02542C9C).value()){const size_t n=raw==story_native_code_identity::CANONICAL_CODE_ID(0x0254465C).value()?12:13;
   if(!g_mod_hook_flags[i]||chain==g_chains.end()||chain->second.ordinal!=unsigned(i)||!chain->second.entry.empty()||!chain->second.ret.empty()||chain->second.replace!=native_wait_trampolines[n]||native_wait_prior[n])return false;
  }else if(g_mod_hook_flags[i]||chain!=g_chains.end())return false;
 }
 return true;
}catch(...){return false;}}
}
std::optional<story_native_manager_cancel_dispatch::Guard>acquire_native_manager_cancel_dispatch(story_native_event_depth::Tracker&t,Cpu*c,std::shared_ptr<const threads::NativeQuiescence> quiescence){
 namespace D=story_native_event_depth;namespace A=story_native_manager_cancel_dispatch::detail;
 if(!c||!quiescence||quiescence->read_only()||!quiescence->current(c)||!cancel_dispatch_current())return {};
 const auto ticket=story_native_event_game_thread::ticket(c);if(!ticket.epoch||!story_native_event_game_thread::verify(c,ticket))return {};
 auto witness=t.current_owned_frame();if(!witness||!witness->current(t))return {};
 const auto call=t.current_owned_call();const auto depth=t.snapshot();
 if(!call||call->entry!=D::Entry::CameraExecute||call->function!=GC(0x024FFA3C)||!depth.coverage||depth.fault||depth.state!=D::State::Active||depth.camera_depth!=1||depth.npc_depth||depth.manager_depth||depth.callback_depth||depth.foreign_depth)return {};
 auto stage=std::make_shared<story_native_stage_owner::Tracker::Scan>(story_native_stage_owner_observer::tracker().try_scan());if(!stage->current())return {};
 auto held=std::make_shared<story_native_manager_cancel_dispatch::detail::PinnedStage>(std::move(stage),std::move(quiescence));
 const auto thread=std::this_thread::get_id();
 A::Read read=[](uint32_t address,std::span<uint8_t>out){if(address<mem::kMem2Start||uint64_t(address)+out.size()>mem::kMem2End)return false;std::copy_n(mem::ptr(address),out.size(),out.begin());return true;};
 A::Census census;if(!A::census(read,GD(0x101F3328),census))return {};
 return story_native_manager_cancel_dispatch::Guard([&t,c,ticket,held,thread,call=*call,depth,witness=*witness,read,census=std::move(census)]{
  if(thread!=std::this_thread::get_id()||held->quiescence->read_only()||!held->quiescence->current(c)||!witness.valid()||!witness.current(t)||!held->stage->current()||!story_native_event_game_thread::verify(c,ticket)||!cancel_dispatch_current())return false;
  const auto now=t.current_owned_call();const auto d=t.snapshot();
  if(!now||now->entry!=call.entry||now->function!=call.function||now->receiver!=call.receiver||now->subject!=call.subject||now->pid!=call.pid||now->token!=call.token||now->identity!=call.identity||now->authority!=call.authority||d.fault||!d.coverage||d.state!=depth.state||d.camera_depth!=depth.camera_depth||d.npc_depth||d.manager_depth||d.callback_depth||d.foreign_depth)return false;
  A::Census fresh;return A::census(read,GD(0x101F3328),fresh)&&fresh==census;
 });
}

bool register_native_outer_return_callback(NativeOuterReturnCallback callback){
 std::lock_guard lock(native_hook_mutex());if(!callback||native_hooks_sealed.load())return false;
 NativeOuterReturnCallback empty=nullptr;return g_native_outer_return_callback.compare_exchange_strong(empty,callback);
}
bool native_camera_run_original_current(const Cpu*c,const story_native_event_depth::Tracker&t)noexcept{try{
 const auto*f=wait_route_frame;const auto fn=GC(0x024FE3E8);
 const auto*p=native_pipeline;if(!p||p->cpu!=c||p->function!=GC(0x024FFA3C)||!p->delegating||p->started!=1||p->completed||p->invalid)return false;
 if(!c||!f||f->cpu!=c||f->function!=fn||!native_hooks_sealed.load()||native_hooks_fault.load()||!dispatch::native_text_sealed()||!native_wait_route_coverage_impl().certified||native_wait_prior[14])return false;
 const int ordinal=ordinal_of(fn);if(ordinal<0||dispatch::lookup(fn)!=g_recomp_funcs[ordinal].fn||g_mod_hook_flags!=sealed_mod_flags||g_mod_bodies!=sealed_mod_bodies||g_mod_hook_count!=sealed_mod_count)return false;
 const auto it=g_chains.find(fn);if(it==g_chains.end()||!it->second.entry.empty()||!it->second.ret.empty()||it->second.replace!=native_wait_trampolines[14])return false;
 const auto witness=t.current_owned_frame();if(!witness||!witness->current(t))return false;
 const auto&call=witness->root_call();const auto&e=witness->expected();
 if(call.entry!=story_native_event_depth::Entry::CameraExecute||call.function!=GC(0x024FFA3C)||call.receiver!=e.camera.address-0x248||call.subject!=call.receiver||call.pid!=e.camera.pid||call.token||call.identity!=e.identity||f->receiver!=e.camera.address)return false;
 return (!f->next&&!f->started&&!f->completed)||(f->next&&f->started==1&&f->completed==1);
}catch(...){return false;}}

bool native_camera_run_original_pending(const Cpu*c,const story_native_event_depth::Tracker&t)noexcept{
 const auto*f=wait_route_frame;return f&&!f->next&&!f->started&&!f->completed&&native_camera_run_original_current(c,t);
}

bool hooks_built() { return g_mod_hook_count != 0 && g_mod_hook_flags && g_mod_bodies; }

// ---- live features (guest_live.h): touches routed to the mods' claims, the input filters
int touch_route(uint32_t kind,float x,float y) {
    std::lock_guard lock(g_touch_mutex);
    const guestlive::Event e{kind,x,y};
    const int owner=g_touch.route(e);
    if(owner>=0&&size_t(owner)<g_loaded.size()&&g_touch_queue.size()<64)g_touch_queue.push_back({owner,e});
    return owner;
}
int touch_pinch(uint32_t kind,float scale,float x,float y) {
    std::lock_guard lock(g_touch_mutex);
    const int owner=g_touch.route_pinch(kind,x,y);
    if(owner>=0&&size_t(owner)<g_loaded.size()&&g_touch_queue.size()<64)g_touch_queue.push_back({owner,guestlive::Event{kind,scale,0}});
    return owner;
}
bool touch_claimed_at(float x,float y) {std::lock_guard lock(g_touch_mutex);return g_touch.owner_at(x,y)>=0;}
bool touch_claims() {std::lock_guard lock(g_touch_mutex);return g_touch.any();}
// test aid: WWHD_TEST_MOD_OPTION=frame:mod:option:json,... changes a mod's option at that (scripted) TV frame, as the
// Settings window would (packages::configure on the main thread: a live option reaches the mod)
void test_option_changes() {
    struct Change {uint64_t frame;std::string mod,option,value;bool done=false;};
    static std::vector<Change> changes=[] {
        std::vector<Change> v;
        const char* e=std::getenv("WWHD_TEST_MOD_OPTION");
        std::string s=e?e:"";
        size_t at=0;
        while(at<s.size()) {
            size_t end=s.find(',',at);if(end==std::string::npos)end=s.size();
            const std::string item=s.substr(at,end-at);at=end+1;
            size_t a=item.find(':'),b=a==std::string::npos?a:item.find(':',a+1),d=b==std::string::npos?b:item.find(':',b+1);
            if(d==std::string::npos)continue;
            v.push_back({std::strtoull(item.c_str(),nullptr,10),item.substr(a+1,b-a-1),item.substr(b+1,d-b-1),item.substr(d+1)});
        }
        return v;
    }();
    if(changes.empty())return;
    const uint64_t f=fixed_seed::script_frame(render::frame_count());
    for(auto& ch:changes) {
        if(ch.done||f<ch.frame)continue;
        ch.done=true;
        hostui::post([ch] {
            std::string error;
            mods::json::Value v;
            try{v=mods::json::parse(ch.value);}catch(...){LOG("[guestmods] test option: bad value %s",ch.value.c_str());return;}
            if(!mods::packages::configure(ch.mod,ch.option,v,error))LOG("[guestmods] test option %s.%s: %s",ch.mod.c_str(),ch.option.c_str(),error.c_str());
            else LOG("[guestmods] test option %s.%s = %s",ch.mod.c_str(),ch.option.c_str(),ch.value.c_str());
        });
    }
}
// the microphone each controller read: a mod whose option is off lets go (the device closes at once); a declined
// permission turns the asking options off again, with a notice
void mic_step() {
    for(auto& mod:g_loaded) if(!mod.mic_options.empty()&&mic::wanted(mod_index(mod))&&!mic_option_on(mod)) mic::want(mod_index(mod),false);
    mic::service();
    if(!mic::take_denied())return;
    for(auto& mod:g_loaded) {
        if(!mic::wanted(mod_index(mod)))continue;
        mic::want(mod_index(mod),false);
        for(const auto& [id,name]:mod.mic_options) {
            const std::string mod_id=mod.id,option=id,label=name;
            hostui::post([mod_id,option,label] {
                std::string error;
                if(!mods::packages::configure(mod_id,option,mods::json::Value(false),error))LOG("[guestmods] %s.%s: %s",mod_id.c_str(),option.c_str(),error.c_str());
                mods::packages::post_toast("\""+label+"\" is off again: the microphone was not allowed. You can allow it in the system settings and turn it on again.");
            });
        }
    }
}
void pad_filter(Cpu* c,input::PadState& p,bool repeat) {
    if(!repeat)test_option_changes();
    if(!repeat)mic_step();
    if(!c||repeat||g_loaded.empty())return;
    std::vector<std::pair<int,guestlive::Event>> events;
    {std::lock_guard lock(g_touch_mutex);events.swap(g_touch_queue);}
    for(const auto& [index,e]:events) {  // the touches first: a mod may claim and filter in the same read
        Loaded& mod=g_loaded[size_t(index)];
        if(!mod.touch_callback||!mod.scratch)continue;
        st32(mod.scratch,e.kind);stf32(mod.scratch+4,e.x);stf32(mod.scratch+8,e.y);
        Cpu saved=*c;
        try{guest_call(c,mod.touch_callback,{mod.scratch});}catch(...){LOG("[guestmod:%s] touch callback failed",mod.id.c_str());}
        *c=saved;
    }
    for(auto& mod:g_loaded) {  // in load order
        if(!mod.input_filter||!mod.scratch)continue;
        const uint32_t a=mod.scratch;
        st32(a,p.buttons);stf32(a+4,p.lx);stf32(a+8,p.ly);stf32(a+12,p.rx);stf32(a+16,p.ry);
        st32(a+20,p.touch);stf32(a+24,p.tx);stf32(a+28,p.ty);
        Cpu saved=*c;
        try{guest_call(c,mod.input_filter,{a});}catch(...){LOG("[guestmod:%s] input filter failed",mod.id.c_str());*c=saved;continue;}
        *c=saved;
        auto stick=[](float v){return std::isfinite(v)?std::clamp(v,-1.0f,1.0f):0.0f;};
        p.buttons=ld32(a);p.lx=stick(float(ldf32(a+4)));p.ly=stick(float(ldf32(a+8)));p.rx=stick(float(ldf32(a+12)));p.ry=stick(float(ldf32(a+16)));
    }
}

void frame(uint64_t step) {g_logic_step.store(step,std::memory_order_relaxed);hud::store().set_step(step);story_notebook::logic_step(step);}
void draw_frame(Cpu* c,uint64_t step) {
    if(!c||!g_hud_callbacks.load(std::memory_order_acquire))return;
    for(auto& mod:g_loaded) {
        uint32_t callback,screen;
        {
            std::lock_guard lock(g_hud_mutex);
            if(!mod.hud_callback||(mod.hud_called&&mod.hud_step==step))continue;
            mod.hud_step=step;mod.hud_called=true;callback=mod.hud_callback;screen=mod.hud_screen;
        }
        auto list=hud::store().begin(mod.id,screen);
        if(!list){maphud::revoke(map_hud_owner(mod));hud::store().drop(mod.id);continue;}
        Cpu saved=*c;
        g_map_hud_draw=true;
        try {guest_call(c,callback,{list});if(!hud::store().commit(mod.id,list))maphud::revoke(map_hud_owner(mod));}
        catch(...) {maphud::revoke(map_hud_owner(mod));hud::store().fail(mod.id,list,"HUD callback failed; draw list dropped");hud::store().commit(mod.id,list);}
        g_map_hud_draw=false;
        *c=saved;
    }
}
void logic_service(Cpu* c) {
    // Reached only from the game's completed logic Execute hook. Native event
    // admission may later use this live thread binding; it grants no lease.
    if(g_native_scenes)story_native_event_game_thread::observe_logic(c);
    const bool beacon=std::getenv("WWHD_TEST_PARTICLE_BEACON_SPARKLE_TICK")!=nullptr;
    const bool sparkle=!beacon&&std::getenv("WWHD_TEST_PARTICLE_AUCTION_SPARKLE_TICK")!=nullptr;
    const char* probe=beacon?"WWHD_TEST_PARTICLE_BEACON_SPARKLE_TICK":sparkle?"WWHD_TEST_PARTICLE_AUCTION_SPARKLE_TICK":"WWHD_TEST_PARTICLE_DOOR_DEBRIS_TICK";
    if(const char* at=std::getenv(probe)){
        static uint32_t tick=0;const unsigned long target=std::strtoul(at,nullptr,10);
        if(++tick==target&&target>0&&target<=18000){
            const uint32_t link=ld32(GD(0x1046F0B0)+0x5B34);
            if(!(link&3)&&link>=mem::kMem2Start&&link<=mem::kMem2End-0x330&&ld16(link+0xE)==0xA8&&ld32(link+4)&&ld32(link+4)!=0xFFFFFFFFu){
                story_particles::Burst burst;burst.effect=beacon?story_particles::Effect::BeaconSparkle:sparkle?story_particles::Effect::AuctionSparkle:story_particles::Effect::DoorDebris;
                burst.position={u32_as_f32(ld32(link+0x314)),u32_as_f32(ld32(link+0x318)),u32_as_f32(ld32(link+0x31C))};
                burst.room=int8_t(ld8(link+0x326));
                LOG("[particles] private %s probe tick=%u result=%u",beacon?"beacon-sparkle":sparkle?"auction-sparkle":"door-debris",tick,unsigned(story_particles::burst(c,burst)));
            }
        }
    }
    story_item_models::tick(c);story_item_get_icon::logic(hud::store().step());item_icons::service(c);mod_actor::heap_sample(c);actor_sweep(c);story_scenes::tick(c,false,2);story_actors::service(c);
    mod_stages::logic(c);
    story_native_wait_runtime::logic(c);
    story_scenes::demo_tick(c);
}
void service(Cpu* c) { story_lyt_screen::service(c); }
void state_loaded() {
    maphud::state_loaded();
    story_native_event_game_thread::state_loaded();
    story_native_wait_runtime::state_loaded();
    std::lock_guard lock(g_hud_mutex);
    g_actor_registry.invalidate_all();g_actor_identities.clear();
    hud::store().reset();
    pcm::store().reset();
    story_item_models::state_loaded();
    story_item_get_icon::state_loaded();
    story_mail::state_loaded();
    story_scenes::state_loaded();
    story_actors::state_loaded();
    story_treasure_chest::state_loaded();
    story_chart::state_loaded();
    story_notebook::state_loaded();
    story_bag::state_loaded();
    story_lyt_screen::state_loaded();
    story_actors::restore_talk_text(); // Hydrate only after every old host text owner has been cleared.
    for(auto& mod:g_loaded)mod.hud_called=false;
}

}  // namespace guestmods
namespace mods {
void guest_pad_filter(Cpu* c, input::PadState& p, bool repeat) { guestmods::pad_filter(c, p, repeat); }
bool guest_touch_claimed_at(float x, float y) { return guestmods::touch_claimed_at(x, y); }
}  // namespace mods

using namespace guestmods;

// A hooked or replaced game function. The calling convention is the game's: hooks get the function's
// arguments (r3..r10, f1..f8 are restored for every hook), a return hook also leaves the return value
// (r3, r4, f1) as the function produced it. r1/r2/r13 are preserved by every callee (ABI).
extern "C" void ppc_mod_run(Cpu* c) {
    uint32_t addr = c->pc;
    auto it = g_chains.find(addr);
    if (it == g_chains.end()) fatal("[guestmods] %08X flagged without hooks", addr);
    const Chain& ch = it->second;
    guestmods::observed_native(c,addr,false,[&]{
    story_native_stage_owner_hooks::invoke_dispatch(c,addr,[&]{
    Args a;
    a.save(c);
    for (PpcFunc h : ch.entry) {
        h(c);
        a.load(c);
    }
    if (ch.replace) ch.replace(c);
    else { c->mod_skip = addr; story_native_stage_owner_hooks::invoke_original(c,addr,[&]{guestmods::original_started(c,addr);g_mod_bodies[ch.ordinal](c);guestmods::original_completed(c,addr);}); }
    if (!ch.ret.empty()) {
        uint32_t r3 = c->r[3], r4 = c->r[4];
        double f1a = c->f[1].ps0, f1b = c->f[1].ps1;
        for (PpcFunc h : ch.ret) {
            a.load(c);
            h(c);
        }
        c->r[3] = r3; c->r[4] = r4; c->f[1].ps0 = f1a; c->f[1].ps1 = f1b;
    }
    });
    });
}
