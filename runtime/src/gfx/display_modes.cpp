// GamePad screen modes and the TV window's layout (display_modes.h), shared by the AppKit host
// (display.mm) and the SDL host (gfx/vulkan/backend.cpp, overlay_sdl.cpp). No window system here:
// the hosts show and hide their windows, read their mouse and save the options.
//
// Debug / test environment (both hosts):
//   WWHD_DRC_MODE=window|pip|auto|off|gamepad   GamePad screen mode at start (not saved)
//   WWHD_DRC_PIP=br:0.25[:0.85]          overlay corner (tl, tr, bl, br), size (fraction of the TV picture
//                                        width) and opacity at start (not saved)
//   WWHD_SCALE_FILTER=smooth|sharp|integer
//   WWHD_SIM_SCREEN=3024x1964            lay the TV picture out for a target of that size (present dumps and
//                                        touch mapping), e.g. to check a full-screen layout without going full screen
//   WWHD_TEST_TOUCH=3400-3410:0.9:0.85   a mouse press at (x, y) in the TV window (0..1 from top left) during
//                                        TV frames 3400..3410; mapped through the overlay like a real click
//   WWHD_DRC_AUTO=0.12:4                 automatic mode: changed-area threshold and hold time in seconds
//   WWHD_DRC_AUTO_LOG=1                  log the automatic mode's change measurements
//   WWHD_TEST_DRC_MODE=3400:gamepad,3600:pip   switch the mode at those TV frames (as the settings overlay)
//   WWHD_VIEW_BUTTON=0|1                 the touch screens' view button (on by default on Android only)
//   WWHD_FULLSCREEN=0|1                  the TV window starts in full screen (1) or windowed (0), instead of as it
//                                        was left (that session's full screen is not saved; 1 takes over the screen!)
#include "display_modes.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "../aspect.h"
#include "input.h"
#include "runtime.h"

