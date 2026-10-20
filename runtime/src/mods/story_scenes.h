// SDK v3 preview scene runtime. Main game thread only; installed only for declared scenes.
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>
#include <optional>
#include <utility>
#include "story_world_manifest.h"
#include "story_scenes_core.h"
struct Cpu;
namespace input {struct PadState;}
namespace story_native_scene_playback {class Activation;}
namespace demo_scene {struct Demo;}
namespace story_scenes {
inline constexpr uint32_t kScratchBytes = 1024;
using Resolve = std::function<uint32_t(Cpu*,const std::string&,const std::string&)>;
using ResolvePlace = std::function<std::optional<story_notebook::WorldPoint>(Cpu*,const std::string&,const std::string&)>;
using ResolveParticle = std::function<bool(Cpu*,const std::string&,const std::string&,unsigned,story_notebook::WorldPoint&)>;
enum class MusicPhase {Start,Stop,Restore};
using MusicHook=std::function<bool(Cpu*,const std::string&,MusicPhase,unsigned,StopReason)>;
// A real owned callback must implement the cue and restore its own prior state.
// No retail BGM ID or implicit successful no-op is provided by this hook.
bool register_music_hook(const std::string& owner,MusicHook);
void set_place_resolver(ResolvePlace);
void set_particle_resolver(ResolveParticle);
using Actions = std::function<bool(const std::string&,const std::vector<story_notebook::StoryAction>&)>;
void set_game_calls(uint32_t (*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>));
bool has_demo_steps();
bool any_demo_step(const std::function<bool(const demo_scene::Demo&)>& test);  // a declared demo step passes the test  // a declared scene has a "demo" step (story_demo.h installs only then)
// The demo-step backend (story_demo.cpp registers itself at startup), so targets that link the scene
// player without it still build: a demo step then refuses to start.
struct DemoBackend {
    uint32_t arena_bytes = 0;
    void (*configure)(uint32_t arena, uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>), void (*original)(Cpu*, uint32_t)) = nullptr;
    bool (*configured)() = nullptr;
    void (*install)(const std::function<std::string(uint32_t)>&, const std::function<void(uint32_t, void (*)(Cpu*))>&) = nullptr;
    bool (*start)(Cpu*, const demo_scene::Demo&, bool camera_stopped, std::function<uint32_t(const std::string&)> characters, std::string& why) = nullptr;
    int (*poll)(Cpu*, std::string& why) = nullptr;  // 0 idle, 1 running, 2 done, 3 failed
    void (*release)(Cpu*, const char* reason) = nullptr;
    void (*tick)(Cpu*) = nullptr;
};
void register_demo_backend(const DemoBackend& backend);
uint32_t demo_arena_bytes();  // 0 without a backend
void configure_demo(uint32_t arena, uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>), void (*original)(Cpu*, uint32_t));
void demo_tick(Cpu* c);       // once per logic step
void set_scratch_memory(uint32_t guest);
bool scratch_memory();  // set_scratch_memory was called
// The world editor (--editor, before install): the scene player, its camera trim, choice and demo hooks
// are installed even when no package declares a scene at start-up, so a new, unsaved scene plays as a
// preview at once (editor_preview). The caller provides the scratch and demo memory as for declared scenes.
void enable_editor_previews();
bool editor_previews();
void set_callbacks(Resolve resolve, Actions actions);
// False with a precise reason when a scene uses a currently unsupported effect.
bool validate(const std::vector<story_notebook::ManifestScene>& scenes,std::string& why);
bool declare(const std::string& mod,const std::vector<story_notebook::ManifestScene>& scenes,std::string& why);
// Startup batch rollback; never mutates an installed or active scene player.
// Pure copy from the registered immutable declaration set. Call only after the
// startup world batch has committed; never authenticates a live native lease.
bool declared_scene(const std::string& mod,const std::string& scene,story_notebook::ManifestScene&);
// Pure copied IDs only; not commit, startup, actor or native authority.
using NativeSceneId=std::pair<std::string,std::string>; // module, scene
std::vector<NativeSceneId> native_scene_ids();
size_t checkpoint();
bool rollback(size_t checkpoint) noexcept;
// Requests wait for the current talk/game event to finish; bounded FIFO, one scene at a time.
bool request(const std::string& mod,const std::string& scene);
void trigger(const std::string& kind,const std::string& mod,const std::string& id,unsigned step=0);
void install(void (*original)(Cpu*,uint32_t),const std::function<std::string(uint32_t)>& replaced_by,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
// frames: 60 Hz timeline units; full 30 Hz base logic services pass 2.
void tick(Cpu* cpu,bool skip=false,unsigned frames=1);
void mute(input::PadState& pad);
void state_loaded();
void abort();
bool active();
// Real frontend ownership predicates, not native leases. Runtime boundaries
// may distinguish their exact native activation from fallback scene activity.
bool native_owner(const story_native_scene_playback::Activation&) noexcept;
bool fallback_active() noexcept;
size_t current_step();
// World editor (main game thread, outside tick): plays `scene` of `mod` as a preview. `declared`: the
// declared scene of that id is requested (its native version may play). Otherwise the steps play from an
// editor-only entry "editor-preview" (no trigger, never native) that replaces the mod's previous one.
// False with why when no scene can start (scene support not installed at start-up: no declared scene
// and not the editor's enable_editor_previews, a scene running, a choice without the choice hooks).
bool editor_preview(const std::string& mod,const story_notebook::ManifestScene& scene,bool declared,std::string& why);
// The next tick skips the current scene (as Plus/Start does).
void editor_skip();
// Whether `mod`'s scene `id` waits in the queue or plays; `step` the fallback step it is at.
bool editor_state(const std::string& mod,const std::string& id,bool& waiting,bool& playing,bool& native,size_t& step);
} // namespace story_scenes
