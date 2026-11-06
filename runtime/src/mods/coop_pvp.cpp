// SPDX-License-Identifier: MPL-2.0
// Online co-op, PvP at sea (coop_pvp.h, GAPS C28).
//
// Hit areas. Each other player who can fight now (the server's mark: PvP active, not protected), outside the party and
// drawn here, gets a cylinder in the game's collision checks (dCcS, play +0x26A4), Link's girth and his height with his cap (radius 30, height 150; 125, his body only, let an aimed boomerang pass over, c28p),
// set every logic step after the actors' executes (coop_world's step), so the play scene's draw (dScnPly_Draw:
// dCcS::Move first) checks it against the step's attacks. It is the game's own dCcD_Cyl with a dCcD_Stts of no actor
// (Init with actor 0 sets the NoActor flag; d_a_bdk does the same), built once on the port's memory (as mod_actor's
// hit areas: the constructors, then the vtables): no game heap. Its TG takes only the sword, bombs (the boat's cannon
// ball is one), the boomerang and the four arrows (coop_pvp_rules.h kTgTypes); TG Spl 0 (1 would shock Link), no shield,
// no push (CO off), except while this game's cannon ball flies: then CO is on with NoCrr, so the ball bursts on the
// puppet (a ball explodes on a CO hit) and its blast hits the TG. The hits are read before the next step's executes
// (the attacker still exists then, a bomb's blast for a while): the hitting collider's actor must be this game's Link,
// one of his items (coop_items, also lately gone) or this game's cannon ball; anything else (an enemy's bomb) is
// dropped. An arrow that hits is removed (an actor-less target does not stop it).
//
// A hit this game makes goes to the server (net::pvp_hit) with its weapon and damage. A hit relayed here (the server
// checked both players, the zones, protection, distance, rate, blocks) is taken when this game agrees (the attacker's
// puppet near, the hit point at this Link, nothing busy): as a monster's blow (coop_world::hurt), or, when it would take
// the last heart, as a knockout: hearts full again, the server told (with the wallet; it answers the rupees lost, which
// vanish), and Link put back at the nearest safe zone's edge (the server's PVP_RESPAWN) by the game's own warp.
//
// The rest: the toggle and "busy" (off the open sea, an event, a message, a menu: protected) to the server on change;
// notes when the server's state changes (safe zone, PvP zone, protection); the safe zones' borders and PvP players on
// the sea radar (coop_radar); a small red mark over PvP players' puppets; another player's cannon shot or bomb blast
// shown here as a ball or bomb of this game's (party members' come as before, net::kBoatShot, kBombed), whose blast is
// kept from hurting any player here (shown_only): only the server's checked hit costs hearts.
#include "coop_pvp.h"
#include "coop_pvp_rules.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "coop_emotes.h"
#include "coop_items.h"
#include "coop_layout.h"
#include "coop_names.h"
#include "coop_net.h"
#include "coop_radar.h"
#include "coop_sync.h"
#include "coop_world.h"
#include "guest_addr.h"
#include "imgui.h"
#include "mods.h"
#include "runtime.h"
#include "../aspect.h"
#include "../overlay/extensions.h"
#include "../savestate.h"

