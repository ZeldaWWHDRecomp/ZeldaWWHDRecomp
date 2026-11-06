// SPDX-License-Identifier: MPL-2.0
// From Galvakron/ZeldaWWHDRecompMP 8a4400d (MPL-2.0), co-op work by AsadSi and Galvakron; adapted to this port.
//
// Online co-op, the placed monsters (coop_monsters.h). Learned from those who did it before (docs/multiplayer.md, prior
// art): a monster is known on every game by its place in the stage's data (ww-multiplayer's key: its home room and set
// id, fopAc_Create 025D4C70 copies them to +0x2FE and +0x2D8; monsters made at run time have set id 0xFFFF and stay each
// game's own); one game runs each place's (Valheim's area owner, coop_sync's owner()); the others' copies keep running
// their own logic, as sm64coopdx's do, and are put where the owner's are each step, 100 ms behind, with its health. A hit
// on a copy goes to the owner (kMonHit); a monster killed anywhere goes from everyone (kMonGone): the one who struck
// last is favoured, as extras.cpp's sea monsters.
//
// On the owner another player's hit is the monster's own: put on its body's collider as a sword's (TG hit flags, the
// blow its hitter, Link standing where the nearest player is: CoopStandIn) just before it executes, so its damage check
// takes it as from Link (cc_at_check: the health, the flinch, the hit sound; a kill its own death). The drop of one
// another player killed is the killer's game's alone (no drop here: fopAcM_createDisappear's type; every monster in
// the decomp drops through it). A third player's copy dies the same way, once the owner's health is gone or the kill
// comes (a blow as much as its health, no drop), instead of vanishing. A monster killed in
// a stage stays dead while anyone is in that stage (ww-multiplayer's kill union, M6.0): the host keeps who was killed
// where and sends it (kDead); dSv_info_c::isActor answers yes for them, so a newcomer's room does not make them again.
#include "coop_monsters.h"
#include "coop_names.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "coop_sync.h"
#include "guest_addr.h"
#include "mods.h"
#include "runtime.h"

