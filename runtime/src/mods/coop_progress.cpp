// SPDX-License-Identifier: MPL-2.0
// From Galvakron/ZeldaWWHDRecompMP 8a4400d (MPL-2.0), co-op work by AsadSi and Galvakron; adapted to this port.
//
// Online co-op, the shared progress (coop_progress.h).
//
// The fields are dSv_save_c's (zeldaret/tww include/d/d_save.h, CC0; the same offsets as savestate.cpp's). A stage's
// memory bits are kept twice: the stage being played has them live in dSv_info_c::mMemory (put back into its slot of
// the table as it is left), the others in the table, so a stage's bits are read and written where they are now. A
// change goes as {u16 offset, u8 size, the field's new value, and for bit fields the bits that changed}; bit fields go
// byte by byte, so two players' changes to different bits of one byte both hold.
//
// The rooms being played see the others' doing at once (Anchor's "a flag set while the actor is loaded"): a loaded
// object whose chest bit or switch came in is made again where it stands, and its own create decides as it would for a
// room entered with that bit: a chest comes up opened (daTbox_c::CreateInit), a boulder or bomb wall is not made at all,
// a locked door has no lock (dDoor_info_c), a ladder is down (kKinds below). The process names are the game's own
// (its object name table), as the HD game numbers processes its own way.
#include "coop_progress.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "coop_net.h"
#include "guest_addr.h"
#include "mods.h"
#include "runtime.h"

