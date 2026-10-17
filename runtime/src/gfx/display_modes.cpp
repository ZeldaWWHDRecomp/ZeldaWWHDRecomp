// GamePad screen modes and the TV window's layout (display_modes.h), shared by the AppKit host
// (display.mm) and the SDL host (gfx/vulkan/backend.cpp, overlay_sdl.cpp). No window system here:
// the hosts show and hide their windows, read their mouse and save the options.
//
// Debug / test environment (both hosts):
//   WWHD_DRC_MODE=window|pip|auto|off|gamepad   GamePad screen mode at start (not saved)
//   WWHD_DRC_PIP=br:0.25[:0.85]          overlay anchor (tl, tc, tr, cl, cc, cr, bl, bc, br, custom), size (fraction of the TV picture
//                                        width) and opacity at start (not saved)
//   WWHD_SCALE_FILTER=smooth|sharp|integer
//   WWHD_SIM_SCREEN=3024x1964            lay the TV picture out for a target of that size (present dumps and
//                                        touch mapping), e.g. to check a full-screen layout without going full screen
//   WWHD_TEST_TOUCH=3400-3410:0.9:0.85   a mouse press at (x, y) in the TV window (0..1 from top left) during
//                                        TV frames 3400..3410; mapped through the overlay like a real click
//   WWHD_TEST_DRC_MODE=3400:gamepad,3600:pip   switch the mode at those TV frames (as the settings overlay)
//   WWHD_VIEW_BUTTON=0|1                 the touch screens' view button (on by default on Android only)
//   WWHD_FULLSCREEN=0|1                  the TV window starts in full screen (1) or windowed (0), instead of as it
//                                        was left (that session's full screen is not saved; 1 takes over the screen!)
#include "display_modes.h"
#include "../platform/touch_buttons.h"

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
const char* const kAnchorNames[10] = {"tl", "tc", "tr", "cl", "cc", "cr", "bl", "bc", "br", "custom"};
const char* const kFilterNames[3] = {"smooth", "sharp", "integer"};

#ifdef __ANDROID__
// one surface, no GamePad window: the TV picture first; the GamePad-only view (with the game's
// Off-TV Play on Minus) and the overlay are a tap on the view button away
std::atomic<int> g_mode{kDrcOff};
#else
std::atomic<int> g_mode{kDrcWindow};
#endif
std::atomic<int> g_pip_anchor{kPipBR};
std::atomic<float> g_pip_x{0.5f}, g_pip_y{0.5f};
std::atomic<bool> g_pip_preview{false};
std::atomic<float> g_pip_size{0.25f};
std::atomic<float> g_pip_opacity{1.0f};
std::atomic<int> g_filter{kSmooth};
std::atomic<bool> g_shown{true};
std::atomic<bool> g_auto_pin{false};
std::atomic<double> g_auto_until{0};
static std::atomic<unsigned> g_gamepad_ui{0};
static std::atomic<double> g_plus_at{-1};
static std::atomic<int> g_pause_restore{-1};
static GamePadUiState gamepad_ui() {
    const unsigned bits = g_gamepad_ui.load();
    return {bool(bits & 1), bool(bits & 2), bool(bits & 4), bool(bits & 8)};
}
std::atomic<float> g_drc_aspect{854.0f / 480.0f};
std::atomic<bool> g_has_drc_window{false};
std::atomic<bool> g_second_screen_off{false};
std::atomic<bool> g_second_display_available{false};
static std::mutex g_second_screen_seed_mu;
static bool g_second_screen_loaded = false;
void seed_second_screen_off(bool off) {
    std::lock_guard<std::mutex> lk(g_second_screen_seed_mu);
    if (!g_second_screen_loaded) g_second_screen_off = off;
}
void load_second_screen_off(bool off) {
    std::lock_guard<std::mutex> lk(g_second_screen_seed_mu);
    g_second_screen_off = off;
    g_second_screen_loaded = true;
}

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
    if (m == kDrcWindow) return g_has_drc_window || g_second_display_available;
    return true;
}

