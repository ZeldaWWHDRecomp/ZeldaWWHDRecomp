// SPDX-License-Identifier: MPL-2.0
// From Galvakron/ZeldaWWHDRecompMP 8a4400d (MPL-2.0), co-op work by AsadSi and Galvakron (its extras.cpp's
// co-op parts: coop_world, the boat's cannon, friendly fire, the hooks); adapted to this port.
//
// Online co-op, the game thread's entry (coop_world.h).
#include "coop_world.h"
#include "coop_deck.h"
#include "coop_emotes.h"

#include <cmath>
#include <unordered_map>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "coop_items.h"
#include "coop_layout.h"
#include "coop_names.h"
#include "coop_monsters.h"
#include "coop_net.h"
#include "coop_pose.h"
#include "coop_pvp.h"
#include "coop_sync.h"
#include "guest_addr.h"
#include "mods.h"
#include "runtime.h"

namespace mods::coop::world {
namespace {

using namespace names;  // (C18: the offsets by name, coop_names.h)
const uint32_t kPlay = GD(0x1046F0B0);
const uint32_t kLinkPtr = kPlay + inf::kLink;  // daPy_lk_c
const uint32_t kStage = kPlay + inf::kCurStage;  // the current stage's name
constexpr uint32_t kPos = ac::kPos, kRoom = ac::kRoom, kAngleY = ac::kAngleY, kCurProc = py::mCurProc;

uint32_t save() { return ld32(GD(0x101F84DC)) + 0x20; }  // dSv_player_c (dSv_info_c::mSavedata, wwhd_src SAVE_OFF)
bool in_mem2(uint32_t a) { return a >= 0x10000000 && a < 0x50000000; }
uint32_t u32_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
struct V3 { double x, y, z; };
V3 pos_of(uint32_t a) { return {ldf32(a + kPos), ldf32(a + kPos + 4), ldf32(a + kPos + 8)}; }
// Link put at a place: his current and old places, his collision's and the camera's (as the fork's mods move him)
void put_link(uint32_t link, V3 w) {
    for (uint32_t a : {link + kPos, link + ac::kOldPos, link + py::kAcchLin, link + py::mLightInfluence, link + py::m3748, GD(0x1046CD48)})
        st32(a, u32_bits((float)w.x)), st32(a + 4, u32_bits((float)w.y)), st32(a + 8, u32_bits((float)w.z));
}

struct Bytes {  // a message's fields, in order
    std::string b;
    template <class T> Bytes& put(T v) { b.append((const char*)&v, sizeof v); return *this; }
};
struct Reader {
    const std::string& b;
    size_t o = 0;
    template <class T> T get() { T v{}; if (o + sizeof v <= b.size()) memcpy(&v, b.data() + o, sizeof v); o += sizeof v; return v; }
};

struct Hurt { uint8_t quarters; int16_t way; } g_hurt{};  // a friend's blow to take (before_execute)

// the boat's own cannon (daShip's execute creates its ball at 02479680, as fire() does): the ball made, sent once its
// speeds are set (the next step), and fired the same on the others
struct BoatShot { uint32_t ball = 0; float pos[3]; int16_t angle[3]; } g_boat_shot;
void boat_shot_out() {
    namespace net = mods::coop::net;
    const uint32_t ball = std::exchange(g_boat_shot.ball, 0);
    if (!ball || net::role() == net::Role::kOff || !in_mem2(ball)) return;
    Bytes b;
    for (float v : g_boat_shot.pos) b.put(v);
    for (int16_t v : g_boat_shot.angle) b.put(v);
    b.put((float)ldf32(ball + ac::speedF)).put((float)ldf32(ball + ac::kSpeedY)).put((float)ldf32(ball + ac::gravity));
    net::send(-1, net::kBoatShot, b.b);
    mods::coop::pvp::fired(ball, g_boat_shot.pos, g_boat_shot.angle[1], (float)ldf32(ball + ac::speedF), (float)ldf32(ball + ac::kSpeedY));
}
void boat_shot_from(Cpu* c, uint32_t link, Reader& r) {
    float p[3];
    for (float& v : p) v = r.get<float>();
    int16_t angle[3];
    for (int16_t& a : angle) a = r.get<int16_t>();
    const float f = r.get<float>(), y = r.get<float>(), g = r.get<float>();
    mods::coop::pvp::shown_only(spawn_ball(c, link, p, angle, f, y, g));  // (C28: a picture: its blast hurts nobody here)
}
}  // namespace

// a boat's cannon ball made here as the ship makes its own (also PvP's, C28: another player's shot shown)
uint32_t spawn_ball(Cpu* c, uint32_t link, const float p[3], const int16_t angle[3], float f, float y, float g) {
    static const uint32_t buf = mem::host_alloc(0x20);  // pos at +0, angle at +0x10
    for (int i = 0; i < 3; i++) st16(buf + 0x10 + 2 * i, (uint16_t)angle[i]);
    // (another game's numbers: a ball off any stage or of no speed would halt the game's checks)
    for (float v : {p[0], p[1], p[2], f, y, g}) if (!std::isfinite(v) || std::fabs(v) > 1e6f) return 0;
    for (int i = 0; i < 3; i++) st32(buf + 4 * i, u32_bits(p[i]));
    const uint32_t layer = guest_call(c, GC(0x025DED64));  // fpcLy_CurrentLayer
    guest_call(c, GC(0x025DEAB4), {ld32(link + bpc::kLayer)});  // fpcLy_SetCurrentLayer (Link's)
    st32(c->r[1] - 0x40 + 8, 0);
    const uint32_t ball = guest_call(c, GC(0x025D5928), {0x126, 0x80020004, buf, (uint32_t)(int8_t)ld8(link + kRoom), buf + 0x10, 0, 0xFFFFFFFF, 0});
    guest_call(c, GC(0x025DEAB4), {layer});
    if (!in_mem2(ball)) return 0;
    guest_call(c, GC(0x020CB89C), {ball, 8});
    st32(ball + ac::speedF, u32_bits(f)), st32(ball + ac::kSpeedY, u32_bits(y)), st32(ball + ac::gravity, u32_bits(g));
    guest_call(c, GC(0x025E1988), {0x2852});
    return ball;
}

void put_link_at(uint32_t link, const float w[3]) { put_link(link, {w[0], w[1], w[2]}); }
void hurt(uint8_t quarters, int16_t way) { g_hurt = {quarters, way}; }

namespace {

// each step in play: the world's messages put in once this game is settled in its stage, its own shots and bombs out,
// friendly fire, pushing
void coop_world(Cpu* c, uint32_t link, bool settled, bool riding) {
    namespace net = mods::coop::net;
    namespace sync = mods::coop::sync;
    if (net::role() == net::Role::kOff) return;
    static std::vector<sync::WorldMsg> held;  // (until this game is settled in its stage)
    for (auto& m : sync::take_world()) held.push_back(std::move(m));
    if (!sync::arrived()) held.clear();
    boat_shot_out();
    mods::coop::items::step(c);
    for (size_t i = 0; settled && i < held.size(); i++) {
        const sync::WorldMsg& m = held[i];
        Reader r{m.body};
        if (m.type == net::kPlayerHit && !net::party_flag(net::kFriendlyFire, true)) continue;  // (the party's switch: off)
        if (m.type == net::kPlayerHit) {  // another player's sword or arrow on this game's Link (friendly fire)
            const uint8_t who = r.get<uint8_t>(), quarters = r.get<uint8_t>();
            const int16_t way = r.get<int16_t>();
            if (who == net::my_id() && quarters >= 1 && quarters <= 4) g_hurt = {quarters, way};
        } else if (m.type == net::kMonHit || m.type == net::kMonGone) {
            mods::coop::monsters::message(c, m.type, m.body);
        } else if (m.type == net::kBoatShot) {
            boat_shot_from(c, link, r);
        } else if (m.type == net::kBombed) {
            mods::coop::items::bombed(c, link, (const uint8_t*)m.body.data(), m.body.size());
        }
    }
    if (settled) held.clear();
    if (settled) mods::coop::monsters::step(c, link);  // the placed monsters: ours out, the others' put in
    mods::coop::pvp::step(c, link, settled);  // (C28: PvP at sea, through a server)
    // friendly fire (the fork's rule: always on): a sword attack of this game's Link (as it starts: daPy_lk_c::mCurProc
    // entering a cut, d_a_player_main.h daPyProc_CUT_*) hits each other player shown within 200 in front of him (75
    // degrees either side); spin and jump cuts a heart, the others half. The puppets have no colliders: this game decides
    const V3 me = pos_of(link);
    {
        static uint32_t was = 0;
        const uint32_t proc = ld32(link + kCurProc);
        const bool spin = proc == 0x55 || proc == 0x58 || proc == 0x59, jump = proc == 0x5B || proc == 0x5E || proc == 0x63;
        const bool cut = (proc >= 0x41 && proc <= 0x4A) || proc == 0x56 || proc == 0x5A || proc == 0x61 || spin || jump;
        if (cut && proc != was) {
            const double yaw = (int16_t)ld16(link + kAngleY) * M_PI / 32768;
            for (int id = 0; id < net::kMaxPlayers; id++)
                if (float p[3]; id != net::my_id() && net::party_flag(net::kFriendlyFire, true) && sync::place(id, p)) {
                    const double dx = p[0] - me.x, dz = p[2] - me.z, d = std::hypot(dx, dz);
                    const double off = std::remainder(atan2(dx, dz) - yaw, 2 * M_PI);
                    if (d > 200 || std::fabs(p[1] - me.y) > 120 || (!spin && std::fabs(off) > 75 * M_PI / 180)) continue;
                    const int16_t way = (int16_t)(atan2(dx, dz) * 32768 / M_PI);
                    net::send(-1, net::kPlayerHit, Bytes{}.put<uint8_t>((uint8_t)id).put<uint8_t>(spin || jump ? 4 : 2).put(way).b);
                    trace("coop: this game's cut %#x hits player %d", proc, id);
                }
        }
        was = proc;
    }
    // the players push each other: this game moves only its own Link out of the others' room (radius 35 each)
    if (riding) return;
    for (int id = 0; id < net::kMaxPlayers; id++)
        if (float p[3]; sync::place(id, p)) {
            const double dx = me.x - p[0], dz = me.z - p[2], d = std::hypot(dx, dz);
            if (d > 0.01 && d < 70 && std::fabs(me.y - p[1]) < 80) put_link(link, {p[0] + dx / d * 70, me.y, p[2] + dz / d * 70});
        }
}

// a placed monster this game runs goes for the nearest player: around its execute, Link stands where that player is
// (every monster reads Link's place through play's mPlayerInfo / mpPlayerPtr), then is put back
std::unordered_map<uint32_t, int> g_target;  // by actor id: the player it goes for
struct StandIn {
    uint32_t link = 0;
    float was[3];
    explicit StandIn(uint32_t actor, bool runs) {
        if (!runs) return;
        const uint32_t l = ld32(kLinkPtr);
        if (!in_mem2(l)) return;
        const float at[3] = {(float)ldf32(actor + kPos), (float)ldf32(actor + kPos + 4), (float)ldf32(actor + kPos + 8)};
        int& target = g_target.try_emplace(ld32(actor + 4), -1).first->second;
        float to[3];
        mods::coop::sync::nearest(at, l, target, to);
        if (target == mods::coop::net::my_id()) return;
        link = l;
        for (int i = 0; i < 3; i++) was[i] = (float)ldf32(l + kPos + 4 * i), st32(l + kPos + 4 * i, u32_bits(to[i]));
    }
    ~StandIn() { if (link) for (int i = 0; i < 3; i++) st32(link + kPos + 4 * i, u32_bits(was[i])); }
};

}  // namespace

// a friend's blow (kPlayerHit) on this game's Link, taken as a monster's is: a hit on his own body's cylinder (mCyl: its
// TG hit flags mObjTg.mRPrm and mGObjTg.mRPrm, the way it came mGObjTg.mRVec) with its damage (mStts's m_dmg), put in
// just before his execute, where daPy_lk_c::changeDamageProc (023FA578) finds it and plays his damage. Not while he still
// flinches from the last (mDamageWaitTimer; mModeFlg bit 8), as the game's own hits; a friend's blow
// leaves the last quarter heart
void before_execute(Cpu* c) {
    mods::coop::deck::move(c);  // (C16: a friend's boat's deck, before Link runs)
    mods::coop::pvp::before_execute(c);  // (C28: the puppets' hit areas read, a hit on this Link put in)
    const uint32_t link = ld32(kLinkPtr);
    if (!g_hurt.quarters || !in_mem2(link) || ld16(link + py::mDamageWaitTimer) || ld32(link + py::mModeFlg) & 8) return;
    const int q = std::min<int>(g_hurt.quarters, ld16(save() + layout::sv::kStatusA + layout::sv::kLife) - 1);
    const double a = g_hurt.way * M_PI / 32768;
    g_hurt = {};
    if (q <= 0) return;
    const uint32_t cyl = link + py::mCyl;
    st32(cyl + cc::mObjTg_mRPrm, ld32(cyl + cc::mObjTg_mRPrm) | 1), st32(cyl + cc::tg_mRPrm, ld32(cyl + cc::tg_mRPrm) | 1);
    st8(link + py::mStts + cc::stts_m_dmg, (uint8_t)q);
    st32(cyl + cc::tg_mRVec, u32_bits((float)(sin(a) * 10))), st32(cyl + cc::tg_mRVec + 4, 0), st32(cyl + cc::tg_mRVec + 8, u32_bits((float)(cos(a) * 10)));
}

void step(Cpu* c) {
    namespace sync = mods::coop::sync;
    const sync::Settings set = sync::settings();
    sync::poll(c, set.on);  // (at the title too: the session's messages)
    static uint32_t last_link = 0;
    static int settle = 0;  // steps left before the world's messages act in a newly entered stage (2 s)
    const uint32_t link = ld32(kLinkPtr);
    const bool fresh = link != last_link;  // a file loaded, a stage entered: start over
    last_link = link;
    const std::string stage((const char*)mem::ptr(kStage), strnlen((const char*)mem::ptr(kStage), 8));
    if (!in_mem2(link) || stage.empty() || stage == "sea_T" || stage == "Name" || stage == "ENDumi") return;  // title, file select
    if (fresh) settle = 60;
    else if (settle > 0) settle--;
    // nor while the next stage is asked for (dComIfGp's next stage enabled)
    const bool settled = !fresh && settle == 0 && !ld8(kPlay + inf::kNextStageEnabled);
    // riding the boat (daPy_lk_c::mCurProc, SHIP_READY..SHIP_RESTART but not SHIP_GET_OFF)
    const int proc = (int)ld32(link + kCurProc);
    const bool riding = proc >= 0x86 && proc <= 0x91 && proc != 0x90;
    mods::coop::emotes::step(link);           // (C22: one asked for sent, the bubble over Link)
    mods::coop::own_character(set.character);  // (also without a session)
    mods::coop::step(c, link, set.on);         // the test aids (record, ghost)
    sync::step(c, link, 0, false);             // (no shared ship's deck in this port)
    coop_world(c, link, settled, riding);
}

}  // namespace mods::coop::world

