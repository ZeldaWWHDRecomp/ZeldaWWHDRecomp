// SPDX-License-Identifier: MPL-2.0
// Talks, answers, number entry, shops, the wind compass and menu pages by touch or mouse, and hold to go on
// (controls plan P5; the rules are touch_talk.h). This file runs them on the game's pad reads (VPADRead /
// KPADReadEx, after the touch controls' own rules), collects the fingers on the TV picture (Android
// TouchControls through nativeTalkFinger, the desktop mouse through tv_pointer.h), and reads what a shop
// shows (dShop_now_triggercheck's ShopItems_c and its items' actors, projected with the drawn camera).
//
// Options (settings.ini; tests: the WWHD_* variable named with each, which also keeps it from being saved):
//   touchTalk (WWHD_TOUCH_TALK)            talks, answers, shops... by touch (Android, default on) or by the
//                                          mouse on the TV picture (desktop, default off)
//   talkHoldFastForward (WWHD_TALK_HOLD_FF)  a held talk also fast forwards (default off)
//   padHoldA (WWHD_PAD_HOLD_A)             hold A on a controller to go on in talks (default off)
// Each applies at once.
// Tests on the Mac: WWHD_TEST_TALK_TOUCH=from-to:x:y[:x2:y2],... holds a finger on the TV picture from TV
// frame `from` to `to` at (x, y) in layout units (moving to (x2, y2) on the way), as Android's finger or the
// mouse would. WWHD_TALKTOUCH_LOG=1 logs fingers, presses, the shop's items, a system question's buttons and the hold ("[talktouch] ...").
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0),
// mods/minigames.cpp and turbo.cpp; reimplemented.
#include "touch_talk.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include "fast_forward.h"
#include "game_settings_page.h"
#include "guest_live.h"
#include "guest_mods.h"
#include "guest_addr.h"
#include "mods.h"
#include "play_context.h"
#include "../fixed_seed.h"
#include "../gfx/renderer.h"
#include "../input.h"
#include "../interp.h"
#include "../overlay/hostui.h"
#include "../runtime.h"
#include "../tv_pointer.h"

static_assert(mods::talktouch::kBtnA == input::kA && mods::talktouch::kBtnB == input::kB &&
              mods::talktouch::kBtnL == input::kL && mods::talktouch::kBtnR == input::kR);

extern "C" void f_025BB698_orig(Cpu* c);  // dShop_now_triggercheck