namespace mods::coop::pvp {
namespace {

using namespace names;
const uint32_t kPlay = GD(0x1046F0B0), kStage = kPlay + inf::kCurStage;
constexpr uint32_t kPos = ac::kPos;
constexpr uint32_t kCutType = 0x3AC;  // daPy_lk_c::mCutType: 8 spin, 10 jump cut
constexpr float kBallGravity = -2.5f;  // daShip_HIO_c0::cannon_gravity (d_a_ship.cpp)

// a puppet's hit area on the port's memory: dCcD_Stts (0x3C), dCcD_Cyl (0x130), its dCcD_SrcCyl (0x44)
constexpr uint32_t kStts = 0, kCyl = 0x40, kSrc = 0x170, kBlock = 0x200;
constexpr float kAreaHeight = 150;
struct Area {
    uint32_t mem = 0;     // the block (0: not built)
    bool set = false;     // put in the collision checks last step
};
Area g_area[net::kMaxPlayers];

uint32_t save() { return ld32(GD(0x101F84DC)) + 0x20; }  // dSv_player_c (coop_world's)
uint32_t status(uint16_t field) { return save() + layout::sv::kStatusA + field; }
bool in_mem2(uint32_t a) { return a >= 0x10000000u && a < 0x50000000u; }
void put_f(uint32_t a, float v) { uint32_t u; memcpy(&u, &v, 4); st32(a, u); }
std::string stage_now() { return std::string((const char*)mem::ptr(kStage), strnlen((const char*)mem::ptr(kStage), 8)); }

bool build(Cpu* c, Area& a) {
    if (a.mem) return true;
    a.mem = mem::host_alloc(kBlock, 0x20);
    if (!a.mem) return false;
    const uint32_t stts = a.mem + kStts, cyl = a.mem + kCyl, src = a.mem + kSrc;
    for (uint32_t i = 0; i < kBlock; i += 4) st32(a.mem + i, 0);
    guest_call(c, GC(0x0200BD2C), {stts});         // cCcD_Stts
    guest_call(c, GC(0x02515DA0), {stts + 0x1C});  // dCcD_GStts
    st32(stts + 0x18, GD(0x1004AE88)), st32(stts + 0x1C, GD(0x1004AEC0));
    guest_call(c, GC(0x02515F14), {stts, 0xFF, 0, 0});  // dCcD_Stts::Init(weight, dmg, no actor)
    guest_call(c, GC(0x02515FB8), {cyl});               // dCcD_GObjInf
    st32(cyl + 0x114, GD(0x100015A8)), st32(cyl + 0x110, GD(0x1001251C));
    guest_call(c, GC(0x02018590), {cyl + 0x118});       // cM3dGCyl
    st32(cyl + 0x3C, GD(0x1004B108)), st32(cyl + 0x114, GD(0x1004B160)), st32(cyl + 0x12C, GD(0x1004B150));
    st32(src + 0x10, kTgTypes);  // TG type
    st32(src + 0x14, 0x0B);      // TG Set | IsEnemy | IsOther (not IsPlayer: no enemy's attack)
    st32(src + 0x28, 0x2);       // TG NoConHit (one hit per attack)
    put_f(src + 0x3C, 30), put_f(src + 0x40, kAreaHeight);
    guest_call(c, GC(0x02516518), {cyl, src});  // dCcD_Cyl::Set
    st32(cyl + 0x44, stts);
    return true;
}

// the server's PvP state flags (proto PvpStateFlags)
constexpr uint8_t kStActive = 1, kStProtected = 2, kStSafe = 4, kStEntering = 8, kStForced = 16;

// ---- what the session says (game thread) ----
net::PvpRules g_rules;
uint8_t g_state = 0;
bool g_ko = false;          // knocked out: waiting for the server's respawn place
bool g_ko_ship = false;     // ... and Link had his boat (he comes back on it)
uint32_t g_ko_at = 0;
std::string g_ko_by;
struct Shot { uint32_t pid; uint8_t kind; uint32_t at; };
std::vector<Shot> g_shots;  // this game's cannon balls and test bombs, by process id, for 6 s
std::vector<Shot> g_shown;  // other players' cannon shots shown here (their blasts must not hurt this Link: c28e)
constexpr uint32_t kBombSph = 0x920;  // daBomb_c::mSph (wwhd_src d_a_bomb.h): its AT's SPrm first (VsPlayer: 4)
std::vector<std::pair<uint64_t, uint32_t>> g_recent;  // (player, attacker process) -> when: one report a blow
uint32_t g_first = 0;       // the session's first step with rules (the test aids' clock)

// the overlay's marks: layout places of PvP players' heads
std::mutex g_mark_mu;
std::vector<std::pair<float, float>> g_marks;
std::atomic<bool> g_marks_on{false};

bool my_shot(uint32_t pid, uint8_t& kind) {
    for (const Shot& s : g_shots) if (s.pid == pid) return kind = s.kind, true;
    return false;
}

float my_pos(uint32_t link, int e) { return (float)ldf32(link + kPos + 4 * e); }

// a hit this game made on player `id`'s area, read after the scene's collision checks
void read(Cpu* c, uint32_t link, int id, Area& a) {
    const uint32_t cyl = a.mem + kCyl;
    if (!(ld32(cyl + 0x1C) & 1)) return;
    const uint32_t hitobj = ld32(cyl + 0x20), apid = ld32(cyl + 0xA0);
    const uint32_t actor = apid != 0xFFFFFFFFu ? guest_call(c, GC(0x025DE50C), {apid}) : 0;  // fopAcM_SearchByID
    const uint32_t type = in_mem2(hitobj) ? ld32(hitobj + 0x10) : 0;
    float at[3];
    for (int e = 0; e < 3; e++) at[e] = (float)ldf32(cyl + 0xCC + 4 * e);
    guest_call(c, GC(0x0251621C), {cyl});  // ClrTgHit
    uint8_t shot = 0;
    const bool is_link = actor && actor == link, cannon = my_shot(apid, shot) && shot == kCannon;
    int16_t item = items::mine_id(apid);
    if (!item && my_shot(apid, shot) && shot == kBomb) item = kProcBomb;
    const bool big = is_link && (ld32(link + kCutType) == 8 || ld32(link + kCutType) == 10);
    const uint8_t kind = weapon(type, is_link, cannon, item);
    if (!kind) {
        mods::trace("coop: PvP: player %d's hit area hit by %08X (type %X, process %08X): not this player's, dropped", id, actor, type, apid);
        return;
    }
    const uint32_t now = net::now_ms();
    const uint64_t key = (uint64_t)id << 32 | apid;
    g_recent.erase(std::remove_if(g_recent.begin(), g_recent.end(), [&](const auto& r) { return now - r.second > 600; }), g_recent.end());
    if (std::any_of(g_recent.begin(), g_recent.end(), [&](const auto& r) { return r.first == key; })) return;  // (the same blow)
    g_recent.push_back({key, now});
    for (float& v : at) if (!std::isfinite(v)) return;
    const uint8_t q = damage(kind, type, big);
    net::pvp_hit(id, kind, q, at);
    mods::trace("coop: PvP: this game's %s hits player %d (%s), %d quarter hearts, at %.0f %.0f %.0f", kind_name(kind), id,
                net::name(id).c_str(), q, at[0], at[1], at[2]);
    if (kind == kArrow && actor) guest_call(c, GC(0x025D57E4), {apid});  // (an arrow that hit is gone: fopAcM_delete)
}

// this step's hit areas (after the executes): the PvP players drawn here, outside the party
void set_areas(Cpu* c, uint32_t link) {
    const bool active = (g_state & kStActive) != 0;
    bool ball_flies = false;
    for (const Shot& s : g_shots) if (s.kind == kCannon && guest_call(c, GC(0x025DE50C), {s.pid})) ball_flies = true;
    const uint32_t ccs = guest_call(c, GC(0x025200D4), {}) + 0x26A4;  // play's dCcS
    static const uint32_t place = mem::host_alloc(16);
    for (int id = 0; id < net::kMaxPlayers; id++) {
        Area& a = g_area[id];
        a.set = false;
        float p[3];
        const uint8_t mark = net::pvp_mark(id);
        if (!active || id == net::my_id() || net::member(id) || !(mark & 1) || (mark & 2) || !sync::shown_at(id, p)) continue;
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) || std::fabs(p[0]) > 1e6f || std::fabs(p[2]) > 1e6f) continue;
        if (!build(c, a)) continue;
        const uint32_t stts = a.mem + kStts, cyl = a.mem + kCyl;
        guest_call(c, GC(0x02515E50), {stts + 0x1C});  // dCcD_GStts::Move (NoConHit's last attacker ids)
        for (int e = 0; e < 3; e++) put_f(place + 4 * e, p[e]);
        guest_call(c, GC(0x020182E0), {cyl + 0x118, place});  // cM3dGCyl::SetC
        put_f(cyl + 0x124, 30), put_f(cyl + 0x128, kAreaHeight);
        st32(cyl + 0x18, 0x0B), st32(cyl + 0x28, kTgTypes), st32(cyl + 0x94, 0x2);
        st32(cyl + 0x2C, ball_flies ? 0x149u : 0u);  // CO: Set | IsOther | VsOther | NoCrr, only for this game's ball
        guest_call(c, GC(0x0200E240), {ccs, cyl});  // cCcS::Set
        a.set = true;
    }
}

