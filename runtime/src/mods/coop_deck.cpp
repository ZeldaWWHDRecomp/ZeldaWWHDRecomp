// SPDX-License-Identifier: MPL-2.0
// Online co-op: standing on a friend's boat (coop_deck.h, GAPS C16).
//
// A deck is a box of the mods' collision meshes (story_world_collision_mesh.h, wood) built into the game's own
// collision data (cBgD_t) and set up as the mods' world objects set up theirs (story_world_collision_native.h): a dBgW
// made (dBgW::dBgW 024F23F4), set as a moving body over a matrix (cBgW::Set 0200A030, flags 1: MOVE_BG), its carry
// function TypicalRotY (+0xA8), registered with this game's own boat as owner (dBgS::Regist 024EEA6C: Link's own
// checks skip his own bodies, the boat's skip the deck), moved each step (dBgW::Move 024F43DC) and let go
// (cBgS::Release 020087EC). Everything is made once per deck on a heap of the port's own memory (as the puppets'
// kits, coop_pose.cpp pool_heap), so no game heap is touched and a stage change frees nothing under it.
#include "coop_deck.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "guest_addr.h"
#include "mods.h"
#include "runtime.h"
#include "story_world_collision_mesh.h"

namespace mods::coop::deck {
namespace {

namespace wm = story_world_collision::mesh;

// the deck: 160 wide, 420 long, its top kTop over the boat's place (the water line; where the riding Link's feet are, the
// sea_boat state: Link 149 over the boat), the boat's middle at its middle
constexpr float kWidth = 160, kLength = 420, kThick = 20, kTop = 150, kEdgeX = kWidth / 2 - 30, kEdgeZ = kLength / 2 - 30;
constexpr uint32_t kPos = 0x314, kOldPos = 0x300, kAngleY = 0x32A;  // fopAc_ac_c (WWHD)
constexpr uint32_t kGndBg = 0x8F8;   // daPy_lk_c's ground poly (mAcch's m_gnd): its body (story_world_collision_native.h)
constexpr uint32_t kRegistry = 0x12A0, kHeapBytes = 0x10000;

struct Deck {
    uint32_t body = 0, mtx = 0;  // the body (on the deck heap) and its matrix (port memory)
    bool in = false;             // registered now
    Boat at, was;                // where it goes this step, where it was the last
    bool moved = false;          // `was` known
};
Deck g_deck[kDecks];
uint32_t g_heap = 0, g_owner = 0;
int g_hop = -1, g_hop_steps = 0;  // just put onto a deck: carried with it until the game sees him stand on it
bool g_failed = false;

uint32_t play(Cpu* c) { return guest_call(c, GC(0x025200D4), {}); }

// the heap for the decks: a frame heap made on a block of the port's memory, as JKRSolidHeap::create makes one
uint32_t deck_heap(Cpu* c) {
    if (g_heap) return g_heap;
    const uint32_t blk = mem::host_alloc(kHeapBytes, 0x40);
    static const uint32_t name = [] {
        const uint32_t b = mem::host_alloc(8);
        st32(b, GD(0x1016DCC8)), st32(b + 4, GD(0x1016DCB0));
        return b;
    }();
    if (!blk) return 0;
    const uint32_t heap = guest_call(c, GC(0x027549FC), {blk, name, 0, blk, kHeapBytes, 1, 0});
    if (heap) {
        guest_call(c, GC(0x02754AC4), {heap});
        st32(heap + 0x90, ld32(heap + 0x90) & ~4u);
    }
    return g_heap = heap;
}

void set_mtx(uint32_t m, const Boat& b) {
    const double t = b.yaw * M_PI / 32768;
    const float cs = (float)cos(t), sn = (float)sin(t);
    const float rows[12] = {cs, 0, sn, b.pos[0], 0, 1, 0, b.pos[1], -sn, 0, cs, b.pos[2]};
    for (int i = 0; i < 12; i++) stf32(m + 4 * i, rows[i]);
}

// a deck's body made (once), over its matrix at the boat
bool make(Cpu* c, Deck& d, const Boat& b) {
    if (d.body) return true;
    if (g_failed) return false;
    wm::Mesh m;
    std::string why;
    if (!wm::make_shape("box", {kWidth, kThick, kLength}, 0, 0, 2, m, why)) return mods::trace("coop: deck: %s", why.c_str()), g_failed = true, false;
    for (wm::Point& v : m.vertices) v.y += kTop - kThick / 2;
    m.minimum = {-kWidth / 2, kTop - kThick, -kLength / 2}, m.maximum = {kWidth / 2, kTop, kLength / 2};
    uint32_t conversion[32];
    for (unsigned k = 0; k < 32; ++k) conversion[k] = ld32(GD(0x101D51CC) + k * 4);
    std::array<uint8_t, wm::NativeResultIds.size()> raw{};
    std::vector<uint8_t> bytes;
    if (!wm::native_attributes(conversion, wm::NativeResultIds, raw) || !wm::build_native(m, raw, (uint16_t)(uint8_t)b.room, bytes, why))
        return mods::trace("coop: deck: not built: %s", why.c_str()), g_failed = true, false;
    const uint32_t heap = deck_heap(c);
    if (!heap) return mods::trace("coop: deck: no heap"), g_failed = true, false;
    const uint32_t r3 = c->r[3];
    const uint32_t was = guest_call(c, GC(0x027EC220), {heap});  // JKRHeap::becomeCurrentHeap
    const uint32_t data = guest_call(c, GC(0x0273AD10), {(uint32_t)bytes.size()});
    bool ok = data && wm::relocate(bytes, data, why);
    if (ok) memcpy(mem::ptr(data), bytes.data(), bytes.size());
    if (!d.mtx) d.mtx = mem::host_alloc(48, 0x10);
    set_mtx(d.mtx, b);
    const uint32_t body = ok && d.mtx ? guest_call(c, GC(0x024F23F4), {0}) : 0;
    ok = body && !guest_call(c, GC(0x0200A030), {body, data, 1, d.mtx});
    guest_call(c, GC(0x027EC220), {was});
    c->r[3] = r3;
    if (!ok) return mods::trace("coop: deck: not set up (%s)", why.c_str()), g_failed = true, false;
    st32(body + 0xA8, GC(0x024EE708));  // dBgS_MoveBGProc_TypicalRotY: what stands on it goes with it, turning too
    d.body = body;
    mods::trace("coop: deck made: body %08X, %zu bytes of collision", body, bytes.size());
    return true;
}

void let_go(Cpu* c, Deck& d) {
    if (!d.in) return;
    const uint32_t r3 = c->r[3];
    guest_call(c, GC(0x020087EC), {play(c) + kRegistry, d.body});
    c->r[3] = r3;
    d.in = false;
    mods::trace("coop: deck %08X let go", d.body);
}

// still in the registry as this game put it there (a save state or a stage change may have emptied it)
bool registered(Cpu* c, const Deck& d) {
    const uint32_t slot = ld32(d.body);
    if (slot >= 256) return false;
    const uint32_t e = play(c) + kRegistry + slot * 20;
    return (ld32(e + 4) & 1) && ld32(e) == d.body && ld32(e + 12) == g_owner;
}

}  // namespace

// the sea's height here at x, z (daSea_calcWave 0246BA0C: f1 x, f2 z, f1 the height; the base height off the waves'
// grid). Each game runs its own waves, so a friend's boat is put on this game's water, not at its height there
bool water(Cpu* c, float x, float z, float& y) {
    if (!c) return false;
    const Cpu saved = *c;
    c->f[1].ps0 = x, c->f[2].ps0 = z;
    guest_call(c, GC(0x0246BA0C), {});
    y = (float)c->f[1].ps0;
    *c = saved;
    return std::isfinite(y) && std::fabs(y) < 1e5f;
}

void update(Cpu* c, const Boat boats[kDecks], uint32_t own_ship) {
    if (own_ship != g_owner) clear(c), g_owner = own_ship;  // (another boat: the decks registered again under it)
    for (int i = 0; i < kDecks; i++) {
        Deck& d = g_deck[i];
        const Boat& b = boats[i];
        if (!b.on || !own_ship) {
            let_go(c, d);
            continue;
        }
        if (!make(c, d, b)) continue;
        d.at = b;  // (on this game's water: Link is carried up and down with it, move(); see water())
        if (d.in && !registered(c, d)) d.in = false;
        const uint32_t r3 = c->r[3];
        if (!d.in) {
            set_mtx(d.mtx, d.at);
            if (guest_call(c, GC(0x024EEA6C), {play(c) + kRegistry, d.body, own_ship}) || !registered(c, d)) {
                c->r[3] = r3;
                if (static int told = 0; told++ < 3) mods::trace("coop: deck %d: not registered", i);
                continue;
            }
            d.in = true;
            mods::trace("coop: deck %d registered (body %08X, slot %u) at %.0f %.0f %.0f", i, d.body, ld32(d.body), b.pos[0], b.pos[1], b.pos[2]);
        }
        c->r[3] = r3;
    }
}

// (before the actors run: dBgW::Move marks the body moved for this step, which Link's ground correction needs; the
// scene's draw clears the mark, d_s_play dScnPly_Draw ClrMoveFlag)
void move(Cpu* c) {
    bool any = false;
    for (const Deck& d : g_deck) any |= d.in;
    if (!any) return;
    const Cpu saved = *c;
    // Link standing on one goes with it (its place and heading), as the game's own ground correction would carry him
    // on a moving platform: his place in the deck's last frame put into its new one. (The game's correction does not
    // move him here, c16m: it needs the body moved by an actor running in that step, which the deck is not.)
    const uint32_t link = ld32(GD(0x1046F0B0) + 0x5B34);
    int on = link >= 0x10000000 ? standing_on(link) : -1;
    // (test aid: WWHD_COOP_DECK_TRACE=2: each step on a deck, Link's procedure, ground body and poly, his place in the
    // deck's frame, the deck's place, this game's own boat)
    if (static const bool every = getenv("WWHD_COOP_DECK_TRACE") && atoi(getenv("WWHD_COOP_DECK_TRACE")) == 2; every && link >= 0x10000000 &&
        (on >= 0 || g_hop_steps > 0 || standing_on(link) != -1 || g_deck[0].in)) {
        const Deck& d = g_deck[0];
        const double t = d.at.yaw * M_PI / 32768, dx = ldf32(link + kPos) - d.at.pos[0], dz = ldf32(link + kPos + 8) - d.at.pos[2];
        mods::trace("coop: deck step: on %d, proc %u, ground %08X poly %u, local %.0f %.0f %.0f, deck %.0f %.0f %.0f yaw %d, own boat %.0f %.0f", on,
                    ld32(link + 0x65F0), ld32(link + kGndBg), ld16(link + kGndBg - 4), dx * cos(t) - dz * sin(t), ldf32(link + kPos + 4) - d.at.pos[1],
                    dx * sin(t) + dz * cos(t), d.at.pos[0], d.at.pos[1], d.at.pos[2], d.at.yaw, g_owner ? ldf32(g_owner + kPos) : 0.0,
                    g_owner ? ldf32(g_owner + kPos + 8) : 0.0);
    }
    if (on >= 0) g_hop_steps = 0;
    else if (g_hop_steps > 0 && g_hop >= 0 && g_deck[g_hop].in) on = g_hop, g_hop_steps--;  // (landing: the deck rides the waves)
    if (static uint32_t n = 0; getenv("WWHD_COOP_DECK_TRACE") && ++n % 20 == 0)
        mods::trace("coop: deck carry: on %d, ground body %08X, deck 0 %.0f %.0f -> %.0f %.0f, Link %.0f %.0f %.0f", on, link ? ld32(link + kGndBg) : 0,
                    g_deck[0].was.pos[0], g_deck[0].was.pos[2], g_deck[0].at.pos[0], g_deck[0].at.pos[2], link ? ldf32(link + kPos) : 0.0,
                    link ? ldf32(link + kPos + 4) : 0.0, link ? ldf32(link + kPos + 8) : 0.0);
    if (on >= 0 && g_deck[on].moved) {
        const Deck& d = g_deck[on];
        const double t0 = d.was.yaw * M_PI / 32768, t1 = d.at.yaw * M_PI / 32768;
        for (uint32_t base : {kPos, kOldPos}) {
            const float w[3] = {(float)ldf32(link + base), (float)ldf32(link + base + 4), (float)ldf32(link + base + 8)};
            const double dx = w[0] - d.was.pos[0], dz = w[2] - d.was.pos[2];
            double lx = dx * cos(t0) - dz * sin(t0), lz = dx * sin(t0) + dz * cos(t0);
            // (kept a step from its edges: pushed by the friend standing in the boat, Link would go over the edge and hang
            // from it, c16t; a player who means to leave jumps off, which this does not hold)
            if (standing_on(link) == on) lx = std::clamp(lx, (double)-kEdgeX, (double)kEdgeX), lz = std::clamp(lz, (double)-kEdgeZ, (double)kEdgeZ);
            stf32(link + base, (float)(d.at.pos[0] + lx * cos(t1) + lz * sin(t1)));
            stf32(link + base + 4, w[1] + d.at.pos[1] - d.was.pos[1]);
            stf32(link + base + 8, (float)(d.at.pos[2] - lx * sin(t1) + lz * cos(t1)));
        }
        const int16_t turn = (int16_t)(d.at.yaw - d.was.yaw);
        st16(link + kAngleY, (uint16_t)(ld16(link + kAngleY) + turn)), st16(link + kAngleY - 8, (uint16_t)(ld16(link + kAngleY - 8) + turn));  // (shape and current.angle y)
    }
    for (Deck& d : g_deck) {
        if (!d.in || !registered(c, d)) { d.moved = false; continue; }
        d.was = d.at, d.moved = true;
        set_mtx(d.mtx, d.at);
        st16(d.body + 0xB8, (uint16_t)(int16_t)d.at.room), st8(d.body + 0xBB, (uint8_t)d.at.room);  // (the room it is in now)
        guest_call(c, GC(0x024F43DC), {d.body});  // dBgW::Move: from its last place to this one
    }
    *c = saved;
}

void clear(Cpu* c) {
    for (Deck& d : g_deck) if (d.in && registered(c, d)) let_go(c, d); else d.in = false;
}

int standing_on(uint32_t link) {
    if (!link) return -1;
    const uint32_t bg = ld32(link + kGndBg);
    for (int i = 0; i < kDecks; i++) if (g_deck[i].in && bg == g_deck[i].body) return i;
    return -1;
}

bool frame(int owner, float pos[3], int16_t& yaw) {
    if (owner < 0 || owner >= kDecks || !g_deck[owner].in) return false;
    memcpy(pos, g_deck[owner].at.pos, 12), yaw = g_deck[owner].at.yaw;
    return true;
}

bool hop(uint32_t link, int owner) {
    float p[3];
    int16_t yaw;
    if (!link || !frame(owner, p, yaw)) return false;
    const double t = yaw * M_PI / 32768;
    const float back = -kLength / 4, up = kTop + 2;  // (on its back half; carried with it while he lands, move())
    const float w[3] = {p[0] + back * (float)sin(t), p[1] + up, p[2] + back * (float)cos(t)};
    for (int e = 0; e < 3; e++) stf32(link + kPos + 4 * e, w[e]), stf32(link + kOldPos + 4 * e, w[e]);
    st16(link + kAngleY, (uint16_t)yaw);
    g_hop = owner, g_hop_steps = 30;
    mods::trace("coop: onto %d's deck at %.0f %.0f %.0f", owner, w[0], w[1], w[2]);
    return true;
}

}  // namespace mods::coop::deck
