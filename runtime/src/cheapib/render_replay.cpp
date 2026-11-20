// "Faster rendering (replays unchanged draws)": the decomp fast mode's cheap in-between frame set (render_replay.h).
#include "cheapib/render_replay.h"

#ifdef WWHD_DECOMP_FAST
#include <cstdlib>
#include <cstring>
#include <string>
#include "gfx/renderer.h"
#include "overlay/hostui.h"
#include "runtime.h"
#ifdef WWHD_HAS_VULKAN
#include "gfx/vulkan/record_worker.h"
#endif

namespace cheapib::render_replay {
namespace {

struct Value { const char* name; const char* value; };
constexpr Value kSet[] = {
    {"WWHD_CHEAPIB", "1"},
    {"WWHD_CHEAPIB_S2", "0xFFF"},
    {"WWHD_CHEAPIB_B1", "1"},
    {"WWHD_CHEAPIB_B1_SKIP", "1"},
    {"WWHD_DRAWOFF", "2"},
    {"WWHD_DRAWOFF_GPU_CAP", "1"},  // (drawoff/gpu_cap.h: no more in-betweens than the GPU draws in a step)
};

const char* g_reason = nullptr;

bool read_chosen() {
    if (const char* e = getenv("WWHD_RENDER_REPLAY")) return atoi(e) != 0;
    std::string value;
    return !hostui::get("renderReplay", value) || value != "0";  // default on (with the GPU cap, user 2026-11-20)
}

bool read_active() {
    const bool on = chosen();
    bool vulkan = false, worker = false;
#ifdef WWHD_HAS_VULKAN
    vulkan = render::vulkan();
    worker = vulkan && gfxvk::record_worker_requested();
#endif
    if (on && !vulkan) g_reason = "Not active: needs the Vulkan renderer.";
    else if (on && !worker) g_reason = "Not active: needs \"Record graphics on a second thread\" (Advanced).";
    const bool v = on && vulkan && worker;
    LOG("[render replay] faster rendering (replays unchanged draws): %s%s%s", v ? "on" : "off",
        getenv("WWHD_RENDER_REPLAY") ? " (WWHD_RENDER_REPLAY)" : (on ? "" : " (setting off)"),
        g_reason ? (vulkan ? " (no second-thread recording)" : " (not the Vulkan renderer)") : "");
    return v;
}

}  // namespace

bool chosen() {
    static const bool v = read_chosen();
    return v;
}

bool active() {
    static const bool v = read_active();
    return v;
}

const char* env(const char* name) {
    if (const char* e = getenv(name)) return e;
    if (!active()) return nullptr;
    for (const Value& s : kSet)
        if (!strcmp(s.name, name)) return s.value;
    return nullptr;
}

const char* inactive_reason() {
    active();
    return g_reason;
}

}  // namespace cheapib::render_replay
#endif
