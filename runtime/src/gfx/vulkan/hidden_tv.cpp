// The TV picture nobody sees (hidden_tv.h)
#include "hidden_tv.h"
#include "backend.h"
#include "stereo.h"
#include "reproj.h"
#include "runtime.h"
#include "present.h"
#include "../display_modes.h"
#include <vector>
#include "../headless_compose.h"
#include <cstdlib>

namespace gfxvk::hidden_tv {
namespace {
int mode() {
    static const int m = [] {
        const char* e = std::getenv("WWHD_VK_SKIP_HIDDEN_TV");
        const int v = e && *e ? std::atoi(e) : 1;  // on by default; WWHD_VK_SKIP_HIDDEN_TV=0 turns it off
        if (v) LOG("[hidden tv] on: the TV copy to the scan waits while no output shows the TV picture%s",
                   v == 2 ? " (test: frame dumps write the older TV scan)" : "");
        else LOG("[hidden tv] off (WWHD_VK_SKIP_HIDDEN_TV=0): the TV copy to the scan is recorded every frame");
        return v;
    }();
    return m;
}
struct Pending { bool active = false; uint32_t cb = 0; Surface* src = nullptr; uint64_t seq = 0; uint32_t addr = 0; } g_pending;
uint64_t g_deferred = 0, g_dropped = 0, g_late = 0, g_inexact = 0;
bool g_shownLast = true;  // the previous frame's layout showed the TV scan (the first frames: yes)
}  // namespace

bool on() { return mode() != 0; }
bool stale_dumps() { return mode() == 2; }

bool defer(uint32_t colorBuffer, Surface* source) {
    if (!on() || !source || stereoEye || stereo::frame.mode != stereo::Off || reproj::enabled() ||
        gfx::headless_compose::policy().enabled() || g_shownLast)
        return false;
    g_pending = {true, colorBuffer, source, source->writeSeq, source->addr};
    ++g_deferred;
    return true;
}
void layout_shown(bool shown) {
    if (shown != g_shownLast)
        LOG("[hidden tv] frame %llu: %s", (unsigned long long)R.frame,
            shown ? "an output shows the TV picture: copies recorded" : "no output shows the TV picture: TV copies wait");
    g_shownLast = shown;
}
bool pending() { return g_pending.active; }
bool shown_last() { return g_shownLast; }
Surface* take(bool& unchanged) {
    unchanged = false;
    if (!g_pending.active) return nullptr;
    g_pending.active = false;
    // the very surface the game copied, if it still exists (the game may point the same colour buffer at another
    // surface after its copy: in Off-TV Play it draws the GamePad picture into the TV buffer's memory, 1280x720)
    auto [first, last] = R.surfaces.equal_range(g_pending.addr);
    for (auto it = first; it != last; ++it)
        if (it->second.get() == g_pending.src) {
            Surface* s = g_pending.src;
            if (!s->image) return nullptr;
            unchanged = s->writeSeq == g_pending.seq;
            return s;
        }
    return nullptr;
}
void count_late(const char* why, bool exact) {
    ++g_late;
    if (!exact) ++g_inexact;
    if (!exact || g_late <= 20 || g_late % 600 == 0)
        LOG("[hidden tv] frame %llu: the waiting TV copy recorded for %s%s (late %llu, changed %llu, of %llu waiting)",
            (unsigned long long)R.frame, why, exact ? "" : ": THE TV PICTURE CHANGED OR WENT SINCE THE GAME'S COPY",
            (unsigned long long)g_late, (unsigned long long)g_inexact, (unsigned long long)g_deferred);
}
void end_frame() {
    if (!g_pending.active) return;
    g_pending.active = false;
    if (++g_dropped % 3600 == 1)
        LOG("[hidden tv] %llu TV copies not needed (frame %llu; %llu recorded late, %llu changed)", (unsigned long long)g_dropped,
            (unsigned long long)R.frame, (unsigned long long)g_late, (unsigned long long)g_inexact);
}
}  // namespace gfxvk::hidden_tv

namespace gfxvk {
std::vector<ComposeQuad> screen_quads_mono(Screen& screen, VkExtent2D target, int& filter);  // (present.cpp)
bool hidden_tv_scan_shown(bool drcWindow) {
    const Surface* tv = R.tv.scan.get();
    if (!tv || !tv->image) return true;  // (no TV scan yet: the next copies make one)
    for (Screen* s : {&R.tv, &R.drc}) {
        if (s == &R.drc && !drcWindow) continue;
        // (the box does not depend on the target's size for a zero box; a real size where there is one)
        VkExtent2D extent = s->swapExtent;
        if (!extent.width || !extent.height) extent = {1920, 1080};
        int filter = 0;
        for (const auto& q : screen_quads_mono(*s, extent, filter))
            if (q.image == tv && q.box.w > 0 && q.box.h > 0) return true;
    }
    return false;
}
}  // namespace gfxvk

