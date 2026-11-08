// SPDX-License-Identifier: MPL-2.0
// From Galvakron/ZeldaWWHDRecompMP 8a4400d (MPL-2.0), co-op work by AsadSi and Galvakron; adapted to this port.
//
// Online co-op, the players' items (coop_items.h). Link makes them with fopAcM_fastCreate while he executes (traced:
// a bomb as he pulls it out, his boomerang and the hookshot as he takes them out, an arrow as he nocks it, the
// grappling hook's rope). A bomb that goes off (daBomb_c::procExplode_init sets its fuse, mRestTime, to 0) is sent
// with its place; the others make a bomb there in state 0, which goes off as it is made (daBomb_c::create_init:
// L_attrState[0] has ATTR_STATE_1), with all a bomb does there: its blast, sound and light, hurting whoever is near
// and breaking what it reaches. It is not Link's there (only state 3 counts against his bombs out).
//
// An arrow of his in flight (moving over 20 a step: not one on his bow) that reaches another player (within 70 of their
// place, up to 170 over it; the puppets have no colliders) hits them as his sword does (net::kPlayerHit, half a heart,
// the way it flew), and is gone.
#include "coop_items.h"
#include "coop_names.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "coop_net.h"
#include "coop_pvp.h"
#include "coop_sync.h"
#include "guest_addr.h"
#include "mods.h"
#include "runtime.h"

