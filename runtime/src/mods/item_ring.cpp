// SPDX-License-Identifier: MPL-2.0
// The item ring's runtime (item_ring.h has the rules): the X / Y / R gate in the game's pad reads
// (VPADRead, or KPADReadEx in Pro Controller mode), the equip and the pace at the frame boundary, the
// ring drawn through the host HUD (guest_hud.h: the TV picture and the GamePad screen alike, on every
// platform), the options (Controls > Item ring) and the Android finger routing (TouchControls.java).
//
// Options (settings: itemRing, itemRingPace, itemButtonNames; tests: WWHD_ITEM_RING=0|1|2,
// WWHD_ITEM_RING_PACE=0|1|2, WWHD_ITEM_NAMES=0|1|2): Off / touch only (default) / touch and controllers;
// while open the game runs normally / in slow motion (default) / pauses; the buttons' names X Y R / A1 A2
// A3 / Auto. Changes apply at once.
// WWHD_ITEM_RING_LOG=1 logs the ring's openings, picks and what they did ("[ring] ...").
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0),
// ItemRing.java and touch_state.cpp; reimplemented.
#include "item_ring.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>

#include "fast_forward.h"
#include "guest_addr.h"
#include "guest_hud.h"
#include "item_ring_icons.h"
#include "mods.h"
#include "play_context.h"
#include "runtime.h"
#include "../input.h"
#include "../overlay/hostui.h"
#include "../overlay/overlay.h"
#ifdef __ANDROID__
#include <jni.h>
#include "../platform/phone_rumble.h"
#endif

