// SPDX-License-Identifier: MPL-2.0
// Online co-op: the party's time of day and sea weather follow the leader (coop_clock.h).
//
// The time of day and the day are the save's dSv_player_status_b_c (mTime, 0..360 a day, and mDate). The game moves the
// time on only where nothing holds it (d_kankyo's dKy_setLight... time step: not an endless story night, not an event,
// not a place of fixed time, a room that lets time pass; a few places only up to dusk or dawn). That rule is not copied:
// a game whose time moved on by itself lately "lets time pass here". The leader sends its clock once a second while its
// time passes; a member whose own time passes moves its clock toward the leader's (the sun runs to it in a few seconds,
// never a jump), and leaves it alone otherwise (a dungeon, a house, a cutscene, a story night: the game's own).
//
// The sea's weather is the game's dice weather (g_env_light's mDiceWeather*: a pattern of modes, sunny to thunder, each
// until a time of day; dKy_... runs it only on the open sea, outside the story's storms, while time passes). The leader's
// pattern, step and times go with its clock; a member at sea whose time passes takes them, and its own weather code goes
// on from there: rain comes and goes as the game ramps it, from the same step as the leader's.
//
// A member whose clock jumps by itself (the Song of Passing, a story moment ending in a new time) asks the leader for
// it (kClockJump): the leader takes it as its own, and every member follows. Players outside the party never get any of
// it (party messages only).
#include "coop_clock.h"
#include "coop_names.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "coop_net.h"
#include "guest_addr.h"
#include "mods.h"
#include "runtime.h"

