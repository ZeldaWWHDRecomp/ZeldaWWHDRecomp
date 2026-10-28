// SPDX-License-Identifier: MPL-2.0
// Controls that depend on whether Link plays (P6 of the controls plan; idea from the GreenNaugahyde
// Android fork, reimplemented):
//   - pad_play_rules: L and d-pad down by the game's state (input_map::play_buttons): in play the
//     game gets no L unless "L turns the GamePad page" is on, and an input shared by a game button
//     and L or d-pad down presses only one of them;
//   - the on-screen Jump and Moon jump buttons (context_buttons.h jump_buttons): which show, and
//     their presses (input::set_touch_virtual), for android/.../TouchControls.java.
// Both read the PlayContext (play_context.h), which this file therefore always wants.
#include <cstdlib>
#include <mutex>

#include "context_buttons.h"
#include "mods.h"
#include "play_context.h"
#include "../input.h"
#include "../input_map.h"
#include "../runtime.h"

#ifdef __ANDROID__
#include <jni.h>
#endif

namespace mods {
namespace {
void want_context() {
    static const bool wanted = (play_context_want(true), true);
    (void)wanted;
}
}  // namespace

void pad_play_rules(input::PadState& p) {
    want_context();
    static std::mutex mu;
    static input_map::Mapping m;
    static uint32_t gen = 0;
    const bool in_play = play_context().has(playctx::kInPlay);
    std::lock_guard<std::mutex> lk(mu);  // VPAD and KPAD reads may come from two guest threads
    if (const uint32_t g = input_map::generation(); g != gen) {
        m = input_map::current();
        gen = g;
    }
    const uint32_t before = p.buttons;
    p.buttons = input_map::play_buttons(m, p.buttons, in_play);
    // debug: WWHD_PLAY_RULES_LOG=1 logs each change of what the rules hold back
    static const bool log = getenv("WWHD_PLAY_RULES_LOG") != nullptr;
    static uint32_t last = 0;
    if (log && (before & ~p.buttons) != last) {
        last = before & ~p.buttons;
        LOG("[controls] %s: held back %04X of %04X", in_play ? "in play" : "not in play", last, before);
    }
}

uint32_t jump_touch_buttons() {
    want_context();
    if (context_touch_aim_latched()) return 0;  // aim-only: an item is held out by a tap
    return ctxbtn::jump_buttons(play_context(), jump_anywhere(), infinite(kMoonJump));
}
}  // namespace mods

#ifdef __ANDROID__
// which of the on-screen Jump (1) and Moon jump (2) buttons show now (polled with nativeTouchState)
extern "C" JNIEXPORT jint JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeJumpButtons(JNIEnv*, jclass) {
    return (jint)mods::jump_touch_buttons();
}
// the on-screen Jump / Moon jump buttons held (input::kVirt* bits): they never press a game button
extern "C" JNIEXPORT void JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeSetVirtual(JNIEnv*, jclass, jint bits) {
    input::set_touch_virtual(uint32_t(bits) & (input::kVirtJump | input::kVirtMoonJump));
}
#endif