namespace gfx {

// ---------------------------------------------------------------- options
const char* const kModeNames[kDrcModeCount] = {"window", "pip", "auto", "off", "gamepad"};
const char* const kCornerNames[4] = {"tl", "tr", "bl", "br"};
const char* const kFilterNames[3] = {"smooth", "sharp", "integer"};

#ifdef __ANDROID__
// one surface, no GamePad window: the TV picture first; the GamePad-only view (with the game's
// Off-TV Play on Minus) and the overlay are a tap on the view button away
std::atomic<int> g_mode{kDrcOff};
#else
std::atomic<int> g_mode{kDrcWindow};
#endif
std::atomic<int> g_corner{3};
std::atomic<float> g_pip_size{0.25f};
std::atomic<float> g_pip_opacity{1.0f};
std::atomic<int> g_filter{kSmooth};
std::atomic<bool> g_shown{true};
std::atomic<bool> g_auto_pin{false};
std::atomic<double> g_auto_until{0};
std::atomic<float> g_drc_aspect{854.0f / 480.0f};
std::atomic<bool> g_has_drc_window{false};

int find_name(const char* const* names, int n, const char* s, int def) {
    for (int i = 0; i < n; i++)
        if (s && !strcmp(s, names[i])) return i;
    return def;
}
double display_now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool drc_mode_offered(int m) {
    if (m < 0 || m >= kDrcModeCount) return false;
    if (m == kDrcWindow) return g_has_drc_window;
    return true;
}

void display_env_overrides() {
    if (const char* e = getenv("WWHD_DRC_MODE")) g_mode = find_name(kModeNames, kDrcModeCount, e, g_mode);
    if (const char* e = getenv("WWHD_DRC_PIP")) {
        char c[8] = {};
        float sz = g_pip_size, op = g_pip_opacity;
        if (sscanf(e, "%2[a-z]:%f:%f", c, &sz, &op) >= 1) {
            g_corner = find_name(kCornerNames, 4, c, g_corner);
            g_pip_size = std::clamp(sz, 0.1f, 0.5f);
            g_pip_opacity = std::clamp(op, 0.2f, 1.0f);
        }
    }
    if (const char* e = getenv("WWHD_SCALE_FILTER")) g_filter = find_name(kFilterNames, 3, e, g_filter);
}

bool display_fullscreen_env() {
    const char* e = getenv("WWHD_FULLSCREEN");
    return e && *e;
}
bool display_start_fullscreen(bool saved, bool hidden_windows) {
    const bool env = display_fullscreen_env();
    const bool on = env ? atoi(getenv("WWHD_FULLSCREEN")) != 0 : saved;
    const char* why = env ? "WWHD_FULLSCREEN" : "as it was left";
    if (!on) {
        LOG("[display] TV window starts windowed (%s)", env ? "WWHD_FULLSCREEN=0" : "as it was left");
        return false;
    }
    // test runs: never take over the user's screen unless asked for with WWHD_FULLSCREEN=1
    const bool test = getenv("WWHD_NO_HOST_INPUT") != nullptr;
    const bool apply = !hidden_windows && (!test || env);
    LOG("[display] TV window starts in full screen (%s)%s", why,
        apply ? "" : hidden_windows ? "; hidden windows: not switched" : "; test run: not switched");
    return apply;
}

int display_test_mode(uint64_t frame) {
    struct Switch { uint64_t at; int mode; };
    static const std::vector<Switch> script = [] {
        std::vector<Switch> v;
        if (const char* e = getenv("WWHD_TEST_DRC_MODE")) {
            unsigned long long at; char name[16]; int n;
            while (sscanf(e, "%llu:%15[a-z]%n", &at, name, &n) == 2) {
                v.push_back({at, find_name(kModeNames, kDrcModeCount, name, -1)});
                e += n;
                if (*e != ',') break;
                e++;
            }
        }
        return v;
    }();
    static size_t i = 0;
    if (i >= script.size() || frame < script[i].at) return -1;
    LOG("[display] test: GamePad screen mode %s at frame %llu", script[i].mode >= 0 ? kModeNames[script[i].mode] : "?",
        (unsigned long long)frame);
    return script[i++].mode;
}

// ---------------------------------------------------------------- mode state
bool drc_window_wanted() { return g_mode == kDrcWindow && g_shown; }
bool pip_shown_now() {
    if (g_mode == kDrcPip) return g_shown;
    if (g_mode == kDrcAuto) return g_auto_pin || display_now() < g_auto_until;
    return false;
}
bool drc_screen_shown(bool drc_window_visible) {
    if (g_mode == kDrcWindow) return drc_window_visible;
    if (g_mode == kDrcGamePad) return true;
    return pip_shown_now();
}
void display_show_drc(bool on) {
    if (g_mode == kDrcAuto) {
        g_auto_pin = on;
        if (!on) g_auto_until = 0;
    } else {
        g_shown = on;
    }
}
void display_set_mode(int m) {
    g_mode = m;
    if (m == kDrcAuto) g_auto_pin = false;
    else if (m != kDrcOff) g_shown = true;
}
void display_touched() { g_auto_until = std::max<double>(g_auto_until, display_now() + 2.0); }

// ---------------------------------------------------------------- GamePad screen while paused
static const bool g_pause_view = [] {
    const char* e = getenv("WWHD_DRC_PAUSE");
#ifdef __ANDROID__
    return !e || atoi(e) != 0;
#else
    return e && atoi(e) != 0;
#endif
}();
static std::atomic<double> g_plus_at{-1};      // display_now() of the last +, -1: none pending
// the view to go back to (-1: not switched) and the time of the switch: written by the sampling
// (render thread; Metal: a completion handler), read by the hosts when they save the view
static std::atomic<int> g_pause_restore{-1};
static std::atomic<double> g_pause_since{0};
static bool pause_switchable(int mode) { return mode == kDrcPip || mode == kDrcOff || mode == kDrcAuto; }
void display_plus_pressed() {
    // only when the view can switch (or the switch is up: + resumes); other views never sample
    if (g_pause_view && (pause_switchable(g_mode) || g_pause_restore >= 0)) g_plus_at = display_now();
}
int display_saved_mode() {
    const int restore = g_pause_restore;
    return restore >= 0 ? restore : int(g_mode);
}
// tv_change: share of the TV signature's cells that changed since the last sample (-1: unknown);
// tv_dark: the TV picture is (nearly) black
static void pause_view_update(float tv_change, bool tv_dark) {
    static int still = 0, moving = 0;
    if (g_pause_restore < 0) {
        const double plus = g_plus_at;
        if (!g_pause_view || plus < 0) return;
        if (display_now() - plus > 2.0) {  // + did not pause the game (or the picture never settled)
            g_plus_at = -1;
            still = 0;
            return;
        }
        // a still picture after +; not a black one (a fade to black, a loading screen)
        still = tv_change >= 0 && tv_change < 0.01f && !tv_dark ? still + 1 : 0;
        const int mode = g_mode;
        if (still >= 2 && pause_switchable(mode)) {
            g_pause_since = display_now();
            g_pause_restore = mode;
            g_mode = kDrcGamePad;
            g_plus_at = -1;
            still = moving = 0;
            LOG("[display] paused: GamePad screen shown");
        }
        return;
    }
    if (g_mode != kDrcGamePad) {  // another view was chosen meanwhile: it stays
        g_pause_restore = -1;
        g_plus_at = -1;
        return;
    }
    auto resume = [&](const char* why) {
        g_mode = int(g_pause_restore);
        g_pause_restore = -1;
        g_plus_at = -1;  // (the + that resumed does not start another wait)
        moving = still = 0;
        LOG("[display] resumed (%s): back to the TV picture", why);
    };
    const double now = display_now(), plus = g_plus_at, since = g_pause_since;
    if (plus >= 0) {
        if (plus > since + 0.3) return resume("+");  // + again: the usual way out of the pause
        g_plus_at = -1;  // a + right after the switch: ignored (and no more sampling every frame)
    }
    // the pause's own transition (the TV picture dims) is not the game moving again
    if (now - since < 0.7) {
        moving = 0;
        return;
    }
    // the game resumed however the menu was left (B, after saving): the TV picture moves again, in
    // 3 samples in a row (a calm scene changes little: 2% of the cells)
    moving = tv_change > 0.02f ? moving + 1 : 0;
    if (moving >= 3) resume("the TV picture moves");
}

// ---------------------------------------------------------------- layout
static Box fit(float dw, float dh, float tw, float th, float* scale) {
    float s = std::min(dw / tw, dh / th);  // scale to fit: bars only when the aspect ratios differ
    if (g_filter == kInteger && s >= 1) s = floorf(s + 1e-3f);
    *scale = s;
    float w = roundf(tw * s), h = roundf(th * s);
    return {floorf((dw - w) / 2), floorf((dh - h) / 2), w, h};
}

Layout layout(float dw, float dh, float tw, float th, float pw, float ph, bool pip_on, bool drc_only) {
    Layout L;
    if (dw <= 0 || dh <= 0) return L;
    if (drc_only && pw > 0 && ph > 0) {
        // GamePad only: its picture where the TV picture would be, touchable like the overlay
        L.pip = fit(dw, dh, pw, ph, &L.scale);
        L.pip_on = L.drc_only = true;
        return L;
    }
    if (tw <= 0 || th <= 0) return L;
    L.tv = fit(dw, dh, tw, th, &L.scale);
    if (pip_on && pw > 0 && ph > 0) {
        // the GamePad picture in a corner of the TV picture
        float ow = roundf(L.tv.w * g_pip_size), oh = roundf(ow * ph / pw);
        float m = roundf(std::min(L.tv.w, L.tv.h) * 0.02f);
        int c = g_corner;
        L.pip = {(c & 1) ? L.tv.x + L.tv.w - ow - m : L.tv.x + m, (c & 2) ? L.tv.y + L.tv.h - oh - m : L.tv.y + m, ow, oh};
        L.pip_on = true;
    }
    return L;
}

// the last TV composition, for mapping clicks into the overlay (main thread reads, render thread writes)
static std::mutex g_layout_mu;
static Layout g_tv_layout;
static Box g_button;
static float g_tv_dw = 0, g_tv_dh = 0;

Box display_layout(float dw, float dh, float tw, float th) { return layout(dw, dh, tw, th, 0, 0, false).tv; }

static bool sim_screen(float* w, float* h) {
    static float sw = 0, sh = 0;
    static bool parsed = [] {
        if (const char* e = getenv("WWHD_SIM_SCREEN")) sscanf(e, "%fx%f", &sw, &sh);
        return true;
    }();
    (void)parsed;
    if (sw <= 0 || sh <= 0) return false;
    *w = sw;
    *h = sh;
    return true;
}

bool overlay_hit(float nx, float ny, float* tx, float* ty, bool clamp_outside) {
    std::lock_guard<std::mutex> lk(g_layout_mu);
    const Layout& L = g_tv_layout;
    if (!L.pip_on || L.pip.w <= 0) return false;
    float x = (nx * g_tv_dw - L.pip.x) / L.pip.w, y = (ny * g_tv_dh - L.pip.y) / L.pip.h;
    if (!clamp_outside && (x < 0 || x > 1 || y < 0 || y > 1)) return false;
    *tx = std::clamp(x, 0.0f, 1.0f);
    *ty = std::clamp(y, 0.0f, 1.0f);
    return true;
}

bool main_picture(float* x, float* y, float* w, float* h) {
    std::lock_guard<std::mutex> lk(g_layout_mu);
    const Layout& L = g_tv_layout;
    const Box& b = L.drc_only ? L.pip : L.tv;
    if (g_tv_dw <= 0 || g_tv_dh <= 0 || b.w <= 0 || b.h <= 0) return false;
    *x = b.x / g_tv_dw, *y = b.y / g_tv_dh, *w = b.w / g_tv_dw, *h = b.h / g_tv_dh;
    return true;
}

// ---------------------------------------------------------------- view button (touch screens)
bool view_button_enabled() {
    static const bool on = [] {
        const char* e = getenv("WWHD_VIEW_BUTTON");
#ifdef __ANDROID__
        return !e || atoi(e) != 0;
#else
        return e && atoi(e) != 0;
#endif
    }();
    return on;
}
// a square of 9% of the window's shorter side in the top left corner (rhemfur's Android layout)
static Box view_button(float dw, float dh) {
    const float m = std::round(std::min(dw, dh) * 0.015f), s = std::round(std::min(dw, dh) * 0.09f);
    return {m, m, s, s};
}
bool view_button_hit(float nx, float ny) {
    std::lock_guard<std::mutex> lk(g_layout_mu);
    const float x = nx * g_tv_dw, y = ny * g_tv_dh;
    return g_button.w > 0 && x >= g_button.x && y >= g_button.y && x < g_button.x + g_button.w && y < g_button.y + g_button.h;
}
int next_view() {
    // picture-in-picture, GamePad only, TV only (the views of a phone held in landscape)
    static const int cycle[] = {kDrcPip, kDrcGamePad, kDrcOff};
    int at = -1;
    for (int i = 0; i < 3; i++)
        if (cycle[i] == g_mode) at = i;
    for (int k = 1; k <= 3; k++) {
        int m = cycle[(at + k + 3) % 3];
        if (drc_mode_offered(m)) return m;
    }
    return g_mode;
}

// ---------------------------------------------------------------- automatic overlay
// The overlay comes up for a few seconds when a large part of the GamePad picture changes between two
// samples (a menu opens, the screen switches). Every 4th frame the renderer reduces both pictures to
// 32x18 display-encoded luma. Not counted:
//  - small changes (the map's position marker, blinking cursors),
//  - fades to or from black and plain brightness shifts (scene changes),
//  - a GamePad picture that mirrors the TV (title screen and other moments where the game shows the
//    same picture on both screens; nothing to look at on the GamePad).
void display_auto_signature(const std::vector<float>& cur_in, const std::vector<float>* tv, uint64_t frame) {
    const uint32_t kSigN = kSignatureW * kSignatureH;
    static std::mutex mu;
    static std::vector<float> prev;
    std::lock_guard<std::mutex> lk(mu);
    {
        // the TV picture's change, for the GamePad screen while paused
        static std::vector<float> prev_tv;
        float tv_change = -1, tv_mean = 1;
        if (tv && tv->size() == kSigN) {
            tv_mean = 0;
            for (float x : *tv) tv_mean += x;
            tv_mean /= kSigN;
            if (prev_tv.size() == kSigN) {
                uint32_t changed = 0;
                for (uint32_t i = 0; i < kSigN; i++)
                    if (fabsf((*tv)[i] - prev_tv[i]) > 0.03f) changed++;
                tv_change = (float)changed / kSigN;
            }
            prev_tv = *tv;
        }
        pause_view_update(tv_change, tv_mean < 0.04f);  // (black: as the automatic overlay's "dark")
    }
    if (g_mode != kDrcAuto) { prev.clear(); return; }
    static float thresh = 0.12f, hold = 4.0f;
    static bool parsed = [] {
        if (const char* e = getenv("WWHD_DRC_AUTO")) sscanf(e, "%f:%f", &thresh, &hold);
        return true;
    }();
    (void)parsed;
    static const bool log = getenv("WWHD_DRC_AUTO_LOG") != nullptr;
    std::vector<float> cur = cur_in;
    if (cur.size() != kSigN) return;
    float mean = 0, mirror_diff = 1;
    for (float x : cur) mean += x;
    mean /= kSigN;
    if (tv && tv->size() == kSigN) {
        const std::vector<float>& t = *tv;
        mirror_diff = 0;
        for (uint32_t i = 0; i < kSigN; i++) mirror_diff += fabsf(t[i] - cur[i]);
        mirror_diff /= kSigN;
    }
    if (prev.size() == kSigN) {
        float pmean = 0;
        uint32_t changed = 0;
        for (uint32_t i = 0; i < kSigN; i++) {
            pmean += prev[i];
            if (fabsf(cur[i] - prev[i]) > 0.08f) changed++;
        }
        pmean /= kSigN;
        float shift = mean - pmean, frac = (float)changed / kSigN;
        uint32_t against = 0;  // cells that changed other than by the overall brightness shift
        for (uint32_t i = 0; i < kSigN; i++)
            if (fabsf((cur[i] - prev[i]) - shift) > 0.08f) against++;
        float content = (float)against / kSigN;
        bool dark = mean < 0.04f || pmean < 0.04f;
        bool mirror = mirror_diff < 0.06f;
        bool event = frac > thresh && content > thresh && !dark && !mirror;
        if (event) {
            double until = display_now() + hold;
            if (until > g_auto_until) g_auto_until = until;
        }
        if (log && (frac > 0.01f || event))
            LOG("[display] auto: frame %llu changed %.3f (content %.3f) mean %.3f vs TV %.3f%s%s%s", (unsigned long long)frame, frac,
                content, mean, mirror_diff, dark ? " dark" : "", mirror ? " mirror" : "", event ? " -> show" : "");
    }
    prev = std::move(cur);
}

// ---------------------------------------------------------------- scripted touch (tests)
struct TestTouch { uint64_t from, to; float x, y; };
static void test_touch(uint64_t frame) {
    static const std::vector<TestTouch> script = [] {
        std::vector<TestTouch> v;
        if (const char* e = getenv("WWHD_TEST_TOUCH")) {
            unsigned long long a, b; float x, y; int n;
            while (sscanf(e, "%llu-%llu:%f:%f%n", &a, &b, &x, &y, &n) == 4) {
                v.push_back({a, b, x, y});
                e += n;
                if (*e != ',') break;
                e++;
            }
        }
        return v;
    }();
    static bool down = false;
    bool any = false;
    for (auto& t : script)
        if (frame >= t.from && frame <= t.to) {
            float tx, ty;
            any = true;
            if (overlay_hit(t.x, t.y, &tx, &ty)) {
                if (!down) LOG("[display] test touch: window (%.3f, %.3f) -> GamePad (%.3f, %.3f) = pixel (%.0f, %.0f) at frame %llu",
                               t.x, t.y, tx, ty, tx * 854, ty * 480, (unsigned long long)frame);
                input::set_touch(true, tx, ty);
                down = true;
            } else if (!down) {
                LOG("[display] test touch: window (%.3f, %.3f) misses the GamePad overlay at frame %llu", t.x, t.y,
                    (unsigned long long)frame);
                down = true;  // log once
                input::set_touch(false, 0, 0);
            }
        }
    if (!any && down) {
        input::set_touch(false, 0, 0);
        down = false;
    }
}

// ---------------------------------------------------------------- present
static std::mutex g_dump_mu;
static std::vector<std::string> g_present_dumps;
// write the composed TV window picture (and, in window mode, the GamePad window's) at the next present
void request_present_dump(const std::string& path) {
    std::lock_guard<std::mutex> lk(g_dump_mu);
    g_present_dumps.push_back(path);
}
std::vector<std::string> display_take_present_dumps() {
    std::vector<std::string> dumps;
    std::lock_guard<std::mutex> lk(g_dump_mu);
    dumps.swap(g_present_dumps);
    return dumps;
}

static PresentPlan plan_from(const Layout& L, float dw, float dh) {
    PresentPlan p;
    p.dw = dw;
    p.dh = dh;
    p.tv = L.tv;
    p.pip = L.pip;
    p.pip_on = L.pip_on;
    p.drc_only = L.drc_only;
    p.scale = L.scale;
    p.pip_opacity = L.drc_only ? 1.0f : g_pip_opacity.load();
    p.filter = g_filter;
    p.drc_window = g_mode == kDrcWindow && g_has_drc_window && g_shown;
    if (view_button_enabled()) p.button = view_button(dw, dh);
    return p;
}

// start of a present (render thread): everything both renderers share
PresentPlan display_plan(bool have_tv, float tw, float th, bool have_drc, float pw, float ph, float layer_w, float layer_h,
                         uint64_t frame) {
    test_touch(frame);
    if (have_drc) g_drc_aspect = pw / ph;
    bool pip = (g_mode == kDrcPip || g_mode == kDrcAuto) && pip_shown_now() && have_drc;
    bool drc_only = g_mode == kDrcGamePad && have_drc;
    float dw = 0, dh = 0;
    bool sim = sim_screen(&dw, &dh);
    if (!sim) dw = layer_w, dh = layer_h;
    if (dw >= 1 && dh >= 1) aspect::set_window_aspect(dw / dh);  // "Match window" (aspect.cpp): the TV window / screen shape
    Layout L;
    if (have_tv || drc_only) L = layout(dw, dh, have_tv ? tw : 0, have_tv ? th : 0, have_drc ? pw : 0, have_drc ? ph : 0, pip, drc_only);
    PresentPlan p = plan_from(L, dw, dh);
    {
        std::lock_guard<std::mutex> lk(g_layout_mu);
        g_tv_layout = L;
        g_button = p.button;
        g_tv_dw = dw;
        g_tv_dh = dh;
    }
    p.sim = sim;
    p.pip_wanted = pip;
    // Sampling waits for the GPU on those frames (the signatures are read back at once): the
    // automatic overlay samples every 4th frame; the GamePad screen while paused only while there is
    // something to see, never in plain gameplay: every frame for at most 2 s after + (whether the game
    // paused: the switch follows within 2-3 frames), every 4th frame while it shows the GamePad
    // screen (the game is paused, whether it resumed).
    const bool plusWaits = g_pause_view && g_plus_at >= 0, pauseShown = g_pause_view && g_pause_restore >= 0;
    p.sample_auto = have_drc && (plusWaits || ((g_mode == kDrcAuto || pauseShown) && frame % 4 == 0));
    return p;
}

PresentPlan display_plan_for(const PresentPlan& p, float dw, float dh, float tw, float th, float pw, float ph) {
    PresentPlan q = plan_from(layout(dw, dh, tw, th, pw, ph, p.pip_wanted, p.drc_only), dw, dh);
    q.sim = p.sim;
    q.pip_wanted = p.pip_wanted;
    q.sample_auto = p.sample_auto;
    q.drc_window = p.drc_window;
    q.button_dot = p.button_dot;
    return q;
}

void display_log_present_dump(const std::string& path, const PresentPlan& p, float tw, float th) {
    char pip[96] = "";
    if (p.pip_on)
        snprintf(pip, sizeof pip, ", GamePad %s at %.0f,%.0f %.0fx%.0f", p.drc_only ? "picture" : "overlay", p.pip.x, p.pip.y, p.pip.w,
                 p.pip.h);
    LOG("[display] present dump %s: target %.0fx%.0f, TV %.0fx%.0f at %.0f,%.0f %.0fx%.0f (scale %.3f, %s)%s", path.c_str(), p.dw, p.dh,
        tw, th, p.tv.x, p.tv.y, p.tv.w, p.tv.h, p.scale, kFilterNames[p.filter], pip);
}

}  // namespace gfx
