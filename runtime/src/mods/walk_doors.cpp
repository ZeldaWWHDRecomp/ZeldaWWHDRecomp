// SPDX-License-Identifier: MPL-2.0
// Walk-through doors (Game menu, off by default): the dungeons' shutter doors open as Link comes near, he walks
// through himself, and the door shuts behind him, with no door scene.
//
// From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (MPL-2.0), mods/walk_doors.cpp. In this port it is the
// "Walk through" mode of Quick doors (mods.cpp: quick_doors() on and door_mode() == kDoorWalk; door scenes that
// still run, such as locked doors, run fast as in the "Faster" mode). Changes to the fork's file: the three door
// executes are not hooked (no hook lines of their own): fpcM_Execute's hook (game_hooks.cpp, hook_025DF940)
// points the door's execute in its method table at a host function for the call (DoorExecuteWrap below), so the
// game's fopAc_Execute (pause, event and culling checks) still decides whether the door runs; addresses go through
// GC()/GD() for the other regional builds; the state is dropped when a save state is loaded (walk_doors_reset).
//
// How the game does it (decomp d_door.cpp, d_a_door10.cpp, d_a_door12.cpp): walking into a door gives Link a door
// event (DEFAULT_SHUTTER_DOOR_10 / _12, F_STOP when the far room locks him in). Its cuts call the door's openInit
// (dDoor_info_c::openInitCom: the far room's "hidden" flag 0x08 cleared, the door's collision released), openProc
// each step until open, openEnd; then Link is walked through and closeInit (collision back), closeProc, closeEnd
// (closeEndCom: the room behind hidden, the restart point set 180 units past the door on Link's side). Which room
// Link is in never comes from the door: Link's own step reads the room of the floor under him
// (dStage_RoomCheck from daPy_lk_c, GameCube d_a_player_main.cpp): a new room becomes the stay room and its room
// list is loaded, exactly as when walking through an open archway.
//
// This hooks the two door actors' executes (daDoor10_Execute 021277D0: the stone shutters that slide up;
// daDoor12_Execute 02129830: the double doors that slide apart). For a door that only waits for Link (its action
// "wait", no key lock, no boss door, no stop bars on Link's side, no light-sensitive Hkyo bars, the far room
// loaded) and Link close to it, the door's own functions run without an event: openInit, openProc (two steps a
// frame) to openEnd; the door stays open while Link stays near; once he is away on either side, closeInit,
// closeProc to closeEnd, with the door's from / to rooms set from the room Link is in now (so the room he left is
// hidden, and the restart point is on his side). Then the door's own execute takes over again: rooms that lock
// Link in drop their stop bars as after the door scene (its wait action's chkStopClose; setStop for the doors whose
// bars come down only in the scene). Every other door, and a door while an event runs, keeps the game's scene.
//
// HD offsets (from the HD code, GameCube + 0x11C for dDoor_info_c): current.pos 0x314, home.angle.x 0x2F8 (front
// room bits 0-5, back room bits 6-11), mRoomNo2 0x3AC, mFromRoomNo 0x3AD, mToRoomNo 0x3AE, mAngleVec 0x3B0,
// mFrontCheck 0x3BC, m2C6 (the door event's kind) 0x3E2. Rooms: dStage_roomControl_c::mStayNo 1047E6C8, the
// status flags byte of room n at 1047E8E8 + n * 0x22C (1 loaded, 2 loading, 4 deleting, 8 hidden).
// Knob doors (houses): see knob_execute below.
// Opening on A instead (walk_doors_by_a): the door's own wait runs, so the game shows its "Open" prompt; Link's A on
// it orders the door event (fopAcM_orderDoorEvent 025D7710, hooked): for such a door the order is not passed on, and
// the door opens at its next execute as above.
// Test switches: WWHD_MOD_QUICK_DOORS=1 with WWHD_MOD_DOOR_MODE=walk, WWHD_MOD_WALK_DOORS_A=1, WWHD_WALK_DOORS_OPEN / _CLOSE (distances), WWHD_WALK_DOORS_LOG=1.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <vector>

#include "guest_addr.h"
#include "mods.h"
#include "runtime.h"

extern "C" {
void f_025D7710_orig(Cpu* c);  // fopAcM_orderDoorEvent
}

