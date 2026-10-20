// SPDX-License-Identifier: MPL-2.0
// The native enemies added after the Rat, one row each: the game's class (profile, size, methods,
// vtable, colliders, enemyice/enemyfire helpers), the fixed parameter word and two pure checks: the
// fields that prove a created actor is the declared one, and the state in which its native drop is
// its defeat. story_enemies, story_enemy_ownership and the profiles read this table, so an enemy
// added here needs no change to their per-kind code. Addresses are canonical (GC/GD apply at use).
// The research behind each row (switches, save bits, children, effects on Link) is in its comment.
#pragma once
#include "story_enemy_ownership.h"
#include "ppc.h"
#include "guest_addr.h"
#include "story_native_canonical_code_id.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>

namespace story_enemies_more {
using story_enemy_ownership::Collider;
using story_enemy_ownership::Descriptor;
using story_enemy_ownership::Kind;
using story_native_code_identity::CANONICAL_CODE_ID;

// The private profile ids of the kinds added from the Kargaroc on, in one place: from 0x7FDF downward
// (the reserved range reaches down to 0x7FD0). Earlier rows keep the ids written in their rows.
inline constexpr uint16_t kKargarocProfile = 0x7FDF, kWizzrobeProfile = 0x7FDE, kWarshipProfile = 0x7FDD,
                          kGyorgProfile = 0x7FDC, kFloormasterProfile = 0x7FDB, kBigOctoProfile = 0x7FDA,
                          kWallCannonProfile = 0x7FD9;

struct Native {
    Kind kind;
    const char* name;           // the manifest's actor
    Descriptor descriptor;      // private profile, native profile, class size, owned colliders
    uint32_t create, execute, draw, destroy;  // the class's methods (canonical code: GC applies at use)
    uint32_t (*vtable)();       // the class vtable at actor+0xB4 (mapped data)
    uint32_t params;            // the default variant's parameter word
    uint16_t home_z;            // home angle z
    uint32_t ice;               // the enemyice helper (0: none)
    uint32_t fire[2];           // the enemyfire helpers (0: none)
    bool offered;               // placeable by mods (false: adapter kept, not offered, see its comment)
    bool (*verify)(uint32_t actor, uint32_t params);  // pure bounded reads after its create
    bool (*terminal)(uint32_t actor);                 // its native drop (fopAcM_createDisappear) is its defeat
    bool (*liftable)(uint32_t actor);                 // Link may lift it now (nullptr: never)
    uint32_t action;            // its action field, for traces (u8; u16 when action16)
    bool action16;
    uint16_t home_x = 0;        // home angle x (0: the default)
    int8_t path_shift = -1;     // where its parameter word holds a room path number (a mod path, story_room_paths); -1: none
};

inline int8_t s8(uint32_t a) { return int8_t(ld8(a)); }

// ---------------------------------------------------------------- Stalfos (d_a_st)
// Parameter 0xFFFFFF00: behaviour 0 (stands and fights; never the ambush or coffin ones, which read the
// ambush switch), sight byte 0xFF, ambush switch 0xFF; angle z 0: no death switch (never isSwitch,
// onSwitch or the boss status). setID 0xFFFF makes its onActor a no-op. It never writes Link.
// Children: its club (Boko 0x1CF, parameter 2) is an owned child (story_enemy_children) and is deleted
// with it (the game leaves it lying; owned, Link could never take it). A spin attack that empties its
// body would split it, creating a second native Stalfos (0xBE, 0xE: the upper body) with the game's own
// profile, drops and colliders; that create is refused and the Stalfos collapses as from any other
// finishing blow instead (the head must still be hit). Colliders on mStts 0x62C: head 0x668, body
// 0x794..0xD70, attack 0xE9C (Link and "other"; the pair policy keeps Link only); enemyice 0x21F0.
inline constexpr Collider kStalfosColliders[] = {
    {0x668, 0x62C, 0}, {0x794, 0x62C, 0}, {0x8C0, 0x62C, 0}, {0x9EC, 0x62C, 0}, {0xB18, 0x62C, 0},
    {0xC44, 0x62C, 0}, {0xD70, 0x62C, 0}, {0xE9C, 0x62C, 0}, {0x225C, 0x2220, 0x21F0}};
inline bool stalfos_verify(uint32_t a, uint32_t p) {
    const unsigned action = ld16(a + 0x3E2);
    return ld8(a + 0x3D0) == (p & 0xF) && ld8(a + 0x3D1) == ((p >> 8) & 0xFF) && ld8(a + 0x3D2) == ((p >> 16) & 0xFF) &&
           ld8(a + 0x3D3) == (p >> 24) && ld8(a + 0x3D4) == 0 && ld8(a + 0x25AD) == 0 &&
           (action == 0 || action == 5 || action == 29 || action == 30 || action == 31);
}
// the head knocked off after the collapse (head part state 8), or the collapse that timed out
inline bool stalfos_terminal(uint32_t a) { return ld8(a + 0x1648) == 8 || (ld16(a + 0x3E2) == 30 && ld16(a + 0x414) == 0); }
// the refused split: the body is down (LOWER_BODY with no upper body); it becomes the ordinary collapse
// (BREAK_WAIT 30, sub-state 0, parts command 1, lifetime 0, body health 0), like the other finishing blows
inline bool stalfos_refused_split(uint32_t a) { return ld16(a + 0x3E2) == 33 && ld32(a + 0x201C) == 0xFFFFFFFFu; }
inline void stalfos_collapse(uint32_t a) {
    st16(a + 0x3E2, 30); st16(a + 0x3E0, 0); st8(a + 0xFEF, 1); st16(a + 0x414, 0); st8(a + 0x20CC, 0);
}
inline constexpr uint16_t kStalfosClub = 0x1CF;     // the club's profile (Boko)
inline constexpr uint32_t kStalfosClubParams = 2;   // Type_STALFOS_MACE
inline constexpr uint16_t kStalfosUpperBody = 0xBE; // the split's second Stalfos (refused)

// ---------------------------------------------------------------- Redead (d_a_rd)
// Parameter 0xFFFFFFFE: standing (bit 0 clear), its own radius (0x7F: 650), the ordinary mode (byte 1
// non-zero: never the coffin, whose wait reads switch byte 3), switch 0xFF. No children, no save writes
// (setID 0xFFFF). Its scream and its bite are the stage's events DEFAULT_RD_CRY / DEFAULT_RD_ATTACK:
// while one runs Link is held in his demo state until the event ends. The game ends them only from the
// Redead's own execute, so story_enemies ends the event it holds when it is removed, stopped or left
// over (release_link). Every stage has both events except four (kNoRedeadStages): a Redead there is
// not created (it would scream without effect and wait in its attack forever). Light (the Mirror Shield
// beam, light volumes) paralyses it; Light Arrows destroy it. Colliders: body cylinder 0x738 on mStts
// 0x6FC (no attack), enemyice 0x8B4, enemyfire 0xC6C.
inline constexpr Collider kRedeadColliders[] = {{0x738, 0x6FC, 0}, {0x920, 0x8E4, 0x8B4}, {0xD48, 0xD0C, 0xC6C}};
inline bool redead_verify(uint32_t a, uint32_t p) {
    return ld32(a + 0x3D0) == (p & 1) && ld32(a + 0x3D8) == ((p >> 8) & 0xFF) && ld32(a + 0x3DC) == (p >> 24) && ld32(a + 0x3C8) <= 9;
}
// DEATH (mode 3) once its last timer ran out (the group already ENV): the one createDisappear
inline bool redead_terminal(uint32_t a) { return ld32(a + 0x3C8) == 3 && ld32(a + 0x4EC) == 0 && ld8(a + 0x2DA) == 3; }
inline bool redead_holds_link(uint32_t a) { return ld32(a + 0x3C8) == 5 || ld32(a + 0x3C8) == 7; }
inline constexpr const char* kNoRedeadStages[] = {"Ebesso", "kazan", "Msmoke", "tincle"};

// ---------------------------------------------------------------- Poe (d_a_pw)
// Parameter 0xFFFFFE00: behaviour 0 (seen, with its lantern), follows the ground, colour 0x7F (0),
// notice range 0xFF (1000), no room path (0xFF). Never the Jalhalla parts (3/4). No switches or save
// bits. Its lantern (Kantera 0xC1, a held lantern without switch) is created by the game and deleted
// with the Poe by story_enemies (the game leaves it floating). Without its lantern it possesses Link:
// a short event (its own potential event, admitted for an owned Poe; the camera stops), then Link's
// confusion flag (mNoResetFlg1 0x100) and the shared TORITUKI_ON. The game ends all of that only from
// the Poe; story_enemies ends it when the Poe is removed, stopped or left over (release_link).
// Outdoors (no light volumes) only Light Arrows defeat it. Colliders on mStts 0x708: body cylinder
// 0x744 (Link only), lantern sphere 0x874 (Link only); enemyice 0x9A0, enemyfire 0xD58.
inline constexpr Collider kPoeColliders[] = {{0x744, 0x708, 0}, {0x874, 0x708, 0}, {0xA0C, 0x9D0, 0x9A0}, {0xE34, 0xDF8, 0xD58}};
inline bool poe_verify(uint32_t a, uint32_t p) {
    return ld8(a + 0x454) == (p & 0xFF) && ld8(a + 0x455) == ((p >> 8) & 1) && ld8(a + 0x456) == ((p >> 16) & 0xFF) && ld8(a + 0x457) == 0 &&
           ld8(a + 0x475) == 0xFF && ld32(a + 0x464) == 0 && ld32(a + 0x488) == 0xFFFFFFFFu && ld16(a + 0x482) <= 4;
}
// mode 61 faded out without fleeing (m38A 0): the one createDisappear; a Poe that fled after its
// possession deletes itself without a drop (no defeat)
inline bool poe_terminal(uint32_t a) { return ld16(a + 0x482) == 2 && ld16(a + 0x484) == 61 && ld16(a + 0x4B6) == 0 && ld16(a + 0x4A6) == 0; }
inline uint32_t poe_possessing() { return GD(0x101CF0A4); }  // TORITUKI_ON (u8)
inline constexpr uint16_t kPoeLantern = 0xC1;

// ---------------------------------------------------------------- Mothula (d_a_gm)
// Parameters (story_enemy_variants "mothula"): 0xFF01FFFF ground (type 1, no wings), 0xFF02FFFF winged
// (type 2); never type 0 (the Forbidden Woods mini-boss with its cutscene, Link and camera writes).
// Byte 3 0xFF: no switch; angle z 0: no save switch; setID 0xFFFF: onActor is a no-op.
// Children: its moths are the game's own Morths (fopAcM_createChild 0xCD, parameters 4/5) with their
// native drops and their own cling statics: an owned Mothula's release is refused (story_enemies), so it
// fights alone (place Morths beside it instead). A winged one's lost wings are falling wing pieces
// (0xCC, no colliders, no save writes) that delete themselves. Its dive and wind cylinders are also given
// to the area manager (grass); that is skipped for an owned one, so they reach Link only through the
// pair policy. A winged one that dives far from where it was placed (2000) turns back home (dive state
// 37, the game's own retreat). Colliders on mStts 0x788; enemyice 0xCA4, enemyfire 0x105C.
inline constexpr Collider kMothulaColliders[] = {
    {0x7C4, 0x788, 0}, {0x8F4, 0x788, 0}, {0xA24, 0x788, 0}, {0xB54, 0x788, 0}, {0xD10, 0xCD4, 0xCA4}, {0x1138, 0x10FC, 0x105C}};
inline bool mothula_verify(uint32_t a, uint32_t p) {
    const unsigned action = ld8(a + 0x3EC);
    return ld8(a + 0x3E9) == 0 && ld8(a + 0x3EA) == ((p >> 16) & 0xFF) && ld8(a + 0x3EB) == 0xFF && ld8(a + 0x3F2) == 0 &&
           (action <= 5 || action == 10);
}
inline bool mothula_terminal(uint32_t a) { return ld8(a + 0x3EC) == 10 && ld16(a + 0x3F8) == 101; }
inline constexpr uint32_t kCreateChild = CANONICAL_CODE_ID(0x025D5A20).value(), kAreaManagerSet = CANONICAL_CODE_ID(0x02516C14).value();
inline constexpr uint16_t kMothulaMoth = 0xCD;
// a winged Mothula diving (dive action 3, states 34..36) farther than 2000 from its home (mHomePos 0x470)
inline bool mothula_strayed(uint32_t a) {
    const unsigned state = ld16(a + 0x3F8);
    if (ld8(a + 0x3EC) != 3 || state < 34 || state > 36) return false;
    const float dx = u32_as_f32(ld32(a + 0x314)) - u32_as_f32(ld32(a + 0x470)), dz = u32_as_f32(ld32(a + 0x31C)) - u32_as_f32(ld32(a + 0x478));
    return dx * dx + dz * dz > 2000.0f * 2000.0f;
}
inline void mothula_return(uint32_t a) { st16(a + 0x3F8, 37); }

// ---------------------------------------------------------------- Morth (d_a_ks)
// Parameter 0xFFFFFF03: type 3, the ordinary single Morth (never the spawners 0/1/6, which create
// unowned Morths, nor the Mothula child 4/5, which needs a parent). No switches, save bits or paths.
// Its death outside Ganon's Tower makes no drop: dead_eff_set (hooked in story_enemies) is its defeat.
// Clinging: an attached Morth (action 4, modes 40..41, joint matrix 0x7A4) counts itself in the shared
// statics KUTTUKU_ALL_COUNT / GORON_COUNT and bumps Link's per-frame counter (Link clears the heavy
// state himself once fewer than 5 cling). It never moves to the stage layer when owned (so it leaves
// with its room), and removed while attached it takes itself out of the statics as the game's own spin
// detach does (story_enemies). Colliders on mStts 0x7BC: body 0x7F8 (Link only); enemyice 0x924, enemyfire 0xCDC.
inline constexpr Collider kMorthColliders[] = {{0x7F8, 0x7BC, 0}, {0x990, 0x954, 0x924}, {0xDB8, 0xD7C, 0xCDC}};
inline bool morth_verify(uint32_t a, uint32_t p) {
    return ld8(a + 0x544) == (p & 0xFF) && ld8(a + 0x545) == ((p >> 8) & 0xFF) && ld8(a + 0x546) == 0 && ld32(a + 0x550) == 0 && ld8(a + 0x547) <= 4;
}
inline bool morth_dying(uint32_t a) { return ld8(a + 0x547) == 3 && (ld8(a + 0x548) == 31 || ld8(a + 0x548) == 32); }
inline bool morth_attached(uint32_t a) { return ld8(a + 0x547) == 4 && (ld8(a + 0x548) == 40 || ld8(a + 0x548) == 41) && ld32(a + 0x7A4) != 0; }
inline uint32_t morth_clinging() { return GD(0x101B8840); }   // KUTTUKU_ALL_COUNT
inline uint32_t morth_shake_off() { return GD(0x101B8848); }  // GORON_COUNT
inline constexpr uint32_t kMorthDeathEffect = CANONICAL_CODE_ID(0x021A7B4C).value(), kStageLayer = CANONICAL_CODE_ID(0x025D537C).value();

// ---------------------------------------------------------------- Magtail (d_a_mt)
// Parameter 0xFFFF0001: type 1 (goes for Link; odd: no per-frame lava drip particles), no snap turn,
// bit 15 clear (angle z is the death switch, and z is 0: no switch; never the respawn switch, whose
// delete would request a new untracked Magtail), no room path (0xFF), no appear switch (0xFF).
// No children: its 8 segments are internal (one mStts 0xF28; spheres 0xF64 + 0x12C*i, eye TG 0x18C4).
// No switch/save writes with this word (setID 0xFFFF makes onActor a no-op). It never writes Link.
// The curled ball is the game's own carry: Link may lift and throw it; a thrown ball's attack is
// limited to Link by the ownership pair policy. enemyice 0x1A1C (light arrows); no enemyfire.
inline constexpr Collider kMagtailColliders[] = {
    {0xF64, 0xF28, 0}, {0x1090, 0xF28, 0}, {0x11BC, 0xF28, 0}, {0x12E8, 0xF28, 0}, {0x1414, 0xF28, 0},
    {0x1540, 0xF28, 0}, {0x166C, 0xF28, 0}, {0x1798, 0xF28, 0}, {0x18C4, 0xF28, 0}, {0x1A88, 0x1A4C, 0x1A1C}};
inline bool magtail_verify(uint32_t a, uint32_t p) {
    const unsigned action = ld8(a + 0x570);
    return ld8(a + 0x3D0) == (p & 0xFF) && ld8(a + 0x3D1) == 0 && ld8(a + 0x3D2) == 0 && ld8(a + 0x3D3) == 0xFF &&
           ld8(a + 0x3D4) == 0xFF && ld8(a + 0x3D5) == 0 && ld8(a + 0x3D6) == 0 && ld8(a + 0x3D7) == 0 && ld8(a + 0x3D8) == 0 &&
           ld32(a + 0x3DC) == 0 && ld8(a + 0x1A1C + 0x1B1) == 0 && (action <= 3 || action == 6);
}
// bakuha's two callers: the execute tail (damage mode >= 2) and the parry pin's last frame
inline bool magtail_terminal(uint32_t a) {
    return ld8(a + 0x570) != 3 && (s8(a + 0x1A14) >= 2 || (ld8(a + 0x570) == 6 && ld16(a + 0x572) == 1));
}
// curled (ROLL), or the ball Link holds while it unrolls (the game's own release follows)
inline bool magtail_liftable(uint32_t a) {
    const unsigned action = ld8(a + 0x570);
    return action == 2 || ((ld32(a + 0x2E0) & 0x2000u) && action == 1);
}

// ---------------------------------------------------------------- Armos (d_a_am2)
// Parameter 0xFF00FF00: wake radius 600 (0xFF), active, no switch (0xFF: every isSwitch read is behind a
// 0xFF guard, and it never writes a switch). No children (the drop and a water pillar only), never
// writes Link. A paralysed Armos is the game's carry; it stays off (keep_owned clears the attention
// bit), so it never leaves its room (carried out, it would delete itself without a drop). In water or
// over an abyss it returns to its spawn point by itself. Defeated from behind; health 2.
inline constexpr Collider kArmosColliders[] = {
    {0x6BC, 0x680, 0}, {0x7EC, 0x680, 0}, {0x91C, 0x680, 0}, {0xA48, 0x680, 0}, {0xC14, 0xBD8, 0xBA8}};
inline bool armos_verify(uint32_t a, uint32_t p) {
    return ld8(a + 0x3E4) == (p & 0xFF) && ld8(a + 0x3E5) == ((p >> 8) & 0xFF) && ld8(a + 0x3E6) == 0 && ld8(a + 0x3E7) == 0xFF &&
           ld8(a + 0x3EC) == 0 && ld8(a + 0x3E8) <= 4;
}
// DEAD3 (ITAI mode 25) after its health ran out: the one createDisappear of daAM2_Execute
inline bool armos_terminal(uint32_t a) { return ld8(a + 0x3E8) == 2 && ld8(a + 0x3E9) == 25 && s8(a + 0x3A1) <= 0; }

// ---------------------------------------------------------------- Armos Knight (d_a_am)
// Parameter 0xFF00FF01: type 1 (it hops back to where it was placed beyond its radius), radius 400,
// active, no switch (0xFF; read only, never written). Only a bomb in its open mouth defeats it (or a
// Light Arrow): it holds Link's bomb by PID (0x400) while it swallows it and sets it off as it dies.
// Removed while it swallows (modes 44..46), the held bomb is deleted with it (story_enemies). Its spikes
// hit every group natively; the ownership pair policy limits them to Link. A fall far below its spawn
// point runs the death without a bomb: no defeat. No fire helper (never set by the game).
inline constexpr Collider kArmosKnightColliders[] = {
    {0x720, 0x6E4, 0}, {0x84C, 0x6E4, 0}, {0x978, 0x6E4, 0}, {0xAA8, 0x6E4, 0}, {0xC44, 0xC08, 0xBD8}};
inline bool armos_knight_verify(uint32_t a, uint32_t p) {
    return ld8(a + 0x3DC) == (p & 0xFF) && ld8(a + 0x3DD) == ((p >> 8) & 0xFF) && ld8(a + 0x3DE) == 0 && ld8(a + 0x3DF) == 0xFF &&
           ld8(a + 0x3E0) <= 3;
}
inline bool armos_knight_terminal(uint32_t a) { return ld8(a + 0x3E0) == 3 && ld8(a + 0x3E1) == 47 && ld32(a + 0x400) != 0xFFFFFFFFu; }
// the swallowed bomb's PID while its native death has not set it off yet (0: none)
inline uint32_t armos_knight_held_bomb(uint32_t a) {
    const unsigned mode = ld8(a + 0x3E1);
    return ld8(a + 0x3E0) == 3 && mode >= 44 && mode <= 46 && ld32(a + 0x400) != 0xFFFFFFFFu ? ld32(a + 0x400) : 0;
}

// ---------------------------------------------------------------- Kargaroc (d_a_bb)
// Parameter byte 0 is the behaviour: 0 wanders around where it was placed and attacks Link on sight
// (back home when he leaves its range), 4 perches at home and wakes when he comes near. Byte 1 0xFF:
// the class's own sight range; byte 2 0xFF: no room path (a path point may delete it without a drop or
// drop its rider); byte 3 0xFF: no wait switch (read only); angle z 0: no existence switch (create reads
// event bit 0x1101 with it, read only). Never the riders 5/6 (a Moblin or Bokoblin it creates through
// fpcSCtRq_Request itself, which it never deletes and only a path releases) nor 3 (always attacking, no
// way home). It never holds Link: it only reads his position and his guard. No children, projectiles,
// events, camera or switch/save writes. Native behaviour kept: it eats bait Link throws (deletes it).
// Death: a fatal hit sets the dead byte 0xDF2; after its knock-back bb_fail_move makes the one drop.
// Colliders on mStts 0x6F4: peck 0x730 (Link only), head 0x85C, body 0x988, push 0xAB4; enemyice 0xDF4
// (cylinder 0xE60 on 0xE24), enemyfire 0x11AC (sphere 0x1288 on 0x124C).
inline constexpr Collider kKargarocColliders[] = {
    {0x730, 0x6F4, 0}, {0x85C, 0x6F4, 0}, {0x988, 0x6F4, 0}, {0xAB4, 0x6F4, 0}, {0xE60, 0xE24, 0xDF4}, {0x1288, 0x124C, 0x11AC}};
// A rider Kargaroc (behaviour 5 a Moblin, 6 a Bokoblin; story_enemy_variants): its mod path's number in
// byte 2 and its path (ppd 0x4D4) set; its rider state 0x455 (unk_2DF: 1 requested, 2 waiting, 3 carrying,
// 0 none or let go), the rider at 0x458, released when 0x456 (unk_2E0) is set at a release point.
inline constexpr uint32_t kKargarocState = 0x453, kKargarocCarry = 0x455, kKargarocLetGo = 0x456, kKargarocRider = 0x458;
inline bool kargaroc_verify(uint32_t a, uint32_t p) {
    const unsigned behaviour = p & 0xFF, path = (p >> 16) & 0xFF;
    if (behaviour == 5 || behaviour == 6)
        return ld8(a + 0x44E) == behaviour && ld8(a + 0x44F) == ((p >> 8) & 0xFF) && ld8(a + 0x450) == path && path != 0xFF && ld32(a + 0x4D4) != 0 &&
               ld8(a + 0x451) == 0xFF && ld8(a + 0x452) == 0 && ld8(a + kKargarocCarry) <= 3;
    return (behaviour == 0 || behaviour == 4) && ld8(a + 0x44E) == behaviour && ld8(a + 0x44F) == ((p >> 8) & 0xFF) &&
           ld8(a + 0x450) == 0xFF && ld8(a + 0x451) == 0xFF && ld8(a + 0x452) == 0 && ld8(a + kKargarocCarry) == 0;
}
inline bool kargaroc_carries(uint32_t a) { return ld8(a + kKargarocCarry) == 3 && ld8(a + kKargarocLetGo) == 0 && ld32(a + kKargarocRider) != 0; }
inline constexpr uint32_t kSceneRequest = CANONICAL_CODE_ID(0x025E14A8).value();  // fpcSCtRq_Request
inline bool kargaroc_terminal(uint32_t a) { return ld8(a + 0xDF2) != 0; }

// ---------------------------------------------------------------- Wizzrobe (d_a_wz)
// Parameter 0xFFFFFF00: type 0 (the red one: it teleports around where it was placed and casts fire),
// death switch byte 1 0xFF (written in its delete and when hurt only behind a 0xFF guard), enable switch
// byte 2 0xFF (read only), no room path (0xFF; without one it teleports within 200 of home). Angle z 0:
// summon table row 0 (the summoner only). Never the summoner 1 (up to 20 enemies of other kinds, plain
// creates without a parent; without a path it also reads a missing path), the Wind Temple mini-boss 2
// (its cutscene, camera and music), 3, or the ball/door/fireball types 10..13; nor on the stage kazeMB,
// where it waits for the mini-boss demo. It never holds Link. Its fireballs are WZ actors of the game's
// profile (parameter 0xFFFFFF0A, created with fopAcM_create, three per volley, no parent): created in the
// owned Wizzrobe's execute they are its owned children (story_enemy_children), hit Link only (the
// ownership pair policy), never join the area manager, and go when it goes. No save writes (setID 0xFFFF).
// Death: action 1 (ITAI) mode 0x2A makes the one drop. Colliders on mStts 0x784: body 0x7C0 (no attack),
// ball sphere 0x8F0 (the fireball types only); enemyice 0xA40 (0xAAC on 0xA70), enemyfire 0xDF8 (0xED4 on 0xE98).
inline constexpr Collider kWizzrobeColliders[] = {{0x7C0, 0x784, 0}, {0x8F0, 0x784, 0}, {0xAAC, 0xA70, 0xA40}, {0xED4, 0xE98, 0xDF8}};
// The summoner (story_enemy_variants "summoner_*"): type 1 on a mod path (byte 3; its record at 0x52C, the next
// path's at 0x540: the summon points), its wave (0x465, the z rotation word) one of the table's ordinary rows.
inline constexpr uint32_t kWizzrobeBallParams = 0xFFFFFF0Bu, kWizzrobeDoorParams = 0xFFFFFF0Cu;  // the summon ball and door
inline bool wizzrobe_verify(uint32_t a, uint32_t p) {
    if ((p & 0xFF) == 1)
        return ld8(a + 0x464) == 1 && ld8(a + 0x465) <= 11 && ld8(a + 0x465) != 7 && ld8(a + 0x466) == 0xFF && ld8(a + 0x467) == 0xFF &&
               ld8(a + 0x53D) == (p >> 24) && (p >> 24) != 0xFF && ld32(a + 0x52C) != 0 && ld32(a + 0x540) != 0 && ld8(a + 0x46C) == 0 && ld8(a + 0x46D) == 0 &&
               ld8(a + 0x468) <= 1;
    return ld8(a + 0x464) == 0 && ld8(a + 0x465) == 0 && ld8(a + 0x466) == 0xFF && ld8(a + 0x467) == 0xFF && ld8(a + 0x53D) == 0xFF &&
           ld8(a + 0x46C) == 0 && ld8(a + 0x46D) == 0 && ld8(a + 0x468) <= 1;
}
inline bool wizzrobe_terminal(uint32_t a) { return ld8(a + 0x468) == 1 && ld8(a + 0x469) == 0x2A && ld8(a + 0x46C) == 0; }
inline constexpr uint16_t kWizzrobeFire = 0xD0;            // the fireball: the Wizzrobe's own class and profile
inline constexpr uint32_t kWizzrobeFireParams = 0xFFFFFF0Au;
inline constexpr uint32_t kWizzrobeBytes = 0x10B0, kWizzrobeFireSphere = 0x8F0, kWizzrobeStts = 0x784;
inline constexpr const char* kNoWizzrobeStages[] = {"kazeMB"};

// ---------------------------------------------------------------- Warship (d_a_oship)
// The Great Sea's enemy ship: it sails where it was placed and fires its cannon at Link. Parameter
// 0xFFFFFF00: sub-mode 0 (stays where it was placed; 1/2 follow a room path), no Triforce chart (0xF),
// no salvage point (0xF), no path (0xFF), no sunk switch (0xFF; with one it writes the switch and the
// salvage record). Home angle x 0xFFFF: no appear switch (low byte) and the ordinary ship (high byte;
// anything else is the golden treasure ship with its event GOLD_SHIP_DELETE and save bit 0x3E80). Only on
// the sea stage (its height is the sea's). Children: its flag (MAJUU_FLAG 0xAE, created by the game and
// left floating when the ship is deleted early: deleted with it, by its PID 0xE6C) and up to five
// cannonballs (BOMB 0x126, cannonball state, created with fopAcM_fastCreate during its execute): owned
// children (story_enemies) that hit only Link and his boat (the ownership pair policy) and go with it.
// No events, holds or switch/save writes. Health 3: only bomb-type hits count (the boat's cannon);
// at 0 its delete mode (proc 3) makes the one drop, then it sinks and deletes itself and its flag. Modes
// 4..7 are its idle modes by Link's distance (set already in its create).
// Colliders on mStts 0x5D0: five cylinders 0x60C + 0x130*i (targets only).
inline constexpr Collider kWarshipColliders[] = {
    {0x60C, 0x5D0, 0}, {0x73C, 0x5D0, 0}, {0x86C, 0x5D0, 0}, {0x99C, 0x5D0, 0}, {0xACC, 0x5D0, 0}};
// The route variant (story_enemy_variants "route"): sub-mode 2 and a mod path's number (byte 2); its path
// pointer (0x3F4) is the path's record, looked up once in its create.
inline bool warship_verify(uint32_t a, uint32_t p) {
    const uint8_t sub = uint8_t(p), path = uint8_t(p >> 16);
    return (sub == 0 || sub == 2) && ld8(a + 0x3CC) == sub && ld8(a + 0x3CD) == 0xFF && ld8(a + 0x3CE) == 0xFF && ld8(a + 0x3CF) == path &&
           (sub == 0) == (path == 0xFF) && (path == 0xFF || ld32(a + 0x3F4) != 0) && ld8(a + 0x3D0) == 0xF &&
           ld8(a + 0x3D1) == 0xF && ld8(a + 0x3D2) == 0xFF && ld32(a + 0x3C8) <= 7;
}
inline bool warship_terminal(uint32_t a) { return ld32(a + 0x3C8) == 3; }
inline constexpr uint16_t kWarshipFlag = 0xAE, kCannonball = 0x126;
inline constexpr uint32_t kWarshipFlagPid = 0xE6C;
inline constexpr uint32_t kCannonballParams = 0x80030004u;     // daBomb prm_make(cannonball, enemy, 1)
inline constexpr uint32_t kBoatCannonballParams = 0x80020004u; // the one Link's boat fires
inline constexpr uint32_t kCannonballBytes = 0xB50, kCannonballSphere = 0x920, kCannonballStts = 0x8E4;
inline constexpr uint32_t kFastCreate = CANONICAL_CODE_ID(0x025D5928).value();

// ---------------------------------------------------------------- Gyorg (d_a_gy_ctrl + d_a_gy)
// A group of one to five Gyorgs is one enemy: the game's controller (GY_CTRL 0xE5, size 0x5D0) placed
// with parameter 0xFFFF05n1: type 1 (an area: its sharks come out while Link is within 5000 of where it
// was placed), n sharks, no switch (0xFF; with one it waits for it). Never type 0 or 15 (the stage-wide
// ones without an area). Its create needs Link to have the boomerang (item 0x2D), as in the game; without
// it nothing appears (the reason is logged). Only on the sea stage. It creates its sharks (GY 0xE4, size
// 0xFAC, fopAcM_createChild in its execute) one by one; they become its owned children: they bite only
// Link and his boat, only Link's weapons, his projectiles and the boat's cannonball hurt them, and they go
// with it. A shark checks that its parent is a GY_CTRL by name: the controller's name reads 0xE5 while an
// owned shark runs. A shark's death (modeDeleteInit, or modeDeleteBombInit after a bomb) is counted in the
// controller's unused 0x470; the game's sea-kill counter (event register 0x7EFF) is left as it was, and
// the shark's own drop is suppressed. When the last shark is gone the controller deletes itself
// (deadCheckGy): with every shark dead that is its defeat, and the one drop. Sharks that dive away (Link
// leaves) without dying leave it undefeated. No colliders of its own; no switch or save writes.
inline bool gyorg_verify(uint32_t a, uint32_t p) {
    const uint8_t count = (p >> 4) & 0xF;
    return ld8(a + 0x444) == 1 && count >= 1 && count <= 5 && ld8(a + 0x445) == count && ld32(a + 0x448) == 0x459C4000u &&
           ld8(a + 0x44C) == 0xFF && ld8(a + 0x470) <= count && ld32(a + 0x440) <= 3;
}
inline bool gyorg_terminal(uint32_t) { return false; }  // the controller makes no drop of its own
inline uint8_t gyorg_count(uint32_t a) { return ld8(a + 0x445); }
inline constexpr uint32_t kGyorgDeaths = 0x470;
inline constexpr uint16_t kGyorgName = 0xE5, kGy = 0xE4;
inline constexpr uint32_t kGyBytes = 0xFAC, kGyStts = 0x9C4, kGyIceStts = 0xA78;
inline constexpr uint32_t kGyCreate = CANONICAL_CODE_ID(0x02167B60).value(), kGyExecute = CANONICAL_CODE_ID(0x02169264).value(),
                          kGyDraw = CANONICAL_CODE_ID(0x021695BC).value(), kGyDelete = CANONICAL_CODE_ID(0x02167BB8).value();
inline constexpr uint32_t kGyDeathInit = CANONICAL_CODE_ID(0x02167FE0).value(), kGyBombDeathInit = CANONICAL_CODE_ID(0x02167F30).value();
inline constexpr uint32_t kCheckGetItem = CANONICAL_CODE_ID(0x02520C0C).value(), kGetEventReg = CANONICAL_CODE_ID(0x025B8BB0).value(),
                          kSetEventReg = CANONICAL_CODE_ID(0x025B8AF4).value();
inline constexpr uint8_t kBoomerang = 0x2D;
inline constexpr uint16_t kSeaKillRegister = 0x7EFF;

// ---------------------------------------------------------------- Floormaster (d_a_fm)
// Parameter 0xFFFF04FF: no exit (0xFF; with one, being dragged down changes the stage), type 0, base
// target 1 (Link only, never Medli or Makar), no path, no switch (0xFF). Home angle x 0xFFFF: no gone
// switch and no restart point. Its grab of Link lasts the game's sink timer, then it lets go and sinks away
// (MODE_DISAPPEAR):
// it never drags Link down (GRAB_DEMO, the event DEFAULT_FM_GRAB and its stage change) and never grabs his
// feet (GRAB_FOOT_DEMO becomes APPEAR, as the game's own no-demo switch does). It never prepares a skull
// pot (PREPARE_ITEM, a retail TSUBO) and never targets bombs or pots (searchNearOtherActor). No NPC demo.
// Death: MODE_DEATH (12) with the hole closed (animation 8) makes the one drop; its onActor uses the
// placed setID 0xFFFF (no save bit). Colliders: body cylinder 0xA10 on mStts 0x86C, hand sphere 0x8E4 on
// mStts2 0x8A8; enemyice 0xC64 (0xCD0 on 0xC94). No enemyfire.
inline constexpr Collider kFloormasterColliders[] = {{0xA10, 0x86C, 0}, {0x8E4, 0x8A8, 0}, {0xCD0, 0xC94, 0xC64}};
// The wandering variant (story_enemy_variants "wandering"): type 1 (0x3EC) on a mod path (its number in
// m_path_no 0x3E8, the record in mpPath 0x4DC).
inline bool floormaster_verify(uint32_t a, uint32_t p) {
    const uint32_t type = (p >> 8) & 3, path = (p >> 16) & 0xFF;
    return ld8(a + 0x3E3) == 0xFF && ld16(a + 0x3E4) == 0xFF && ld32(a + 0x3E8) == path && ld32(a + 0x3EC) == type && (type == 1) == (path != 0xFF) &&
           (path == 0xFF || ld32(a + 0x4DC) != 0) &&
           ld32(a + 0x3F0) == 0xFF && ld32(a + 0x3F4) == 0xFF && ld32(a + 0x3F8) == 1 && ld8(a + 0x400) == 0 &&
           ld32(a + 0x3C8) <= 20 && ld32(a + 0x3C8) != 14 && ld32(a + 0x3C8) != 15 && ld32(a + 0x3C8) != 16 && ld32(a + 0x3C8) != 17;
}
inline bool floormaster_terminal(uint32_t a) { return ld32(a + 0x3C8) == 12 && s8(a + 0x3E1) == 8; }
inline constexpr uint32_t kFmModeProc = CANONICAL_CODE_ID(0x021412BC).value(), kFmCancelGrab = CANONICAL_CODE_ID(0x02142028).value(),
                          kFmSetAnm = CANONICAL_CODE_ID(0x021418D0).value(), kFmSearchOther = CANONICAL_CODE_ID(0x02140D04).value();

// ---------------------------------------------------------------- Big Octo (d_a_daiocta + d_a_daiocta_eye)
// Offered only as a target: Link shoots its eyes; it never swallows the boat and makes no whirlpool.
// Parameter 0xFFFFFFtt: type tt (0: 4 eyes, 1: 8, 2: 12), the default appear radius (5000), no exit
// after the swallow (0xFF), no switch (0xFF; the game sets it at the end of its death event); home
// angle z 0 (never the fairy-island one). Only on the sea stage; as in the game it rises only while Link
// rides his boat within its radius. Its eyes (DAIOCTA_EYE 0xE2, size 0x67C, fopAcM_createChild in its
// create) are its owned children; an eye checks its parent by name in its create, so the octo's name
// reads DAIOCTA while an owned eye is created. Only Link's weapons, his projectiles and the boat's
// cannonball hurt them. Its whirlpool (Obj_Auzu 0x11B, fopAcM_create in its create, which would pull
// the boat and make the weather tag KYTAG01) is refused, and so is its swallow (MODE_DEMO 6, the event
// DAIOCTA_SUIKOMI and its stage restart). Without the whirlpool it rises by itself (big_octo_rise). Defeat: modeDeleteInit (every eye dead) is the one drop; the
// octo is then removed at once, so its death event (DAIOCTA_DEAD, the quake, the switch) never runs.
// Its battle music (sub-BGM, latched in 0x2840) is stopped when it goes. No enemyice or enemyfire.
// Colliders on mStts 0x22E4: six spheres 0x724 + 0x12C*i and seventeen capsules 0xE2C + 0x138*i.
inline constexpr Collider kBigOctoColliders[] = {
    {0x724, 0x22E4, 0}, {0x850, 0x22E4, 0}, {0x97C, 0x22E4, 0}, {0xAA8, 0x22E4, 0}, {0xBD4, 0x22E4, 0}, {0xD00, 0x22E4, 0},
    {0xE2C, 0x22E4, 0}, {0xF64, 0x22E4, 0}, {0x109C, 0x22E4, 0}, {0x11D4, 0x22E4, 0}, {0x130C, 0x22E4, 0}, {0x1444, 0x22E4, 0},
    {0x157C, 0x22E4, 0}, {0x16B4, 0x22E4, 0}, {0x17EC, 0x22E4, 0}, {0x1924, 0x22E4, 0}, {0x1A5C, 0x22E4, 0}, {0x1B94, 0x22E4, 0},
    {0x1CCC, 0x22E4, 0}, {0x1E04, 0x22E4, 0}, {0x1F3C, 0x22E4, 0}, {0x2074, 0x22E4, 0}, {0x21AC, 0x22E4, 0}};
inline bool big_octo_verify(uint32_t a, uint32_t p) {
    return ld8(a + 0x68F) == (p & 0xFF) && (p & 0xFF) <= 2 && ld8(a + 0x690) == 0xFF && ld8(a + 0x691) == 0xFF && ld8(a + 0x698) == 0 &&
           ld32(a + 0x688) <= 5;
}
inline bool big_octo_terminal(uint32_t) { return false; }  // its defeat is modeDeleteInit (story_enemies)
// The game's APPEAR raises it only together with its whirlpool (once the Obj_Auzu has fully appeared);
// without one an owned octo rises to the water by itself, and is then ready for MODE_WAIT (true).
inline bool big_octo_rise(uint32_t a) {
    if (ld32(a + 0x688) != 1) return false;
    const float water = u32_as_f32(ld32(a + 0x25B0)), y = u32_as_f32(ld32(a + 0x318)), step = 30.0f;
    const float next = y + std::max(-step, std::min(step, water - y));
    st32(a + 0x318, f32_as_u32(next));
    return std::fabs(water - next) < 0.5f;
}
inline constexpr uint16_t kBigOctoName = 0xE1, kOctoEye = 0xE2;
inline constexpr uint32_t kOctoEyeBytes = 0x67C, kOctoEyeSphere = 0x4D4, kOctoEyeStts = 0x600, kOctoMusic = 0x2840;
inline constexpr uint32_t kOctoModeProc = CANONICAL_CODE_ID(0x0211889C).value(), kOctoDeathInit = CANONICAL_CODE_ID(0x0211BF44).value(),
                          kSubBgmStop = CANONICAL_CODE_ID(0x025E1928).value();

// The native children that are a table kind's owned children by their lineage (Gyorg sharks, Big Octo
// eyes): their class, and the name their parent must read while one runs (they check it by name).
struct ChildClass {
    Kind parent;
    uint16_t profile;
    uint32_t bytes;
    uint16_t parent_name;
    uint32_t create, execute, draw, destroy;
};
inline constexpr ChildClass kChildClasses[] = {
    {Kind::Gyorg, kGy, kGyBytes, kGyorgName, kGyCreate, kGyExecute, kGyDraw, kGyDelete},
    {Kind::BigOcto, kOctoEye, kOctoEyeBytes, kBigOctoName, CANONICAL_CODE_ID(0x0211DAD0).value(), CANONICAL_CODE_ID(0x0211DEEC).value(),
     CANONICAL_CODE_ID(0x0211DFB4).value(), CANONICAL_CODE_ID(0x0211DB24).value()}};
inline const ChildClass* child_class(uint16_t profile) {
    for (const ChildClass& k : kChildClasses)
        if (k.profile == profile) return &k;
    return nullptr;
}
inline const ChildClass* child_class_of(Kind parent) {
    for (const ChildClass& k : kChildClasses)
        if (k.parent == parent) return &k;
    return nullptr;
}

// ---------------------------------------------------------------- Wall cannon (d_a_obj_canon)
// The cannon set in walls and on forts: it turns towards Link and fires cannonballs while he (and the
// spot it aims at) is inside its room path, an area (dNpc_PathRun_c::chkInside); without its path the
// game refuses its create. Parameter 0xFF <path> 0xFF 0xFF: scale byte 0xFF (its own size), appear switch
// 0xFF (never waits hidden for a switch, modes 3/4), the path number (a mod path, story_room_paths: the
// room-path lookup answers it with the mod's area only for this cannon), death switch 0xFF (its wreck
// writes no switch; a set one would make it start as a wreck). It reads Link's position and the boat's
// state, writes no save bits, events or holds. Health 2: a bomb-type hit (type 0x20) takes 2, a 0x40 hit
// 1; at 0 its delete mode's init (deleteInit, mode 2) plays the explosion: that is its defeat (it has no
// native drop). Children: the cannonballs (BOMB 0x126, the warship's cannonball parameters, created with
// fopAcM_fastCreate in its attack): owned children that hit only Link and his boat, as the warship's.
// Collider: the sphere 0x3F4 on mStts 0x520 (target and attack: the pair policy keeps Link only).
inline constexpr Collider kWallCannonColliders[] = {{0x3F4, 0x520, 0}};
inline constexpr uint32_t kWallCannonParams = 0xFFFFFFFFu;  // the path byte (16) is the mod path's number
inline bool wall_cannon_verify(uint32_t a, uint32_t p) {
    const uint32_t mode = ld32(a + 0x3AC);
    return ld8(a + 0x3B0) == ((p >> 16) & 0xFF) && ld8(a + 0x3B1) == 0xFF && ld8(a + 0x3B2) == 0xFF && ld8(a + 0x3B3) == 0xFF && mode <= 2 &&
           ld32(a + 0x3B4) != 0 && ld32(a + 0x3B4) == ld32(a + 0x3CC);
}
inline bool wall_cannon_terminal(uint32_t) { return false; }  // no native drop: its defeat is its delete mode
inline constexpr uint32_t kWallCannonDeleteInit = CANONICAL_CODE_ID(0x02331558).value();  // daObj_Canon_c::deleteInit
inline bool wall_cannon_destroyed(uint32_t a) { return int8_t(ld8(a + 0x3A1)) <= 0; }

inline constexpr Native kNatives[] = {
    {Kind::Stalfos, "stalfos", {0x7FE0, 0xBE, 0x25B0, kStalfosColliders, std::size(kStalfosColliders)},
     CANONICAL_CODE_ID(0x02495C70).value(), CANONICAL_CODE_ID(0x02493FB0).value(), CANONICAL_CODE_ID(0x02491F34).value(),
     CANONICAL_CODE_ID(0x024957B4).value(), [] { return GD(0x1003D5BC); }, 0xFFFFFF00u, 0, 0x21F0, {0, 0}, true,
     stalfos_verify, stalfos_terminal, nullptr, 0x3E2, true},
    {Kind::Magtail, "magtail", {0x7FE4, 0xD8, 0x1DD8, kMagtailColliders, std::size(kMagtailColliders)},
     CANONICAL_CODE_ID(0x021DCE84).value(), CANONICAL_CODE_ID(0x021D87A0).value(), CANONICAL_CODE_ID(0x021D7FFC).value(),
     CANONICAL_CODE_ID(0x021DC634).value(), [] { return GD(0x1001565C); }, 0xFFFF0001u, 0, 0x1A1C, {0, 0}, true,
     magtail_verify, magtail_terminal, magtail_liftable, 0x570, false},
    {Kind::Morth, "morth", {0x7FE3, 0xCD, 0xF08, kMorthColliders, std::size(kMorthColliders)},
     CANONICAL_CODE_ID(0x021AA4C8).value(), CANONICAL_CODE_ID(0x021A7F94).value(), CANONICAL_CODE_ID(0x021A71EC).value(),
     CANONICAL_CODE_ID(0x021A9EC8).value(), [] { return GD(0x100136C0); }, 0xFFFFFF03u, 0, 0x924, {0xCDC, 0}, true,
     morth_verify, morth_dying, nullptr, 0x547, false},
    {Kind::Mothula, "mothula", {0x7FE2, 0xCC, 0x1290, kMothulaColliders, std::size(kMothulaColliders)},
     CANONICAL_CODE_ID(0x021507C4).value(), CANONICAL_CODE_ID(0x0214D128).value(), CANONICAL_CODE_ID(0x0214BF60).value(),
     CANONICAL_CODE_ID(0x0214FFD8).value(), [] { return GD(0x1000FDD4); }, 0xFF01FFFFu, 0, 0xCA4, {0x105C, 0}, true,
     mothula_verify, mothula_terminal, nullptr, 0x3EC, false},
    {Kind::Redead, "redead", {0x7FE1, 0xE0, 0xF1C, kRedeadColliders, std::size(kRedeadColliders)},
     CANONICAL_CODE_ID(0x0245B7B8).value(), CANONICAL_CODE_ID(0x0245C498).value(), CANONICAL_CODE_ID(0x0245C728).value(),
     CANONICAL_CODE_ID(0x0245B814).value(), [] { return GD(0x10038EE4); }, 0xFFFFFFFEu, 0, 0x8B4, {0xC6C, 0}, true,
     redead_verify, redead_terminal, nullptr, 0x3CB, false},
    {Kind::Poe, "poe", {0x7FE5, 0xD4, 0xF8C, kPoeColliders, std::size(kPoeColliders)},
     CANONICAL_CODE_ID(0x0244E398).value(), CANONICAL_CODE_ID(0x0244B3E4).value(), CANONICAL_CODE_ID(0x02449F80).value(),
     CANONICAL_CODE_ID(0x0244DD6C).value(), [] { return GD(0x1003841C); }, 0xFFFFFE00u, 0, 0x9A0, {0xD58, 0}, true,
     poe_verify, poe_terminal, nullptr, 0x482, true},
    {Kind::Armos, "armos", {0x7FE6, 0xCB, 0xF60, kArmosColliders, std::size(kArmosColliders)},
     CANONICAL_CODE_ID(0x0204FE84).value(), CANONICAL_CODE_ID(0x0204D3A4).value(), CANONICAL_CODE_ID(0x0204C5F8).value(),
     CANONICAL_CODE_ID(0x0204F9D0).value(), [] { return GD(0x1000702C); }, 0xFF00FF00u, 0, 0xBA8, {0, 0}, true,
     armos_verify, armos_terminal, nullptr, 0x3E8, false},
    {Kind::ArmosKnight, "armos_knight", {0x7FE7, 0xCA, 0x11BC, kArmosKnightColliders, std::size(kArmosKnightColliders)},
     CANONICAL_CODE_ID(0x0204B8E0).value(), CANONICAL_CODE_ID(0x0204938C).value(), CANONICAL_CODE_ID(0x02048B3C).value(),
     CANONICAL_CODE_ID(0x0204B430).value(), [] { return GD(0x10006EA4); }, 0xFF00FF01u, 0, 0xBD8, {0, 0}, true,
     armos_knight_verify, armos_knight_terminal, nullptr, 0x3E0, false},
    {Kind::Kargaroc, "kargaroc", {kKargarocProfile, 0xB5, 0x13D8, kKargarocColliders, std::size(kKargarocColliders)},
     CANONICAL_CODE_ID(0x02061DEC).value(), CANONICAL_CODE_ID(0x0205EA58).value(), CANONICAL_CODE_ID(0x0205E420).value(),
     CANONICAL_CODE_ID(0x02061964).value(), [] { return GD(0x10007C8C); }, 0xFFFFFF00u, 0, 0xDF4, {0x11AC, 0}, true,
     kargaroc_verify, kargaroc_terminal, nullptr, 0x465, false, 0, 16},
    {Kind::Wizzrobe, "wizzrobe", {kWizzrobeProfile, 0xD0, 0x10B0, kWizzrobeColliders, std::size(kWizzrobeColliders)},
     CANONICAL_CODE_ID(0x024E6BEC).value(), CANONICAL_CODE_ID(0x024E398C).value(), CANONICAL_CODE_ID(0x024E2468).value(),
     CANONICAL_CODE_ID(0x024E61E0).value(), [] { return GD(0x10042D90); }, 0xFFFFFF00u, 0, 0xA40, {0xDF8, 0}, true,
     wizzrobe_verify, wizzrobe_terminal, nullptr, 0x468, false, 0, 24},
    {Kind::Warship, "warship", {kWarshipProfile, 0xB3, 0xF1C, kWarshipColliders, std::size(kWarshipColliders)},
     CANONICAL_CODE_ID(0x023C637C).value(), CANONICAL_CODE_ID(0x023C6D38).value(), CANONICAL_CODE_ID(0x023C6DAC).value(),
     CANONICAL_CODE_ID(0x023C6418).value(), [] { return GD(0x10034084); }, 0xFFFFFF00u, 0, 0, {0, 0}, true,
     warship_verify, warship_terminal, nullptr, 0x3CB, false, 0xFFFF, 16},
    {Kind::Gyorg, "gyorg", {kGyorgProfile, 0xE5, 0x5D0, nullptr, 0},
     CANONICAL_CODE_ID(0x0216BBB0).value(), CANONICAL_CODE_ID(0x0216C1A0).value(), CANONICAL_CODE_ID(0x0216C1DC).value(),
     CANONICAL_CODE_ID(0x0216BBB4).value(), [] { return GD(0x10010D78); }, 0xFFFF0531u, 0, 0, {0, 0}, true,
     gyorg_verify, gyorg_terminal, nullptr, 0x443, false},
    {Kind::Floormaster, "floormaster", {kFloormasterProfile, 0x77, 0x101C, kFloormasterColliders, std::size(kFloormasterColliders)},
     CANONICAL_CODE_ID(0x02141FD4).value(), CANONICAL_CODE_ID(0x0214399C).value(), CANONICAL_CODE_ID(0x0214418C).value(),
     CANONICAL_CODE_ID(0x02142154).value(), [] { return GD(0x1000F350); }, 0xFFFF04FFu, 0, 0xC64, {0, 0}, true,
     floormaster_verify, floormaster_terminal, nullptr, 0x3CB, false, 0xFFFF, 16},
    {Kind::BigOcto, "big_octo", {kBigOctoProfile, 0xE1, 0x5910, kBigOctoColliders, std::size(kBigOctoColliders)},
     CANONICAL_CODE_ID(0x02119EE8).value(), CANONICAL_CODE_ID(0x0211B058).value(), CANONICAL_CODE_ID(0x0211B52C).value(),
     CANONICAL_CODE_ID(0x02119F80).value(), [] { return GD(0x1000D034); }, 0xFFFFFF00u, 0, 0, {0, 0}, true,
     big_octo_verify, big_octo_terminal, nullptr, 0x68B, false},
    {Kind::WallCannon, "wall_cannon", {kWallCannonProfile, 0x3F, 0x5A8, kWallCannonColliders, std::size(kWallCannonColliders)},
     CANONICAL_CODE_ID(0x0233063C).value(), CANONICAL_CODE_ID(0x023308E8).value(), CANONICAL_CODE_ID(0x02330A84).value(),
     CANONICAL_CODE_ID(0x02330698).value(), [] { return GD(0x10026A90); }, kWallCannonParams, 0, 0, {0, 0}, true,
     wall_cannon_verify, wall_cannon_terminal, nullptr, 0x3AF, false, 0, 16},
};

inline const Native* find(Kind k) {
    for (const Native& n : kNatives)
        if (n.kind == k) return &n;
    return nullptr;
}
inline const Native* by_name(const std::string& name) {
    for (const Native& n : kNatives)
        if (name == n.name) return &n;
    return nullptr;
}
inline constexpr uint32_t params(Kind k) {
    for (const Native& n : kNatives)
        if (n.kind == k) return n.params;
    return 0;
}
inline constexpr uint16_t home_x(Kind k) {
    for (const Native& n : kNatives)
        if (n.kind == k) return n.home_x;
    return 0;
}
inline constexpr int path_shift(Kind k) {
    for (const Native& n : kNatives)
        if (n.kind == k) return n.path_shift;
    return -1;
}
// the parameter bits a kind's room path number takes (0: none)
inline constexpr uint32_t path_mask(Kind k) { return path_shift(k) < 0 ? 0u : 0xFFu << path_shift(k); }
inline constexpr uint16_t home_z(Kind k) {
    for (const Native& n : kNatives)
        if (n.kind == k) return n.home_z;
    return 0;
}
// the private profile ids of this table (0x7FD0..0x7FE7)
inline constexpr bool reserved(uint16_t profile) { return profile >= 0x7FD0 && profile <= 0x7FE7; }
}  // namespace story_enemies_more