// why this game counts as busy (protected) now, "" when not: off the open sea, settling in, an event, a message, a menu
std::string busy_why(uint32_t link, bool settled) {
    if (stage_now() != "sea") return "not on the open sea";
    if (!settled) return "settling in";
    if (!in_mem2(link)) return "no Link";
    return ss::control_problem();
}

void note(const std::string& s) { ss::notice(s); mods::trace("coop: PvP: \"%s\"", s.c_str()); }

// the test aids' list "<s>:<a>[,<s>:<a>...]" (a: free text up to the next comma)
struct Planned { uint32_t at_ms; std::string what; bool done = false; };
std::vector<Planned> planned(const char* env) {
    std::vector<Planned> out;
    const char* e = getenv(env);
    for (const char* p = e; p && *p;) {
        char* end = nullptr;
        const double s = strtod(p, &end);
        if (end == p || *end != ':') break;
        const char* q = end + 1;
        const char* stop = strchr(q, ',');
        out.push_back({(uint32_t)(s * 1000), std::string(q, stop ? size_t(stop - q) : strlen(q))});
        p = stop ? stop + 1 : nullptr;
    }
    return out;
}

// the nearest other player drawn here, outside the party first (the test aids' target), -1 none
int nearest_stranger(uint32_t link, float out[3]) {
    int best = -1;
    float bd = 1e30f;
    for (int id = 0; id < net::kMaxPlayers; id++) {
        float p[3];
        if (id == net::my_id() || !sync::shown_at(id, p)) continue;
        if (net::member(id) && best >= 0 && !net::member(best)) continue;
        const float d = std::hypot(p[0] - my_pos(link, 0), p[2] - my_pos(link, 2));
        if (d < bd || (best >= 0 && net::member(best) && !net::member(id))) bd = d, best = id, memcpy(out, p, 12);
    }
    return best;
}