void display_env_overrides() {
    if (const char* e = getenv("WWHD_DRC_MODE")) g_mode = find_name(kModeNames, kDrcModeCount, e, g_mode);
    if (const char* e = getenv("WWHD_DRC_PIP")) {
        char c[16] = {};
        float sz = g_pip_size, op = g_pip_opacity;
        if (sscanf(e, "%15[a-z]:%f:%f", c, &sz, &op) >= 1) {
            g_pip_anchor = find_name(kAnchorNames, 10, c, g_pip_anchor);
            g_pip_size = pip_clamp(sz, 0.1f, 0.5f, 0.25f);
            g_pip_opacity = pip_clamp(op, 0.2f, 1.0f, 1.0f);
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
bool drc_screen_off() { return g_second_screen_off; }
bool pip_shown_now() {
    if (g_pip_preview) return true;
    if (g_mode == kDrcPip) return g_shown;
    if (g_mode == kDrcAuto) {
        AutoPipDecision decision{g_auto_until.load()};
        return decision.shown(gamepad_ui(), g_auto_pin, display_now());
    }
    return false;
}
bool drc_screen_shown(bool drc_window_visible) {
    if (g_mode == kDrcWindow) return drc_window_visible;
    if (g_mode == kDrcGamePad) return true;
    if (g_mode == kDrcAuto) return g_auto_pin; // controls toggle the pin, even while a menu is visible
    return pip_shown_now();
}
void display_show_drc(bool on) {
    if (g_has_drc_window || g_second_display_available) g_second_screen_off = !on;
    if (g_mode == kDrcAuto) {
        g_auto_pin = on;
    } else {
        g_shown = on;
    }
}
void display_set_mode(int m) {
    g_pause_restore = -1;
    g_plus_at = -1;
    g_mode = m;
    if (g_has_drc_window || g_second_display_available) g_second_screen_off = m == kDrcOff;  // chosen while the second screen is there
    if (m == kDrcAuto) { g_auto_pin = false; g_auto_until = 0; g_gamepad_ui = 0; }
    else if (m != kDrcOff) g_shown = true;
}

// ---------------------------------------------------------------- GamePad screen while paused
static const bool g_pause_view = [] {
    const char* e = getenv("WWHD_DRC_PAUSE");
#ifdef __ANDROID__
    return !e || atoi(e) != 0;
#else
    return e && atoi(e) != 0;
#endif
}();
// Host-only pause view state, updated on the guest thread and read by the window hosts.
static bool pause_switchable(int mode) { return mode == kDrcPip || mode == kDrcOff; }
void display_plus_pressed() {
    // Only while a pause view can switch; the menu state determines when it resumes.
    if (g_pause_view && (pause_switchable(g_mode) || g_pause_restore >= 0)) g_plus_at = display_now();
}
int display_saved_mode() {
    const int restore = g_pause_restore;
    return restore >= 0 ? restore : int(g_mode);
}
bool display_gamepad_state_needed() {
    return g_mode == kDrcAuto || (g_pause_view && (g_plus_at >= 0 || g_pause_restore >= 0));
}
void display_gamepad_ui(GamePadUiState state) {
    g_gamepad_ui = unsigned(state.available) | (unsigned(state.menu) << 1) |
        (unsigned(state.off_tv) << 2) | (unsigned(state.switching) << 3);
    const double now = display_now();
    if (g_mode == kDrcAuto) {
        AutoPipDecision decision{g_auto_until.load()};
        decision.update(state, now);
        g_auto_until = decision.until;
        return; // auto always keeps menus in PiP, including when the pause preference is on
    }
    if (!g_pause_view) return;
    if (g_pause_restore >= 0) {
        if (g_mode != kDrcGamePad) { // a manually chosen view stays
            g_pause_restore = -1;
            g_plus_at = -1;
        } else if (!state.available || !state.menu || state.off_tv || state.switching) {
            g_mode = int(g_pause_restore);
            g_pause_restore = -1;
            g_plus_at = -1;
        }
    } else if (g_plus_at >= 0) {
        if (state.available && state.menu && !state.off_tv && !state.switching && pause_switchable(g_mode)) {
            g_pause_restore = int(g_mode);
            g_mode = kDrcGamePad;
            g_plus_at = -1;
        } else if (now - g_plus_at > 2.0) g_plus_at = -1;
    }
}

// Pure placement geometry is shared by the compositor, Settings diagram and
// drag input.
float pip_clamp(float v, float lo, float hi, float fallback) {
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}
int pip_load_anchor(const char *anchor, const char *legacy) {
    return find_name(kAnchorNames, 10, anchor, find_name(kAnchorNames, 9, legacy, kPipBR));
}
PipPosition pip_position() { return {g_pip_anchor.load(), g_pip_x.load(), g_pip_y.load()}; }
void pip_set_position(PipPosition p) {
    g_pip_x = pip_clamp(p.x, 0, 1, 0.5f);
    g_pip_y = pip_clamp(p.y, 0, 1, 0.5f);
    g_pip_anchor = std::clamp(p.anchor, 0, int(kPipCustom));
}
Box pip_box(Box tv, float aspect, float size, PipPosition p) {
    if (!std::isfinite(tv.w) || !std::isfinite(tv.h) || tv.w <= 0 || tv.h <= 0)
        return {};
    aspect = pip_clamp(aspect, 0.01f, 100, 854.0f / 480);
    float w = tv.w * pip_clamp(size, 0.1f, 0.5f, 0.25f), h = w / aspect;
    // Keep the requested width wherever it fits. Only the selected preset's
    // margins/HUD clearance may reduce it; Custom can use the whole TV picture.
    const float margin = std::min(tv.w, tv.h) * 0.02f;
    float maxHeight = tv.h;
    if (p.anchor >= 0 && p.anchor < kPipCustom) {
        maxHeight -= 2 * margin;
        if (p.anchor == kPipBR) maxHeight = tv.h * kPipRupeeBottom - margin;
        if (p.anchor == kPipTL) maxHeight = tv.h * (1 - kPipLeftHudBottom) - margin;
        if (p.anchor == kPipTR) maxHeight = tv.h * (1 - kPipRightHudBottom) - margin;
    }
    if (h > maxHeight) {
        w *= maxHeight / h;
        h = maxHeight;
    }
    const float mx = std::min(std::min(tv.w, tv.h) * 0.02f, (tv.w - w) / 2);
    const float my = std::min(std::min(tv.w, tv.h) * 0.02f, (tv.h - h) / 2);
    float x, y;
    if (p.anchor >= 0 && p.anchor < kPipCustom) {
        const int col = p.anchor % 3, row = p.anchor / 3;
        x = col == 0 ? mx : col == 1 ? (tv.w - w) / 2 : tv.w - w - mx;
        y = row == 0 ? my : row == 1 ? (tv.h - h) / 2 : tv.h - h - my;
        if (p.anchor == kPipBR)
            y = std::max(0.0f, tv.h * kPipRupeeBottom - h);
        if (p.anchor == kPipTL) y = std::max(my, tv.h * kPipLeftHudBottom);
        if (p.anchor == kPipTR) y = std::max(my, tv.h * kPipRightHudBottom);
    } else {
        x = tv.w * pip_clamp(p.x, 0, 1, 0.5f) - w / 2;
        y = tv.h * pip_clamp(p.y, 0, 1, 0.5f) - h / 2;
    }
    return {tv.x + std::clamp(x, 0.0f, tv.w - w), tv.y + std::clamp(y, 0.0f, tv.h - h), w, h};
}
PipPosition pip_place(Box tv, float aspect, float size, float x, float y, bool snap) {
    if (!std::isfinite(tv.w) || !std::isfinite(tv.h) || tv.w <= 0 || tv.h <= 0)
        return {kPipCustom, 0.5f, 0.5f};
    Box b = pip_box(tv, aspect, size, {kPipCustom, x, y});
    PipPosition p{kPipCustom, (b.x - tv.x + b.w / 2) / tv.w, (b.y - tv.y + b.h / 2) / tv.h};
    if (snap) {
        float best = 0.035f * std::min(tv.w, tv.h);
        for (int a = 0; a < kPipCustom; ++a) {
            Box q = pip_box(tv, aspect, size, {a, 0, 0});
            // Compare the requested centre before Custom's edge clamping:
            // a HUD-safe preset can be smaller than Custom at the same size.
            const float qx = (q.x - tv.x + q.w / 2) / tv.w;
            const float qy = (q.y - tv.y + q.h / 2) / tv.h;
            const float requested = std::hypot((pip_clamp(x, 0, 1, 0.5f) - qx) * tv.w,
                                               (pip_clamp(y, 0, 1, 0.5f) - qy) * tv.h);
            const float actual = std::hypot(b.x + b.w / 2 - q.x - q.w / 2,
                                            b.y + b.h / 2 - q.y - q.h / 2);
            const float d = std::min(requested, actual);
            if (d <= best) {
                best = d;
                p = {a, qx, qy};
            }
        }
    }
    return p;
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
        L.pip = pip_box(L.tv, pw / ph, g_pip_size, pip_position());
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

void display_input_plan(const PresentPlan& p) {
    std::lock_guard<std::mutex> lk(g_layout_mu);
    g_tv_layout={p.tv,p.pip,p.pip_on,p.drc_only,p.scale};
    g_button=p.button;
    g_tv_dw=p.dw;
    g_tv_dh=p.dh;
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
std::atomic<int> g_touch_buttons{touch_buttons::Automatic};
std::atomic<bool> g_touch_controller{false}, g_touch_controls{true};
static std::atomic<int64_t> g_touch_reveal_until{0};
static int64_t touch_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool touch_buttons_visible() {
    return touch_buttons::visible(g_touch_buttons, g_touch_controller, touch_time_ms() < g_touch_reveal_until);
}
void reveal_touch_buttons() { g_touch_reveal_until = touch_time_ms() + 5000; }
bool view_button_enabled() {
    static const bool on = [] {
        const char* e = getenv("WWHD_VIEW_BUTTON");
#ifdef __ANDROID__
        return !e || atoi(e) != 0;
#else
        return e && atoi(e) != 0;
#endif
    }();
    return on && touch_buttons_visible();
}
// a square of 9% of the window's shorter side in the top left corner (rhemfur's Android layout)
static Box view_button(float dw, float dh) {
    const float m = std::round(std::min(dw, dh) * 0.015f), s = std::round(std::min(dw, dh) * 0.09f);
    return {m, m, s, s};
}
bool view_button_hit(float nx, float ny) {
    if (!view_button_enabled()) return false;
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
    if (!g_pip_preview && view_button_enabled()) p.button = view_button(dw, dh);
    return p;
}

// Deterministic placement captures without manipulating a user's settings or pointer.
// WWHD_TEST_PIP=frame:anchor:size:opacity:x:y,... (requires WWHD_NO_HOST_INPUT).
static void test_pip(uint64_t frame) {
    struct Step { uint64_t frame; PipPosition position; float size, opacity; };
    static const std::vector<Step> steps = [] {
        std::vector<Step> result;
        const char* e = getenv("WWHD_NO_HOST_INPUT") ? getenv("WWHD_TEST_PIP") : nullptr;
        unsigned long long at; char anchor[16]; float size, opacity, x, y; int n;
        while (e && sscanf(e, "%llu:%15[a-z]:%f:%f:%f:%f%n", &at, anchor, &size, &opacity, &x, &y, &n) == 6) {
            result.push_back({at, {find_name(kAnchorNames, 10, anchor, kPipBR), x, y}, size, opacity});
            e += n;
            if (*e != ',') break;
            ++e;
        }
        return result;
    }();
    static size_t next = 0;
    while (next < steps.size() && frame >= steps[next].frame) {
        const auto& step = steps[next++];
        pip_set_position(step.position);
        g_pip_size = pip_clamp(step.size, 0.1f, 0.5f, 0.25f);
        g_pip_opacity = pip_clamp(step.opacity, 0.2f, 1.0f, 1);
        LOG("[display] test PiP: %s, %.0f%% size, %.0f%% opacity at frame %llu", kAnchorNames[step.position.anchor],
            step.size * 100, step.opacity * 100, (unsigned long long)frame);
    }
}

// start of a present (render thread): everything both renderers share
PresentPlan display_plan(bool have_tv, float tw, float th, bool have_drc, float pw, float ph, float layer_w, float layer_h,
                         uint64_t frame, bool publish_input) {
    test_touch(frame);
    test_pip(frame);
    if (have_drc) g_drc_aspect = pw / ph;
    // Preserve a requested separate-window mode across display removal. Until
    // that window returns, keep the GamePad accessible in the primary picture.
    bool window_fallback = g_mode == kDrcWindow && !g_has_drc_window && g_shown;
    bool pip = (g_pip_preview || ((g_mode == kDrcPip || g_mode == kDrcAuto) && pip_shown_now()) || window_fallback) && have_drc;
    bool drc_only = !g_pip_preview && (g_mode == kDrcGamePad ||
        (g_mode == kDrcAuto && gamepad_ui().available && gamepad_ui().off_tv)) && have_drc;
    // Releasing a connected Android display also suspends its single-screen
    // fallback (including a hidden GamePad-only or pinned automatic view).
    if (g_second_display_available && g_second_screen_off) pip = drc_only = false;
    float dw = 0, dh = 0;
    bool sim = sim_screen(&dw, &dh);
    if (!sim) dw = layer_w, dh = layer_h;
    if (dw >= 1 && dh >= 1) aspect::set_window_aspect(dw / dh);  // "Match window" (aspect.cpp): the TV window / screen shape
    Layout L;
    if (have_tv || drc_only) L = layout(dw, dh, have_tv ? tw : 0, have_tv ? th : 0, have_drc ? pw : 0, have_drc ? ph : 0, pip, drc_only);
    PresentPlan p = plan_from(L, dw, dh);
    if(publish_input) display_input_plan(p);
    p.sim = sim;
    p.pip_wanted = pip;
    return p;
}

PresentPlan display_plan_for(const PresentPlan& p, float dw, float dh, float tw, float th, float pw, float ph) {
    PresentPlan q = plan_from(layout(dw, dh, tw, th, pw, ph, p.pip_wanted, p.drc_only), dw, dh);
    q.sim = p.sim;
    q.pip_wanted = p.pip_wanted;
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
