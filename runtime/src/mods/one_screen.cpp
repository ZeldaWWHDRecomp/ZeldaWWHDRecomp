// SPDX-License-Identifier: MPL-2.0
// One screen (handheld) mode: the runtime (rules and the why: one_screen.h).
//
// Option: oneScreen in settings.ini (WWHD_ONE_SCREEN=0/1 for tests, not saved); read once at start, a change
// applies at the next start. Default: on for Android unless the player chose the Pro Controller there
// before (proController=1), off elsewhere.
//
// The game's parts, re-checked in our build (USA addresses; GD() / the hooks list map the European build):
// - hdInputManager (singleton *101F5088) +0x1D0 the screen mode; its setter 02618094 (this, mode); its setup
//   02617858 stores the start's 3 itself (stw r0, 0x1D0(r27) at 02617874).
// - The file select (d_s_name): its ControllerSelect state's execute opens the picker widget (owner +0x130) with
//   changeState(widget + 0x18, sControllerInDescriptor 1049AA74) at 0270AC04, then waits for its answer
//   (widget +0x50 cancelled, +0x51 decided, +0x54 the controller: 0 the GamePad). The save list's Back
//   (0270ADD0) asks for ControllerSelect (1049EA2C); without the picker it goes to l_nameCancelled (1049EAB0),
//   where the picker's own Back goes.
// - The pause menu's Options window (constructor 026D7828): item descriptors at 1010307C (6 x 24 bytes; item 3,
//   Controller, has the callback 026D87D0), values +0x104 (a byte per item), +0x118 0 for Options (1: the
//   Miiverse window), the item count stored at 026D8514, the values loaded by 026D5CE4 (rows filled after
//   026D5D84), saved by 026D6DD4 by fixed offsets.
// - The GamePad's slide-conducting guide in Off-TV Play: BatonSignList_00's N_SlideInput_00.
//
// Idea from SwitchWakerHD (thanks @l0nkDev, its #5, MPL-2.0), reimplemented.
#include "../one_screen.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "guest_addr.h"
#include "mods.h"
#include "../gfx/display_modes.h"
#include "../input.h"
#include "../overlay/hostui.h"
#include "../runtime.h"

extern "C" void f_02618094_orig(Cpu* c);
extern "C" void f_026D7828_orig(Cpu* c);
extern "C" void f_026D6DD4_orig(Cpu* c);
extern "C" void f_026D87D0_orig(Cpu* c);

namespace mods {
namespace {
bool object(uint32_t p) { return p >= mem::kMem2Start && p < mem::kMem2End - 0x400; }

constexpr uint32_t kMode = 0x1D0;  // hdInputManager screen mode
constexpr uint32_t kOptDescSize = 0x18, kOptValues = 0x104, kOptWindowKind = 0x118;

// the option, decided once at start
bool start_choice() {
    if (const char* e = getenv("WWHD_ONE_SCREEN"); e && *e) return atoi(e) != 0;
    std::string v;
    const bool saved = hostui::get("oneScreen", v) && !v.empty();
    std::string pro;
#ifdef __ANDROID__
    const bool android = true;
#else
    const bool android = false;
#endif
    // a dual-screen handheld: the second screen already shows the GamePad picture (one_screen.h)
    const bool second = one_screen::second_screen_in_use(gfx::g_second_display_present.load(), gfx::g_second_display_available.load(),
                                                         gfx::drc_screen_off());
    if (!saved && android && second) LOG("[one-screen] a second screen shows the GamePad picture: off by default");
    return one_screen::start_on(saved, saved && atoi(v.c_str()) != 0, android, hostui::get("proController", pro) && pro == "1",
                                second);
}
bool enabled_at_start() {
    static const bool on = [] {
        const bool v = start_choice();
        LOG("[one-screen] One screen (handheld) mode %s", v ? "on: the GamePad in Off-TV Play" : "off");
        gfx::g_one_screen.store(v, std::memory_order_relaxed);
        return v;
    }();
    return on;
}
std::atomic<int> g_next{-1};  // the choice for the next start (-1: as now)

int game_mode() {
    const uint32_t mgr = ld32(GD(0x101F5088));
    return object(mgr) ? int(ld32(mgr + kMode)) : -1;
}

// the Options window's item order (the descriptor table is shared by every window: changed with the mode)
bool g_opt_shown_order = false;
void descriptors_to(bool shown) {
    if (g_opt_shown_order == shown) return;
    const uint32_t base = GD(0x1010307C);
    uint32_t d[one_screen::kOptionItems][6];
    for (int i = 0; i < one_screen::kOptionItems; i++)
        for (int w = 0; w < 6; w++) d[i][w] = ld32(base + uint32_t(i) * kOptDescSize + 4u * uint32_t(w));
    struct Row { uint32_t w[6]; };
    Row rows[one_screen::kOptionItems];
    for (int i = 0; i < one_screen::kOptionItems; i++) memcpy(rows[i].w, d[i], sizeof rows[i].w);
    if (shown) one_screen::to_shown(rows);
    else one_screen::to_game(rows);
    for (int i = 0; i < one_screen::kOptionItems; i++)
        for (int w = 0; w < 6; w++) st32(base + uint32_t(i) * kOptDescSize + 4u * uint32_t(w), rows[i].w[w]);
    g_opt_shown_order = shown;
    LOG("[one-screen] Options: the Controller item %s", shown ? "left out" : "back");
}
void values_to(uint32_t win, bool shown) {
    uint8_t v[one_screen::kOptionItems];
    for (int i = 0; i < one_screen::kOptionItems; i++) v[i] = ld8(win + kOptValues + uint32_t(i));
    if (shown) one_screen::to_shown(v);
    else one_screen::to_game(v);
    for (int i = 0; i < one_screen::kOptionItems; i++) st8(win + kOptValues + uint32_t(i), v[i]);
}
bool options_window(uint32_t win) { return object(win) && ld32(win + kOptWindowKind) == 0; }

one_screen::MinusAsPlus g_minus;
}  // namespace

bool one_screen_enabled() { return enabled_at_start(); }
bool one_screen_active() { return enabled_at_start() && !input::pro_controller(); }
bool one_screen_next() {
    const int n = g_next.load(std::memory_order_relaxed);
    return n < 0 ? enabled_at_start() : n != 0;
}
void set_one_screen_next(bool on) {
    g_next.store(on ? 1 : 0, std::memory_order_relaxed);
    if (!getenv("WWHD_ONE_SCREEN")) hostui::set("oneScreen", on ? "1" : "0");
}
bool one_screen_off_tv() { return one_screen_active() && game_mode() == one_screen::kOffTv; }

// the GamePad's pad read (VPADRead, full reads): Minus as Plus
void one_screen_pad(input::PadState& p) {
    gfx::g_one_screen_off_tv.store(one_screen_off_tv(), std::memory_order_relaxed);  // (the view follows)
    if (!one_screen_active()) {
        g_minus.reset();
        return;
    }
    p.buttons = g_minus.filter(p.buttons, step());
}

// N_SlideInput_00 (the slide-conducting guide) in Off-TV Play
bool one_screen_hidden_pane(const char* name) {
    static const char kSlide[] = "N_SlideInput_00";
    return name[0] == 'N' && !strncmp(name, kSlide, sizeof kSlide) && one_screen_off_tv();
}
}  // namespace mods