namespace mods::coop::clock {
namespace {

const uint32_t kSavePtr = GD(0x101F84DC);
constexpr uint32_t kSave = 0x20, kStatusB = 0x18, kTime = 0x0C, kDate = 0x10;  // dSv_player_c, dSv_player_status_b_c
const uint32_t kEnv = GD(0x10475A68);                                           // g_env_light (wwhd_src d_kankyo.h)
constexpr uint32_t kDiceChangeTime = 0xFC0, kDiceTime = 0xFC4, kDiceCounter = 0x1068, kDiceMode = 0x1093, kDiceState = 0x1094,
                   kDicePattern = 0x1095;
const uint32_t kStage = GD(0x1046F0B0) + names::inf::kCurStage;

uint32_t status() { const uint32_t s = ld32(kSavePtr); return s ? s + kSave + kStatusB : 0; }
float time_now() { return (float)ldf32(status() + kTime); }
void put_time(float t) { uint32_t u; memcpy(&u, &t, 4); st32(status() + kTime, u); }
void put_f(uint32_t a, float v) { uint32_t u; memcpy(&u, &v, 4); st32(a, u); }
bool at_sea() { return !strncmp((const char*)mem::ptr(kStage), "sea", 4); }
float wrap(float d) { while (d > 180) d -= 360; while (d <= -180) d += 360; return d; }

#pragma pack(push, 1)
struct Clock {  // kClock (leader -> party), kClockJump (member -> leader: time and date only)
    float time;
    uint16_t date;
    uint8_t sea;  // the dice weather below is the leader's at sea
    uint8_t mode, state, pattern;
    uint32_t counter;
    float change_time, dice_time;
};
#pragma pack(pop)

struct Target { Clock c{}; uint32_t at = 0; };
Target g_leader;          // the leader's newest clock (a member)
uint32_t g_jump_ms = 0;   // this game's own jump asked for: not pulled back for a while
float g_last = -1;        // the time the step before
float g_wrote = -1;       // the time this module wrote (not a jump of the game's)
uint32_t g_passing = 0;   // steps since the time last moved on by itself (0: never)
float g_rate = 0.f;       // how far it moves a step, lately

Clock mine() {
    Clock c{};
    c.time = time_now(), c.date = ld16(status() + kDate), c.sea = at_sea();
    if (c.sea) {
        c.mode = ld8(kEnv + kDiceMode), c.state = ld8(kEnv + kDiceState), c.pattern = ld8(kEnv + kDicePattern);
        c.counter = ld32(kEnv + kDiceCounter), c.change_time = (float)ldf32(kEnv + kDiceChangeTime), c.dice_time = (float)ldf32(kEnv + kDiceTime);
    }
    return c;
}
std::string body(const Clock& c) { return std::string((const char*)&c, sizeof c); }

}  // namespace

void message(int from, uint8_t type, const uint8_t* d, size_t n) {
    if (n < sizeof(Clock)) return;
    Clock c;
    memcpy(&c, d, sizeof c);
    if (!std::isfinite(c.time) || c.time < 0 || c.time >= 360 || !std::isfinite(c.change_time) || !std::isfinite(c.dice_time)) return;
    if (type == net::kClock && from == 0 && net::my_id() != 0) g_leader = {c, net::now_ms()};
    else if (type == net::kClockJump && net::my_id() == 0 && status()) {
        if (g_passing == 0 || g_passing > 120) return mods::trace("coop: clock: player %d's new time (%.1f) not taken: time stands still here", from, c.time);
        put_time(c.time), g_wrote = c.time, st16(status() + kDate, c.date);
        mods::trace("coop: clock: player %d's new time taken by the party: %.1f, day %u", from, c.time, c.date);
        net::send(-1, net::kClock, body(mine()));
    }
}

void step(bool settled) {
    if (net::role() == net::Role::kOff || !status()) return (void)(g_leader = {}, g_last = -1, g_passing = 0);
    const uint32_t now = net::now_ms();
    const float t = time_now();
    // does the game let time pass here? (it moved on by itself a little: not a jump, not this module's write)
    const float moved = g_last < 0 ? 0 : wrap(t - g_last);
    const bool own = g_wrote >= 0 && std::fabs(wrap(t - g_wrote)) < 0.001f;
    if (g_last >= 0 && !own && moved > 0 && moved < 1) g_passing = 1, g_rate = g_rate * 0.95f + moved * 0.05f;
    else if (g_passing) g_passing++;
    const bool passing = g_passing && g_passing <= 120;  // (moved within the last 2 s or so)
    // a jump of its own (the Song of Passing...): a member asks the leader for it
    if (g_last >= 0 && !own && std::fabs(moved) >= 20 && settled) {
        if (net::my_id() != 0 && net::member(0)) {
            Clock c = mine();
            net::send(0, net::kClockJump, body(c));
            g_jump_ms = now ? now : 1;
            mods::trace("coop: clock: this game's time jumped to %.1f: asked of the leader", t);
        } else if (net::my_id() == 0) {
            net::send(-1, net::kClock, body(mine()));  // (the leader's own: everyone follows)
        }
    }
    g_last = t, g_wrote = -1;
    if (static uint32_t n = 0; ++n % 300 == 0)
        mods::trace("coop: clock: %.2f, day %u, %s (%.4f a step)%s", t, ld16(status() + kDate), passing ? "time passes" : "time stands still", g_rate,
                    at_sea() ? ", at sea" : "");
    // test aids: WWHD_COOP_TEST_CLOCK=<s>:<degrees> moves this game's time on that much (as the Song of Passing moves it
    // 180), WWHD_COOP_TEST_WEATHER=<s> starts the sea's next dice weather now (the game's own pattern), both that long
    // after the first settled step
    if (static uint32_t first = 0; settled && (first || (first = now ? now : 1))) {
        static bool clock_done = false, weather_done = false;
        float secs = 0, deg = 0;
        if (const char* e = getenv("WWHD_COOP_TEST_CLOCK"); e && !clock_done && sscanf(e, "%f:%f", &secs, &deg) == 2 && now - first >= secs * 1000) {
            clock_done = true;
            const float n = std::fmod(t + deg + 360.f, 360.f);
            put_time(n);  // (the game's own jump: seen as one at the next step)
            mods::trace("coop: clock: test: this game's time %.1f -> %.1f", t, n);
        }
        if (const char* e = getenv("WWHD_COOP_TEST_WEATHER"); e && !weather_done && at_sea() && now - first >= atof(e) * 1000) {
            weather_done = true;
            put_f(kEnv + kDiceChangeTime, t), st8(kEnv + kDiceState, 1);  // (DICE_STATE_INIT: a pattern drawn now)
            mods::trace("coop: clock: test: the sea's weather changes now");
        }
    }
    // the leader: its clock out once a second, while its time passes
    if (net::my_id() == 0) {
        static uint32_t sent = 0;
        if (passing && settled && now - sent >= 1000) {
            sent = now;
            const Clock c = mine();
            net::send(-1, net::kClock, body(c));
            if (static uint32_t n = 0; n++ % 10 == 0)
                mods::trace("coop: clock: the leader's %.1f, day %u%s (weather mode %u, step %u of pattern %u)", c.time, c.date, c.sea ? ", at sea" : "",
                            c.mode, c.counter, c.pattern);
        }
        return;
    }
    // a member: toward the leader's clock while its own time passes and the leader's came lately
    if (!net::member(0) || !g_leader.at || now - g_leader.at > 3000 || !passing || !settled) return;
    if (g_jump_ms && now - g_jump_ms < 5000) return;  // (its own new time asked of the leader: not pulled back meanwhile)
    const Clock& L = g_leader.c;
    const float want = std::fmod(L.time + g_rate * (now - g_leader.at) / (1000.f / 30.f) + 360.f, 360.f);  // (on since it came)
    const float diff = wrap(want - t);
    if (std::fabs(diff) > 0.05f) {
        // a few seconds to it, as the sky can show (never a jump): a share of the way each step, at least a little
        const float d = std::copysign(std::min(std::fabs(diff), std::max(0.05f, std::fabs(diff) / 45.f)), diff);
        float n = t + d;
        uint16_t date = ld16(status() + kDate);
        if (n >= 360) n -= 360, date++;
        else if (n < 0) n += 360, date--;
        put_time(n), g_wrote = n, g_last = n;
        st16(status() + kDate, date);
        if (static uint32_t told = 0; std::fabs(diff) > 2 && now - told > 2000)
            told = now, mods::trace("coop: clock: toward the leader's time: %.1f -> %.1f (%.1f to go)", t, want, diff);
    } else if (ld16(status() + kDate) != L.date) {
        st16(status() + kDate, L.date);  // (the same time: the leader's day too)
    }
    // the sea's weather: the leader's dice mode, when both are at sea. This game's own dice is held (its state never
    // INIT: no pattern of its own drawn; its change times kept ahead of its clock), so only the leader's changes come; its
    // weather code goes on ramping the rain and thunder to the mode as the game does
    if (L.sea && at_sea()) {
        const uint8_t mode = L.mode == 0xFF ? 0 : L.mode, state = L.state == 0 ? 0 : 2;  // (RESET or EXEC)
        if (ld8(kEnv + kDiceMode) != mode || ld8(kEnv + kDiceState) != state) {
            if (ld8(kEnv + kDiceMode) != mode)
                mods::trace("coop: clock: the leader's weather: mode %u -> %u", ld8(kEnv + kDiceMode), mode);
            st8(kEnv + kDiceMode, mode), st8(kEnv + kDiceState, state);
        }
        const float ahead = std::fmod(time_now() + 90.f, 360.f);
        put_f(kEnv + kDiceChangeTime, ahead), put_f(kEnv + kDiceTime, ahead);
    }
}

}  // namespace mods::coop::clock