namespace mods::coop::monsters {
namespace {

constexpr uint32_t kProcName = 0x08, kSetId = 0x2D8, kGroup = 0x2DA, kHomeRoom = 0x2FE, kOldPos = 0x300, kPos = 0x314,
                   kAngleY = 0x322, kShapeY = 0x32A, kHealth = 0x3A1;
constexpr uint8_t kEnemy = 2;  // fopAc_ENEMY_e, the actor's group (fopAc_Create: its profile's +0x2C)
constexpr size_t kRecord = 4 + 12 + 2 + 2 + 1, kMost = 40;
constexpr int kKill = 127;  // a hit to put in: killed by another player (as much as its health)
const uint32_t kPlay = GD(0x1046F0B0), kStage = kPlay + names::inf::kCurStage, kActorQueue = GD(0x101F3328);
// dCcS (play +0x26A4, cCcS::Set 0200E240): the colliders taking hits this step at +0x400, their count at +0x2804. A
// collider (dCcD_GObjInf, as the GameCube's): TG hit flags +0x1C and +0x98, the blow +0x20, its hitter's id and actor
// +0xA0 and +0xA4, the way it came +0xC0, where +0xCC, mStts +0x44 (its actor +0xC, damage +0x16), its box +0xF8; a
// blow's type +0x10, damage +0x14, special +0x6F. Link's body cylinder is at +0x765C
const uint32_t kCcS = kPlay + names::inf::kCcS;
// (C18) the same offsets by name, from the WWHD decomp's layouts (wwhd_src include/SSystem/SComponent/c_cc_s.h
// cCcSState, include/d/d_cc_d.h dCcD_GObjInf / dCcD_Stts, whose WWHD_OFFSET checks hold them; d_com_inf_game.h PLAY_CCS)
namespace ccs {  // cCcSState
constexpr uint32_t mpObjTg = 0x400, mObjTgCount = 0x2804, mObjCount = 0x280C, kTgMax = 0x300;
}
namespace obj {  // dCcD_GObjInf
constexpr uint32_t mObjTg_mRPrm = 0x1C, mObjTg_mHitObj = 0x20, mStts = 0x44, mGObjTg = 0x94;
constexpr uint32_t tg_mRPrm = mGObjTg + 0x04, tg_mApid = mGObjTg + 0x0C, tg_mAc = mGObjTg + 0x10, tg_mRVec = mGObjTg + 0x2C,
                   tg_mHitPos = mGObjTg + 0x38;
constexpr uint32_t kShapeAab = 0xF8;  // sizeof(dCcD_GObjInf): the shape attribute's box follows (min, then max +0xC)
}
namespace status {  // dCcD_Stts
constexpr uint32_t mp_actor = 0x0C, m_dmg = 0x16;
}

struct Rec { float p[3]; int16_t yaw, shape; int8_t hp; };
struct Copy { int8_t hp = -128; bool dying = false; int dying_steps = 0; };
struct Mine { int8_t hp, room; uint16_t set; };
struct Dead { char stage[8]; int8_t room; uint16_t set; uint32_t seen; };
struct Hit { int dmg = 0, steps = 0; };
std::unordered_map<uint32_t, Copy> g_copies;   // another's monsters here: the health last put in
std::unordered_map<uint32_t, Mine> g_mine;     // ours: their health last step
std::vector<Dead> g_dead;                      // the placed monsters killed in the stages the players are in
std::unordered_map<uint32_t, Hit> g_hits;      // ours, by key: other players' hits, put in as it executes
std::unordered_map<uint32_t, uint32_t> g_body; // ours, by process id: the collider it takes hits on
std::unordered_set<uint32_t> g_dropless;       // process ids of ours another player killed: the drop was theirs
uint32_t g_tg_was, g_hp_was;
char g_stage[8];
uint32_t steps_here = 0;  // logic steps in this stage (the test aid)

const char* stage() { return (const char*)mem::ptr(kStage); }
float f32(uint32_t a) { return (float)ldf32(a); }
uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
bool placed(uint32_t a) { return ld8(a + kGroup) == kEnemy && ld16(a + kSetId) != 0xFFFF; }
uint32_t key(uint32_t a) { return ld16(a + kProcName) * 0x9E3779B1u ^ ld8(a + kHomeRoom) * 0x85EBCA6Bu ^ ld16(a + kSetId); }
int owner_of(uint32_t a) { return sync::owner(stage(), (int8_t)ld8(a + kHomeRoom)); }

template <class F> void each_monster(F f) {  // g_fopAcTg_Queue: node next +8, actor +0xC (coop_progress.cpp)
    uint32_t n = ld32(kActorQueue);
    for (int k = 0; n >= 0x10000000 && n < 0x50000000 && k < 4096; k++, n = ld32(n + 8))
        if (const uint32_t a = ld32(n + 0xC); a && placed(a)) f(a);
}
uint32_t find(uint32_t k) {
    uint32_t out = 0;
    each_monster([&](uint32_t a) { if (!out && key(a) == k) out = a; });
    return out;
}
bool is_dead(const char* st, int room, uint16_t set) {
    return std::any_of(g_dead.begin(), g_dead.end(), [&](const Dead& d) { return !memcmp(d.stage, st, 8) && d.room == room && d.set == set; });
}
void add_dead(const char* st, int room, uint16_t set) {
    if (is_dead(st, room, set) || g_dead.size() >= 1024) return;  // (bounded: another game's messages)
    Dead d{{}, (int8_t)room, set, net::now_ms()};
    memcpy(d.stage, st, 8);
    g_dead.push_back(d);
}
void gone(uint32_t k, int room, uint16_t set) {
    add_dead(stage(), room, set);
    net::send(-1, net::kMonGone, std::string(stage(), 8) + std::string((const char*)&k, 4) + (char)room + std::string((const char*)&set, 2));
}
void del(Cpu* c, uint32_t a) { guest_call(c, GC(0x025D57E4), {ld32(a + 4)}); }  // fopAcM_delete
// another player's kill, as its own death here: a blow as much as its health as it executes, its drop theirs
void die(uint32_t a, uint32_t k) { g_hits[k] = {kKill, 0}, g_dropless.insert(ld32(a + 4)); }
// a snapshot's records by key
std::unordered_map<uint32_t, Rec> records(const std::string& s) {
    std::unordered_map<uint32_t, Rec> out;
    if (s.size() < 8 || memcmp(s.data(), stage(), 8)) return out;
    for (size_t o = 8; o + kRecord <= s.size(); o += kRecord) {
        uint32_t k;
        Rec r;
        memcpy(&k, s.data() + o, 4), memcpy(r.p, s.data() + o + 4, 12), memcpy(&r.yaw, s.data() + o + 16, 2);
        memcpy(&r.shape, s.data() + o + 18, 2), r.hp = (int8_t)s[o + 20];
        if (std::isfinite(r.p[0]) && std::isfinite(r.p[1]) && std::isfinite(r.p[2])) out[k] = r;
    }
    return out;
}

}  // namespace

bool runs(uint32_t a) { return net::role() != net::Role::kOff && placed(a) && owner_of(a) == net::my_id(); }

bool takes(uint32_t a) { return net::role() != net::Role::kOff && placed(a) && owner_of(a) >= 0; }

bool killed(int set, int room) { return net::role() != net::Role::kOff && is_dead(stage(), room, (uint16_t)set); }

bool dropless(uint32_t a) { return net::role() != net::Role::kOff && g_dropless.count(ld32(a + 4)); }

void dead_from(const uint8_t* d, size_t n) {
    g_dead.clear();
    for (size_t o = 0; o + 11 <= n; o += 11) {
        uint16_t set;
        memcpy(&set, d + o + 9, 2);
        add_dead((const char*)d + o, (int8_t)d[o + 8], set);
    }
}

void before(uint32_t a) {
    g_tg_was = ld32(kCcS + ccs::mObjTgCount), g_hp_was = ld8(a + kHealth);
    const auto h = g_hits.find(key(a));
    const auto b = g_body.find(ld32(a + 4));
    const uint32_t link = ld32(kPlay + names::inf::kLink);
    if (h != g_hits.end() && h->second.steps == 0 && (b == g_body.end() || !link))
        trace("coop: monster %08X: another player's blow waits: %s", h->first, b == g_body.end() ? "its body collider is not known" : "no Link");
    if (h == g_hits.end() || b == g_body.end() || !link) return;
    // the blow: Link's body cylinder copied (its kind and his status: the hitter is him), as a sword's of that damage
    static const uint32_t blow = mem::host_alloc(0x140, 0x20);
    memcpy(mem::ptr(blow), mem::ptr(link + 0x765C), 0x140);
    const int dmg = std::min(h->second.dmg == kKill ? std::max<int>((int8_t)g_hp_was, 1) : h->second.dmg, 126);
    st32(blow + 0x10, 2), st8(blow + 0x14, (uint8_t)dmg), st8(blow + 0x6F, 0);  // AT_TYPE_SWORD, no special
    const uint32_t o = b->second;
    st32(o + obj::mObjTg_mRPrm, ld32(o + obj::mObjTg_mRPrm) | 1), st32(o + obj::tg_mRPrm, ld32(o + obj::tg_mRPrm) & ~1u), st32(o + obj::mObjTg_mHitObj, blow);
    st32(o + obj::tg_mApid, ld32(link + 4)), st32(o + obj::tg_mAc, link);
    const float dx = f32(a + kPos) - f32(link + kPos), dz = f32(a + kPos + 8) - f32(link + kPos + 8), d = std::hypot(dx, dz) + 0.01f;
    st32(o + obj::tg_mRVec, bits(dx / d * 10)), st32(o + obj::tg_mRVec + 4, 0), st32(o + obj::tg_mRVec + 8, bits(dz / d * 10));
    st32(o + obj::tg_mHitPos, ld32(a + kPos)), st32(o + obj::tg_mHitPos + 4, bits(f32(a + kPos + 4) + 50)), st32(o + obj::tg_mHitPos + 8, ld32(a + kPos + 8));
    if (const uint32_t stts = ld32(o + obj::mStts)) st8(stts + status::m_dmg, (uint8_t)dmg);
}

void after(Cpu* c, uint32_t a) {
    // its body: of the colliders taking hits it set as it executed, the one whose box is nearest its place
    const uint32_t n = std::min<uint32_t>(ld32(kCcS + ccs::mObjTgCount), ccs::kTgMax);
    // test aid: WWHD_COOP_TEST_CC=1 traces, for each monster's first 3 steps run here, the hit colliders it set and
    // what their status names as the actor (C18: why no body collider matched)
    if (static const bool cc = getenv("WWHD_COOP_TEST_CC"); cc) {
        static std::unordered_map<uint32_t, int> seen;
        if (seen[a]++ < 3) {
            std::string l;
            int mine = 0;
            for (uint32_t i = 0; i < n; i++) {
                const uint32_t o = ld32(kCcS + ccs::mpObjTg + 4 * i), stts = o ? ld32(o + obj::mStts) : 0;
                if (stts && ld32(stts + status::mp_actor) == a) mine++;
                if (i >= g_tg_was && l.size() < 200) {
                    char t[48];
                    snprintf(t, sizeof t, " %08X:%08X:%08X", o, stts, stts ? ld32(stts + status::mp_actor) : 0);
                    l += t;
                }
            }
            trace("coop: monster test cc: actor %08X proc %d tg %u->%u (all %u objs %u), %d name it:%s", a, (int16_t)ld16(a + kProcName),
                  g_tg_was, n, ld32(kCcS + ccs::mObjTgCount), ld32(kCcS + ccs::mObjCount), mine, l.c_str());
        }
    }
    float best = 1e9f;
    for (uint32_t i = g_tg_was; i < n; i++) {
        const uint32_t o = ld32(kCcS + ccs::mpObjTg + 4 * i), stts = ld32(o + obj::mStts);
        if (!stts || ld32(stts + status::mp_actor) != a) continue;
        float d = 0;
        for (int e = 0; e < 3; e++) d += std::pow((f32(o + obj::kShapeAab + 4 * e) + f32(o + obj::kShapeAab + 0xC + 4 * e)) / 2 - f32(a + kPos + 4 * e), 2.f);
        if (d < best) best = d, g_body[ld32(a + 4)] = o;
    }
    // another player's hit: taken once its health went down (a kill once it is gone); else again, at most 30 steps
    const auto h = g_hits.find(key(a));
    if (h == g_hits.end()) return;
    const int hp = (int8_t)ld8(a + kHealth);
    if (hp < (int8_t)g_hp_was && (h->second.dmg != kKill || hp <= 0)) {
        trace("coop: monster %08X took another player's %s here (health %d)", h->first, h->second.dmg == kKill ? "kill" : "hit", hp);
        g_hits.erase(h);
    } else if (!g_body.count(ld32(a + 4)) && ++h->second.steps > 10) {
        // (no body collider seen yet: one far from Link skips its collision; the health is taken directly so both
        // games agree; a kill is the game's own death where it can, else the monster is taken away)
        const int left = h->second.dmg == kKill ? 0 : std::max(hp - h->second.dmg, 0);
        st8(a + kHealth, (uint8_t)left);
        trace("coop: monster %08X: another player's %s taken without a blow (health %d)", h->first, h->second.dmg == kKill ? "kill" : "hit", left);
        if (left <= 0) del(c, a);
        g_hits.erase(h);
    } else if (++h->second.steps > 30) {
        if (h->second.dmg == kKill) del(c, a), trace("coop: monster %08X took no blow: deleted", h->first);
        g_hits.erase(h);
    }
}

void step(Cpu* c, uint32_t link) {
    if (net::role() == net::Role::kOff || !link) return g_copies.clear(), g_mine.clear();
    if (memcmp(g_stage, stage(), 8))
        memcpy(g_stage, stage(), 8), g_copies.clear(), g_mine.clear(), g_hits.clear(), g_body.clear(), g_dropless.clear(), steps_here = 0;
    const int me = net::my_id();
    // test aid: WWHD_COOP_TEST_MONSTER_HIT=<step>:<damage>[,<step>:<damage>...] (logic steps in this stage): that much
    // health taken from the first copy here of another player's monster, as a hit landing on it here would (its
    // owner takes it as this player's blow; as much as its health is a kill)
    if (static const char* e = getenv("WWHD_COOP_TEST_MONSTER_HIT"); e) {
        steps_here++;
        for (const char* p = e; *p;) {
            unsigned at = 0, dmg = 0;
            int used = 0;
            if (sscanf(p, "%u:%u%n", &at, &dmg, &used) != 2) break;
            if (at == steps_here) {
                bool hit = false;
                each_monster([&](uint32_t a) {
                    const auto cp = g_copies.find(key(a));
                    if (hit || owner_of(a) == me || owner_of(a) < 0 || cp == g_copies.end() || cp->second.hp == -128 || cp->second.dying) return;
                    hit = true;
                    st8(a + kHealth, (uint8_t)std::max<int>((int8_t)ld8(a + kHealth) - (int)dmg, 0));
                    trace("coop: monster test: %u health taken from %08X here (now %d)", dmg, key(a), (int8_t)ld8(a + kHealth));
                });
                if (!hit) trace("coop: monster test: no copy of another player's monster here");
            }
            p += used;
            if (*p == ',') p++;
        }
    }
    // ours out: key, place, heading, body heading, health; one killed (its health gone as it went) from everyone
    std::string out(stage(), 8);
    std::unordered_map<uint32_t, Mine> mine;
    std::unordered_map<int, std::unordered_map<uint32_t, Rec>> from_a, from_b;
    std::unordered_map<int, float> from_t;
    each_monster([&](uint32_t a) {
        const int o = owner_of(a);
        const uint32_t k = key(a);
        const int8_t room = (int8_t)ld8(a + kHomeRoom), hp = (int8_t)ld8(a + kHealth);
        const uint16_t set = ld16(a + kSetId);
        // killed before it was made here (the dead came after its room): gone
        if (const auto cp = g_copies.find(k); hp > 0 && !g_hits.count(k) && (cp == g_copies.end() || !cp->second.dying) && is_dead(stage(), room, set)) {
            g_copies[k].dying = true;
            del(c, a);
            trace("coop: monster %08X was killed before: deleted", k);
            return;
        }
        if (o == me) {
            mine[k] = {hp, room, set}, g_copies.erase(k);  // (ours now, if it was another's)
            if (out.size() + kRecord > 8 + kRecord * kMost) return;
            out.append((const char*)&k, 4);
            for (int e = 0; e < 3; e++) { const float v = f32(a + kPos + 4 * e); out.append((const char*)&v, 4); }
            const int16_t y = (int16_t)ld16(a + kAngleY), s = (int16_t)ld16(a + kShapeY);
            out.append((const char*)&y, 2), out.append((const char*)&s, 2), out += (char)hp;
            return;
        }
        if (o < 0) return;
        // another's: put where theirs is (the two snapshots around 100 ms ago), with its health
        if (!from_t.count(o)) {
            std::string sa, sb;
            float t = 0;
            if (sync::state_of(o, sync::kArea, sa, sb, t)) from_a[o] = records(sa), from_b[o] = records(sb);
            from_t[o] = t;
        }
        const auto rb = from_b[o].find(k);
        Copy& cp = g_copies[k];
        // (dying here: its own death plays; one that does not die by itself within 1.5 s, as one whose health was taken
        // without a blow, is taken away)
        if (cp.dying && ++cp.dying_steps == 45 && hp <= 0) del(c, a), trace("coop: monster %08X did not die by itself here: deleted", k);
        if (rb == from_b[o].end() || cp.dying) return;
        const auto ra = from_a[o].find(k);
        const Rec& x = ra == from_a[o].end() ? rb->second : ra->second;
        const Rec& y = rb->second;
        const float t = from_t[o];
        // the owner's health gone (killed there): its own death here, from the health it had
        if (y.hp <= 0 && cp.hp > 0) {
            cp.dying = true, die(a, k);
            trace("coop: monster %08X killed on %s's game: dies here", k, net::name(o).c_str());
            return;
        }
        // a hit here since its health was last put in: to the owner; killed here: gone from everyone, dying here as it is
        if (cp.hp != -128 && hp < cp.hp) {
            if (hp <= 0) {
                cp.dying = true;
                gone(k, room, set);
                trace("coop: monster %08X killed here (%s's)", k, net::name(o).c_str());
                return;
            }
            net::send(-1, net::kMonHit, std::string(stage(), 8) + std::string((const char*)&k, 4) + (char)(uint8_t)(cp.hp - hp));
        }
        for (int e = 0; e < 3; e++) {
            const uint32_t v = bits(x.p[e] + (y.p[e] - x.p[e]) * t);
            st32(a + kPos + 4 * e, v), st32(a + kOldPos + 4 * e, v);
        }
        st16(a + kAngleY, (uint16_t)(int16_t)(x.yaw + (int16_t)(y.yaw - x.yaw) * t));
        st16(a + kShapeY, (uint16_t)(int16_t)(x.shape + (int16_t)(y.shape - x.shape) * t));
        st8(a + kHealth, (uint8_t)y.hp), cp.hp = y.hp;
    });
    if (static int n = 0; ++n % 300 == 0) {
        trace("coop: placed monsters here: %zu run here (%zu colliders known, %u hit colliders this step), %zu another's (%zu put in), %zu killed in the players' stages", mine.size(),
              g_body.size(), ld32(kCcS + ccs::mObjTgCount), g_copies.size(), (size_t)std::count_if(g_copies.begin(), g_copies.end(), [](const auto& e) { return e.second.hp != -128; }), g_dead.size());
        each_monster([](uint32_t a) {
            trace("coop:   %#x room %d set %u at %.0f %.0f %.0f, health %d, run by %d", ld16(a + kProcName), (int8_t)ld8(a + kHomeRoom),
                  ld16(a + kSetId), f32(a + kPos), f32(a + kPos + 4), f32(a + kPos + 8), (int8_t)ld8(a + kHealth), owner_of(a));
        });
    }
    for (const auto& [k, m] : g_mine)
        if (m.hp <= 0 && !mine.count(k)) gone(k, m.room, m.set), trace("coop: monster %08X killed", k);
    g_mine = std::move(mine);
    net::put_state(sync::kArea, out);
    // (the host) the killed: kept while anyone is in their stage (10 s more, for loads), sent as they change and every 3 s
    if (sync::host()) {
        const uint32_t now = net::now_ms();
        for (Dead& d : g_dead) if (sync::anyone_in(d.stage)) d.seen = now;
        g_dead.erase(std::remove_if(g_dead.begin(), g_dead.end(), [&](const Dead& d) { return now - d.seen > 10000; }), g_dead.end());
        static uint32_t sent = 0;
        static size_t had = 0;
        if (g_dead.size() != had || now - sent > 3000) {
            std::string b;
            for (const Dead& d : g_dead) b.append(d.stage, 8), b += (char)d.room, b.append((const char*)&d.set, 2);
            net::send(-1, net::kDead, b), sent = now, had = g_dead.size();
        }
    }
}

void message(Cpu* c, net::Msg type, const std::string& b) {
    if (net::role() == net::Role::kOff || b.size() < 12) return;
    if (type == net::kMonGone && b.size() >= 15) {  // (in any stage: dead while anyone is there)
        uint16_t set;
        memcpy(&set, b.data() + 13, 2);
        add_dead(b.data(), (int8_t)b[12], set);
    }
    if (memcmp(b.data(), stage(), 8)) return;
    uint32_t k;
    memcpy(&k, b.data() + 8, 4);
    const uint32_t a = find(k);
    if (!a) return;
    const bool ours = owner_of(a) == net::my_id();
    if (type == net::kMonGone) {  // its own death here, the drop the killer's
        if (g_hits.count(k) && g_hits[k].dmg == kKill) return;  // (dying here already)
        if (!ours && g_copies[k].dying) return;                 // (killed here too: dying as it is)
        if ((int8_t)ld8(a + kHealth) <= 0) return del(c, a);    // (no health to take a blow with)
        if (!ours) g_copies[k].dying = true, g_mine.erase(k);
        die(a, k);
        trace("coop: monster %08X killed by another player: dies here", k);
    } else if (type == net::kMonHit && b.size() >= 13 && ours) {
        if (Hit& h = g_hits[k]; h.dmg != kKill) h.dmg = std::min(h.dmg + (uint8_t)b[12], (int)kKill - 1), h.steps = 0;
    }
}

}  // namespace mods::coop::monsters