namespace mods::coop::progress {
namespace {

const uint32_t kSaveInfoPtr = GD(0x101F84DC);  // dComIfGs save area, dSv_info_c at +0x20 (savestate.cpp)
const uint32_t kNextStage = GD(0x1046F0B0) + 0x5140;  // dStage_nextStage_c, enabled at +12
constexpr uint32_t kInfo = 0x20, kLive = 0x778, kStageNo = 0x79C, kDataNum = 0x1290;  // dSv_info_c: mMemory, mDan.mStageNo, mDataNum
constexpr uint16_t kTable = 0x380, kMemSize = 0x24, kStages = 16, kSaveSize = 0x778;
constexpr int kReplaced = 40;  // more fields changed in one step than play does: the save data was replaced (a load)

// the union merge (Anchor's way, DESIGN.md Q6): kBits only set, kMax the larger, kFill only where this game has
// none (0xFF) or the same kind's better one, kSet as it comes (a shared count: small keys), kBottle a new bottle
// as an empty one
enum Mode : uint8_t { kBits, kSet, kMax, kBottle, kFill };
struct Field { uint16_t off, size; Mode mode; };

std::vector<Field> fields() {
    std::vector<Field> f = {
        {0x000, 2, kMax},      // dSv_player_status_a_c: max life (heart containers and pieces)
        // (current life +0x002, magic, rupees +0x004: each player's own, Anchor's way; the fork shared the life)
        {0x00E, 3, kFill},     //   sword, shield, bracelets (mSelectEquip): filled where empty, never taken away
        {0x012, 1, kMax},      //   wallet size
        {0x013, 1, kMax},      //   max magic
        {0x051, 21, kBits},    // dSv_player_get_item_c
        {0x06E, 1, kMax}, {0x06F, 1, kMax}, {0x070, 1, kMax},  // dSv_player_item_max_c: arrows, bombs
        {0x0B4, 13, kBits},    // dSv_player_collect_c: collected items, songs, Triforce, pearls
        {0x0C4, 0x84, kBits},  // dSv_player_map_c: charts, sea map
        {0x5C0, 0x64, kBits},  // dSv_ocean_c: salvage
        {0x624, 0x100, kBits}, // dSv_event_c: the story
    };
    for (uint16_t i = 0; i < 21; i++)  // dSv_player_item_c, dInvSlot_* (bottles 14-17)
        f.push_back({(uint16_t)(0x03C + i), 1, i >= 14 && i <= 17 ? kBottle : kFill});
    for (uint16_t s = 0; s < kStages; s++) {  // dSv_memBit_c of each stage
        const uint16_t m = kTable + s * kMemSize;
        f.push_back({m, 4, kBits});                 // chests
        f.push_back({(uint16_t)(m + 0x04), 16, kBits});  // switches 0-127
        f.push_back({(uint16_t)(m + 0x14), 4, kBits});   // pickups
        f.push_back({(uint16_t)(m + 0x18), 8, kBits});   // visited rooms
        f.push_back({(uint16_t)(m + 0x20), 1, kSet});    // small keys
        f.push_back({(uint16_t)(m + 0x21), 1, kBits});   // map, compass, big key, boss beaten, heart taken, boss met
    }
    return f;
}
const std::vector<Field> kFields = fields();

const uint32_t kActorQueue = GD(0x101F3328);  // g_fopAcTg_Queue: node next +8, actor +0xC (turbo.cpp)
const uint32_t kObjNames = GD(0x101EBFDC);    // the object name table: name[8], s16 process, s8 subtype, u8 (extras.cpp)
const uint32_t kLinkPtr = GD(0x1046F0B0) + 0x5B34;
// fopAc_ac_c (WWHD): base_process_class process name +8, parameters +0xB0, argument +0xB4 (as on the GameCube); home
// (pos +0, angle +0xC, room +0x12) +0x2EC, scale +0x330, Link's process layer +0x2C (extras.cpp create_at)
constexpr uint32_t kProcName = 0x08, kParams = 0xB0, kArgument = 0xB4, kHome = 0x2EC, kScale = 0x330, kLayer = 0x2C;

std::vector<uint8_t> g_was;           // the save data as of the last step (empty: none yet)
uint8_t g_data_num = 0xFF;
std::deque<std::string> g_in;         // the others' changes not put in yet

// Rupees: the wallet (sv +4) plus the meter's count still to come (play +0x5B48, s32), which a pickup or chest adds to and
// a shop takes from. A rise is a gain, told to the others; a fall is spending (or a full wallet), this player's own. What
// comes in goes through the meter too, and the baseline moves past it, so it isn't told again.
const uint32_t kRupeeCount = GD(0x1046F0B0) + 0x5B48;
constexpr int kMostRupees = 5000;  // the largest wallet
int g_purse = -1;                  // the wallet as of the last step (-1: none yet)
int g_rupees_in = 0;               // the others' gains not put in yet
// rupee sharing: a party option, off by default (each player's own wallet; WWHD_COOP_SHARE_RUPEES=1 turns it on)
bool share_rupees() {
    static const bool on = [] { const char* e = getenv("WWHD_COOP_SHARE_RUPEES"); return e && atoi(e); }();
    return on;
}
int wallet(uint32_t info) { return ld16(info + 4) + (int32_t)ld32(kRupeeCount); }

// a byte of dSv_save_c where it is now (a played stage's memory bits: live)
uint32_t at(uint32_t info, uint16_t off) {
    if (off >= kTable && off < kTable + kStages * kMemSize) {
        const int8_t cur = (int8_t)ld8(info + kStageNo);
        if (cur >= 0 && cur < kStages && (off - kTable) / kMemSize == cur) return info + kLive + (off - kTable) % kMemSize;
    }
    return info + off;
}
void read(uint32_t info, std::vector<uint8_t>& out) {
    out.resize(kSaveSize);
    for (uint16_t o = 0; o < kSaveSize; o++) out[o] = ld8(at(info, o));
}
uint32_t value(const uint8_t* p, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = v << 8 | p[i];
    return v;
}

// the others' changes in; out: the fields as they are now ("04A=50 66B=03"), for the trace
std::string put_in(uint32_t info, const std::string& m) {
    const auto* d = (const uint8_t*)m.data();
    std::string done;
    for (size_t i = 0; i + 3 <= m.size();) {
        const uint16_t off = (uint16_t)(d[i] | d[i + 1] << 8);
        const uint8_t size = d[i + 2];
        const Field* f = nullptr;
        for (const Field& k : kFields) if (off >= k.off && off + size <= k.off + k.size) { f = &k; break; }
        const size_t len = 3 + size * (f && f->mode == kBits ? 2 : 1);
        if (!f || i + len > m.size()) break;  // (the same build on both: not expected)
        const uint8_t* v = d + i + 3;
        if (f->mode == kBits) {
            for (int b = 0; b < size; b++) {
                const uint32_t a = at(info, off + b);
                st8(a, (uint8_t)((ld8(a) & ~v[size + b]) | (v[b] & v[size + b])));
            }
        } else if (f->mode == kFill) {
            for (int b = 0; b < size; b++) {
                const uint32_t a = at(info, off + b);
                const uint8_t cur = ld8(a);
                if (v[b] != 0xFF && (cur == 0xFF || cur == 0 || (cur < v[b] && off == 0x00E))) st8(a, v[b]);
            }
        } else if (f->mode == kBottle) {
            if (ld8(at(info, off)) == 0xFF && v[0] != 0xFF) st8(at(info, off), 0x50);  // dItemNo_EMPTY_BOTTLE_e
        } else {
            uint8_t cur[4];
            for (int b = 0; b < size; b++) cur[b] = ld8(at(info, off + b));
            if (f->mode == kSet || value(v, size) > value(cur, size))
                for (int b = 0; b < size; b++) st8(at(info, off + b), v[b]);
        }
        char t[16];
        snprintf(t, sizeof t, " %03X=%02X", off + size - 1, ld8(at(info, off + size - 1)));  // (its last byte)
        done += t, i += len;
    }
    return done;
}

// a process name from the object name table (-1: none)
int16_t proc_of(const char* name) {
    for (uint32_t e = kObjNames; e < kObjNames + 12 * 2000 && ld8(e); e += 12)
        if (!strncmp((const char*)mem::ptr(e), name, 8)) return (int16_t)ld16(e + 8);
    return -1;
}
int16_t tbox_proc() {
    static const int16_t proc = proc_of("takara");
    return proc;
}

// Kinds of objects made from a switch (zeldaret/tww d_a_*.cpp: their create or chk_appear reads it): a door made with
// its switch set is unlocked, a wall or boulder is not made at all, a ladder is down, a hammer peg is in. Each one's
// switches are its parameter bits `sw[i]`..+7 (dDoor_info_c::getSwbit/getSwbit2, prm_get_swSave and the like); -1 none.
// Enemies, NPCs, event tags, triggers and water levels are left alone (Anchor's list keeps water levels local too).
struct Kind { const char* name; int8_t sw[2]; };
constexpr Kind kKinds[] = {
    {"door10", {0, 20}}, {"door12", {0, 20}}, {"ATdoor", {0, -1}}, {"SMBdor", {0, -1}}, {"Mori1", {0, -1}},  // doors
    {"SWTdoor", {24, -1}}, {"Wall", {0, -1}},                                              // bomb walls (daWall)
    {"Ebrock", {8, -1}}, {"koisi1", {8, -1}}, {"Eskban", {0, -1}}, {"Ebomzo", {0, -1}},  // boulders, bomb rocks
    {"Akabe", {0, -1}}, {"MkieBA", {16, -1}}, {"MkieK", {0, -1}}, {"MknjD", {0, -1}},    // breakable walls
    {"Ikori", {0, -1}}, {"Vochi", {13, -1}}, {"Hcbh", {13, -1}}, {"Kanat", {0, -1}}, {"Mcube", {8, -1}},
    {"Ohatch", {0, -1}}, {"Shmrgrd", {0, -1}}, {"Tenmado", {0, -1}}, {"Vfan", {0, -1}}, {"VmcBS", {8, -1}},
    {"MsdanSb", {0, -1}}, {"MsdanS2", {0, -1}}, {"Msdan2", {0, -1}}, {"SMtoge", {0, -1}}, {"Yboil00", {8, -1}},
    {"Gryw00", {0, -1}}, {"Hami3", {0, 8}}, {"Hami4", {0, -1}}, {"Hbrf1", {0, -1}}, {"Hlift", {8, -1}},
    {"Mhsg6", {8, -1}}, {"Esekh", {8, -1}}, {"Ykzyg", {8, -1}}, {"YLzou", {0, -1}}, {"MhmrSW0", {8, -1}},  // ladders..
};
constexpr int kKindCount = sizeof kKinds / sizeof kKinds[0];

// what came in and is still to be shown here: chest bits per stage, the played stage's switches; each object made again
// once (by its kind and home), not while Link is within 400 of it (he may be going through that door), for 300 steps
uint32_t g_pend_tbox[kStages], g_pend_sw[4];
int g_pend_stage = -1, g_pend_steps = 0;
std::vector<uint64_t> g_done;

uint64_t ident(int16_t proc, uint32_t a) { return (uint64_t)(uint16_t)proc << 48 ^ (uint64_t)ld32(a + kHome) << 16 ^ ld32(a + kHome + 8); }

// the loaded objects the others' bits change, made again where they stand (fopAcM_delete, fopAcM_create with their own
// parameters, home, scale and argument), so their create decides as for a room entered with those bits
void catch_up(Cpu* c, int cur) {
    static int16_t procs[kKindCount];
    static const bool looked = [] { for (int i = 0; i < kKindCount; i++) procs[i] = proc_of(kKinds[i].name); return true; }();
    (void)looked;
    const uint32_t link = ld32(kLinkPtr);
    if (!link) return;
    const float lx = (float)ldf32(link + 0x314), lz = (float)ldf32(link + 0x31C);
    struct Remake { uint32_t a; int16_t proc; const char* what; };
    std::vector<Remake> todo;
    uint32_t n = ld32(kActorQueue);
    for (int k = 0; n >= 0x10000000 && n < 0x50000000 && k < 4096; k++, n = ld32(n + 8)) {
        const uint32_t a = ld32(n + 0xC);
        if (!a) continue;
        const int16_t proc = (int16_t)ld16(a + kProcName);
        const uint32_t prm = ld32(a + kParams);
        const char* what = nullptr;
        if (proc >= 0 && proc == tbox_proc()) {
            const uint32_t type = prm & 0x7F, no = prm >> 7 & 0x1F;
            const int stage = type == 7 || type == 8 ? 1 : cur;  // FUNC_TYPE_EXTRA_SAVE_INFO(_SPAWN): STAGE_SEA2's bits
            if (stage >= 0 && g_pend_tbox[stage] >> no & 1) what = "takara";
        } else if (proc >= 0) {
            for (int i = 0; i < kKindCount && !what; i++)
                if (procs[i] == proc)
                    for (const int8_t sh : kKinds[i].sw)
                        if (const uint32_t sw = prm >> (sh < 0 ? 0 : sh) & 0xFF; sh >= 0 && sw < 128 && g_pend_sw[sw / 32] >> (sw % 32) & 1)
                            what = kKinds[i].name;
        }
        if (!what || std::find(g_done.begin(), g_done.end(), ident(proc, a)) != g_done.end()) continue;
        if (std::hypot((float)ldf32(a + kHome) - lx, (float)ldf32(a + kHome + 8) - lz) < 400) continue;  // (later)
        todo.push_back({a, proc, what});
    }
    static const uint32_t buf = mem::host_alloc(0x30);  // pos +0, angle +0x10, scale +0x20
    for (const Remake& r : todo) {
        const uint32_t a = r.a, prm = ld32(a + kParams), arg = ld32(a + kArgument), room = (uint32_t)(int8_t)ld8(a + kHome + 0x12);
        g_done.push_back(ident(r.proc, a));
        for (int i = 0; i < 12; i += 4) st32(buf + i, ld32(a + kHome + i)), st32(buf + 0x20 + i, ld32(a + kScale + i));
        for (int i = 0; i < 6; i += 2) st16(buf + 0x10 + i, ld16(a + kHome + 0xC + i));
        guest_call(c, GC(0x025D57E0), {a});  // fopAcM_delete
        const uint32_t layer = guest_call(c, GC(0x025DED64));  // fpcLy_CurrentLayer: made in Link's, as create_at does
        guest_call(c, GC(0x025DEAB4), {ld32(link + kLayer)});
        const uint32_t id = guest_call(c, GC(0x025D5834), {(uint32_t)(uint16_t)r.proc, prm, buf, room, buf + 0x10, buf + 0x20, arg, 0});
        guest_call(c, GC(0x025DEAB4), {layer});
        mods::trace("coop: progress: %s (parameters %08X, room %d) changed by another player: made again (%#x)", r.what, prm,
                    (int)(int32_t)room, id);
    }
}

// The others' doors (Asad: only the one opening a door saw it open): a shutter's opening (door10: stone, rising 300;
// door12: two halves sliding apart 200) is its event's, on the game of the player going through. Its openInit and
// closeInit (hooked below) tell the others, whose own copy of that door (its kind and home) moves the same way, by
// its open amount and speed as its own openProc and closeProc move them (not by those: they wait for the room beyond
// to be loaded and move the player), its matrices worked out again (calcMtx). A knob door (daKnob00, house doors) swings
// by its animation, opening and closing in one (about 2 s): its openInit's first part done here (the animation of its
// way, pushed or pulled, from its archive, started on its model), not the rest (the doors' common init, which places
// this game's Link), then played each step until done
struct DoorKind { int16_t proc; uint32_t amount; float full, open_speed, open_step, close_speed, close_step; uint32_t calc, action; };
const DoorKind kDoorKinds[] = {{0x12C, 0x4F0, 300, 30, 4, 60, 6, GC(0x02125FA4), 0x4EC},   // door10 (m358, speedF +0x370)
                               {0x12D, 0x4B4, 200, 20, 2, 20, 2, GC(0x02127FC0), 0x4AC}};  // door12 (m31C)
constexpr int16_t kKnob = 0x131;  // daKnob00 (its model +0x500, mBckAnm +0x504, flags +0x5A6)
struct DoorMove { int16_t proc; float x, z; bool open; int steps; uint8_t way; };
uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
std::vector<DoorMove> g_doors;
const DoorKind* door_kind(int16_t proc) {
    for (const DoorKind& k : kDoorKinds) if (k.proc == proc) return &k;
    return nullptr;
}
// a knob door's swing, as its openInit (021A3AA0) starts it, then mDoExt_bckAnm::play (025E742C) each step: true done
bool knob(Cpu* c, uint32_t a, const DoorMove& d) {
    if (d.steps == 1) {
        static const uint32_t arc = mem::host_alloc(8);  // (its archive's names, as the door passes them)
        st32(arc, GD(0x100134A0)), st32(arc + 4, GD(0x100132F8));
        const uint32_t res = guest_call(c, GC(0x026066C4), {ld32(GD(0x101F4F28)), arc, ld32(GD(0x101B8628) + 4 * (d.way & 3))});
        const double f1 = c->f[1].ps0;
        c->f[1].ps0 = ldf32(GD(0x100133C0));
        guest_call(c, GC(0x025E8508), {a + 0x504, ld32(ld32(a + 0x500) + 0xAC), res, 1, 0, 0, 0xFFFFFFFF, 1});  // (mBckAnm.init)
        c->f[1].ps0 = f1;
        if (d.way >= 2) st32(a + 0x508, ld32(GD(0x10013408)));
        st8(a + 0x5A6, ld8(a + 0x5A6) | 1);
        return false;
    }
    if (!(guest_call(c, GC(0x025E742C), {a + 0x504}) & 0xFF)) return false;
    st8(a + 0x5A6, ld8(a + 0x5A6) & ~1);
    return true;
}
void move_doors(Cpu* c) {
    for (auto it = g_doors.begin(); it != g_doors.end();) {
        DoorMove& d = *it;
        const DoorKind* k = door_kind(d.proc);
        uint32_t a = 0;
        for (uint32_t n = ld32(kActorQueue), i = 0; !a && n >= 0x10000000 && n < 0x50000000 && i < 4096; i++, n = ld32(n + 8))
            if (const uint32_t o = ld32(n + 0xC); o && (int16_t)ld16(o + kProcName) == d.proc &&
                std::fabs(ldf32(o + kHome) - d.x) < 1 && std::fabs(ldf32(o + kHome + 8) - d.z) < 1)
                a = o;
        // (not one going through it here as well: its own event moves it; door10/12's action 3, a knob door's 2)
        if (!a || (d.proc == kKnob ? ld8(a + 0x5A4) == 2 : ld8(a + k->action) == 3) || ++d.steps > 300) { it = g_doors.erase(it); continue; }
        if (d.proc == kKnob) { it = knob(c, a, d) ? g_doors.erase(it) : it + 1; continue; }
        // (opened and no close came in 3 s: closed as its own would be)
        if (d.open && d.steps > 90) d.open = false, d.steps = 0;
        const float at = (float)ldf32(a + k->amount), speed = (float)ldf32(a + 0x370);
        const float v = d.open ? std::min(speed + k->open_step, k->open_speed) : std::min(speed + k->close_step, k->close_speed);
        const float to = d.open ? std::min(at + v, k->full) : std::max(at - v, 0.f);
        st32(a + k->amount, bits(to)), st32(a + 0x370, bits(to == (d.open ? k->full : 0.f) ? 0.f : v));
        guest_call(c, k->calc, {a});
        if (!d.open && to <= 0) { it = g_doors.erase(it); continue; }
        ++it;
    }
}

}  // namespace

void door_moved(const uint8_t* d, size_t n) {
    if (n < 8 + 2 + 8 + 1 || memcmp(d, mem::ptr(GD(0x1046F0B0) + 0x5134), 8)) return;
    DoorMove m{};
    int16_t proc;
    memcpy(&proc, d + 8, 2), memcpy(&m.x, d + 10, 4), memcpy(&m.z, d + 14, 4);
    if ((!door_kind(proc) && proc != kKnob) || !std::isfinite(m.x) || !std::isfinite(m.z)) return;
    m.proc = proc, m.open = d[18] != 0, m.way = n > 19 ? d[19] : 0;
    for (DoorMove& o : g_doors)
        if (o.proc == m.proc && o.x == m.x && o.z == m.z) return void(o = m);
    if (g_doors.size() < 8) g_doors.push_back(m);
}

void received(const uint8_t* d, size_t n) {
    if (g_in.size() < 512) g_in.emplace_back((const char*)d, n);
}

void rupees_received(const uint8_t* d, size_t n) {
    int32_t gain;
    if (n != sizeof gain) return;
    memcpy(&gain, d, sizeof gain);
    if (share_rupees() && gain > 0 && gain <= kMostRupees) g_rupees_in = std::min(g_rupees_in + gain, kMostRupees);
}

void stop() { g_in.clear(), g_was.clear(), g_doors.clear(), g_purse = -1, g_rupees_in = 0; }

void step(Cpu* c, bool live) {
    const uint32_t sv = ld32(kSaveInfoPtr);
    if (!live || !sv || ld8(kNextStage + 12)) {  // (a stage change moves the played stage's bits: compared after it)
        if (!live) g_was.clear();
        return;
    }
    const uint32_t info = sv + kInfo;
    // test aid: WWHD_COOP_TEST_PROGRESS=<offset>:<value>:<mask> (hex, dSv_save_c): those bits set here 300 steps after
    // arriving
    static unsigned t_off = 0, t_val = 0, t_mask = 0, t_steps = 0;
    static const bool t_on = getenv("WWHD_COOP_TEST_PROGRESS") &&
                             sscanf(getenv("WWHD_COOP_TEST_PROGRESS"), "%x:%x:%x", &t_off, &t_val, &t_mask) == 3 && t_off < kSaveSize;
    if (t_on && t_steps + 1 == 300) {
        const uint8_t was = ld8(at(info, t_off));
        st8(at(info, t_off), (uint8_t)((was & ~t_mask) | (t_val & t_mask)));
        mods::trace("coop: progress test: byte %03X from %02X to %02X", t_off, was, ld8(at(info, t_off)));
    }
    // test aid: WWHD_COOP_TEST_CHESTS=1 traces the chests loaded here every 300 steps (number, type, room, place, opened)
    if (static const bool list = getenv("WWHD_COOP_TEST_CHESTS"); list && t_steps % 300 == 150 && tbox_proc() >= 0) {
        uint32_t n = ld32(kActorQueue);
        for (int k = 0; n >= 0x10000000 && n < 0x50000000 && k < 4096; k++, n = ld32(n + 8))
            if (const uint32_t a = ld32(n + 0xC); a && (int16_t)ld16(a + kProcName) == tbox_proc()) {
                const uint32_t prm = ld32(a + kParams);
                mods::trace("coop: progress test: chest %u type %u room %d at %.0f %.0f %.0f, stage %d bits %08X", prm >> 7 & 0x1F, prm & 0x7F,
                            (int8_t)ld8(a + kHome + 0x12), ldf32(a + kHome), ldf32(a + kHome + 4), ldf32(a + kHome + 8),
                            (int8_t)ld8(info + kStageNo), ld32(info + kLive));
            }
    }
    t_steps++;
    std::vector<uint8_t> now;
    read(info, now);
    if (g_was.empty() || ld8(info + kDataNum) != g_data_num) {  // the first step in the host's world, or another Quest Log
        g_was = now, g_data_num = ld8(info + kDataNum), g_purse = -1;
    }
    // this game's changes out
    std::string out, said;
    int changed = 0;
    for (const Field& f : kFields) {
        if (!memcmp(&now[f.off], &g_was[f.off], f.size)) continue;
        changed++;
        if (char t[8]; said.size() < 64) snprintf(t, sizeof t, " %03X", f.off), said += t;
        if (f.mode != kBits) {
            out += std::string{(char)(f.off & 0xFF), (char)(f.off >> 8), (char)f.size};
            out.append((const char*)&now[f.off], f.size);
            continue;
        }
        for (uint16_t b = f.off; b < f.off + f.size; b++)
            if (const uint8_t diff = now[b] ^ g_was[b])
                out += std::string{(char)(b & 0xFF), (char)(b >> 8), (char)1, (char)now[b], (char)diff};
    }
    if (changed > kReplaced) {
        mods::trace("coop: progress: %d fields changed at once (the save data replaced): not sent", changed);
        g_purse = -1;
    } else if (!out.empty()) {
        net::send(-1, net::kProgress, out);
        mods::trace("coop: progress out:%s", said.c_str());
    }
    if (const int purse = wallet(info); share_rupees() && g_purse >= 0 && purse > g_purse) {
        const int32_t gain = purse - g_purse;
        net::send(-1, net::kRupees, std::string((const char*)&gain, sizeof gain));
        mods::trace("coop: rupees out: +%d", gain);
    }
    if (g_rupees_in) {
        st32(kRupeeCount, ld32(kRupeeCount) + (uint32_t)g_rupees_in);
        mods::trace("coop: rupees in: +%d", g_rupees_in);
        g_rupees_in = 0;
    }
    g_purse = wallet(info);
    // the others' in; what came in before shown in the rooms loaded here (forgotten in another stage: made fresh there)
    const int cur = (int8_t)ld8(info + kStageNo);
    if (cur != g_pend_stage || (g_pend_steps > 0 && --g_pend_steps == 0))
        memset(g_pend_tbox, 0, sizeof g_pend_tbox), memset(g_pend_sw, 0, sizeof g_pend_sw), g_done.clear(), g_pend_stage = cur, g_pend_steps = 0;
    if (g_pend_steps > 0) catch_up(c, cur);
    move_doors(c);
    if (g_in.empty()) return read(info, g_was);
    for (const std::string& m : g_in) mods::trace("coop: progress in:%s", put_in(info, m).c_str());
    g_in.clear();
    read(info, g_was);
    // what came in newly: each stage's chest bits; the played stage's switches (dSv_memBit_c::mSwitch[4], bit i of word i/32)
    for (int s = 0; s < kStages; s++) {
        const uint16_t t = kTable + s * kMemSize;
        if (const uint32_t got = value(&g_was[t], 4) & ~value(&now[t], 4)) g_pend_tbox[s] |= got, g_pend_steps = 300;
    }
    if (cur >= 0 && cur < kStages)
        for (int w = 0; w < 4; w++) {
            const uint16_t t = kTable + cur * kMemSize + 4 + w * 4;
            if (const uint32_t got = value(&g_was[t], 4) & ~value(&now[t], 4)) g_pend_sw[w] |= got, g_pend_steps = 300;
        }
}

std::vector<std::string> team_state() {
    const uint32_t sv = ld32(kSaveInfoPtr);
    if (!sv) return {};
    std::vector<uint8_t> now;
    read(sv + kInfo, now);
    // every shared field as a change from nothing: bits with their mask (all set bits), the others as they are; in
    // messages of at most 1000 bytes (a reliable message must fit a packet), cut between fields
    std::vector<std::string> out(1);
    auto add = [&](const std::string& rec) {
        if (out.back().size() + rec.size() > 1000) out.emplace_back();
        out.back() += rec;
    };
    for (const Field& f : kFields) {
        if (f.mode != kBits) {
            std::string rec{(char)(f.off & 0xFF), (char)(f.off >> 8), (char)f.size};
            rec.append((const char*)&now[f.off], f.size);
            add(rec);
            continue;
        }
        for (uint16_t b = f.off; b < f.off + f.size; b++)
            if (now[b]) add(std::string{(char)(b & 0xFF), (char)(b >> 8), (char)1, (char)now[b], (char)now[b]});
    }
    return out;
}

}  // namespace mods::coop::progress

