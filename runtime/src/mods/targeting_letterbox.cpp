// Targeting trim shares its table write with camera modes 1 and 3. Record provenance
// there, then change only the Vista height operand in CalcTrimSize. Scripted trim,
// forced trim and all other modes continue through the original instructions.
#include "mods.h"
#include "targeting_letterbox.h"
#include "runtime.h"
#include "guest_addr.h"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <vector>

extern "C" void f_02515280_orig(Cpu*);
extern "C" void f_024FBF9C_orig(Cpu*);

namespace {
std::atomic<int> mode{[]{
    if (const char* e = std::getenv("WWHD_TARGET_LETTERBOX_MODE")) {
        if (e[0] >= '0' && e[0] <= '2' && e[1] == 0) return int(e[0] - '0');
    }
    const char* e = std::getenv("WWHD_TARGET_LETTERBOX");
    return e && std::atoi(e) == 0 ? 1 : 0; // Preserve the original boolean override.
}()};
uint32_t normal_camera = 0;
uint64_t normal_step = ~0ull;
mods::targeting_letterbox::Context context(uint32_t camera) {
    const uint32_t play = GD(0x1046F0B0);
    const uint32_t attention = play + 0x5804;
    const uint8_t lock_state = ld8(attention + 0x18);
    const uint32_t target = ld32(camera + 0x12C);
    const uint32_t count = ld32(attention + 0xD4), index = ld32(attention + 0xD8);
    const uint32_t selected_pid = count <= 8 && index < count
        ? ld32(attention + 0x54 + index * 16 + 12) : UINT32_MAX;
    // Mode 2 also follows a returning boomerang or a forced camera target. Require
    // the actual selected lock-on actor, not just a non-null camera target and
    // state 2 (whose native LockonTruth additionally requires a live target).
    const bool selected = target && (lock_state == 1 || lock_state == 2) &&
        selected_pid != UINT32_MAX && selected_pid == ld32(target + 4);
    return {ld32(camera + 0x600), ld32(camera + 0x13C),
            ld8(play + 0x5292) != 0,
            ld32(play + 0x52E4) != 0 || (ld32(camera + 0x510) & 0x20000000u) != 0,
            ld8(play + 0x5CEA) != 0,
            selected,
            normal_camera == camera && normal_step == mods::step(),
            target == 0 && (ld32(attention + 0x20) & 0x20000000u) != 0};
}
}
namespace mods {
int targeting_letterbox_mode() { return mode.load(std::memory_order_relaxed); }
void set_targeting_letterbox_mode(int value) {
    if (value >= 0 && value <= 2) mode.store(value, std::memory_order_relaxed);
}
bool letterbox_while_targeting() { return targeting_letterbox_mode() == targeting_letterbox::On; }
void set_letterbox_while_targeting(bool on) {
    set_targeting_letterbox_mode(on ? targeting_letterbox::On : targeting_letterbox::OffWithEnemies);
}
}
using namespace mods;
namespace {
// Headless integration aid, matching the other timed camera/input test controls.
void test_toggle() {
    struct Toggle { double at; bool on; };
    static const auto toggles = [] {
        std::vector<Toggle> result;
        const char* e = std::getenv("WWHD_TEST_TARGET_LETTERBOX");
        while (e && *e) {
            double at; int on, n = 0;
            if (std::sscanf(e, "%lf:%d%n", &at, &on, &n) != 2 || (on != 0 && on != 1)) break;
            result.push_back({at, on != 0}); e += n;
            if (*e != ',') break;
            ++e;
        }
        return result;
    }();
    static size_t next = 0;
    while (next < toggles.size() && game_time() >= toggles[next].at) {
        set_letterbox_while_targeting(toggles[next].on);
        trace("trim toggle: time %.6f enabled %u", game_time(), toggles[next].on);
        ++next;
    }
}
}
// Explicit scripted trim requests and normal-play default trim are separate paths.
// Keep the original calls and their register/memory results intact while measuring.
extern "C" void hook_02515280(Cpu* c) {
    normal_camera = 0;
    if (trace_on() && c->r[4] == 1) {
        const uint32_t camera = c->r[3];
        trace("trim explicit: caller %08X camera %08X mode %u event %u target %08X height %.6f",
              c->lr - 4, camera, ld32(camera + 0x13C), ld8(GD(0x1046F0B0) + 0x5292),
              ld32(camera + 0x12C), ldf32(camera + 0x5FC));
    }
    f_02515280_orig(c);
}
extern "C" void hook_024FBF9C(Cpu* c) {
    test_toggle();
    normal_camera = 0;
    const uint32_t camera = c->r[3];
    const uint32_t caller = c->lr - 4;
    const int32_t force = static_cast<int32_t>(ld32(camera + 0x604));
    f_024FBF9C_orig(c);
    if (trace_on()) {
        trace("trim default: caller %08X camera %08X mode %u event %u target %08X force %d size %u height %.6f",
              caller, camera, ld32(camera + 0x13C), ld8(GD(0x1046F0B0) + 0x5292),
              ld32(camera + 0x12C), force, ld32(camera + 0x600), ldf32(camera + 0x5FC));
    }
}

extern "C" void site_024FF8C0(Cpu* c) {
    if (!trace_on()) return;
    const uint32_t camera = c->r[31];
    const auto state = context(camera);
    trace("trim calc: camera %08X mode %u event %u scripted %u minigame %u target %u normal %u size %u height %.6f",
          camera, state.mode, state.event, state.scripted, state.minigame, state.target,
          state.normal_write, state.size, ldf32(camera + 0x5FC));
}

static void event_trim(Cpu* c) {
    normal_camera = 0;
    if (trace_on() && c->r[5] == 1) trace("trim event VISTA: camera %08X mode %u event %u",
        c->r[31], ld32(c->r[31] + 0x13C), ld8(GD(0x1046F0B0) + 0x5292));
}
extern "C" void site_024FF4C0(Cpu* c) { event_trim(c); }
extern "C" void site_024FF4E4(Cpu* c) { event_trim(c); }


// Immediately before the shared table-result store. The native force/player gates
// have already passed. Mode 2 is native lock-on; mode 1 is attention without a target.
extern "C" void site_024FC080(Cpu* c) {
    const uint32_t camera_mode = ld32(c->r[31] + 0x13C);
    if (c->r[3] == 1 && (camera_mode == 1 || camera_mode == 2)) {
        normal_camera = c->r[31];
        normal_step = step();
    }
}
// Before Vista target - old height. Default-on does not change a guest register or byte.
extern "C" void site_024FF980(Cpu* c) {
    const uint32_t camera = c->r[31];
    const auto state = context(camera);
    if (targeting_letterbox::suppress(targeting_letterbox_mode(), state)) {
        c->f[11].ps0 = c->f[11].ps1 = 0.0;
    }
    if (trace_on()) trace("trim vista: camera %08X mode %u event %u scripted %u target %u normal %u suppressed %u height %.6f",
        camera, state.mode, state.event, state.scripted, state.target, state.normal_write,
        targeting_letterbox::suppress(targeting_letterbox_mode(), state), ldf32(camera + 0x5FC));
    normal_camera = 0;
}
