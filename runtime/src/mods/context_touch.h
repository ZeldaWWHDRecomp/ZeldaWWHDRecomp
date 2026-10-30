// SPDX-License-Identifier: MPL-2.0
// Context buttons on the Android touch controls (controls plan P3): what Java (TouchControls) polls
// and how it is laid out. Pure, so context_touch_test checks the layout without the game. The rules
// themselves are context_buttons.h; the touch-only behaviour behind them (items by tap, aim-only
// buttons, auto put-away, the ZL latch, the golden rings) is decided natively too; Java only draws
// what this pack says and routes fingers to the virtual pad (nativeSetPad) as before.
//
// Contract (P3-CONTRACT.md has the long form):
//   TouchControls.nativeContextButtons(int[] out) -> pack version (kPackVersion), out as pack() below.
//   TouchControls.nativeContextAssets(String dir) -> 0 not ready yet (ask again later), -1 failed
//     (draw letters / English words), > 0 the generation of the files in `dir` (re-load when it changes):
//       dir/actions.txt   lines "HH<TAB>word": the game's word for action code HH (hex), UTF-8
//       dir/item_HH.png   the game's icon for item number HH (hex), RGBA PNG 128 x 128
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0),
// ControlsView's context buttons; reimplemented for our design.
#pragma once
#include <cstdint>

#include "context_buttons.h"
#include "startup_touch.h"

namespace mods::ctxtouch {

// header flags (out[1])
enum : int32_t {
    kEnabled = 1 << 0,       // the "Context buttons" option is on: Java draws by the per-button states
    kActive = 1 << 1,        // the rules apply now (off: a menu, the title screen... Java shows the full pad)
    kTapContinues = 1 << 2,  // a tap anywhere on the picture goes on (a talk's "go on" icon is up)
    kAimLatched = 1 << 3,    // items by tap: an item is held out / aimed by a tap (the buttons are the aim set)
    kZlLatched = 1 << 4,     // the ZL latch holds the lock-on (draw ZL pressed)
    kClusterHidden = 1 << 5, // the game's own button cluster is hidden now (information only)
    kItemNamesA = 1 << 6,    // the item buttons are named A1 A2 A3 now (Controls > Item button names): draw those, not X Y R
};
// per-button state (out[kButtons + 3 * button]); kind as ctxbtn::Kind
enum : int32_t {
    kShown = 1 << 0,
    kKindShift = 1, kKindMask = 3 << 1,  // 0 plain, 1 action word, 2 item icon, 3 fast forward
    kHeld = 1 << 3,  // natively held (a latched item, the ZL latch): draw it pressed
};

constexpr int kPackVersion = 1;
constexpr int kButtons = 4;  // first per-button slot
constexpr int kPackLen = kButtons + 3 * ctxbtn::kCount + 3;  // 40

struct Extra {          // what the touch-only state adds to the rules' view
    bool enabled = false;
    bool aim_latched = false, zl_latched = false, cluster_hidden = false, item_names_a = false;
    int assets_generation = 0;
    startup_touch::Scene startup_scene = startup_touch::kUnknown;
    uint32_t held = 0;       // bit per ctxbtn::Button drawn pressed
    int32_t ring[ctxbtn::kCount] = {};  // golden-ring counters
};

// out[0] version, [1] header flags, [2] assets generation (as nativeContextAssets last returned, 0 none),
// [3] startup_touch::pack(scene), 0 in runtimes before this extension; then per ctxbtn::Button b
// (A B X Y R ZR ZL + R3 Up LR): [4 + 3b] state, [5 + 3b] label
// (action code for kind 1, item number for kind 2), [6 + 3b] golden-ring counter (it changes each time
// the button gains a new action: Java plays the ring once per change).
inline void pack(const ctxbtn::Buttons& b, const Extra& e, int32_t* out, int n) {
    int32_t v[kPackLen] = {};
    v[0] = kPackVersion;
    v[1] = (e.enabled ? kEnabled : 0) | (b.active ? kActive : 0) | (b.tap_continues ? kTapContinues : 0) |
           (e.aim_latched ? kAimLatched : 0) | (e.zl_latched ? kZlLatched : 0) | (e.cluster_hidden ? kClusterHidden : 0) | (e.item_names_a ? kItemNamesA : 0);
    v[2] = e.assets_generation;
    v[3] = startup_touch::pack(e.startup_scene);
    for (int i = 0; i < ctxbtn::kCount; i++) {
        const ctxbtn::ButtonView& bv = b.b[i];
        v[kButtons + 3 * i] = (bv.shown ? kShown : 0) | (int32_t(bv.kind) << kKindShift) | ((e.held >> i & 1) ? kHeld : 0);
        v[kButtons + 3 * i + 1] = bv.label;
        v[kButtons + 3 * i + 2] = e.ring[i];
    }
    for (int i = 0; i < n; i++) out[i] = i < kPackLen ? v[i] : 0;
}

}  // namespace mods::ctxtouch
