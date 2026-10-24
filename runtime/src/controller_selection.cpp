// Issue #189: supply an input event, then let the original selection handler do
// the availability checks, outgoing event, animations and controller-mode change.
#include "controller_selection.h"
#include "file_select_quit.h"
#include "input.h"
#include "input_map.h"
#include "runtime.h"
#include "guest_addr.h"

namespace {
bool object(uint32_t p) { return p >= mem::kMem2Start && p < mem::kMem2End - 0x200; }

int option(Cpu* c, uint32_t screen, bool background_transition = false) {
    if (!input_map::skip_controller_selection() || !object(screen)) return -1;
    if (!object(ld32(GD(0x101F4FF0)))) return -1;
    uint32_t owner = ld32(GD(0x101F82EC)); // title/name controller (created by 0270BE70)
    if (!object(owner) || ld32(owner + 0x130) != screen) return -1;
    // Only the title's SelectController state (enter 0270AAAC, execute 0270AB10).
    // Reconnect/error prompts and the in-game controller menu are left alone.
    uint32_t state = guest_call(c, GC(0x02006478), {owner + 0x10});
    if (!object(state)) return -1;
    uint32_t vt = ld32(state + 8);
    if (!object(vt)) return -1;
    uint32_t key = guest_call(c, ld32(vt + 0x14), {state});
    uint32_t expected = GD(0x1049EA2C);
    vt = ld32(expected + 8);
    if (!object(vt)) return -1;
    if (key != guest_call(c, ld32(vt + 0x14), {expected})) {
        // The background fades its old caption after the owner has entered file
        // select. Hide that outgoing caption until the game's text update runs.
        uint32_t background = ld32(owner + 0x12C);
        uint32_t file_select = GD(0x1049EA58);
        vt = ld32(file_select + 8);
        if (!background_transition || ld32(owner + 0x1C) != expected || !object(background) ||
            ld32(background + 0x50) != 1 || ld32(background + 0x54) != 0 || !object(vt) ||
            key != guest_call(c, ld32(vt + 0x14), {file_select})) return -1;
    }
    if (ld32(screen + 0x48) != 2 || !object(ld32(screen + 0x4C))) return -1;
    if (ld32(GD(0x10102B80)) == ld32(GD(0x10102B80) + 4)) return -1;
    // Validate both named child slots; do not use the game's fallback-to-child-0.
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t index = ld32(GD(0x10102B80) + i * 4);
        if (index >= 2) return -1;
        uint32_t child = ld32(ld32(screen + 0x4C) + index * 4);
        if (!object(child) || !object(ld32(child + 0x50))) return -1;
    }
    bool pro = input::pro_controller();
    bool available = guest_call(c, GC(pro ? 0x026D43DC : 0x026D43AC), {screen}) != 0;
    return controller_selection::decision(true, true, pro ? 1 : 0, !pro && available, pro && available);
}
}

// After updateSelection copied the current input event into its local frame,
// before it reads the event kind. The original code consumes this exactly as a
// button's SPECIAL (17) event. No game flags, mode or selection state are written.
extern "C" void site_026D44A8(Cpu* c) {
    Cpu saved = *c;
    uint32_t screen = c->r[28];
    int selected = option(c, screen);
    if (selected >= 0) {
        uint32_t index = ld32(GD(0x10102B80) + selected * 4);
        uint32_t child = ld32(ld32(screen + 0x4C) + index * 4);
        uint32_t receiver = ld32(child + 0x50);
        uint32_t id = guest_call(c, GC(0x026F5B74), {child});
        // updateSelection also accepts the focused receiver. Move focus using
        // the game's own setter, or its previous focus could win before this id.
        guest_call(c, GC(0x02615154), {ld32(GD(0x101F4FF0)), receiver});
        guest_call(c, GC(0x02001338), {saved.r[1] + 0x30, 17, id, 0});
        LOG("[controller-select] title: automatic %s", selected ? "Pro Controller" : "GamePad");
    }
    *c = saved;
}

// Keep the title prompt invisible while its own in/out animations finish.
extern "C" void f_026D50C4_orig(Cpu*);
extern "C" void hook_026D50C4(Cpu* c) {
    Cpu saved = *c;
    bool automatic = option(c, c->r[3]) >= 0;
    *c = saved;
    if (!automatic) f_026D50C4_orig(c);
}


// StartBackground is a separate part, so hiding SelectController_00 alone
// leaves its heading visible. Suppress only this owner's background draw until
// the normal caption transition has replaced it with File Selection.
extern "C" void f_026F8B54_orig(Cpu*);
// Parts::draw runs for every HUD part each frame: only the title background pays for a Cpu copy.
extern "C" void hook_026F8B54(Cpu* c) {
    uint32_t title = ld32(GD(0x101F82EC));
    if (object(title) && ld32(title + 0x12C) == c->r[3]) file_select_quit::observe(c);
    if (input_map::skip_controller_selection()) {
        uint32_t owner = ld32(GD(0x101F82EC));
        if (object(owner) && ld32(owner + 0x12C) == c->r[3]) {
            Cpu saved = *c;
            bool automatic = option(c, ld32(owner + 0x130), true) >= 0;
            *c = saved;
            if (automatic) return;
        }
    }
    f_026F8B54_orig(c);
}
