// Read-only view of SCENE-ingame_ui and hdInputManager from the HD decomp.
// Called on the guest thread at GX2SwapScanBuffers, never from a GPU completion handler.
#include "display_modes.h"
#include "auto_pip.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cstdlib>

namespace gfx {
namespace {
bool object(uint32_t p, uint32_t size) {
    return (p & 3) == 0 && p >= mem::kMem2Start && p <= mem::kMem2End - size;
}
}
void display_read_gamepad_ui() {
    if (!display_gamepad_state_needed()) return;
    GamePadUiState state;
    {
        const uint32_t scene = ld32(GD(0x101F8344));
        const uint32_t input = ld32(GD(0x101F5088));
        if (object(scene, 0x24c) && object(input, 0x270)) {
            // hdStateMachine +8 is the current state object; its +8 is the descriptor.
            // With no current object, +4 is the pending descriptor (02006478).
            const uint32_t current = ld32(scene + 0x18);
            const uint32_t descriptor = object(current, 12) ? ld32(current + 8) : ld32(scene + 0x14);
            const uint32_t controller = ld32(input + 0x1d0);
            state.available = gamepad_scene_kind(descriptor) != GamePadSceneKind::Unknown && controller <= 3;
            state.off_tv = controller == 2;
            state.switching = gamepad_switch_state(descriptor);
            const uint32_t routing = ld32(GD(0x101F8348));
            // MENU-menu_main::openWindow (0268EC88) selects one of the three bag windows;
            // finishSaveWindow (02692A50) and requestOutAll (026942E0) restore sentinel 3.
            const uint32_t items = ld32(scene + 0x17c);
            const bool bag = object(items, 0x58) && ld32(items + 0x54) < 3;
            state.menu = gamepad_menu_state(descriptor, routing, controller, bag);
            // dComIfGp_setMetronomeOn/Off (play +5BD1): daPy_lk_c::procTactWait_init (023E92A0)
            // sets it while Link conducts with the Wind Waker; the song's end, a cancel and
            // dProcTalk_init clear it. The play object is the function-local static behind
            // dComIfGp_get (025200D4); before its first use the bytes are still zero.
            const uint32_t metronome = ld8(GD(0x1046F0B0) + 0x5BD1);
            state.baton = gamepad_baton_state(descriptor, metronome != 0, controller);
            // Opt-in evidence for scripted menu/cutscene captures; no image measurements.
            static const bool trace = getenv("WWHD_TEST_GAMEPAD_UI_TRACE") != nullptr;
            if (trace) {
                static uint32_t previous = ~0u, previousRouting = ~0u, previousController = ~0u;
                static bool previousMenu = false, previousBaton = false;
                if (descriptor != previous || routing != previousRouting || controller != previousController || state.menu != previousMenu ||
                    state.baton != previousBaton) {
                    LOG("[display] GamePad UI: state=%08X routing=%u controller=%u menu=%d baton=%d off-tv=%d switching=%d",
                        descriptor, routing, controller, state.menu, state.baton, state.off_tv, state.switching);
                    previous = descriptor; previousRouting = routing; previousController = controller; previousMenu = state.menu;
                    previousBaton = state.baton;
                }
            }
        }
    }
    display_gamepad_ui(state);
}
} // namespace gfx