namespace mods {
namespace {
using namespace item_ring;
namespace hud = guestmods::hud;
constexpr const char* kOwner = "item-ring";

bool log_on() {
    static const bool on = [] {
        const char* e = getenv("WWHD_ITEM_RING_LOG");
        return e && *e && *e != '0';
    }();
    return on;
}

// ---- options ----
struct Option {
    const char* env;
    const char* key;
    int def, max;
    std::atomic<int> value{-1};
    int get() {
        int v = value.load(std::memory_order_relaxed);
        if (v >= 0) return v;
        v = def;
        std::string s;
        if (const char* e = getenv(env); e && *e) v = atoi(e);
        else if (hostui::get(key, s) && !s.empty()) v = atoi(s.c_str());
        v = std::clamp(v, 0, max);
        value.store(v, std::memory_order_relaxed);
        return v;
    }
    void set(int v) {
        v = std::clamp(v, 0, max);
        value.store(v, std::memory_order_relaxed);
        if (!getenv(env)) hostui::set(key, std::to_string(v));
    }
};
Option g_mode{"WWHD_ITEM_RING", "itemRing", kTouchOnly, kAll};
Option g_pace{"WWHD_ITEM_RING_PACE", "itemRingPace", kPaceSlow, kPacePaused};
Option g_names{"WWHD_ITEM_NAMES", "itemButtonNames", kNamesAuto, kNamesAuto};

// ---- the game ----
const uint32_t kGameInfo = GD(0x1046F0B0);
const uint32_t kSaveInfoPtr = GD(0x101F84DC);        // *this + 0x20: dSv_player_c
const uint32_t kPlaySelect = GD(0x1046F0B0) + 0x5BBB;  // play mSelectItem (play_context.h kSelectItem)
const uint32_t kNextPauseTimer = GD(0x101EACB7);     // dScnPly_ply_c::nextPauseTimer (item_ring.h pace)
const uint32_t kPauseTimer = GD(0x101EACB6);         // dScnPly_ply_c::pauseTimer

struct GuestMem {
    uint8_t u8(uint32_t a) const { return ld8(a); }
    void w8(uint32_t a, uint8_t v) { st8(a, v); }
};
bool guest(uint32_t p, uint32_t size) { return p >= mem::kMem2Start && p <= mem::kMem2End - size; }
uint32_t save_address() {
    const uint32_t info = ld32(kSaveInfoPtr);
    return guest(info, 0x200) ? info + 0x20 : 0;
}

bool playing_by_touch() {
#ifdef __ANDROID__
    return phone_rumble::last_input_touch.load(std::memory_order_relaxed);
#else
    return false;
#endif
}

// ---- state (VPAD and KPAD reads may come from two guest threads; the frame boundary reads it too) ----
std::mutex g_mu;
Ring g_ring;
Action g_pending;            // an equip decided by a read, done at the next frame boundary
uint64_t g_open_step = 0;    // the logic step the ring opened on (the pace's schedule)
bool g_drawn = false;        // a HUD list is published
bool g_paused_last = false;  // the ring was open at the last pass (the pace's log)
Finger g_finger;             // the finger on an item button (Android)
std::chrono::steady_clock::time_point g_t0 = std::chrono::steady_clock::now();

double now_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count(); }

void log_entry(const char* what, const Entry& e) {
    LOG("[ring] %s: %s (slot %u, item %02X)", what, entry_name(e), e.slot, e.item);
}

// ---- drawing (the frame boundary, game thread) ----
// TV units (1280 x 720), centred; the list goes to both screens (the GamePad's is scaled from the TV's)
constexpr float kCx = 640, kCy = 360, kRadius = 215;
float text_width(const char* s, float size) { return float(strlen(s)) * size * 0.5f; }
struct Out {
    std::vector<hud::Command> cmds;
    std::vector<hud::Handle> pics;  // per command (0: not a picture)
    void add(hud::Command c, hud::Handle pic = 0) {
        cmds.push_back(std::move(c));
        pics.push_back(pic);
    }
    void text(const char* s, float x, float y, float size, uint32_t rgba) {
        hud::Command c;
        c.kind = hud::Command::Text;
        c.x = x - text_width(s, size) * 0.5f + size * 0.06f;
        c.y = y - size * 0.5f + size * 0.06f;
        c.size = size;
        c.rgba = 0x000000C0;  // a shadow first
        c.text = s;
        add(c);
        c.x -= size * 0.06f;
        c.y -= size * 0.06f;
        c.rgba = rgba;
        add(std::move(c));
    }
    void circle(float x, float y, float r, uint32_t rgba, bool outline = false, float thick = 2) {
        hud::Command c;
        c.kind = outline ? hud::Command::CircleOutline : hud::Command::Circle;
        c.x = x, c.y = y, c.size = r, c.rgba = rgba, c.thickness = thick;
        add(c);
    }
    void line(float x0, float y0, float x1, float y1, uint32_t rgba, float thick) {
        hud::Command c;
        c.kind = hud::Command::Line;
        c.x = x0, c.y = y0, c.w = x1 - x0, c.h = y1 - y0, c.rgba = rgba, c.thickness = thick;
        add(c);
    }
};
// a short label for an entry without a picture: its name's first word, at most 8 letters
std::string short_label(const Entry& e) {
    std::string s = entry_name(e);
    if (s.rfind("Hero's ", 0) == 0 || s.rfind("Deluxe ", 0) == 0) s = s.substr(7);
    const size_t sp = s.find(' ');
    if (sp != std::string::npos && sp >= 3) s = s.substr(0, sp);
    return s.size() > 8 ? s.substr(0, 8) : s;
}

void draw(Cpu* c, const Ring& r, bool touch) {
    auto& store = hud::store();
    const Entries& l = r.list();
    const int names = g_names.get();
    Out o;
    {
        uint8_t items[kMaxEntries];
        for (int i = 0; i < l.n; i++) items[i] = l.e[i].item;
        item_ring_icons::keep_only(items, l.n, kOwner);
    }
    const int n = std::max(l.n, 1);
    const float slot = float(2 * M_PI * kRadius / n);
    const float size = std::min(84.f, slot * 0.8f);  // an entry's circle diameter
    o.circle(kCx, kCy, kRadius + size * 0.85f, 0x00000090);
    o.circle(kCx, kCy, kRadius, 0xFFFFFF50, true, 3);
    // the middle: the button being set; above it the open bag; below it the picked entry's name
    o.text(button_name(r.button(), names, touch), kCx, kCy - 8, 64, 0xFFFFFFFF);
    if (r.bag() >= 0) o.text(item_name(r.save().inventory[kBagInventorySlot[r.bag()]]), kCx, kCy - 74, 24, 0xFFE8B0FF);
    if (r.sel() >= 0) o.text(entry_name(l.e[r.sel()]), kCx, kCy + 52, 26, 0xFFE680FF);
    for (int i = 0; i < l.n; i++) {
        const Entry& e = l.e[i];
        float dx, dy;
        direction(i, l.n, dx, dy);
        const float x = kCx + dx * kRadius, y = kCy - dy * kRadius;
        const bool sel = i == r.sel();
        const float s = sel ? size * 1.3f : size;
        if (sel) o.circle(x, y, s * 0.62f, 0xFFE680D0);
        o.circle(x, y, s * 0.5f, sel ? 0x302010E0 : 0x10101CC0);
        if (e.kind == kEntryEmpty) {  // a struck-through circle
            o.circle(x, y, s * 0.3f, 0xFFFFFFFF, true, s * 0.06f);
            o.line(x - s * 0.21f, y + s * 0.21f, x + s * 0.21f, y - s * 0.21f, 0xFFFFFFFF, s * 0.06f);
            continue;
        }
        if (e.kind == kEntryBack) {  // an arrow to the left
            o.line(x + s * 0.2f, y, x - s * 0.2f, y, 0xFFFFFFFF, s * 0.07f);
            o.line(x - s * 0.2f, y, x - s * 0.04f, y - s * 0.16f, 0xFFFFFFFF, s * 0.07f);
            o.line(x - s * 0.2f, y, x - s * 0.04f, y + s * 0.16f, 0xFFFFFFFF, s * 0.07f);
            continue;
        }
        const hud::Handle pic = e.item != kNone ? item_ring_icons::handle(c, e.item, kOwner) : 0;
        if (pic) {
            hud::Command p;
            p.kind = hud::Command::Picture;
            p.x = x - s * 0.42f, p.y = y - s * 0.42f, p.w = p.h = s * 0.84f;
            o.add(p, pic);
        } else {
            const std::string lab = short_label(e);
            o.text(lab.c_str(), x, y, std::min(s * 0.24f, 22.f), 0xFFFFFFFF);
        }
        for (int b = 0; b < kButtons; b++) {  // the buttons it is on now
            if ((e.kind != kEntryItem && e.kind != kEntryConduct) || r.save().select[b] != e.slot) continue;
            const float bx = x + s * 0.36f, by = y + s * 0.36f, br = s * 0.2f;
            o.circle(bx, by, br, b == r.button() ? 0xFFC040FF : 0x404040FF);
            o.text(button_name(b, names, touch), bx, by, br * 1.2f, 0xFFFFFFFF);
        }
    }
    const hud::Handle list = store.begin(kOwner, 2);
    if (!list) return;
    for (size_t i = 0; i < o.cmds.size(); i++) {
        if (o.pics[i]) store.picture(kOwner, list, o.pics[i], o.cmds[i]);
        else store.append(kOwner, list, o.cmds[i]);
    }
    store.commit(kOwner, list);
    g_drawn = true;
}
}  // namespace