namespace mods::coop::items {
namespace {

constexpr int16_t kItems[] = {0x126, 0x1B0, 0xA9, 0x1D8, 0x1BE};  // Bomb, Boom, the hookshot, Arrow, Himo2 (the rope)
constexpr int16_t kBomb = 0x126, kArrow = 0x1D8;
constexpr uint32_t kPos = 0x314, kRoom = 0x326, kFuse = 0xA7C;  // daBomb_c::mRestTime (GC +0x6FC)
constexpr int16_t kBomb2 = 0x127;  // daBomb2::Act_c, a Bomb Flower's bomb (GC fpcNm_Bomb2_e 0x129, after the bomb's: 0x128 there, 0x126 here)
constexpr uint32_t kBomb2State = 0x88C;  // daBomb2::Act_c::mState (wwhd_src d_a_bomb2.h): 2 Mode_EXPLODE
constexpr float kBombSight = 6000;  // (another player's bomb in another room of this stage: shown within this of Link)
const uint32_t kStage = GD(0x1046F0B0) + names::inf::kCurStage;
struct Mine { uint32_t id; int16_t proc; int unseen = 0; float was[3] = {}; };  // unseen: steps not found yet (still being made)
std::vector<Mine> g_mine;
struct Gone { uint32_t id; int16_t proc; uint32_t at; };  // (C28) gone lately: a bomb's blast still hits a while after
std::vector<Gone> g_gone;

uint32_t actor_of(Cpu* c, uint32_t id) { return guest_call(c, GC(0x025DE50C), {id}); }  // fopAcM_SearchByID

// an arrow of his in flight on another player: the hit to them, the arrow gone (true)
bool arrow_hit(Cpu* c, uint32_t a, Mine& m) {
    float at[3];
    for (int e = 0; e < 3; e++) at[e] = (float)ldf32(a + kPos + 4 * e);
    const float dx = at[0] - m.was[0], dz = at[2] - m.was[2], y_was = m.was[1];
    const bool flying = std::hypot(dx, dz, at[1] - m.was[1]) > 20 && (m.was[0] || m.was[2]);
    memcpy(m.was, at, sizeof at);
    if (!flying) return false;
    // (its whole way this step: an arrow flies further in a step than a player is wide)
    float from[3];
    for (int e = 0; e < 3; e++) from[e] = at[e] - (e == 0 ? dx : e == 2 ? dz : at[1] - y_was);
    const auto near = [&](const float p[3]) {
        const float vx = at[0] - from[0], vz = at[2] - from[2], len = vx * vx + vz * vz;
        const float t = len > 0 ? std::clamp(((p[0] - from[0]) * vx + (p[2] - from[2]) * vz) / len, 0.f, 1.f) : 1.f;
        const float y = from[1] + (at[1] - from[1]) * t;
        return std::hypot(from[0] + vx * t - p[0], from[2] + vz * t - p[2]) < 70 && y > p[1] && y < p[1] + 170;
    };
    for (int id = 0; id < net::kMaxPlayers; id++)
        if (float p[3]; id != net::my_id() && net::party_flag(net::kFriendlyFire, true) && sync::place(id, p) && near(p)) {
            const int16_t way = (int16_t)(std::atan2(dx, dz) * 32768 / M_PI);
            net::send(-1, net::kPlayerHit, std::string{(char)id, (char)2} + std::string((const char*)&way, 2));
            guest_call(c, GC(0x025D57E4), {ld32(a + 4)});  // fopAcM_delete
            mods::trace("coop: this game's arrow hits player %d", id);
            return true;
        }
    return false;
}

}  // namespace

void made(int16_t proc, uint32_t actor) {
    if (actor && net::role() != net::Role::kOff && std::find(std::begin(kItems), std::end(kItems), proc) != std::end(kItems)) {
        g_mine.push_back({ld32(actor + 4), proc});
        if (proc == kBomb) mods::trace("coop: this game's Link made a bomb (%u of his items followed)", (unsigned)g_mine.size());
    }
}

// a Bomb Flower picked here (daBFlower_c: carried, it makes its bomb with start 2, Start_UNK2, and hands it to Link;
// started 0 or 1 a blast or a blow set it off where it grows): his from then on, so its blast goes out as his bombs'
// do. The plant and its regrowing stay each game's own; the others are shown a bomb's blast where it went off
void flower(int16_t proc, uint32_t actor, uint32_t param) {
    if (!actor || proc != kBomb2 || net::role() == net::Role::kOff || (param & 3) != 2) return;
    g_mine.push_back({ld32(actor + 4), kBomb2});
    mods::trace("coop: this game's Link picked a Bomb Flower");
}

int16_t mine(uint32_t actor) { return mine_id(ld32(actor + 4)); }
int16_t mine_id(uint32_t id) {
    const auto it = std::find_if(g_mine.begin(), g_mine.end(), [&](const Mine& m) { return m.id == id; });
    if (it != g_mine.end()) return it->proc;
    const auto g = std::find_if(g_gone.begin(), g_gone.end(), [&](const Gone& m) { return m.id == id; });
    return g == g_gone.end() ? 0 : g->proc;
}

uint32_t spawn(Cpu* c, uint32_t link, int16_t proc, uint32_t param, const float pos[3]) {
    static const uint32_t buf = mem::host_alloc(0x20);  // pos +0, angle +0x10
    for (int e = 0; e < 3; e++) { uint32_t u; memcpy(&u, &pos[e], 4); st32(buf + 4 * e, u); }
    for (int e = 0; e < 3; e++) st16(buf + 0x10 + 2 * e, 0);
    const uint32_t layer = guest_call(c, GC(0x025DED64));  // made in Link's process layer (fpcLy_SetCurrentLayer)
    guest_call(c, GC(0x025DEAB4), {ld32(link + 0x2C)});
    st32(c->r[1] - 0x40 + 8, 0);
    const uint32_t a = guest_call(c, GC(0x025D5928), {(uint32_t)proc, param, buf, (uint32_t)(int8_t)ld8(link + kRoom), buf + 0x10, 0, 0xFFFFFFFF, 0});
    guest_call(c, GC(0x025DEAB4), {layer});
    return a;
}

namespace {
// friendly fire (the party's switch): a blast of this game's (its bomb, a Bomb Flower, the boat's cannon ball) costs each
// other player shown within the game's own blast (daBomb_c / daBomb2: a sphere of 200 at its place, Link's middle 50 over
// his feet) a heart, as a spin attack does: this game decides, as for the sword; the receiver never takes the last
// quarter heart (coop_world's hurt)
constexpr float kBlast = 200, kLinkMid = 50;
std::vector<uint32_t> g_balls;  // the boat's cannon balls this game fired, by process id, until they go off
void blast(const float at[3], const char* what) {
    if (!net::party_flag(net::kFriendlyFire, true)) return;
    for (int id = 0; id < net::kMaxPlayers; id++) {
        float p[3];
        if (id == net::my_id() || !net::member(id) || !sync::place(id, p)) continue;
        const float d = std::hypot(p[0] - at[0], p[1] + kLinkMid - at[1], p[2] - at[2]);
        if (d > kBlast) continue;
        const int16_t way = (int16_t)(std::atan2(p[0] - at[0], p[2] - at[2]) * 32768 / M_PI);
        net::send(-1, net::kPlayerHit, std::string{(char)id, (char)4} + std::string((const char*)&way, 2));
        mods::trace("coop: this game's %s hits player %d (%.0f from the blast): a heart", what, id, d);
    }
}
}  // namespace

void fired_ball(uint32_t ball) {
    if (ball && net::role() != net::Role::kOff) g_balls.push_back(ld32(ball + 4));
}

void step(Cpu* c) {
    if (net::role() == net::Role::kOff) return g_mine.clear(), g_gone.clear(), g_balls.clear();
    for (size_t i = 0; i < g_balls.size();) {  // (the boat's cannon balls: a blast where one goes off, as a bomb's)
        const uint32_t a = actor_of(c, g_balls[i]);
        if (a && ld16(a + kFuse) != 0) { i++; continue; }
        if (a) {
            float at[3];
            for (int e = 0; e < 3; e++) at[e] = (float)ldf32(a + kPos + 4 * e);
            blast(at, "cannon shot");
        }
        g_balls.erase(g_balls.begin() + i);
    }
    const uint32_t now = net::now_ms();
    g_gone.erase(std::remove_if(g_gone.begin(), g_gone.end(), [&](const Gone& g) { return now - g.at > 3000; }), g_gone.end());
    // test aid: WWHD_COOP_TEST_FLOWERS=1 traces the Bomb Flower plants loaded here (once each), to stand Link at one
    if (static const bool flowers = getenv("WWHD_COOP_TEST_FLOWERS"); flowers && now % 3000 < 40) {
        static const int16_t plant = [] {
            const uint32_t names = GD(0x101EBFDC);  // the object name table (coop_progress.cpp kObjNames)
            for (uint32_t e = names; e < names + 12 * 2000 && ld8(e); e += 12)
                if (!strncmp((const char*)mem::ptr(e), "BFlower", 8)) return (int16_t)ld16(e + 8);
            return (int16_t)-1;
        }();
        static std::vector<uint32_t> told;
        for (uint32_t q = ld32(GD(0x101F3328)), k = 0; q >= 0x10000000 && q < 0x50000000 && k < 4096; k++, q = ld32(q + 8))
            if (const uint32_t a = ld32(q + 0xC); a && (int16_t)ld16(a + 8) == plant && std::find(told.begin(), told.end(), ld32(a + 4)) == told.end())
                told.push_back(ld32(a + 4)), mods::trace("coop: test: a Bomb Flower plant at %.0f %.0f %.0f (room %d)", (float)ldf32(a + kPos),
                                                         (float)ldf32(a + kPos + 4), (float)ldf32(a + kPos + 8), (int8_t)ld8(a + kRoom));
    }
    for (size_t i = 0; i < g_mine.size();) {
        Mine& m = g_mine[i];
        const uint32_t a = actor_of(c, m.id);
        if (!a && m.unseen >= 0 && ++m.unseen < 30) { i++; continue; }
        if (a) m.unseen = -1;
        if (a && m.proc == kArrow && arrow_hit(c, a, m)) { g_mine.erase(g_mine.begin() + i); continue; }
        if (a && !(m.proc == kBomb && ld16(a + kFuse) == 0) && !(m.proc == kBomb2 && ld32(a + kBomb2State) == 2)) { i++; continue; }
        if (a) {  // (gone off: its stage, room and place out)
            std::string b(8, '\0');
            names::clean_stage(mem::ptr(kStage), &b[0]);
            b += (char)ld8(a + kRoom);
            for (int e = 0; e < 3; e++) { const uint32_t v = ld32(a + kPos + 4 * e); b.append((const char*)&v, 4); }
            net::send(-1, net::kBombed, b);
            mods::trace("coop: this game's bomb went off: room %d at %.0f %.0f %.0f", (int8_t)ld8(a + kRoom), (float)ldf32(a + kPos),
                        (float)ldf32(a + kPos + 4), (float)ldf32(a + kPos + 8));
            float at[3];
            for (int e = 0; e < 3; e++) at[e] = (float)ldf32(a + kPos + 4 * e);
            const float still[3] = {};
            net::pvp_shot(3, at, still);  // (C28: through a server, shown to the players near by outside the party too)
            blast(at, m.proc == kBomb2 ? "Bomb Flower" : "bomb");
        }
        g_gone.push_back({m.id, m.proc, now});
        g_mine.erase(g_mine.begin() + i);
    }
}

void bombed(Cpu* c, uint32_t link, const uint8_t* d, size_t n) {
    char here[8];
    names::clean_stage(mem::ptr(kStage), here);
    if (n < 21) return;
    float pos[3];
    memcpy(pos, d + 9, 12);
    for (float v : pos) if (!std::isfinite(v) || std::fabs(v) > 1e6f) return;  // (another game's numbers)
    // here: in this stage, in Link's room or in sight of him (a bomb thrown across a room's border, or one in the next
    // room seen through a doorway, goes off on both sides; it is made in Link's room here)
    const float d2 = std::hypot(pos[0] - (float)ldf32(link + kPos), pos[2] - (float)ldf32(link + kPos + 8));
    if (memcmp(d, here, 8) || ((int8_t)d[8] != (int8_t)ld8(link + kRoom) && d2 > kBombSight)) {
        if (static uint32_t told = 0; told++ % 20 == 0)
            mods::trace("coop: another player's bomb went off elsewhere: %.8s room %d, %.0f away (here %.8s room %d)", (const char*)d, (int8_t)d[8], d2,
                        here, (int8_t)ld8(link + kRoom));
        return;
    }
    pvp::shown_only(spawn(c, link, kBomb, 0x80000000, pos));  // (C28: its blast hurts nobody here)
    mods::trace("coop: another player's bomb went off here (room %d, %.0f away)", (int8_t)d[8], d2);
}

}  // namespace mods::coop::items
