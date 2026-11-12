// A read-only synthetic guest heap: the reader must never write to the game or call guest code.
#include "gfx/display_modes.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cassert>
#include <sys/mman.h>
#include <unistd.h>

void log_msg(const char*, ...) {}

extern "C" {
// Deliberately shift canonical data, as a regional build does.
const GuestStep g_guest_data_steps[] = {
    {0x10000000, 0x1000}, {0x1049EE10, 0x2000}, {0x1049F338, 0x3000}
};
const unsigned g_guest_data_step_count = 3;
const uint32_t g_guest_data_lo = 0x10000000, g_guest_data_hi = 0x11000000;
const char g_guest_build_name[] = "test regional map";
}
namespace {
bool needed = false;
gfx::GamePadUiState observed;
void map(uint32_t at, size_t size) {
    void* p = mmap(mem::ptr(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    assert(p == mem::ptr(at));
}
void protect(uint32_t at, size_t size, int flags) { assert(mprotect(mem::ptr(at), size, flags) == 0); }
}
namespace gfx {
bool display_gamepad_state_needed() { return needed; }
void display_gamepad_ui(GamePadUiState state) { observed = state; }
}
int main() {
    using namespace gfx;
    display_read_gamepad_ui(); // no mapped memory: inactive modes must not read it
    needed = true;
    const uint32_t globals = 0x101F0000, heap = 0x11000000;
    const size_t region = 0x10000;
    map(globals, region); map(heap, region);
    // dComIfGp play object (function-local static); its metronome byte is the Wind Waker signal.
    const uint32_t metronome = GD(0x1046F0B0) + 0x5BD1, playPage = metronome & ~uint32_t(region - 1);
    map(playPage, region);
    protect(playPage, region, PROT_READ);
    const uint32_t scene = heap, input = heap + 0x1000;
    st32(GD(0x101F8344), scene); st32(GD(0x101F5088), input);
    st32(scene + 0x18, scene + 4); // state object is the embedded state interface
    const auto set = [&](uint32_t id, uint32_t routing, uint32_t mode) {
        protect(globals, region, PROT_READ | PROT_WRITE);
        protect(heap, region, PROT_READ | PROT_WRITE);
        st32(scene + 12, GD(id)); st32(GD(0x101F8348), routing); st32(input + 0x1d0, mode);
        protect(globals, region, PROT_READ); protect(heap, region, PROT_READ);
        display_read_gamepad_ui(); // writes would fault
    };
    // Exercise every menu-table entry with routing=0 and no bag; neither shortcut may hide
    // an untranslated table comparison. The map changes shift twice inside the descriptor range.
    for (uint32_t canonical : {
        0x1049EC84u, 0x1049ECB0u, 0x1049ECDCu, 0x1049ED08u, 0x1049ED34u,
        0x1049ED60u, 0x1049ED8Cu, 0x1049EE10u, 0x1049EE3Cu, 0x1049EEECu,
        0x1049EF18u, 0x1049EF44u, 0x1049EF70u, 0x1049EF9Cu, 0x1049EFC8u,
        0x1049EFF4u, 0x1049F020u, 0x1049F04Cu, 0x1049F078u, 0x1049F0A4u,
        0x1049F0D0u, 0x1049F0FCu, 0x1049F1D8u, 0x1049F204u, 0x1049F288u,
        0x1049F2B4u, 0x1049F2E0u, 0x1049F30Cu, 0x1049F338u}) {
        assert(gamepad_scene_kind(GD(canonical)) == GamePadSceneKind::Menu);
        assert(gamepad_menu_state(GD(canonical), 0, 1));
        assert(gamepad_scene_kind(canonical) == GamePadSceneKind::Unknown);
        assert(!gamepad_menu_state(canonical, 0, 1)); // raw USA pointers must not match
        set(canonical, 0, 1);
        assert(observed.available && observed.menu && !observed.switching);
    }
    for (uint32_t canonical : {0x1049F498u, 0x1049F4C4u, 0x1049F4F0u}) {
        assert(gamepad_switch_state(GD(canonical)));
        assert(!gamepad_switch_state(canonical));
        set(canonical, 0, 1);
        assert(observed.available && observed.switching && !observed.menu);
    }
    set(0x1049EC00, 0, 1);
    assert(observed.available && !observed.menu && !observed.off_tv);
    set(0x1049ECB0, 2, 1);
    assert(observed.available && observed.menu);
    protect(heap, region, PROT_READ | PROT_WRITE);
    st32(scene + 0x17c, heap + 0x2000); st32(heap + 0x2054, 2); // Delivery Bag
    protect(heap, region, PROT_READ);
    set(0x1049EC00, 0, 1);
    assert(observed.menu); // read the bag lifecycle independently of scene transitions
    protect(heap, region, PROT_READ | PROT_WRITE);
    st32(heap + 0x2054, 3);
    protect(heap, region, PROT_READ);
    set(0x1049EC00, 0, 1);
    assert(!observed.menu);
    const auto conduct = [&](uint8_t on) {
        protect(playPage, region, PROT_READ | PROT_WRITE);
        st8(metronome, on);
        protect(playPage, region, PROT_READ);
    };
    conduct(1);
    set(0x1049EC00, 0, 1);
    assert(observed.available && observed.baton && !observed.menu);
    set(0x1049EC00, 0, 0);
    assert(!observed.baton); // Pro Controller
    set(0x1049EC00, 0, 2);
    assert(!observed.baton && observed.off_tv);
    set(0x1049EDE4, 0, 1);
    assert(observed.baton && !observed.menu);
    conduct(0);
    set(0x1049EDE4, 0, 1); // song list closing after the cancel
    assert(observed.baton);
    assert(!gamepad_baton_state(0x1049EDE4, false, 1)); // raw USA pointer must not match
    set(0x1049EC00, 0, 1);
    assert(!observed.baton);
    set(0x1049EC58, 4, 1); // cutscene-induced PauseDRC is not an interactive menu
    assert(observed.available && !observed.menu);
    set(0x1049ECB0, 1, 2);
    assert(observed.off_tv && !observed.menu);
    set(0x1049F498, 0, 1);
    assert(observed.switching);
    set(0x1049ECB0, 4, 0);
    assert(!observed.menu); // Pro-controller menus on TV
    protect(heap, region, PROT_READ | PROT_WRITE);
    st32(scene + 0x18, 0); st32(scene + 0x14, GD(0x1049EC84));
    protect(heap, region, PROT_READ);
    set(0x1049EC00, 0, 1);
    assert(observed.menu); // pending state, matching 02006478 fallback
    protect(globals, region, PROT_READ | PROT_WRITE);
    st32(GD(0x101F8344), 0);
    protect(globals, region, PROT_READ);
    display_read_gamepad_ui();
    assert(!observed.available && !observed.menu && !observed.off_tv && !observed.switching);
    assert(munmap(mem::ptr(globals), region) == 0);
    assert(munmap(mem::ptr(heap), region) == 0);
    assert(munmap(mem::ptr(playPage), region) == 0);
    needed = false;
    display_read_gamepad_ui(); // no new guest reads after leaving auto
}