// test aid WWHD_COOP_TEST_PVP_FIRE=<s>:cannon|bomb[,...]: that long after the session's first step with PvP rules, a
// cannon ball of this game's flies at the nearest player outside the party (from 200 units off, level, as fast as the
// boat's), or one of its bombs goes off at them; each counts as this game's own
void test_fire(Cpu* c, uint32_t link, const std::string& what) {
    float p[3];
    const int id = nearest_stranger(link, p);
    if (id < 0) return mods::trace("coop: PvP test: %s: nobody to aim at", what.c_str());
    if (what == "cannon") {
        const float dx = p[0] - my_pos(link, 0), dz = p[2] - my_pos(link, 2), d = std::max(1.f, std::hypot(dx, dz));
        const float from[3] = {p[0] - dx / d * 200, p[1] + 60, p[2] - dz / d * 200};
        const int16_t angle[3] = {0, (int16_t)(std::atan2(dx, dz) * 32768 / M_PI), 0};
        const uint32_t ball = world::spawn_ball(c, link, from, angle, 40, 2, kBallGravity);
        if (ball) fired(ball, from, angle[1], 40, 2);
        mods::trace("coop: PvP test: a cannon ball at player %d (%s): %08X", id, net::name(id).c_str(), ball);
    } else if (what == "bomb") {
        const uint32_t bomb = items::spawn(c, link, kProcBomb, 0x80000000, p);
        if (bomb) g_shots.push_back({ld32(bomb + 4), kBomb, net::now_ms()}), items::made(kProcBomb, bomb);  // (as his own: its blast goes out)
        mods::trace("coop: PvP test: a bomb at player %d (%s): %08X", id, net::name(id).c_str(), bomb);
    }
}

}  // namespace

void shown_only(uint32_t bomb) {
    if (!in_mem2(bomb)) return;
    g_shown.push_back({ld32(bomb + 4), kBomb, net::now_ms()});
    st32(bomb + kBombSph, ld32(bomb + kBombSph) & ~4u);
}

void fired(uint32_t ball, const float pos[3], int16_t yaw, float speed_f, float speed_y) {
    if (!in_mem2(ball) || !net::via_server()) return;
    g_shots.push_back({ld32(ball + 4), kCannon, net::now_ms()});
    if (!(g_state & kStActive)) return;
    const double t = yaw * M_PI / 32768;
    const float v[3] = {(float)(speed_f * sin(t) * 30), speed_y * 30, (float)(speed_f * cos(t) * 30)};  // (per second)
    net::pvp_shot(kCannon, pos, v);
}