// ---- options ----
int item_ring_mode() { return g_mode.get(); }
void set_item_ring_mode(int m) { g_mode.set(m); }
int item_ring_pace() { return g_pace.get(); }
void set_item_ring_pace(int p) { g_pace.set(p); }
int item_button_names() { return g_names.get(); }
void set_item_button_names(int n) { g_names.set(n); }
const char* item_button_name(int button) { return button_name(button, g_names.get(), playing_by_touch()); }
bool item_names_on_hud() {
    const int n = g_names.get();
    return n == kNamesA123 || (n == kNamesAuto && playing_by_touch());
}

// ---- the pad reads (game thread) ----
void item_ring_pad(input::PadState& p, bool repeat) {
    const int mode = g_mode.get();
    std::lock_guard<std::mutex> lk(g_mu);
    if (repeat) {  // true 60's half passes: buttons repeat; fresh sticks must not walk while a controller picks
        if (g_ring.open() && !g_ring.by_touch()) p.lx = p.ly = 0;
        return;
    }
    if (mode == kOff && !g_ring.open()) {
        g_ring.reset();
        return;
    }
    static const bool wanted = (play_context_want(true), true);
    (void)wanted;
    const playctx::PlayContext ctx = play_context();
    Env env;
    env.mode = mode;
    env.on_ship = playctx::in_ship(ctx);
    const uint32_t s = save_address();
    // (quick use presses the item buttons itself at a locked enemy: those presses go through untouched)
    env.eligible = s && eligible(ctx) && !fast_forward_active() && !overlay::blocks_input() &&
                   !(quick_use() && ctx.has(playctx::kLocked));
    if (env.eligible || g_ring.open()) {
        GuestMem m;
        env.save = s ? read_save(m, s) : Save{};
    }
    PadIn in;
    in.buttons = p.buttons;
    in.lx = p.lx, in.ly = p.ly;
    in.now = now_s();
    in.touch = playing_by_touch();
    in.finger = g_finger;
    const bool was_open = g_ring.open();
    const int was_sel = g_ring.sel(), was_bag = g_ring.bag();
    Action act;
    p.buttons = g_ring.filter(in, env, act);
    p.lx = in.lx, p.ly = in.ly;
    if (act.kind == Action::kEquip) g_pending = act;
    if (!was_open && g_ring.open()) g_open_step = step();
    if (log_on()) {
        if (!was_open && g_ring.open())
            LOG("[ring] %s open (%s, %d entries, step %llu)", button_name(g_ring.button(), kNamesGame, false),
                g_ring.by_touch() ? "touch" : "controller", g_ring.list().n, (unsigned long long)step());
        if (g_ring.open() && (!was_open || g_ring.bag() != was_bag)) {
            std::string list;
            for (int i = 0; i < g_ring.list().n; i++) list += std::string(i ? ", " : "") + entry_name(g_ring.list().e[i]);
            LOG("[ring] %s %d: %s", g_ring.bag() >= 0 ? "bag" : "items", g_ring.bag(), list.c_str());
        }
        if (g_ring.open() && g_ring.sel() != was_sel && g_ring.sel() >= 0) log_entry("on", g_ring.list().e[g_ring.sel()]);
        if (was_open && !g_ring.open())
            LOG("[ring] closed: %s%s", act.kind == Action::kEquip ? "equip" : act.kind == Action::kPress ? "press" : "nothing",
                env.eligible ? "" : " (no longer in play)");
    }
}