namespace mods {
namespace {
// USA addresses (the fork's release::Code / Data), translated for the build the port was made from with GC() / GD()
// the doors' own executes (the game's code, reached through the dispatch table: a decompiled one in the decomp build)
const uint32_t kDoor10Execute = GC(0x021277D0), kDoor12Execute = GC(0x02129830), kKnobExecute = GC(0x021A5008);
PpcFunc g_knob_orig = nullptr;
void f_021A5008_orig(Cpu* c) { g_knob_orig(c); }
// Link: g_dComIfG_gameInfo.play mpPlayerPtr[0] (as story_chart.cpp)
uint32_t link_actor() { return ld32(GD(0x1046F0B0) + 0x5B34); }
// the fork's event_mode_now(): dComIfGp_event_runCheck, the event control's mode (0 no event; as jump.cpp)
int event_mode_now() { return ld8(GD(0x1046F0B0) + 0x5292); }
bool env_on(const char* n) {
    const char* e = getenv(n);
    return e && atoi(e) != 0;
}
float env_f(const char* n, float d) {
    const char* e = getenv(n);
    return e ? (float)atof(e) : d;
}
// the mode's switches live in mods.cpp (Quick doors: WWHD_MOD_QUICK_DOORS=1 WWHD_MOD_DOOR_MODE=walk;
// WWHD_MOD_WALK_DOORS_A=1 opens them with A)
const bool kLog = env_on("WWHD_WALK_DOORS_LOG");

// the zone in front of and behind a door (units along the door's facing; sideways; height above its foot)
const float kOpenAlong = env_f("WWHD_WALK_DOORS_OPEN", 280.0f);
const float kCloseAlong = env_f("WWHD_WALK_DOORS_CLOSE", 330.0f);
constexpr float kOpenSide = 110.0f, kCloseSide = 170.0f;
constexpr float kLowY = -80.0f, kHighY = 260.0f;

struct Kind {
    const char* name;
    uint32_t open_init, open_proc, open_end, close_init, close_proc, close_end, set_stop;  // GC()
    uint32_t key;     // dDoor_key2_c (its first byte: the lock is on)
    uint32_t stop;    // dDoor_stop_c (+4 mOffsY, +8 m8: bars down on Link's side)
    uint32_t action;  // the actor's action (0 init, 1 wait, 2 bars closing, 3 event)
    uint32_t amount;  // how far the door is open (0 shut)
    uint8_t boss_type;
    bool hkyo;  // door10's light-sensitive bars (mHkyo.m11 at 0x4E1)
};
const Kind kDoor10{"door10", GC(0x021262D8), GC(0x0212662C), GC(0x0212673C), GC(0x02126334), GC(0x021267A8), GC(0x0212683C),
                   GC(0x02126594), 0x424, 0x4C4, 0x4EC, 0x4F0, 1, true};
const Kind kDoor12{"door12", GC(0x02128430), GC(0x021287A0), GC(0x021288E0), GC(0x0212848C), GC(0x02128974), GC(0x021289EC),
                   GC(0x02128708), 0x400, 0x4A0, 0x4AC, 0x4B4, 3, false};
const uint32_t kStopCloseInit = GC(0x0252BB9C);  // dDoor_stop_c::closeInit
const uint32_t kStayNo = GD(0x1047E6C8);
const uint32_t kRoomStatus = GD(0x1047E8E8);
const uint32_t kCounter = GD(0x101FF558);  // g_Counter.mCounter0: +1 per logic step
constexpr uint32_t kRoomStride = 0x22C;
constexpr uint32_t kPos = 0x314, kHomeAngleX = 0x2F8, kRoomNo2 = 0x3AC, kFrom = 0x3AD, kTo = 0x3AE, kVec = 0x3B0,
                   kFront = 0x3BC, kEvtKind = 0x3E2;
constexpr uint32_t kParams = 0xB0;          // base_process_class parameters (getType: bits 8-11)
constexpr uint32_t kLinkKeepGrab = 0x65A0;  // daPy_lk_c::mActorKeepGrab {process id, actor}

enum State { kOpening, kOpen, kClosing };
struct Door {
    State st;
    uint32_t seen;  // logic step of its last execute
};
std::unordered_map<uint32_t, Door> g_doors;
// the doors seen executing (for fopAcM_orderDoorEvent: which kind its partner is), by logic step
struct Seen {
    const Kind* kind;  // nullptr: a knob door (houses)
    uint32_t seen;
};
std::unordered_map<uint32_t, Seen> g_seen;
uint32_t g_open_request = 0;  // a door whose A press was taken: it opens at its next execute

bool guest_ptr(uint32_t a) { return a >= 0x10000000 && a < 0x50000000; }
float ldf(uint32_t a) { return u32_as_f32(ld32(a)); }
uint8_t room_flags(int room) { return ld8(kRoomStatus + (uint32_t)room * kRoomStride); }
int stay_no() { return (int8_t)ld8(kStayNo); }

// where Link stands against the door: along its facing, sideways, height
struct Where {
    float along, side, up;
};
Where where(uint32_t door, uint32_t link) {
    float dx = ldf(link + kPos) - ldf(door + kPos), dy = ldf(link + kPos + 4) - ldf(door + kPos + 4),
          dz = ldf(link + kPos + 8) - ldf(door + kPos + 8);
    float vx = ldf(door + kVec), vz = ldf(door + kVec + 8);
    float along = dx * vx + dz * vz;
    float side = std::fabs(dx * vz - dz * vx);
    return {along, side, dy};
}
bool in_zone(const Where& w, float along, float side) {
    return std::fabs(w.along) < along && w.side < side && w.up > kLowY && w.up < kHighY;
}

bool carrying(uint32_t link) {
    uint32_t id = ld32(link + kLinkKeepGrab), a = ld32(link + kLinkKeepGrab + 4);
    return guest_ptr(a) && ld32(a + 4) == id;
}

// the door may open by itself now: returns the side Link is on (0 front room, 1 back room), else -1
int may_open(Cpu* c, uint32_t door, uint32_t link, const Kind& k) {
    if (event_mode_now() != 0) return -1;
    if (ld8(door + k.action) != 1) return -1;
    if (ld8(door + k.key) != 0 || ld8(door + k.stop + 8) != 0) return -1;
    if (k.hkyo && ld8(door + 0x4E1) != 0) return -1;
    if (&k == &kDoor12 && carrying(link)) return -1;  // its scene makes Link put the thing down first
    uint16_t ax = ld16(door + kHomeAngleX);
    int f = ax & 0x3F, b = (ax >> 6) & 0x3F, stay = stay_no();
    if (f == b || f == 0x3F || b == 0x3F) return -1;
    int front = stay == f ? 0 : stay == b ? 1 : -1;
    if (front < 0) return -1;
    uint8_t far = room_flags(front == 0 ? b : f);
    if (!(far & 1) || (far & 6)) return -1;  // the far room must be loaded (else its floor isn't there yet)
    if (((ld32(door + kParams) >> 8) & 0xF) == k.boss_type) return -1;  // dDoor_info_c::getType
    return front;
}

void door_execute(Cpu* c, const Kind& k, void (*orig)(Cpu*)) {
    uint32_t door = c->r[3];
    uint32_t now = ld32(kCounter);
    if (g_seen.size() > 64) {
        for (auto s = g_seen.begin(); s != g_seen.end();) s = now - s->second.seen > 8 ? g_seen.erase(s) : std::next(s);
    }
    if (kLog && !g_seen.count(door))  // (port: where the doors are, for tests)
        LOG("[doors] %s %08X at (%.0f, %.0f, %.0f) facing (%.2f, %.2f), rooms %d/%d", k.name, door, ldf(door + kPos),
            ldf(door + kPos + 4), ldf(door + kPos + 8), ldf(door + kVec), ldf(door + kVec + 8), ld16(door + kHomeAngleX) & 0x3F,
            (ld16(door + kHomeAngleX) >> 6) & 0x3F);
    g_seen[door] = Seen{&k, now};
    auto it = g_doors.find(door);
    if (it != g_doors.end() && now - it->second.seen > 8) {  // a door from another scene (or a loaded state)
        g_doors.erase(it);
        it = g_doors.end();
    }
    uint32_t link = link_actor();
    if (!guest_ptr(link) || (it == g_doors.end() && !walk_doors())) {
        orig(c);
        return;
    }
    Where w = where(door, link);
    bool by_a = walk_doors_by_a();
    if (it == g_doors.end()) {
        int front = -1;
        if (by_a) {  // the door's own wait gives the "Open" prompt; A is taken in fopAcM_orderDoorEvent
            if (g_open_request == door) front = may_open(c, door, link, k);
        } else if (in_zone(w, kOpenAlong, kOpenSide)) {
            front = may_open(c, door, link, k);
            if (front < 0 && kLog && now % 30 == 0) {  // (port: why a door stays shut, for tests)
                uint16_t ax = ld16(door + kHomeAngleX);
                int f = ax & 0x3F, b = (ax >> 6) & 0x3F;
                LOG("[doors] %s %08X stays: event %d action %u key %u bars %u type %u stay %d rooms %d/%d flags %02X/%02X",
                    k.name, door, event_mode_now(), ld8(door + k.action), ld8(door + k.key), ld8(door + k.stop + 8),
                    (ld32(door + kParams) >> 8) & 0xF, stay_no(), f, b, room_flags(f), room_flags(b));
            }
        }
        if (g_open_request == door) g_open_request = 0;
        bool left_open = ldf(door + k.amount) > 0.0f && ld8(door + k.action) == 1;  // a loaded state's open door
        if (front < 0 && !left_open) {
            orig(c);
            return;
        }
        if (left_open) {
            it = g_doors.emplace(door, Door{kOpen, now}).first;
            if (kLog) LOG("[doors] %s %08X found open: walk-through", k.name, door);
        } else {
            st8(door + kFront, (uint8_t)front);
            guest_call(c, k.open_init, {door});
            it = g_doors.emplace(door, Door{kOpening, now}).first;
            if (kLog) LOG("[doors] %s %08X opening (Link %.0f along, %.0f side; room %d)", k.name, door, w.along, w.side, stay_no());
        }
    }
    Door& d = it->second;
    d.seen = now;
    switch (d.st) {
        case kOpening:
            for (int i = 0; i < 2; i++) {
                st8(door + kEvtKind, 0);  // door12's openProc pulls Link to the door for the scene's kinds 2, 3, 11
                if (guest_call(c, k.open_proc, {door})) {
                    guest_call(c, k.open_end, {door});
                    d.st = kOpen;
                    if (kLog) LOG("[doors] %s %08X open", k.name, door);
                    break;
                }
            }
            break;
        case kOpen:
            if (!in_zone(w, kCloseAlong, kCloseSide) && event_mode_now() == 0) {
                guest_call(c, k.close_init, {door});
                d.st = kClosing;
                if (kLog) LOG("[doors] %s %08X closing (Link %.0f along; room %d)", k.name, door, w.along, stay_no());
            }
            break;
        case kClosing: {
            if (!by_a && in_zone(w, kOpenAlong, kOpenSide) && walk_doors() && event_mode_now() == 0) {
                // Link came back: open again from where it is
                int front = may_open(c, door, link, k);
                if (front >= 0) {
                    st8(door + kFront, (uint8_t)front);
                    guest_call(c, k.open_init, {door});
                    d.st = kOpening;
                    if (kLog) LOG("[doors] %s %08X opening again", k.name, door);
                    break;
                }
            }
            if (!guest_call(c, k.close_proc, {door})) break;
            uint16_t ax = ld16(door + kHomeAngleX);
            int f = ax & 0x3F, b = (ax >> 6) & 0x3F, stay = stay_no();
            if (stay == f || stay == b) {  // the room behind Link hidden, the restart point on his side
                st8(door + kFrom, (uint8_t)(stay == f ? b : f));
                st8(door + kTo, (uint8_t)stay);
                st8(door + kFront, stay == f ? 0 : 1);
            } else {
                st8(door + kFrom, (uint8_t)f);
                st8(door + kTo, (uint8_t)f);  // neither room is Link's: hide nothing
            }
            guest_call(c, k.close_end, {door});
            // the door's own wait again; bars that come down only in the scene (setStop) come down now
            guest_call(c, k.set_stop, {door});
            if (ld8(door + k.stop + 8) != 0 && ld32(door + k.stop) != 0) {
                guest_call(c, kStopCloseInit, {door + k.stop, door});
                st8(door + k.action, 2);
            } else {
                st8(door + k.action, 1);
            }
            if (kLog) LOG("[doors] %s %08X shut (room %d, bars %u)", k.name, door, stay, ld8(door + k.stop + 8));
            g_doors.erase(it);
            break;
        }
    }
    st8(door + kRoomNo2, ld8(kStayNo));
    c->r[3] = 1;
}

// ---- knob doors (daKnob00: the wooden doors of houses, shops, ...) ----
// Most lead into another stage: their scene's Link walks through the doorway onto an exit polygon, and the exit
// (dStage_changeSceneExitId from daPy_lk_c::checkFallCode, during the door event or a compulsory event Link orders
// for it, as for any exit) loads the next stage behind the fade. Here the door swings open and stays open, Link walks
// in himself and the same exit takes him in; he walks away instead: the door swings shut.
// The door's own animation (Knob.bfres FSKA dooropenadoor / dooropenbdoor, 80 frames, Y rotation of DoorDummy):
// A (Link on the front side) moves from frame 26, wide open at 53, shut at 66; B from 20, open at 45, shut at 63.
// HD daKnob00_c: openInit(int) 021A3AA0 (anim index 0 A, 1 B; the collision released), openProc(int) 021A3BF8 (one
// frame of the animation, its sounds for the cut kind: 14 for A, 15 for B, no fade; TRUE at the end), openEnd 021A3E14
// (collision back, closeEndCom); mAction 0x5A4 (1 wait), m312 0x5A6, a pending collision registration 0x5A7,
// the animation's frame 0x508 (mBckAnm 0x504 + 4).
// Wider swing: the animation opens the door only about 40 degrees (0.68 rad at A's frame 53, 0.61 at B's 45; the
// scene's fade hides it), so while one of our doors is open the rotation curve is scaled (the curve's own scale and
// offset in the animation resource; values = offset + scale * cubic). The resource: mBckAnm + 0x88 (0x58C) holds the
// animation object set by openInit; the FSKA block is found from it (the object or a pointer in it) and checked by
// its magic. Restored once no door of ours is open.
// Arrival: coming through a knob door, the next stage puts Link on the far side of its door there and his start event
// (a system event: daPy_demo_c type 2, mode DEMO_DOOR 35) walks him through it to about 85 units inside, with the
// door still in its init action. With walk-through doors that walk is ended on its first frame: the event reset
// (dEvt_control_c reset flag 8 in the halfword at game info + 0x52B8, as the knob's actionDemo does) and Link set
// where the walk ends with the door's own setStart 021A39CC (f1 sideways, f2 along: minus along its facing); his own
// step ends his demo mode once no event runs. The door then waits until Link has left its zone once.
// HD daPy_py_c::mDemo at 0x420 (the knob's setAngle writes its move angle at 0x422): type u16 0x420, mode u32 0x430.
const uint32_t kKnobOpenInit = GC(0x021A3AA0), kKnobOpenProc = GC(0x021A3BF8), kKnobOpenEnd = GC(0x021A3E14);
const uint32_t kKnobSetStart = GC(0x021A39CC), kGameInfo = GC(0x025200D4);
constexpr uint32_t kKnobAction = 0x5A4, kKnobRegist = 0x5A7, kKnobFrame = 0x508, kKnobAnmRes = 0x58C;
constexpr uint32_t kEvtResetFlags = 0x52B8;
const float kKnobSwing = env_f("WWHD_WALK_DOORS_SWING", 2.2f);
constexpr float kArriveAlong = 85.0f;  // where the door walk ends, on the side away from where Link starts
constexpr uint32_t kArriveSteps = 60;  // a door created this many logic steps ago or less: an arrival
constexpr uint32_t kLinkDemoType = 0x420, kLinkDemoMode = 0x430;
// The event hides Link while he waits behind the door (its PLAYER_NODRAW step, daPy_py_c::mNoResetFlg0 bit
// daPyFlg0_NO_DRAW 0x08000000; HD 0x3B8 = GameCube 0x29C + 0x11C, the bit cleared in 31 places of the HD code) and
// shows him again in a later step: cleared here. Link is set again each step until the event is over and he
// stays where he was set (a set during the event's last steps did not hold: he was left on the exit in the doorway
// and the exit took him back).
constexpr uint32_t kLinkFlg0 = 0x3B8, kLinkNoDraw = 0x08000000;
// The walk's start (daPy_lk_c::dProcDoorOpen_init, HD 02423890) turns off Link's wall collision and line check
// (mAcch.m_flags |= WALL_NONE 0x4 | LINE_CHECK_NONE 0x4000, HD Link + 0x834); its end turns them on again. Ended
// early, they stayed off: Link walked through the closed door and into the void. Cleared here.
constexpr uint32_t kLinkAcchFlags = 0x834, kLinkAcchNone = 0x4004;
// The exit itself (the doorway's exit polygon, Link's walk while the stage changes): with the house door opened by us
// and Link in its doorway, the scene change's fade runs as with Fast scene changes (turbo.cpp: extra runs of the
// overlap's timers and the scene request phases per frame, nothing else) for up to 8 s, ended 2 s after the arrival.
std::atomic<std::chrono::steady_clock::time_point> g_exit_until{};
uint32_t g_start_walk_cut = 0;  // logic step the door walk was ended
// setStart's "along" is not the same way round on every door (shape angle against home angle: outside Orca's house
// it put Link on the exit in the doorway): where he lands is checked and the other way taken if needed.
// The door's collision is released for the walk and registered again in a step skipped here: once Link holds,
// a door whose collision (mpBgW 0x594, its first word: 0x100 or more = not registered) is out gets its own
// pending registration (0x5A7), done at the start of its next step.
constexpr uint32_t kKnobBgW = 0x594;
struct Placing {
    uint32_t door = 0;
    float along = 0.0f, z = 0.0f;
    int held = 0, tries = 0;
} g_place;
constexpr float kKnobOpenAlong = 170.0f, kKnobOpenSide = 90.0f, kKnobCloseAlong = 230.0f, kKnobCloseSide = 140.0f;
struct KnobAnim {
    float start, hold;
    uint32_t cut;
};
constexpr KnobAnim kKnobAnim[2] = {{24.0f, 53.0f, 14}, {18.0f, 45.0f, 15}};
struct Knob {
    State st;
    uint32_t seen;
    int side;
    bool past;  // logged: Link went past the doorway
};
std::unordered_map<uint32_t, Knob> g_knobs;
std::unordered_map<uint32_t, uint32_t> g_first;  // knob door -> logic step it was first seen
std::unordered_map<uint32_t, bool> g_arrived;    // knob door Link arrived through: no opening until he leaves
struct CurvePatch {
    uint32_t fska, curve;
    uint32_t scale, offset;  // the original words
};
std::vector<CurvePatch> g_swing;  // curves scaled now

uint32_t find_fska(uint32_t res) {
    if (!guest_ptr(res)) return 0;
    if (ld32(res) == 0x46534B41) return res;
    for (uint32_t o = 0; o <= 0x20; o += 4) {
        uint32_t p = ld32(res + o);
        if (guest_ptr(p) && ld32(p) == 0x46534B41) return p;
    }
    for (uint32_t o = 0; o <= 0x10; o += 4) {
        uint32_t p = ld32(res + o);
        if (!guest_ptr(p)) continue;
        for (uint32_t q = 0; q <= 0x20; q += 4) {
            uint32_t r = ld32(p + q);
            if (guest_ptr(r) && ld32(r) == 0x46534B41) return r;
        }
    }
    return 0;
}
uint32_t rel(uint32_t a) { return a + (uint32_t)(int32_t)ld32(a); }

void swing_wide(uint32_t door) {
    if (kKnobSwing == 1.0f) return;
    uint32_t res = ld32(door + kKnobAnmRes);
    uint32_t fska = find_fska(res);
    if (!fska) {
        if (kLog) LOG("[doors] knob %08X: no FSKA from %08X", door, res);
        return;
    }
    int bones = ld16(fska + 0x14);
    uint32_t ba = rel(fska + 0x20);
    for (int i = 0; i < bones && i < 4; i++) {
        uint32_t bo = ba + (uint32_t)i * 0x18;
        int ncur = ld8(bo + 0xA);
        uint32_t ca = rel(bo + 0x10);
        for (int k = 0; k < ncur && k < 8; k++) {
            uint32_t co = ca + (uint32_t)k * 0x24;
            bool done = false;
            for (auto& p : g_swing) done |= p.curve == co;
            if (done) continue;
            uint32_t sc = ld32(co + 0x10), off = ld32(co + 0x14);
            g_swing.push_back({fska, co, sc, off});
            st32(co + 0x10, f32_as_u32(u32_as_f32(sc) * kKnobSwing));
            st32(co + 0x14, f32_as_u32(u32_as_f32(off) * kKnobSwing));
            if (kLog) LOG("[doors] knob %08X: curve %08X scale %.4f -> %.4f", door, co, u32_as_f32(sc), u32_as_f32(sc) * kKnobSwing);
        }
    }
}
void swing_restore() {
    for (auto& p : g_swing) {
        if (ld32(p.fska) != 0x46534B41) continue;  // the resource is gone
        st32(p.curve + 0x10, p.scale);
        st32(p.curve + 0x14, p.offset);
    }
    g_swing.clear();
}

int knob_may_open(uint32_t door, uint32_t link) {
    if (event_mode_now() != 0) return -1;
    if (ld8(door + kKnobAction) != 1 || ld8(door + kKnobRegist) != 0) return -1;
    if (carrying(link)) return -1;
    int front = ld8(door + kFront);
    if (front > 1) return -1;
    uint16_t ax = ld16(door + kHomeAngleX);
    int f = ax & 0x3F, b = (ax >> 6) & 0x3F;
    if (f != b && f != 0x3F && b != 0x3F) {  // two rooms of one stage: the far one must be there
        uint8_t far = room_flags(front == 0 ? b : f);
        if (!(far & 1) || (far & 6)) return -1;
    }
    return front;
}

// Link's start walk through the door he arrived by: ended, Link set where it would end (see above).
bool knob_door_walk(Cpu* c, uint32_t door, uint32_t link, uint32_t now) {
    if (ld16(link + kLinkDemoType) != 2 || ld32(link + kLinkDemoMode) != 35 || event_mode_now() == 0) return false;
    if (now - g_start_walk_cut < 60) return false;
    auto f = g_first.find(door);
    if (f == g_first.end() || now - f->second > kArriveSteps) return false;
    Where w = where(door, link);
    if (std::fabs(w.along) > 160.0f || w.side > 90.0f || std::fabs(w.up) > 100.0f) return false;
    float along = w.along > 0.0f ? -kArriveAlong : kArriveAlong;
    float z = -along;
    c->f[1].ps0 = 0.0;
    c->f[2].ps0 = z;
    guest_call(c, kKnobSetStart, {door});
    if (std::fabs(where(door, link).along - along) > 25.0f) {  // the other way round on this door
        z = -z;
        c->f[1].ps0 = 0.0;
        c->f[2].ps0 = z;
        guest_call(c, kKnobSetStart, {door});
    }
    st32(link + kLinkFlg0, ld32(link + kLinkFlg0) & ~kLinkNoDraw);
    st32(link + kLinkAcchFlags, ld32(link + kLinkAcchFlags) & ~kLinkAcchNone);
    g_place = Placing{door, along, z, 0, 0};
    uint32_t gi = guest_call(c, kGameInfo);
    st16(gi + kEvtResetFlags, ld16(gi + kEvtResetFlags) | 8);
    g_start_walk_cut = now;
    g_arrived[door] = true;
    trace("walk doors: arrival walk ended at knob %08X", door);
    // the fade-in still runs fast for a moment
    auto soon = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    if (g_exit_until.load() > soon) g_exit_until = soon;
    if (kLog) LOG("[doors] knob %08X: Link's door walk ended (he was %.0f along, %.0f side; set at %.0f)", door, w.along, w.side, along);
    c->r[3] = door;  // the calls above returned in r3: the door's own step needs the door
    f_021A5008_orig(c);
    return true;
}

void knob_execute(Cpu* c) {
    uint32_t door = c->r[3];
    uint32_t now = ld32(kCounter);
    auto sn = g_seen.find(door);
    if (sn == g_seen.end() || sn->second.kind || now - sn->second.seen > 8) {  // a door new here (or a new one at an old address)
        g_first[door] = now;
        g_arrived.erase(door);
        if (kLog)  // (port: where the doors are, for tests)
            LOG("[doors] knob %08X at (%.0f, %.0f, %.0f) facing (%.2f, %.2f)", door, ldf(door + kPos), ldf(door + kPos + 4),
                ldf(door + kPos + 8), ldf(door + kVec), ldf(door + kVec + 8));
    }
    g_seen[door] = Seen{nullptr, now};
    // doors of ours not seen for a while (another stage now): forgotten, the swing put back once none is left
    for (auto k = g_knobs.begin(); k != g_knobs.end();) {
        if (now - k->second.seen > 8) k = g_knobs.erase(k);
        else ++k;
    }
    if (g_knobs.empty() && !g_swing.empty()) swing_restore();
    auto it = g_knobs.find(door);
    uint32_t link = link_actor();
    if (!guest_ptr(link) || (it == g_knobs.end() && !walk_doors())) {
        f_021A5008_orig(c);
        return;
    }
    if (g_place.door == door) {  // Link set after the door walk: again until it holds
        Where wp = where(door, link);
        bool there = std::fabs(wp.along - g_place.along) < 25.0f && wp.side < 40.0f;
        st32(link + kLinkFlg0, ld32(link + kLinkFlg0) & ~kLinkNoDraw);
        st32(link + kLinkAcchFlags, ld32(link + kLinkAcchFlags) & ~kLinkAcchNone);
        if (kLog && g_place.tries < 40) LOG("[doors] knob %08X: Link %.0f along %.0f side (event %u, demo %u/%u)", door, wp.along, wp.side, event_mode_now(), ld16(link + kLinkDemoType), ld32(link + kLinkDemoMode));
        if (there && event_mode_now() == 0) {
            if (++g_place.held >= 3) {
                g_place.door = 0;
                uint32_t bgw = ld32(door + kKnobBgW);
                if (guest_ptr(bgw) && ld32(bgw) >= 0x100 && ld8(door + kKnobRegist) == 0) {
                    st8(door + kKnobRegist, 1);
                    if (kLog) LOG("[doors] knob %08X: collision registered again", door);
                }
            }
        } else if (!there) {
            g_place.held = 0;
            c->f[1].ps0 = 0.0;
            c->f[2].ps0 = g_place.z;
            guest_call(c, kKnobSetStart, {door});
            c->r[3] = door;
        }
        if (++g_place.tries > 90) g_place.door = 0;
    }
    if (it == g_knobs.end() && knob_door_walk(c, door, link, now)) {
        c->r[3] = 1;
        return;
    }
    if (it == g_knobs.end() && now - g_start_walk_cut < 30 && !g_arrived.count(door) &&
        in_zone(where(door, link), kKnobCloseAlong, kKnobCloseSide)) {
        g_arrived[door] = true;  // Link stands at the door he came through: it waits until he has left once
    }
    // (port) Link starts the stage standing at this door (the stage's start event runs as the door is first seen,
    // e.g. the doors of the Forsaken Fortress's tower): it waits until he has left once, rather than swinging open
    // behind him at once
    if (it == g_knobs.end() && !g_arrived.count(door) && event_mode_now() != 0 && now - g_first[door] <= 3 &&
        in_zone(where(door, link), kKnobOpenAlong, kKnobOpenSide)) {
        g_arrived[door] = true;
        if (kLog) LOG("[doors] knob %08X: Link starts here; it waits until he has left", door);
    }
    Where w = where(door, link);
    bool by_a = walk_doors_by_a();
    if (it == g_knobs.end() && g_arrived.count(door)) {
        if (!in_zone(w, kKnobCloseAlong, kKnobCloseSide)) {
            g_arrived.erase(door);
        } else if (!by_a) {
            f_021A5008_orig(c);
            return;
        }
    }
    if (it == g_knobs.end()) {
        int front = -1;
        if (by_a) {
            if (g_open_request == door) front = knob_may_open(door, link);
        } else if (in_zone(w, kKnobOpenAlong, kKnobOpenSide)) {
            front = knob_may_open(door, link);
        }
        if (g_open_request == door) g_open_request = 0;
        if (front < 0) {
            f_021A5008_orig(c);
            return;
        }
        guest_call(c, kKnobOpenInit, {door, (uint32_t)front});
        swing_wide(door);
        st32(door + kKnobFrame, f32_as_u32(kKnobAnim[front].start));
        it = g_knobs.emplace(door, Knob{kOpening, now, front, false}).first;
        if (kLog) LOG("[doors] knob %08X opening (side %d; Link %.0f along, %.0f side; room %d)", door, front, w.along, w.side, stay_no());
    }
    Knob& d = it->second;
    d.seen = now;
    const KnobAnim& an = kKnobAnim[d.side];
    if (!d.past && std::fabs(w.along) < 30.0f && w.side < 60.0f) {
        d.past = true;
        g_exit_until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        trace("walk doors: Link in the doorway of knob %08X", door);
        if (kLog) LOG("[doors] knob %08X: Link in the doorway (%.0f along)", door, w.along);
    }
    switch (d.st) {
        case kOpening:
            for (int i = 0; i < 2 && d.st == kOpening; i++) {
                guest_call(c, kKnobOpenProc, {door, an.cut});
                if (ldf(door + kKnobFrame) >= an.hold) {
                    st32(door + kKnobFrame, f32_as_u32(an.hold));
                    d.st = kOpen;
                    if (kLog) LOG("[doors] knob %08X open", door);
                }
            }
            break;
        case kOpen:
            if (!in_zone(w, kKnobCloseAlong, kKnobCloseSide) && event_mode_now() == 0) {
                d.st = kClosing;
                if (kLog) LOG("[doors] knob %08X closing (Link %.0f along)", door, w.along);
            }
            break;
        case kClosing:
            if (!guest_call(c, kKnobOpenProc, {door, an.cut})) break;
            guest_call(c, kKnobOpenEnd, {door});
            if (kLog) LOG("[doors] knob %08X shut", door);
            g_knobs.erase(it);
            if (g_knobs.empty()) swing_restore();
            break;
    }
    st8(door + kRoomNo2, ld8(kStayNo));
    c->r[3] = 1;
}

// the door executes, called instead of the game's through the method table (DoorExecuteWrap)
PpcFunc g_door10_orig = nullptr, g_door12_orig = nullptr;
void door10_execute(Cpu* c) { door_execute(c, kDoor10, g_door10_orig); }
void door12_execute(Cpu* c) { door_execute(c, kDoor12, g_door12_orig); }
void knob_host_execute(Cpu* c) { knob_execute(c); }
struct Wrapped {
    uint32_t game, host;
};
Wrapped g_wrapped[3];
bool g_wrap_ready = false;
}  // namespace

bool walk_door_exit() {
    return walk_doors() && std::chrono::steady_clock::now() < g_exit_until.load();
}

// fpcM_Execute's hook (game_hooks.cpp): while the mode is on, or a door of ours is still open, a door's
// execute in its method table (fopAc_ac_c::sub_method 0xF0, its execute +8) points at our host function for this
// one call. The game's fopAc_Execute between the two still decides whether the door runs this step.
DoorExecuteWrap::DoorExecuteWrap(uint32_t proc) {
    if (!walk_doors() && g_doors.empty() && g_knobs.empty() && g_place.door == 0) return;
    if (!g_wrap_ready) {
        g_door10_orig = dispatch::lookup(kDoor10Execute);
        g_door12_orig = dispatch::lookup(kDoor12Execute);
        g_knob_orig = dispatch::lookup(kKnobExecute);
        if (!g_door10_orig || !g_door12_orig || !g_knob_orig) return;
        g_wrapped[0] = {kDoor10Execute, dispatch::register_host(door10_execute, "walk_doors door10 execute")};
        g_wrapped[1] = {kDoor12Execute, dispatch::register_host(door12_execute, "walk_doors door12 execute")};
        g_wrapped[2] = {kKnobExecute, dispatch::register_host(knob_host_execute, "walk_doors knob execute")};
        g_wrap_ready = true;
    }
    uint32_t sub = ld32(proc + 0xF0);
    if (!guest_ptr(sub)) return;
    uint32_t fn = ld32(sub + 8);
    for (const Wrapped& w : g_wrapped)
        if (fn == w.game) {
            slot_ = sub + 8, game_ = fn;
            st32(slot_, w.host);
            return;
        }
}
DoorExecuteWrap::~DoorExecuteWrap() {
    if (slot_) st32(slot_, game_);
}

// a save state was loaded (interp::ss_reset): the doors, rooms and Link are the state's now
void walk_doors_reset() {
    g_doors.clear();
    g_seen.clear();
    g_knobs.clear();
    g_first.clear();
    g_arrived.clear();
    g_swing.clear();  // (the curves are the state's now: nothing to put back)
    g_place = Placing{};
    g_open_request = 0;
    g_exit_until = std::chrono::steady_clock::time_point{};
}

}  // namespace mods

// fopAcM_orderDoorEvent (Link, door): A on a door's "Open" prompt. With walk-through doors opening on A, a door that
// may open by itself takes the press instead of the door scene (Link goes back to waiting, as after his order).
// Off (or for any other door): the game's own function.
extern "C" void hook_025D7710(Cpu* c) {
    using namespace mods;
    uint32_t link = c->r[3], door = c->r[4];
    if (walk_doors() && walk_doors_by_a() && guest_ptr(door)) {
        auto s = g_seen.find(door);
        if (s != g_seen.end() && ld32(kCounter) - s->second.seen <= 8 &&
            (s->second.kind ? may_open(c, door, link, *s->second.kind) : knob_may_open(door, link)) >= 0) {
            g_open_request = door;
            if (kLog) LOG("[doors] %s %08X: A taken", s->second.kind ? s->second.kind->name : "knob", door);
            c->r[3] = 1;
            return;
        }
    }
    f_025D7710_orig(c);
}
