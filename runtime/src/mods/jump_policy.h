// SPDX-License-Identifier: MPL-2.0
// Jump anywhere (jump.cpp): which of Link's procedures may start a jump, and the decision per step.
// Pure logic, so jump_policy_test checks it without the game.
// Idea from GreenNaugahyde/ZeldaWWHDRecompAndroid (MPL-2.0), mods/jump.cpp.
#pragma once
#include <algorithm>
#include <cstdint>

namespace mods::jump_policy {

// daPy_lk_c::daPy_PROC ids (the WWHD ids match the GameCube ones)
constexpr uint32_t kProcAutoJump = 0x24, kProcFall = 0x27;

// On the ground: standing, walking, running, Z-targeting, turning and landing. Not in events, while
// swimming, climbing, hanging, crawling or on ladders (those procedures are not listed).
inline bool ground_proc(uint32_t p) {
    switch (p) {
        case 0x04: case 0x05:             // WAIT, FREE_WAIT
        case 0x06: case 0x07:             // MOVE, ATN_MOVE
        case 0x08: case 0x09:             // ATN_ACTOR_WAIT, ATN_ACTOR_MOVE
        case 0x0B: case 0x23: case 0x25:  // SIDE_STEP_LAND, BACK_JUMP_LAND, LAND
        case 0x17: case 0x18:             // WAIT_TURN, MOVE_TURN
            return true;
        default: return false;
    }
}
// In the air, where the double jump may start: the jump itself, a fall, a back flip, a side hop.
inline bool air_proc(uint32_t p) {
    switch (p) {
        case 0x24: case 0x27:  // AUTO_JUMP, FALL
        case 0x22: case 0x0A:  // BACK_JUMP, SIDE_STEP
        case 0x29: case 0x2A:  // SMALL_JUMP, VERTICAL_JUMP
            return true;
        default: return false;
    }
}

// Steps (30 Hz) after Link left the ground without jumping (ran off an edge, or the fall that
// replaced a ledge jump) in which the button still counts as the jump from the ground.
constexpr int kGraceSteps = 6;
// The jump from the ground is a little higher than the game's ledge jump (12.5 from standing,
// 23.7 at full run); the double jump keeps the game's.
constexpr float kGroundBoost = 1.15f, kGroundMin = 16.f;
inline float ground_speed_y(float game_speed_y) { return std::max(game_speed_y * kGroundBoost, kGroundMin); }

enum class Action { kNone, kJump, kGraceJump, kDoubleJump };

// Host state across Link's steps (full 30 Hz steps only; true 60 previews never change it).
struct State {
    bool own = false;       // the jump under way is ours, not the game's ledge jump
    bool doubled = false;   // the double jump was used in this airtime
    int since_ground = -1;  // full steps since Link was last on the ground; -1: no grace jump
    bool held = false;      // the button at the last full step
};

struct Input {
    uint32_t proc;
    bool button;      // the jump button is held now
    bool blocked;     // event, menu, swimming, title screen or the moon jump on the same button
    bool double_jump; // the option
};

// One full step, before Link's execute: what to do. Updates the state.
inline Action step(State& s, const Input& in) {
    const bool press = in.button && !s.held;
    s.held = in.button;
    if (ground_proc(in.proc)) {
        s.doubled = false;
        s.since_ground = 0;
    } else if (s.since_ground >= 0) {
        ++s.since_ground;
    }
    if (!press || in.blocked) return Action::kNone;
    const bool just_left = in.proc == kProcFall && !s.own && !s.doubled && s.since_ground > 0 && s.since_ground <= kGraceSteps;
    if (ground_proc(in.proc) || just_left) {
        s.since_ground = -1;  // one grace jump per edge
        s.own = true;
        return just_left ? Action::kGraceJump : Action::kJump;
    }
    if (air_proc(in.proc) && in.double_jump && !s.doubled) {
        s.doubled = true;
        s.own = true;
        return Action::kDoubleJump;
    }
    return Action::kNone;
}

// After Link's execute (full step): whether the game's own ledge jump just began and should become a
// plain fall (option "no automatic ledge jumps"). Updates `own` (our jump ended when the proc changed).
inline bool after_step(State& s, uint32_t proc, bool no_auto_jump) {
    if (proc != kProcAutoJump) {
        s.own = false;
        return false;
    }
    return !s.own && no_auto_jump;
}

}  // namespace mods::jump_policy