// fopAcM_fastCreate (025D5928): the boat's own cannon making its ball (daShip execute, returning to 02479684), and what
// Link makes as he uses an item, for the other players
extern "C" void f_025D5928_orig(Cpu* c);
extern "C" void hook_025D5928(Cpu* c) {
    using namespace mods::coop;
    const bool coop = net::role() != net::Role::kOff;
    const bool boat = coop && c->lr == GC(0x02479684) && c->r[3] == 0x126;
    if (boat) {
        const uint32_t pos = c->r[5], angle = c->r[7];
        auto& b = world::g_boat_shot;
        for (int i = 0; i < 3; i++) b.pos[i] = (float)ldf32(pos + 4 * i), b.angle[i] = (int16_t)ld16(angle + 2 * i);
    }
    const int16_t proc = (int16_t)c->r[3];
    const uint32_t param = c->r[4];
    const bool his = coop && executing() != 0;  // (Link's items: coop_items.h)
    f_025D5928_orig(c);
    if (his) items::made(proc, c->r[3]);
    if (coop) items::flower(proc, c->r[3], param);  // (a Bomb Flower's plant makes it: not while Link executes)
    if (boat) world::g_boat_shot.ball = c->r[3];
}

// fopAc_Execute (025D475C): a placed monster this game runs goes for the nearest player, and takes the other players'
// hits as its own; another's copy here takes their kills as its own death (coop_monsters.h)
extern "C" void f_025D475C_orig(Cpu* c);
extern "C" void hook_025D475C(Cpu* c) {
    using namespace mods::coop;
    const uint32_t a = c->r[3];
    if (net::role() == net::Role::kOff) return f_025D475C_orig(c);
    const bool takes = monsters::takes(a);
    world::StandIn s(a, monsters::runs(a));
    if (takes) monsters::before(a);
    const bool sail = sync::sail_swap(a, true);  // (a friend's sail: their boat's sail values, coop_sync.cpp)
    f_025D475C_orig(c);
    if (sail) sync::sail_swap(a, false);
    if (const uint32_t r = c->r[3]; takes) monsters::after(c, a), c->r[3] = r;
}
// dSv_info_c::isActor (025BA6A4, r4 set id, r5 room): a placed monster killed in this stage while a player was in it is
// not made again (coop_monsters.h)
extern "C" void f_025BA6A4_orig(Cpu* c);
extern "C" void hook_025BA6A4(Cpu* c) {
    const int set = (int)c->r[4], room = (int)c->r[5];
    f_025BA6A4_orig(c);
    if (!c->r[3] && mods::coop::monsters::killed(set, room)) c->r[3] = 1;
}
// fopAcM_createDisappear (025D99E8, r6 the drop): one of ours another player killed drops nothing here (theirs did)
extern "C" void f_025D99E8_orig(Cpu* c);
extern "C" void hook_025D99E8(Cpu* c) {
    if (mods::coop::monsters::dropless(c->r[3])) c->r[6] = 1, mods::trace("coop: a monster another player killed: no drop here");  // daDisItem_NONE1_e
    f_025D99E8_orig(c);
}