// door10 / door12 openInit and closeInit, daKnob00 openInit (r3 the door, r4 a knob door's way): a player's own door
// opening or closing, to the others
static void door_told(uint32_t a, bool open, uint8_t way = 0) {
    if (mods::coop::net::role() == mods::coop::net::Role::kOff) return;
    std::string b((const char*)mem::ptr(GD(0x1046F0B0) + 0x5134), 8);
    const int16_t proc = (int16_t)ld16(a + 0x08);
    const float x = (float)ldf32(a + 0x2EC), z = (float)ldf32(a + 0x2F4);
    b.append((const char*)&proc, 2), b.append((const char*)&x, 4), b.append((const char*)&z, 4), b += (char)open, b += (char)way;
    mods::coop::net::send(-1, mods::coop::net::kDoorMove, b);
    mods::trace("coop: a door (%#x) %s here: to the others", proc, open ? "opens" : "closes");
}
#define COOP_DOOR(addr, open)                         \
    extern "C" void f_##addr##_orig(Cpu* c);          \
    extern "C" void hook_##addr(Cpu* c) {             \
        const uint32_t a = c->r[3];                   \
        f_##addr##_orig(c);                           \
        const uint32_t r3 = c->r[3];                  \
        door_told(a, open), c->r[3] = r3;             \
    }
COOP_DOOR(021262D8, true)   // door10 openInit
COOP_DOOR(02126334, false)  // door10 closeInit
COOP_DOOR(02128430, true)   // door12 openInit
COOP_DOOR(0212848C, false)  // door12 closeInit
extern "C" void f_021A3AA0_orig(Cpu* c);
extern "C" void hook_021A3AA0(Cpu* c) {  // daKnob00 openInit
    const uint32_t a = c->r[3], way = c->r[4];
    f_021A3AA0_orig(c);
    const uint32_t r3 = c->r[3];
    door_told(a, true, (uint8_t)way), c->r[3] = r3;
}