// ---- the game's screen mode
extern "C" void hook_02618094(Cpu* c) {
    const uint32_t mgr = c->r[3];
    const bool on = mods::one_screen_active();
    const int asked = int(c->r[4]);
    const int mode = one_screen::requested_mode(on, asked);
    if (mode != asked) {
        c->r[4] = uint32_t(mode);
        LOG("[one-screen] the game asked for TV play: Off-TV Play instead");
    }
    f_02618094_orig(c);
    if (on && mods::object(mgr)) {
        const int stored = int(ld32(mgr + mods::kMode)), keep = one_screen::stored_mode(on, stored);
        if (keep != stored) {
            st32(mgr + mods::kMode, uint32_t(keep));
            static int logged = 0;  // (the title asks again each frame until it settles)
            if (logged < 3 && ++logged) LOG("[one-screen] the picture on both screens: Off-TV Play instead");
        }
    }
}
extern "C" void site_02617874(Cpu* c) {  // the setup's own store of the start's mode (r0)
    if (mods::one_screen_active()) c->r[0] = uint32_t(one_screen::stored_mode(true, int(c->r[0])));
}

// ---- the file select: no controller picker
extern "C" void site_0270AC04(Cpu* c) {
    if (c->r[4] != GD(0x1049AA74) || !mods::one_screen_active()) return;  // sControllerInDescriptor
    const uint32_t machine = c->r[3], widget = machine - 0x18;
    // the state it is in now (hidden; hdStateMachine: +4 the requested id, +8 the current state object, whose +8
    // is its id; measured: no request yet, the current one the screen's initial state 1049AADC): asked for again
    uint32_t requested = ld32(machine + 4);
    if (!requested && mods::object(ld32(machine + 8))) requested = ld32(ld32(machine + 8) + 8);
    if (!mods::object(widget) || !requested) return;
    c->r[4] = requested;
    st8(widget + 0x50, 0);   // not cancelled
    st8(widget + 0x51, 1);   // decided
    st32(widget + 0x54, 0);  // the GamePad
    LOG("[one-screen] file select: the GamePad, without asking");
}
extern "C" void site_0270ADD0(Cpu* c) {  // the save list's Back
    if (c->r[4] == GD(0x1049EA2C) && mods::one_screen_active()) c->r[4] = GD(0x1049EAB0);  // ControllerSelect -> cancelled
}

// ---- the pause menu's Options without Controller
extern "C" void hook_026D7828(Cpu* c) {
    mods::descriptors_to(mods::one_screen_active());
    f_026D7828_orig(c);
}
extern "C" void site_026D8514(Cpu* c) {  // stw r7 (the item count) to +0x120
    if (mods::g_opt_shown_order && c->r[7] == uint32_t(one_screen::kOptionItems)) c->r[7] = one_screen::kOptionItems - 1;
}
extern "C" void site_026D5D84(Cpu* c) {  // the values loaded (r31 the window)
    if (mods::g_opt_shown_order && mods::options_window(c->r[31])) mods::values_to(c->r[31], true);
}
extern "C" void hook_026D6DD4(Cpu* c) {  // saved by fixed offsets
    const uint32_t win = c->r[3];
    const bool swap = mods::g_opt_shown_order && mods::options_window(win);
    if (swap) mods::values_to(win, false);
    f_026D6DD4_orig(c);
    if (swap) mods::values_to(win, true);
}
extern "C" void hook_026D87D0(Cpu* c) {  // the Controller item's callback (its value by fixed offset)
    const uint32_t win = c->r[3];
    const bool swap = mods::g_opt_shown_order && mods::options_window(win);
    if (swap) mods::values_to(win, false);
    f_026D87D0_orig(c);
    if (swap) mods::values_to(win, true);
}