// ---- the frame boundary (game thread, interp.cpp, every pass) ----
void item_ring_service(Cpu* c, bool full, bool half60) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_pending.kind == Action::kEquip) {
        const Action a = g_pending;
        g_pending = Action{};
        GuestMem m;
        const uint32_t s = save_address();
        const bool ok = s && equip(m, s, kPlaySelect, a.button, a.slot);
        if (log_on() || !ok)
            LOG("[ring] %s: slot %02X %s; buttons now %02X %02X %02X (items %02X %02X %02X)",
                button_name(a.button, kNamesGame, false), a.slot, ok ? "put on" : "NOT put on", s ? ld8(s + kSaveSelect) : 0,
                s ? ld8(s + kSaveSelect + 1) : 0, s ? ld8(s + kSaveSelect + 2) : 0, ld8(kPlaySelect), ld8(kPlaySelect + 1),
                ld8(kPlaySelect + 2));
    }
    // the pace: the game's pause timer, written each pass while the ring is open
    // (full passes by the schedule; true 60's half passes held whenever the pace isn't normal, so Link's
    // half-step preview never runs ahead of a held step and is then taken back; interpolation's in-between
    // passes run no logic and are left alone)
    const int pace = g_pace.get();
    const int v = g_ring.open() && full ? pause_value(pace, step() - g_open_step) : 0;
    if (v) st8(kNextPauseTimer, uint8_t(v));
    // (a half pass runs no scene step, so nothing would take a "next" value there and it would hold the
    // following full step: the running timer itself is set; the next full step counts it down)
    if (g_ring.open() && half60 && pace != kPaceNormal && !ld8(kPauseTimer)) st8(kPauseTimer, 1);
    if (log_on() && g_ring.open() != g_paused_last) {
        static const char* const names[] = {"normal", "slow motion", "paused"};
        if (g_ring.open()) LOG("[ring] pace: held (%s, step %llu)", names[g_pace.get()], (unsigned long long)step());
        else LOG("[ring] pace: runs (step %llu)", (unsigned long long)step());
    }
    g_paused_last = g_ring.open();
    if (g_ring.open()) {
        draw(c, g_ring, playing_by_touch());
    } else if (g_drawn) {
        hud::store().drop(kOwner);
        g_drawn = false;
    }
}

void item_ring_reset() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_ring.reset();
    g_pending = Action{};
}

bool item_ring_open() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_ring.open();
}

// the finger on an on-screen item button (Android TouchControls; tests)
void item_ring_finger(int button, bool down, float dx, float dy) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_finger.down = down && button >= 0 && button < kButtons;
    g_finger.button = g_finger.down ? button : -1;
    g_finger.dx = dx, g_finger.dy = dy;
}
}  // namespace mods

#ifdef __ANDROID__
// the finger on X (0), Y (1) or R (2): its drag from where it landed, in the button's radii (+y down)
extern "C" JNIEXPORT void JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeItemRingFinger(JNIEnv*, jclass, jint button,
                                                                                                jboolean down, jfloat dx,
                                                                                                jfloat dy) {
    mods::item_ring_finger(int(button), down, float(dx), float(dy));
}
// the ring is open (TouchControls keeps the item button drawn pressed while its finger roams)
extern "C" JNIEXPORT jboolean JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeItemRingOpen(JNIEnv*, jclass) {
    return mods::item_ring_open();
}
#endif
