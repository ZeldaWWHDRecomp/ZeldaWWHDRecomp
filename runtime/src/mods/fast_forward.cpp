// SPDX-License-Identifier: MPL-2.0
// Event gating/display-clock acceleration adapted from GreenNaugahyde's
// ZeldaWWHDRecompAndroid 9551a2508c9eb6f4417ee27116f998237d52addc, mods/turbo.cpp.
#include "fast_forward.h"
#include "mods.h"
#include "../game_clock.h"
#include "../audio_out.h"
#include "guest_addr.h"
#include "../runtime.h"
#include "../overlay/overlay.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
namespace mods {
namespace {
unsigned env(const char* name, unsigned fallback) {
    const char* s = std::getenv(name);
    if (!s || !*s || *s == '-') return fallback;
    char* end = nullptr;
    unsigned long n = std::strtoul(s, &end, 0);
    return *end || n > UINT32_MAX ? fallback : unsigned(n);
}
std::atomic<bool> enabled{env("WWHD_MOD_FAST_FORWARD", 0) != 0};
std::atomic<unsigned> speed{[] { auto n = env("WWHD_MOD_FF_RATE", 2); return n >= 2 && n <= 4 ? n : 2; }()};
std::atomic<uint32_t> button{[] { auto n = env("WWHD_MOD_FF_BUTTON", 0x40); return valid_fast_forward_button(n) ? n : 0x40; }()};
std::atomic<bool> mute{env("WWHD_MOD_FF_MUTE", 1) != 0};
std::atomic<bool> active{false}, release_required{false};
std::atomic<uint32_t> held{0};
const uint32_t play = GD(0x1046F0B0);
bool eligible(uint32_t buttons) {
    const char* stage = reinterpret_cast<const char*>(mem::ptr(play + 0x5134));
    const bool gameplay = *stage && std::strncmp(stage, "sea_T", 8) && std::strncmp(stage, "Name", 8) && std::strncmp(stage, "ENDumi", 8);
    // Same HD event mode as savestate.cpp's kEventRun. Exclude menus and scene transitions.
    return fast_forward_gate(fast_forward(), ld8(play + 0x5292),
        gameplay && ld32(play + 0x5B34) != 0 && !ld8(play + 0x5140 + 12) && !ld32(GD(0x101F36CC)),
        ld8(GD(0x101EA069)) != 0, overlay::blocks_input(), buttons, fast_forward_button());
}
}
bool fast_forward() { return enabled.load(); }
void set_fast_forward(bool on) { enabled = on; if (!on) fast_forward_reset(); }
unsigned fast_forward_rate() { return speed.load(); }
void set_fast_forward_rate(unsigned r) { if (r >= 2 && r <= 4) speed = r; }
uint32_t fast_forward_button() { return button.load(); }
void set_fast_forward_button(uint32_t b) {
    if (valid_fast_forward_button(b) && button.exchange(b) != b) fast_forward_reset();
}
bool fast_forward_mute() { return mute.load(); }
void set_fast_forward_mute(bool on) { mute = on; }
bool fast_forward_active() { return active.load(); }
void fast_forward_reset() {
    if (!fast_forward() && !active.load()) return;
    release_required = true;
    held = 0;
    timebase::set_clock_rate(1);
    // The pass mode is changed only by service(), at a safe frame boundary.
}
void fast_forward_input(uint32_t& buttons) {
    if (!fast_forward()) return;
    held = buttons;
    if (!(buttons & fast_forward_button())) release_required = false;
    if (eligible(buttons)) buttons &= ~fast_forward_button();
}
void fast_forward_service() {
    if (!fast_forward() && !active.load()) return;
    const bool on = !release_required.load() && eligible(held.load());
    if (active.exchange(on) != on) {
        audio::flush(); // discard queued output at the boundary, never guest audio callbacks
        trace("fast forward %s (%ux)", on ? "on" : "off", fast_forward_rate());
    }
    timebase::set_clock_rate(on ? fast_forward_rate() : 1);
}
}