void before_execute(Cpu* c) {
    if (!net::via_server()) return;
    const uint32_t link = ld32(kPlay + inf::kLink);
    if (!in_mem2(link) || std::none_of(std::begin(g_area), std::end(g_area), [](const Area& a) { return a.set; })) return;
    // (the guest calls below change the registers: the execute handler's own arguments are in them (r3: its callback;
    // c28a/b crashed on a clobbered r3), so all of them are kept)
    const Cpu saved = *c;
    for (int id = 0; id < net::kMaxPlayers; id++)
        if (g_area[id].set) read(c, link, id, g_area[id]);
    *c = saved;
}

void step_in(Cpu* c, uint32_t link, bool settled);
void step(Cpu* c, uint32_t link, bool settled) {
    const Cpu saved = *c;  // (the registers as the caller had them: see before_execute)
    step_in(c, link, settled);
    *c = saved;
}

void step_in(Cpu* c, uint32_t link, bool settled) {
    const uint32_t now = net::now_ms();
    g_shots.erase(std::remove_if(g_shots.begin(), g_shots.end(), [&](const Shot& s) { return now - s.at > 6000; }), g_shots.end());
    g_shown.erase(std::remove_if(g_shown.begin(), g_shown.end(), [&](const Shot& s) { return now - s.at > 6000; }), g_shown.end());
    // another player's shot shown here is only a picture: its ball's or bomb's blast (set by its execute, just before)
    // hurts no player here; the hit itself comes from the server, checked. (Also without PvP: party members' shots)
    for (const Shot& s : g_shown)
        if (const uint32_t ball = guest_call(c, GC(0x025DE50C), {s.pid}); in_mem2(ball)) st32(ball + kBombSph, ld32(ball + kBombSph) & ~4u);
    g_rules = net::pvp_rules();
    if (!net::via_server() || g_rules.mode <= 0) {
        for (Area& a : g_area) a.set = false;
        radar::set_zones({}, {});
        g_marks_on = false, g_state = 0, g_ko = false, g_first = 0;
        return;
    }
    if (!g_first) g_first = now ? now : 1;
    // the toggle and busy, to the server on change
    static const bool test_off = getenv("WWHD_COOP_TEST_PVP_OFF") != nullptr;
    const std::string why = busy_why(link, settled);
    net::pvp_flag(sync::settings().pvp_sea && !test_off, !why.empty());
    if (static std::string was = "-"; why != was) was = why, mods::trace("coop: PvP: busy: %s", why.empty() ? "no" : why.c_str());
    // the server's state: notes as it changes
    uint32_t left = 0;
    const uint8_t st = net::pvp_state(&left);
    if (st != g_state) {
        const uint8_t was = g_state;
        g_state = st;
        const bool shown = (st | was) & (kStActive | kStSafe);
        if (shown && (st & kStSafe) && !(was & kStSafe)) note("Safe zone: no fighting here");
        else if (shown && (st & kStEntering) && !(was & kStEntering)) note("Safe zone: you are safe in " + std::to_string(g_rules.enter_delay_ms / 1000) + " s");
        else if ((was & kStSafe) && !(st & kStSafe) && (st & kStActive)) note("PvP zone: other players can attack you here");
        if ((st & kStProtected) && !(was & kStProtected) && (st & kStActive)) note("Protected for " + std::to_string((left + 500) / 1000) + " s");
        else if ((was & kStProtected) && !(st & kStProtected) && (st & kStActive)) note("Protection over");
    }
    // what came: hits on this Link, knockouts, the rupees, the place to come back at, others' shots
    const std::vector<net::PvpEvent> events = net::pvp_take();
    const int life = in_mem2(link) ? (int)ld16(status(layout::sv::kLife)) : 0;
    for (const net::PvpEvent& e : events) {
        switch (e.kind) {
        case net::PvpEvent::kHitOn: {
            float from[3] = {};
            const bool seen = e.from >= 0 && sync::shown_at(e.from, from);
            const float me[3] = {my_pos(link, 0), my_pos(link, 1), my_pos(link, 2)};
            const float ad = seen ? std::hypot(from[0] - me[0], from[2] - me[2]) : -1.f;
            const float hd = std::hypot(e.at[0] - me[0], e.at[2] - me[2]);
            const Take t = take(ad, hd, g_rules.max_hit_dist, !busy_why(link, settled).empty() || g_ko, e.quarters, life);
            mods::trace("coop: PvP: hit by player %d (%s): %s, %d quarter hearts (attacker %.0f away, hit point %.0f): %s", e.from,
                        e.from >= 0 ? net::name(e.from).c_str() : "?", kind_name(e.what), e.quarters, ad, hd,
                        t == Take::kRefused ? "refused" : t == Take::kHurt ? "taken" : "a knockout");
            if (t == Take::kHurt) {
                const int16_t way = (int16_t)(std::atan2(me[0] - from[0], me[2] - from[2]) * 32768 / M_PI);
                world::hurt(e.quarters, way);
            } else if (t == Take::kKnockout) {
                const int wallet = (int)ld16(status(layout::sv::kRupee));
                st16(status(layout::sv::kLife), ld16(status(layout::sv::kMaxLife)));
                g_ko = true, g_ko_at = now, g_ko_by = net::name(e.from), g_ko_ship = in_mem2(ld32(kPlay + 0x5B3C));  // (play's ship)
                net::pvp_ko(e.from, (uint16_t)std::clamp(wallet, 0, 65535));
                note("Knocked out by " + g_ko_by);
            }
            break;
        }
        case net::PvpEvent::kRupees: {
            const int wallet = (int)ld16(status(layout::sv::kRupee)), lost = std::min<int>((int)e.value, wallet);
            st16(status(layout::sv::kRupee), (uint16_t)(wallet - lost));
            mods::trace("coop: PvP: knocked out: %d rupees gone (%d -> %d)", lost, wallet, wallet - lost);
            if (lost > 0) note("-" + std::to_string(lost) + " rupees");
            break;
        }
        case net::PvpEvent::kRespawn: {
            if (!g_ko) break;
            g_ko = false;
            ss::Warp w;
            w.stage = "sea", w.room = (int)e.value + 1, w.at_position = true;
            w.pos[0] = e.at[0], w.pos[1] = g_ko_ship ? 1.f : 0.f, w.pos[2] = e.at[2], w.on_ship = g_ko_ship;
            const net::PvpZone* z = nullptr;
            for (const net::PvpZone& s : g_rules.zones) if (s.square == (int)e.value) z = &s;
            w.angle = z ? (int)(int16_t)(std::atan2(z->x - e.at[0], z->z - e.at[2]) * 32768 / M_PI) : 0;
            mods::trace("coop: PvP: back at %.0f %.0f (square %u)", e.at[0], e.at[2], e.value);
            ss::request_warp(w, [](bool ok, const std::string& no) {
                if (!ok) mods::trace("coop: PvP: the warp back was refused: %s", no.c_str());
            });
            break;
        }
        case net::PvpEvent::kKoOf:
            if (e.from == net::my_id()) break;  // (this game's own: told above)
            mods::trace("coop: PvP: player %d (%s) knocked out by %d (%s)", e.from, e.from >= 0 ? net::name(e.from).c_str() : "?", e.by, e.by_name.c_str());
            if (e.by == net::my_id() && e.from >= 0) note("You knocked out " + net::name(e.from));
            else if (e.from >= 0 && !e.by_name.empty()) note(e.by_name + " knocked out " + net::name(e.from));
            break;
        case net::PvpEvent::kShotOf: {  // (a cannon ball or a bomb's blast shown; party members' come as kBoatShot, kBombed)
            if (e.from < 0 || net::member(e.from) || stage_now() != "sea") break;
            if (e.what == kBomb) {
                if (std::any_of(std::begin(e.at), std::end(e.at), [](float v) { return !std::isfinite(v) || std::fabs(v) > 1e6f; })) break;
                const uint32_t bomb = items::spawn(c, link, kProcBomb, 0x80000000, e.at);  // (state 0: goes off as it is made)
                shown_only(bomb);
                mods::trace("coop: PvP: player %d's bomb went off here: %08X", e.from, bomb);
                break;
            }
            if (e.what != kCannon) break;
            const float f = std::hypot(e.v[0], e.v[2]) / 30, y = e.v[1] / 30;
            const int16_t angle[3] = {0, (int16_t)(std::atan2(e.v[0], e.v[2]) * 32768 / M_PI), 0};
            const uint32_t ball = world::spawn_ball(c, link, e.at, angle, f, y, kBallGravity);
            if (ball) g_shown.push_back({ld32(ball + 4), kCannon, now}), st32(ball + kBombSph, ld32(ball + kBombSph) & ~4u);
            mods::trace("coop: PvP: player %d's cannon shot shown here: %08X", e.from, ball);
            break;
        }
        }
    }
    if (g_ko && now - g_ko_at > 8000) g_ko = false;  // (no place came: stays where it is, hearts full)
    // the test aids
    if (static std::vector<Planned> fire = planned("WWHD_COOP_TEST_PVP_FIRE"); !fire.empty())
        for (Planned& p : fire)
            if (!p.done && now - g_first >= p.at_ms) p.done = true, test_fire(c, link, p.what);
    // test aid WWHD_COOP_TEST_PVP_MOVE=<s>:<dx>/<dz>[,...]: this Link moved by that much, that long after the first step
    if (static std::vector<Planned> move = planned("WWHD_COOP_TEST_PVP_MOVE"); !move.empty())
        for (Planned& p : move)
            if (float dx = 0, dz = 0; !p.done && now - g_first >= p.at_ms && sscanf(p.what.c_str(), "%f/%f", &dx, &dz) == 2) {
                p.done = true;
                const float to[3] = {my_pos(link, 0) + dx, my_pos(link, 1), my_pos(link, 2) + dz};
                world::put_link_at(link, to);
                if (const uint32_t ship = ld32(kPlay + 0x5B3C); in_mem2(ship))  // (his boat with him: riding, it carries him)
                    for (const uint32_t o : {ac::kPos, ac::kOldPos})
                        put_f(ship + o, (float)ldf32(ship + o) + dx), put_f(ship + o + 8, (float)ldf32(ship + o + 8) + dz);
                mods::trace("coop: PvP test: moved to %.0f %.0f", to[0], to[2]);
            }
    // test aid WWHD_COOP_TEST_PVP_EQUIP=<slot>[,<slot>...] (hex item slots, FF keeps: 0C the bow, 05 the boomerang): those
    // items on the item buttons in the save's order (0 X, 1 Y, 2 R, ...) at the first settled step, as the item menu puts them (the buttons refreshed by SaveMgr's refreshGame)
    if (static const char* q = getenv("WWHD_COOP_TEST_PVP_EQUIP"); q && settled && in_mem2(link)) {
        unsigned v[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        const int n = sscanf(q, "%x,%x,%x,%x,%x", &v[0], &v[1], &v[2], &v[3], &v[4]);
        for (int i = 0; i < n; i++) {
            if (v[i] == 0xFF) continue;
            st8(status(layout::sv::kSelectItem) + i, (uint8_t)v[i]);
            // (and play's copy of the buttons' items, play.mSelectItem, game info +0x5BBB (item_ring's kPlaySelect), which Link uses: the save's
            // alone moved only the pictures, c28l)
            st8(kPlay + 0x5BBB + i, v[i] < 21 ? ld8(save() + layout::sv::kItem + v[i]) : 0xFF);
            mods::trace("coop: PvP test: button %d: slot %02X (item %02X)", i, v[i], v[i] < 21 ? ld8(save() + layout::sv::kItem + v[i]) : 0xFF);
        }
        if (n > 0) guest_call(c, GC(0x02721880), {0});
        q = nullptr;
    }
    // test aid WWHD_COOP_TEST_PVP_AIM=<from s>:<to s>[,...]: in those windows this Link faces the nearest other player
    // (shape and current heading), so a sword cut, an arrow or the boomerang the presses start (WWHD_PRESS) goes at them
    if (static std::vector<Planned> aim = planned("WWHD_COOP_TEST_PVP_AIM"); !aim.empty() && in_mem2(link))
        for (const Planned& p : aim)
            if (float until = 0; sscanf(p.what.c_str(), "%f", &until) == 1 && now - g_first >= p.at_ms && now - g_first < (uint32_t)(until * 1000)) {
                float t[3];
                if (nearest_stranger(link, t) < 0) break;
                const int16_t yaw = (int16_t)(std::atan2(t[0] - my_pos(link, 0), t[2] - my_pos(link, 2)) * 32768 / M_PI);
                st16(link + ac::kAngleY, (uint16_t)yaw), st16(link + ac::current + 0xE, (uint16_t)yaw);
                if (static uint32_t said = 0; now - said > 2000) said = now, mods::trace("coop: PvP test: facing %d at %.0f %.0f", yaw, t[0], t[2]);
            }
    // test aid WWHD_COOP_TEST_WALLET=<n>: the wallet set at the first step (the knockout's rupee loss shows)
    if (static const char* w = getenv("WWHD_COOP_TEST_WALLET"); w && settled) {
        st16(status(layout::sv::kRupee), (uint16_t)atoi(w));
        mods::trace("coop: PvP test: wallet %d", atoi(w));
        w = nullptr;
    }
    // test aid WWHD_COOP_TEST_LIFE=<quarters>: this Link's hearts set at the first settled step (a knockout comes sooner)
    if (static const char* q = getenv("WWHD_COOP_TEST_LIFE"); q && settled && in_mem2(link)) {
        st16(status(layout::sv::kLife), (uint16_t)atoi(q));
        mods::trace("coop: PvP test: hearts %d quarters", atoi(q));
        q = nullptr;
    }
    if (static int was = -1; in_mem2(link) && settled && (int)ld16(status(layout::sv::kLife)) != was) {  // (the trace: hearts)
        const int now_life = (int)ld16(status(layout::sv::kLife));
        if (was >= 0) mods::trace("coop: PvP: hearts %d -> %d quarters (wallet %u)", was, now_life, ld16(status(layout::sv::kRupee)));
        was = now_life;
    }
    // the radar: the safe zones' borders
    {
        std::vector<radar::Far> at;
        std::vector<float> r;
        if (sync::settings().pvp_sea || (g_state & kStForced))
            for (const net::PvpZone& z : g_rules.zones) at.push_back({z.x, z.z, false}), r.push_back(z.r);
        radar::set_zones(at, r);
    }
    set_areas(c, link);
    // the marks over PvP players' puppets (layout units, for the overlay)
    std::vector<std::pair<float, float>> marks;
    for (int id = 0; id < net::kMaxPlayers; id++) {
        float p[3], x, y;
        if (id == net::my_id() || net::member(id) || !(net::pvp_mark(id) & 1) || !sync::shown_at(id, p)) continue;
        p[1] += 175;
        if (emotes::to_layout(p, x, y)) marks.emplace_back(x, y);
    }
    {
        std::lock_guard<std::mutex> l(g_mark_mu);
        g_marks.swap(marks);
    }
    g_marks_on = !g_marks.empty();
}

void register_overlay() {
    overlay::ext::add_frame_layer({[](std::string&) { return g_marks_on.load(); },
                                   [](float w, float h) {
                                       std::vector<std::pair<float, float>> marks;
                                       {
                                           std::lock_guard<std::mutex> l(g_mark_mu);
                                           marks = g_marks;
                                       }
                                       const float a = aspect::game() > 0 ? aspect::game() : 16.f / 9.f;
                                       const float s = std::min(h / 720.f, w / (720.f * a));
                                       ImDrawList* d = ImGui::GetForegroundDrawList();
                                       for (const auto& [x, y] : marks) {  // a small red diamond: this player can fight
                                           const float cx = w / 2 + x * s, cy = h / 2 - y * s, r = 7 * s;
                                           d->AddQuadFilled(ImVec2(cx, cy - r), ImVec2(cx + r, cy), ImVec2(cx, cy + r), ImVec2(cx - r, cy),
                                                            IM_COL32(225, 50, 40, 235));
                                           d->AddQuad(ImVec2(cx, cy - r), ImVec2(cx + r, cy), ImVec2(cx, cy + r), ImVec2(cx - r, cy),
                                                      IM_COL32(255, 240, 220, 255), 1.5f * s);
                                       }
                                       return false;
                                   }});
}

}  // namespace mods::coop::pvp
