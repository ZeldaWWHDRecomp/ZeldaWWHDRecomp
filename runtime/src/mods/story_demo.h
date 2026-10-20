// SPDX-License-Identifier: MPL-2.0
// Mod demos through the game's own demo system: a scene's "demo" step (demo_scene.h) is compiled to a
// JStudio STB and started by a generated event (demo_event_image.h) whose PACKAGE staff loads it from
// a virtual demo archive. The game plays it, removes it when the data ends and ends the cut; the
// port only restores the stage's event list afterwards. Every file the demo uses comes from the
// player's own game archives through the virtual archive (resource id bridge).
//
// Guest record: the state of a running demo lives in guest memory (part of full save states), so a
// state loaded mid-demo is released from that record, never from host memory alone.
// Installed only when a loaded mod declares a demo step; retail demos (and the title screen's)
// never see the hooks change anything.
#pragma once
#include "demo_scene.h"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

struct Cpu;
namespace story_demo {
inline constexpr uint32_t kArenaBytes = 0xF80 + 0xC000 + 0x80 + 0x100 + 0x40 + 0xC00 + 0x10;  // ... + light, attach, scratch, large event image, kept words
inline constexpr char kVirtualArchive[] = "ModDemo";  // <= 11 characters: mDemoArcName is char[12]
using Call = uint32_t (*)(Cpu*, uint32_t, std::initializer_list<uint32_t>);
void configure(uint32_t arena, Call call, void (*original)(Cpu*, uint32_t));
bool configured();
// The files of an object archive by the game's resource id table ("dk.bdl", "fly1.bck"; unnamed ids left out):
// static game data, readable from any thread once the game code is loaded; empty for an unknown archive.
std::vector<std::string> resource_names(const std::string& archive);
void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace);

// Start a demo step. The caller (the scene player) already owns a compulsory event. Checks every
// participant and archive file first and touches nothing when it refuses (why says what is missing).
using CharacterResolver = std::function<uint32_t(const std::string&)>;  // a mod-placed actor by its id, or 0
bool start(Cpu* c, const demo_scene::Demo& demo, bool camera_stopped, CharacterResolver characters, std::string& why);
struct Poll {
    enum class State { Idle, Running, Done, Failed } state = State::Idle;
    std::string why;  // State::Failed
};
// Messages are the game's own demo messages (JMSG, then a suspend point): the port arms each
// message's text before its point and opens the game's demo message window there (the game's demo
// message flag); the game shows it and resumes the demo when the player closes it.
Poll poll(Cpu* c);
// Ends a demo early (skip, abort, state load) and restores the stage's event list. Safe to call
// any time; does nothing without a demo record in guest memory.
void release(Cpu* c, const char* reason);
bool active();       // a demo record exists in guest memory (not counting one that is being cleared)
void tick(Cpu* c);   // once per logic step: finishes clearing a released demo (archives back after a few frames)
}  // namespace story_demo
