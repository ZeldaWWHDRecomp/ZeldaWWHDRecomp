// Mod SDK v2 runtime. Guest modules load only at startup, through the mod manager's trust flow.
#pragma once
#include <cstdint>
#include <array>
#include <functional>
#include <memory>
#include "guest_identity.h"
#include "mod_json.h"
#include <string>
#include "story_native_refresh_dispatch_guard.h"
#include "story_native_manager_cancel_dispatch_guard.h"
struct Cpu;
namespace story_native_event_depth {class Tracker;struct Call;}
namespace story_native_outer_return {class OuterReturn;}
namespace story_native_restore_resume {class Boundary;}
namespace guestmods {
// Internal observer capability only; registration does not install hooks, change
// owners, publish native events or enable any public route. Immutable once set.
// Resolver is pure/noexcept and supplies a persistent Tracker and fresh native
// identity for an actual outer; false means ordinary unobserved delegation.
using NativeEventObserver=bool(*)(const Cpu*,uint32_t,story_native_event_depth::Tracker*&,story_native_event_depth::Call&) noexcept;
bool register_native_event_observer(NativeEventObserver);
// Synchronous post-dispatch private cleanup boundary; no public activation.
using NativeOuterReturnCallback=void(*)(Cpu*,const story_native_outer_return::OuterReturn&) noexcept;
bool register_native_outer_return_callback(NativeOuterReturnCallback);
// Provisional Draining tracker only, from the genuine post-restore boundary
// and actual sealed observer/routes. Never supplies a ready native lease.
std::unique_ptr<story_native_event_depth::Tracker>acquire_native_restore_tracker(
 const story_native_restore_resume::Boundary&,Cpu*);
// Same genuine CameraRun route before delegation or after exactly one completed original.
bool native_camera_run_original_current(const Cpu*,const story_native_event_depth::Tracker&) noexcept;
bool native_camera_run_original_pending(const Cpu*,const story_native_event_depth::Tracker&) noexcept;
// Private WAIT route capability. Install once before guest threads start, after
// all regular guest/port hooks have been registered. `next` runs the prior
// known world-actors replacement (including its staff-candidate filter) or
// the native original exactly once. A route
// may call it itself, or return Delegate to have the runner call it.
inline constexpr uint32_t kNativeWaitRouteCount=21;
enum class NativeWaitDecision { Delegate, Handled };
using NativeWaitNext = std::function<void()>;
using NativeWaitDispatch = NativeWaitDecision (*)(Cpu*,uint32_t,
                                                   const NativeWaitNext&);
struct NativeWaitRouteCoverage {
    uint32_t requested{}, installed{}, world_actors_composed{};
    bool certified{};
    uint32_t notebook_composed{};
    // Host-only identities of the three certified outer route trampolines.
    // Never persist these addresses in guest memory or a snapshot.
    std::array<uint64_t,3> outer_owner{};
};
bool install_native_wait_routes(NativeWaitDispatch);
bool routes_owned();
NativeWaitRouteCoverage native_wait_route_coverage();
bool hooks_built();  // marker registered by the running generated game code, never the setting
inline constexpr uint32_t kRegionStart=0x7F000000,kRegionSize=0x01000000;
std::vector<ModIdentity> enabled_mods();
void init(); // after dispatch::init, before guest threads start
// Called for each loaded package with world content or quests (the world editor's inspector); set before init.
using PackageObserver=void(*)(const std::string& mod,const std::string& name,const std::string& dir,const mods::json::Value& manifest);
void set_package_observer(PackageObserver observer);
// World editor: prepares an imported model of `mod` (its manifest entry) for the game; `archive` names it.
bool editor_prepare_model(const std::string& mod,const std::string& package_dir,const std::string& data_dir,const mods::json::Value& model,std::string& archive,std::string& why);
void logic_service(Cpu* cpu); // once per game logic pass, never interpolation-only
void frame(uint64_t step); // original full-step clock
void draw_frame(Cpu* cpu,uint64_t executed_step); // once per actual logic pass, including true60
void state_loaded(); // discard host HUD lists and texture handles after a full state load
void service(Cpu* cpu); // at the frame boundary (main thread, other threads idle): story layouts' builds after a load
}