namespace mods {
namespace {
using namespace playctx;
using talktouch::Finger;

bool log_on() {
    static const bool on = [] {
        const char* e = getenv("WWHD_TALKTOUCH_LOG");
        return e && *e && *e != '0';
    }();
    return on;
}

struct Option {
    const char* env;
    const char* key;
    int def;
    std::atomic<int> value{-1};
    int get() {
        int v = value.load(std::memory_order_relaxed);
        if (v >= 0) return v;
        v = def;
        std::string s;
        if (const char* e = getenv(env); e && *e) v = atoi(e);
        else if (hostui::get(key, s) && !s.empty()) v = atoi(s.c_str());
        v = v ? 1 : 0;
        value.store(v, std::memory_order_relaxed);
        return v;
    }
    void set(bool on) {
        value.store(on ? 1 : 0, std::memory_order_relaxed);
        if (!getenv(env)) hostui::set(key, on ? "1" : "0");
    }
};
#ifdef __ANDROID__
constexpr int kTouchDefault = 1;  // a finger on the picture: there is no other pointer
#else
constexpr int kTouchDefault = 0;  // the mouse: a click to focus the window must not answer a talk
#endif
Option g_touch{"WWHD_TOUCH_TALK", "touchTalk", kTouchDefault};
Option g_hold_ff{"WWHD_TALK_HOLD_FF", "talkHoldFastForward", 0};
Option g_pad_hold{"WWHD_PAD_HOLD_A", "padHoldA", 0};

talktouch::Options options() {
    talktouch::Options o;
    o.touch = g_touch.get() != 0;
    o.hold_ff = g_hold_ff.get() != 0;
    o.pad_hold = g_pad_hold.get() != 0;
    return o;
}

// ---- fingers (any thread) -> the next pad read (game thread)
std::mutex g_finger_mu;
std::vector<Finger> g_fingers;
std::atomic<bool> g_claims{false};

void push_finger(Finger::Kind kind, float x, float y) {
    std::lock_guard<std::mutex> lk(g_finger_mu);
    if (g_fingers.size() >= 64) {  // nobody reads (paused): keep the latest only
        g_fingers.erase(g_fingers.begin(), g_fingers.begin() + 32);
    }
    // consecutive moves: only the latest counts
    if (kind == Finger::kMove && !g_fingers.empty() && g_fingers.back().kind == Finger::kMove) g_fingers.back() = {kind, x, y};
    else g_fingers.push_back({kind, x, y});
}

// the desktop mouse on the TV picture (tv_pointer.h) while the talk claims it; 0..1 of the picture -> layout units
bool g_pointer_down = false;
void pointer_fingers(bool claim) {
#ifdef __ANDROID__
    (void)claim;  // (Android: TouchControls sends the fingers)
#else
    tv_pointer::set_wanted(tv_pointer::kOwnerTalk, claim);
    tv_pointer::State s = tv_pointer::read();
    if (!tv_pointer::wanted(tv_pointer::kOwnerTalk)) s.down = false;
    float px, py;
    tv_pointer::to_page(s, px, py);
    const float x = px - 640.0f, y = 360.0f - py;
    if (s.down) push_finger(g_pointer_down ? Finger::kMove : Finger::kDown, x, y);
    else if (g_pointer_down) push_finger(Finger::kUp, x, y);
    g_pointer_down = s.down;
#endif
}

// test aid: WWHD_TEST_TALK_TOUCH=from-to:x:y[:x2:y2],... (TV frames, layout units)
struct Hold {
    uint64_t from, to;
    float x, y, x2, y2;
};
void test_fingers() {
    static const std::vector<Hold> holds = [] {
        std::vector<Hold> v;
        const char* e = getenv("WWHD_TEST_TALK_TOUCH");
        while (e && *e) {
            unsigned long long a, b;
            float x, y, x2, y2;
            int n = 0;
            if (sscanf(e, "%llu-%llu:%f:%f:%f:%f%n", &a, &b, &x, &y, &x2, &y2, &n) == 6) v.push_back({a, b, x, y, x2, y2});
            else if (sscanf(e, "%llu-%llu:%f:%f%n", &a, &b, &x, &y, &n) == 4) v.push_back({a, b, x, y, x, y});
            else break;
            e += n;
            if (*e != ',') break;
            e++;
        }
        return v;
    }();
    if (holds.empty()) return;
    static int active = -1;
    const uint64_t f = fixed_seed::script_frame(render::frame_count());
    int now = -1;
    for (size_t i = 0; i < holds.size(); i++)
        if (f >= holds[i].from && f < holds[i].to) now = int(i);
    auto at = [&](int i, float& x, float& y) {
        const Hold& h = holds[size_t(i)];
        const float t = h.to > h.from + 1 ? std::clamp(float(f - h.from) / float(h.to - 1 - h.from), 0.0f, 1.0f) : 0.0f;
        x = h.x + (h.x2 - h.x) * t, y = h.y + (h.y2 - h.y) * t;
    };
    float x, y;
    if (active >= 0 && active != now) {
        at(active, x, y);
        push_finger(Finger::kUp, x, y);
        if (log_on()) LOG("[talktouch] test finger up at %.0f %.0f (frame %llu)", x, y, (unsigned long long)f);
        active = -1;
    }
    if (now >= 0) {
        at(now, x, y);
        push_finger(active == now ? Finger::kMove : Finger::kDown, x, y);
        if (active != now && log_on()) LOG("[talktouch] test finger down at %.0f %.0f (frame %llu)", x, y, (unsigned long long)f);
        active = now;
    }
}

// ---- pinches for code mods (guest_live.h): the desktop's mouse wheel over a mod's claim (tv_pointer::take_wheel), each
// notch 15 % more or less spread, over 0.3 s without a notch; the test aid WWHD_TEST_PINCH=from-to:x:y:s0:s1,... (TV
// frames, layout units, the spread going from s0 to s1); Android's two fingers (touch_talk_mod_pinch)
void wheel_pinch() {
#ifndef __ANDROID__
    static float scale = 1.0f;
    static int quiet = -1;  // pad reads without a notch (-1: no pinch)
    float notches;
    tv_pointer::State at;
    if (tv_pointer::take_wheel(notches, at)) {
        float px, py;
        tv_pointer::to_page(at, px, py);
        scale *= std::pow(1.15f, std::clamp(notches, -8.0f, 8.0f));
        guestmods::touch_pinch(guestlive::kPinch, scale, px - 640.0f, 360.0f - py);
        quiet = 0;
    } else if (quiet >= 0 && ++quiet >= 9) {
        guestmods::touch_pinch(guestlive::kPinchEnd, scale, 0, 0);
        scale = 1.0f, quiet = -1;
    }
#endif
}
void test_pinches() {
    struct Pinch {
        uint64_t from, to;
        float x, y, s0, s1;
    };
    static const std::vector<Pinch> pinches = [] {
        std::vector<Pinch> v;
        const char* e = getenv("WWHD_TEST_PINCH");
        unsigned long long a, b;
        float x, y, s0, s1;
        int n = 0;
        while (e && sscanf(e, "%llu-%llu:%f:%f:%f:%f%n", &a, &b, &x, &y, &s0, &s1, &n) == 6) {
            v.push_back({a, b, x, y, s0, s1});
            e += n;
            if (*e != ',') break;
            e++;
        }
        return v;
    }();
    if (pinches.empty()) return;
    static int active = -1;
    const uint64_t f = fixed_seed::script_frame(render::frame_count());
    int now = -1;
    for (size_t i = 0; i < pinches.size(); i++)
        if (f >= pinches[i].from && f < pinches[i].to) now = int(i);
    if (active >= 0 && active != now) guestmods::touch_pinch(guestlive::kPinchEnd, 1, 0, 0), active = -1;
    if (now < 0) return;
    const Pinch& p = pinches[size_t(now)];
    const float t = p.to > p.from + 1 ? std::clamp(float(f - p.from) / float(p.to - 1 - p.from), 0.0f, 1.0f) : 0.0f;
    const int owner = guestmods::touch_pinch(guestlive::kPinch, p.s0 + (p.s1 - p.s0) * t, p.x, p.y);
    if (active != now && log_on()) LOG("[talktouch] test pinch at %.0f %.0f: mod %d (frame %llu)", p.x, p.y, owner, (unsigned long long)f);
    active = now;
}

// ---- the shop (game thread): dShop_now_triggercheck notes its ShopItems_c each step the item choice runs
std::atomic<uint32_t> g_shop_items{0};
std::atomic<uint64_t> g_shop_step{0};
constexpr uint32_t kShopSelected = 0x00, kShopIds = 0x04;  // ShopItems_c: s16 mSelectedItemIdx, u32 ids[8]
constexpr uint32_t kActorProcId = 0x04, kActorPos = 0x314;  // fopAc_ac_c
constexpr float kItemLift = 25;  // world units from a shop item's foot to its middle (Beedle's shop, measured)
bool guest_ptr(uint32_t p) { return p >= 0x10000000 && p < 0x50000000 && !(p & 3); }

struct V3 {
    float x, y, z;
};
V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 norm(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 0 ? V3{a.x / l, a.y / l, a.z / l} : a;
}
V3 ldv(uint32_t a) { return {float(ldf32(a)), float(ldf32(a + 4)), float(ldf32(a + 8))}; }

// the drawn camera as a projection to layout units (the picture is 720 units high: fovy spans them)
struct View {
    V3 eye, f, r, u;
    float k = 0;
    bool ok = false;
};
View camera_view() {
    View v;
    const uint32_t cam = interp::last_camera();
    if (!guest_ptr(cam)) return v;
    const V3 eye = ldv(cam + 0xDC), center = ldv(cam + 0xE8), up = ldv(cam + 0xF4);
    const float fovy = float(ldf32(cam + 0xD4));
    if (!(fovy > 1 && fovy < 170)) return v;
    v.eye = eye;
    v.f = norm(sub(center, eye));
    v.r = norm(cross(v.f, up));
    v.u = cross(v.r, v.f);
    v.k = 360.0f / std::tan(fovy * 3.14159265f / 360.0f);
    v.ok = std::isfinite(v.k) && dot(v.f, v.f) > 0.5f && dot(v.r, v.r) > 0.5f;
    return v;
}
bool project(const View& v, V3 p, float& x, float& y) {
    if (!v.ok) return false;
    const V3 d = sub(p, v.eye);
    const float z = dot(d, v.f);
    if (z <= 1) return false;
    x = dot(d, v.r) / z * v.k;
    y = dot(d, v.u) / z * v.k;
    return std::isfinite(x) && std::isfinite(y);
}

talktouch::Shop read_shop() {
    talktouch::Shop s;
    const uint32_t items = g_shop_items.load(std::memory_order_relaxed);
    if (!guest_ptr(items) || g_shop_step.load(std::memory_order_relaxed) + 3 < step()) return s;
    s.open = true;
    s.selected = int16_t(ld16(items + kShopSelected));
    uint32_t ids[8];
    for (int i = 0; i < 8; i++) ids[i] = ld32(items + kShopIds + 4 * uint32_t(i));
    const View v = camera_view();
    const uint32_t queue = GD(0x101F3328);  // g_fopAcTg_Queue (story_notebook.cpp scan_villagers)
    uint32_t n = ld32(queue);
    for (int k = 0; n && k < 2048; k++, n = ld32(n + 8)) {
        if (!guest_ptr(n)) break;
        const uint32_t a = ld32(n + 0xC);
        if (!guest_ptr(a)) continue;
        const uint32_t id = ld32(a + kActorProcId);
        for (int i = 0; i < 8; i++)
            if (ids[i] == id && id != 0xFFFFFFFFu && !s.seen[i]) {
                V3 p = ldv(a + kActorPos);
                p.y += kItemLift;  // (the actor stands at the item's foot: aim at its middle)
                s.seen[i] = project(v, p, s.x[i], s.y[i]);
            }
    }
    return s;
}

// ---- state (game thread)
talktouch::Machine g_machine;
bool g_ff = false;

void log_shop(const talktouch::Shop& s) {
    static std::string last;
    char buf[256];
    int k = snprintf(buf, sizeof buf, "selected %d:", s.selected);
    for (int i = 0; i < 8 && k < int(sizeof buf); i++)
        if (s.seen[i]) k += snprintf(buf + k, sizeof buf - k, " [%d] %.0f,%.0f", i, s.x[i], s.y[i]);
    if (last == buf) return;
    last = buf;
    LOG("[talktouch] shop %s (step %llu)", buf, (unsigned long long)step());
}
}  // namespace

// ---- options ----
bool touch_talk_on() { return g_touch.get() != 0; }
void set_touch_talk_on(bool on) { g_touch.set(on); }
bool talk_hold_ff_on() { return g_hold_ff.get() != 0; }
void set_talk_hold_ff_on(bool on) { g_hold_ff.set(on); }
bool pad_hold_a_on() { return g_pad_hold.get() != 0; }
void set_pad_hold_a_on(bool on) { g_pad_hold.set(on); }

void touch_talk_finger(int kind, float x, float y) {
    if (kind < Finger::kDown || kind > Finger::kCancel || !std::isfinite(x) || !std::isfinite(y)) return;
    push_finger(Finger::Kind(kind), x, y);
}
// Android (TouchControls): a second finger next to a code mod's finger; phase 0 begin, 1 move, 2 end; scale = the spread
// now / at the start; (x, y) the fingers' middle in layout units. True when a mod has the pinch.
bool touch_talk_mod_pinch(int phase, float scale, float x, float y) {
    if (!std::isfinite(scale) || !std::isfinite(x) || !std::isfinite(y)) return false;
    return guestmods::touch_pinch(phase >= 2 ? guestlive::kPinchEnd : guestlive::kPinch, scale, x, y) >= 0;
}
bool touch_talk_claims() { return g_claims.load(std::memory_order_relaxed); }
// a finger landing at (x, y) (layout units) is the talk's or a code mod's (its claim, guest_live.h)
bool touch_talk_claims_at(float x, float y) { return g_claims.load(std::memory_order_relaxed) || guestmods::touch_claimed_at(x, y); }

// ---- the pad reads (VPADRead / KPADReadEx, after the touch controls' rules)
void touch_talk_pad(input::PadState& p, bool repeat) {
    const talktouch::Options o = options();
    if (repeat) {  // interpolation's repeated read: the logic pass's decision again (the pushed stick stays)
        const talktouch::Out& out = g_machine.held();
        if (out.stick) p.lx = out.lx, p.ly = out.ly;
        return;
    }
    static bool wanted = false;
    if (!wanted && (o.touch || o.pad_hold)) wanted = true, play_context_want(true);
    else if (wanted && !o.touch && !o.pad_hold) wanted = false, play_context_want(false);
    const PlayContext c = play_context();
    const bool menu_on_tv = input::pro_controller() && !game_settings_page::page_open();
    const bool talk_claim = talktouch::claims(c, o, menu_on_tv);
    g_claims.store(talk_claim, std::memory_order_relaxed);
    const bool claim = talk_claim || guestmods::touch_claims();  // (code mods' claims: their fingers first)
    pointer_fingers(claim);
    test_fingers();
    wheel_pinch();
    test_pinches();
    std::vector<Finger> fingers;
    {
        std::lock_guard<std::mutex> lk(g_finger_mu);
        fingers.swap(g_fingers);
    }
    // a code mod's claimed fingers are the mod's (guest_mods.cpp delivers them in this read)
    fingers.erase(std::remove_if(fingers.begin(), fingers.end(), [](const Finger& f) { return guestmods::touch_route(uint32_t(f.kind), f.x, f.y) >= 0; }),
                  fingers.end());
    // fingers that came while nothing was claimed are nobody's (the touch controls had them)
    if (!talk_claim) {
        bool cancel = false;
        for (const Finger& f : fingers) cancel = cancel || f.kind == Finger::kUp || f.kind == Finger::kCancel;
        fingers.clear();
        if (cancel || !o.touch) fingers.push_back({Finger::kCancel, 0, 0});
    }
    talktouch::In in;
    in.step = step();
    in.fingers = fingers.data();
    in.nfingers = int(fingers.size());
    in.pad_a = (p.buttons & input::kA) != 0;
    in.pad_b = (p.buttons & input::kB) != 0;
    in.menu_on_tv = menu_on_tv;
    in.choice_x = c.choice_x;
    in.wind_x = c.wind_x, in.wind_y = c.wind_y;
    if (o.touch && c.has(kShop)) in.shop = read_shop();
    const bool was_busy = g_machine.busy(), was_hold = g_machine.holding() || g_machine.pad_holding();
    const uint32_t holds = g_machine.holds();
    const talktouch::Out out = g_machine.step(c, in, o);
    const uint32_t before = p.buttons;
    p.buttons = (p.buttons & ~out.release) | out.press;
    if (out.stick) p.lx = out.lx, p.ly = out.ly;
    if (out.fast_forward != g_ff) {
        g_ff = out.fast_forward;
        fast_forward_hold(g_ff);
    }
    if (log_on()) {
        for (const Finger& f : fingers)
            if (f.kind != Finger::kMove) LOG("[talktouch] finger %s at %.0f %.0f (step %llu)", f.kind == Finger::kDown ? "down" : f.kind == Finger::kUp ? "up" : "cancel", f.x, f.y, (unsigned long long)in.step);
        if (in.shop.open) log_shop(in.shop);
        static std::string last_choice;
        char cb[96] = "";
        if (c.has(kChoice)) snprintf(cb, sizeof cb, "%u lines, box at %d,%d, cursor at y %d", c.choice_count, c.choice_x, c.choice_y, c.cursor_y);
        if (last_choice != cb) last_choice = cb, LOG("[talktouch] choice: %s (step %llu)", *cb ? cb : "none", (unsigned long long)in.step);
        // a system question's buttons (drawn ones: centre, width scale per mille) and the mode, when they change
        static std::string last_question;
        char qb[160] = "";
        if (c.question_shown) {
            int n = snprintf(qb, sizeof qb, "mode %d%s", int(talktouch::mode_of(c, menu_on_tv)), talk_claim ? " claimed" : "");
            for (int i = 0; i < 3 && n > 0 && n < int(sizeof qb); i++)
                if (c.question_shown >> i & 1)
                    n += snprintf(qb + n, sizeof qb - size_t(n), ", %c at %d,%d scale %d", "LRC"[i], c.question_x[i], c.question_y[i], c.question_scale[i]);
        }
        if (last_question != qb) last_question = qb, LOG("[talktouch] question: %s (step %llu)", *qb ? qb : "none", (unsigned long long)in.step);
        static uint32_t last_buttons = 0;
        static bool last_stick = false;
        static float last_lx = 0, last_ly = 0;
        if (p.buttons != before || out.stick || last_buttons != 0 || last_stick) {
            const uint32_t changed = (p.buttons ^ before);
            if (changed != last_buttons || out.stick != last_stick || (out.stick && (out.lx != last_lx || out.ly != last_ly)))
                LOG("[talktouch] pad %04X -> game %04X%s (step %llu)", before, p.buttons,
                    out.stick ? (out.lx > 0.5f ? " stick right" : out.lx < -0.5f ? " stick left" : out.ly > 0.5f ? " stick up" : out.ly < -0.5f ? " stick down" : " stick centre") : "",
                    (unsigned long long)in.step);
            last_buttons = changed, last_stick = out.stick, last_lx = out.lx, last_ly = out.ly;
        }
        const bool hold = g_machine.holding() || g_machine.pad_holding();
        if (hold != was_hold) LOG("[talktouch] hold %s (step %llu)", hold ? "on" : "off", (unsigned long long)in.step);
        if (g_machine.holds() != holds) LOG("[talktouch] the hold goes on (step %llu)", (unsigned long long)in.step);
        if (was_busy != g_machine.busy() && !g_machine.busy()) LOG("[talktouch] queue done (step %llu)", (unsigned long long)in.step);
    }
}

void touch_talk_reset() {
    g_machine.reset();
    {
        std::lock_guard<std::mutex> lk(g_finger_mu);
        g_fingers.clear();
    }
    if (g_ff) g_ff = false, fast_forward_hold(false);
}
}  // namespace mods

// dShop_now_triggercheck(stick, ShopItems_c*, msg*, cb, arg): the shop's item choice runs this step
extern "C" void hook_025BB698(Cpu* c) {
    mods::g_shop_items.store(c->r[4], std::memory_order_relaxed);
    mods::g_shop_step.store(mods::step(), std::memory_order_relaxed);
    f_025BB698_orig(c);
}
