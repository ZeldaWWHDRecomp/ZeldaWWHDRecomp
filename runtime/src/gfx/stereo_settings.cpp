// render::load_saved_stereo (renderer.h): the 3D settings the overlay saved, for every renderer offering stereo
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#include "renderer.h"
#include "runtime.h"
#include "stereo.h"
#include "../overlay/hostui.h"

namespace render {
void load_saved_stereo() {
    std::string v;
    auto num = [&](const char* key) { return ::hostui::get(key, v) ? std::optional<float>(float(atof(v.c_str()))) : std::nullopt; };
    if (auto x = num("stereoMode")) stereo::set_mode(int(*x));
    if (auto x = num("stereoDepth")) stereo::set_depth(*x);
    if (auto x = num("stereoConvergence")) stereo::set_convergence(*x);
    if (auto x = num("stereoSwap")) stereo::requestedSwap = *x != 0;
    if (auto x = num("stereoHudDistance")) stereo::set_hud_distance(*x);
    if (auto x = num("stereoBehindGlass")) stereo::requestedBehindGlass = *x != 0;
    if (auto x = num("stereoHudAuto")) stereo::requestedHudAuto = *x != 0;
    // tests: WWHD_STEREO_DEPTH, WWHD_STEREO_HUD (HUD distance), WWHD_STEREO_HUD_AUTO and WWHD_STEREO_GLASS (0/1)
    // override the saved values
    if (const char* e = getenv("WWHD_STEREO_DEPTH")) stereo::set_depth(float(atof(e)));
    if (const char* e = getenv("WWHD_STEREO_HUD")) stereo::set_hud_distance(float(atof(e)));
    if (const char* e = getenv("WWHD_STEREO_GLASS")) stereo::requestedBehindGlass = atoi(e) != 0;
    if (const char* e = getenv("WWHD_STEREO_HUD_AUTO")) stereo::requestedHudAuto = atoi(e) != 0;
    if (const char* e = getenv("WWHD_STEREO")) {
        static const char* const names[] = {"off", "half-sbs", "full-sbs", "tab"};
        int mode = *e >= '0' && *e <= '9' ? atoi(e) : -1;
        for (int i = 0; i < stereo::Count; i++)
            if (!strcasecmp(e, names[i])) mode = i;
        if (mode < 0 || mode >= stereo::Count) LOG("[stereo] WWHD_STEREO=%s: not a mode (off, half-sbs, full-sbs, tab)", e);
        else stereo::set_mode(mode);
    }
    stereo::latch();
}

void stereo_test_switch(uint64_t frame) {
    static const std::vector<std::pair<uint64_t, int>> at = [] {
        std::vector<std::pair<uint64_t, int>> v;
        unsigned long long f;
        int mode, n;
        for (const char* p = getenv("WWHD_TEST_STEREO_AT"); p && sscanf(p, "%llu:%d%n", &f, &mode, &n) == 2; p += n + (p[n] == ',')) {
            v.push_back({f, mode});
            if (!p[n]) break;
        }
        return v;
    }();
    static size_t next = 0;
    if (next < at.size() && frame >= at[next].first) {
        stereo::set_mode(at[next].second);
        LOG("[stereo] test: mode %d from frame %llu", at[next].second, (unsigned long long)at[next].first);
        next++;
    }
}
}  // namespace render
