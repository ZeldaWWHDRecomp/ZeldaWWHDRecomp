// SPDX-License-Identifier: MPL-2.0
// The context buttons' runtime for the touch controls (context_touch.h has the Java pack, touch_play.h the
// rules): the options, touch play on the game's pad reads (items by tap, aim-only, auto put-away, the ZL
// latch), the golden rings, hiding the game's own button cluster while the context buttons show, and the
// export of the game's action words, item icons and Link's poses (link_poses.h) for the Android side.
//
// Options (settings.ini; tests: the WWHD_* variable named with each, which also keeps it from being saved):
//   touchContextButtons (WWHD_CONTEXT_BUTTONS)  on: the touch controls show only what does something now
//   touchItemTap (WWHD_ITEM_TAP), touchAimOnly (WWHD_AIM_ONLY), touchAutoPutAway (WWHD_AUTO_PUT_AWAY)
//   touchZlLatch (WWHD_ZL_LATCH)                0 off, 1 let go after a defeat (default), 2 next target
// All on by default; they act only while the player plays by touch (a controller gets the game's buttons).
// Each applies at once.
// Tests on the Mac: WWHD_TOUCH_PLAY=1 plays as if by touch with the on-screen controls shown (the pad's
// input, e.g. WWHD_TEST_VIRTUAL_PAD, counts as the touch pad's). WWHD_TOUCHPLAY_LOG=1 logs the latches,
// put-aways, golden rings and the context buttons on each change ("[touchplay] ...").
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0),
// ControlsView's context buttons and ui_icons.cpp's text export; reimplemented.
#include "context_touch.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "fast_forward.h"
#include "game_layouts.h"
#include "guest_addr.h"
#include "item_ring_icons.h"
#include "link_poses.h"
#include "mods.h"
#include "msbt_text.h"
#include "play_context.h"
#include "touch_play.h"
#include "../gfx/display_modes.h"
#include "../input.h"
#include "../overlay/hostui.h"
#include "../runtime.h"
#include "../screenshot.h"
#ifdef __ANDROID__
#include "../platform/phone_rumble.h"
#endif

static_assert(mods::touchplay::kBtnA == input::kA && mods::touchplay::kBtnB == input::kB && mods::touchplay::kBtnX == input::kX &&
              mods::touchplay::kBtnY == input::kY && mods::touchplay::kBtnR == input::kR && mods::touchplay::kBtnZL == input::kZL);

namespace mods {
namespace {
using namespace playctx;

bool log_on() {
    static const bool on = [] {
        const char* e = getenv("WWHD_TOUCHPLAY_LOG");
        return e && *e && *e != '0';
    }();
    return on;
}
bool test_touch() {
    static const bool on = [] {
        const char* e = getenv("WWHD_TOUCH_PLAY");
        return e && *e && *e != '0';
    }();
    return on;
}

struct Option {
    const char* env;
    const char* key;
    int def, max;
    std::atomic<int> value{-1};
    int get() {
        int v = value.load(std::memory_order_relaxed);
        if (v >= 0) return v;
        v = def;
        std::string s;
        if (const char* e = getenv(env); e && *e) v = atoi(e);
        else if (hostui::get(key, s) && !s.empty()) v = atoi(s.c_str());
        v = std::clamp(v, 0, max);
        value.store(v, std::memory_order_relaxed);
        return v;
    }
    void set(int v) {
        v = std::clamp(v, 0, max);
        value.store(v, std::memory_order_relaxed);
        if (!getenv(env)) hostui::set(key, std::to_string(v));
    }
};
Option g_context{"WWHD_CONTEXT_BUTTONS", "touchContextButtons", 1, 1};
Option g_item_tap{"WWHD_ITEM_TAP", "touchItemTap", 1, 1};
Option g_aim_only{"WWHD_AIM_ONLY", "touchAimOnly", 1, 1};
Option g_put_away{"WWHD_AUTO_PUT_AWAY", "touchAutoPutAway", 1, 1};
Option g_zl{"WWHD_ZL_LATCH", "touchZlLatch", touchplay::kLatchLetGo, touchplay::kLatchNext};

touchplay::Options play_options() {
    touchplay::Options o;
    o.item_tap = g_item_tap.get() != 0;
    o.aim_only = g_aim_only.get() != 0;
    o.auto_put_away = g_put_away.get() != 0;
    o.zl_latch = g_zl.get();
    return o;
}

// the on-screen controls are shown (Android), or a test plays as if
bool controls_shown() {
    if (test_touch()) return true;
#ifdef __ANDROID__
    return gfx::g_touch_controls.load() && gfx::touch_buttons_visible();
#else
    return false;
#endif
}
// the player plays by touch now (the last input came from the on-screen controls)
bool touch_play() {
    if (test_touch()) return true;
#ifdef __ANDROID__
    return controls_shown() && phone_rumble::last_input_touch.load(std::memory_order_relaxed);
#else
    return false;
#endif
}

void want_context() {
    static const bool wanted = (play_context_want(true), true);
    (void)wanted;
}

// ---- state: the pad reads (VPAD or KPAD) and the logic step (play_context_step) are the game thread;
// the JNI poll is the UI thread ----
std::mutex g_mu;
touchplay::Machine g_machine;
touchplay::RingTracker g_rings;
ctxbtn::Buttons g_buttons;  // as last decided (with aim-only)
uint32_t g_held = 0;
std::atomic<bool> g_hide_cluster{false};
std::atomic<int> g_assets_generation{0};

void log_buttons(const ctxbtn::Buttons& b) {
    static std::string last;
    char buf[200];
    ctxbtn::format(b, buf, sizeof buf);
    if (last == buf) return;
    last = buf;
    LOG("[touchplay] buttons: %s (step %llu)", buf, (unsigned long long)step());
}

// ---- the assets: the game's action words and item icons, exported for the Android side ----
// The action table (USA 10491350; dActionStatus code -> {message label, ...}, 8 bytes each, 0x4E codes)
// names each code's text in CommandGuide_00's message file ("T_ACT_Command_NN").
constexpr int kActionCodes = 0x4E;
std::mutex g_assets_mu;
std::string g_assets_dir;
bool g_assets_running = false;

bool write_file(const std::filesystem::path& p, const void* data, size_t n) {
    const std::filesystem::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(static_cast<const char*>(data), std::streamsize(n));
        if (!f) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    return !ec;
}

// the game's word per action code, "" where it has none
bool action_words(std::string words[kActionCodes], std::string& error) {
    game_layouts::Bytes msbt;
    if (!game_layouts::file("CommandGuide_00_msbt", "CommandGuide_00.msbt", msbt, error)) return false;
    const auto texts = msbt::texts(msbt);
    if (texts.empty()) {
        error = "CommandGuide_00.msbt has no texts";
        return false;
    }
    const uint32_t table = GD(0x10491350);
    int found = 0;
    for (int i = 0; i < kActionCodes; i++) {
        const uint32_t p = ld32(table + 8 * uint32_t(i));
        if (p < 0x10000000 || p >= 0x10800000) continue;  // (the labels are the game's constant strings)
        const char* label = reinterpret_cast<const char*>(mem::ptr(p));
        if (strncmp(label, "T_ACT_Command_", 14) != 0) continue;
        const auto it = texts.find(std::string(label, strnlen(label, 32)));
        if (it == texts.end() || it->second.find_first_not_of(' ') == std::string::npos) continue;  // (blank: no word)
        words[i] = it->second;
        found++;
    }
    if (!found) {
        error = "no action words matched the game's table";
        return false;
    }
    return true;
}

// the export (a worker thread): 1 done, 0 the pack isn't open yet, -1 failed
int export_assets(const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string error;
    std::string words[kActionCodes];
    if (!action_words(words, error)) {
        LOG("[touchplay] action words: %s", error.c_str());
        return error.find("not open yet") != std::string::npos ? 0 : -1;
    }
    std::string text;
    int nwords = 0;
    for (int i = 0; i < kActionCodes; i++) {
        if (words[i].empty()) continue;
        char head[8];
        snprintf(head, sizeof head, "%02X\t", i);
        text += head + words[i] + "\n";
        nwords++;
    }
    int icons = 0;
    for (int item = 0; item < 0xFF; item++) {
        const char* collect = item == 0x22 ? "15" : item == 0x78 ? "06" : item == 0x77 ? "16" : nullptr;
        const int f = collect ? 0 : item_ring_icons::frame_of(uint8_t(item));
        if (f < 0) continue;
        char spec[64];
        if (collect) snprintf(spec, sizeof spec, "BtnCollectIcon_00/CollectIcon118_%s^l", collect);
        else snprintf(spec, sizeof spec, "BtnItemIcon_00/Icon128_%02d^l", f);
        game_layouts::Image img;
        std::string e;
        if (!game_layouts::texture(spec, img, e)) continue;
        std::vector<uint8_t> png;
        if (!screenshot::encode_png_rgba(img.width, img.height, img.rgba.data(), png)) continue;
        char name[32];
        snprintf(name, sizeof name, "item_%02X.png", item);
        if (write_file(std::filesystem::path(dir) / name, png.data(), png.size())) icons++;
    }
    // Link in the pose of an action (link_poses.h), drawn from the game's 3D pack: pose_HH.png; without it
    // (the files can't be read) the buttons keep their words
    const int poses = link_poses::export_to(dir, screenshot::encode_png_rgba);
    if (!write_file(std::filesystem::path(dir) / "actions.txt", text.data(), text.size())) return -1;
    LOG("[touchplay] exported %d action words, %d item icons and %d Link poses to %s", nwords, icons, std::max(poses, 0), dir.c_str());
    return 1;
}
}  // namespace

// ---- options ----
bool context_buttons_on() { return g_context.get() != 0; }
void set_context_buttons_on(bool on) { g_context.set(on); }
bool item_tap_on() { return g_item_tap.get() != 0; }
void set_item_tap_on(bool on) { g_item_tap.set(on); }
bool aim_only_on() { return g_aim_only.get() != 0; }
void set_aim_only_on(bool on) { g_aim_only.set(on); }
bool auto_put_away_on() { return g_put_away.get() != 0; }
void set_auto_put_away_on(bool on) { g_put_away.set(on); }
int zl_latch_mode() { return g_zl.get(); }
void set_zl_latch_mode(int m) { g_zl.set(m); }

// ---- the pad reads (VPADRead / KPADReadEx, after the item ring's gate) ----
void context_touch_pad(input::PadState& p, bool repeat) {
    if (repeat) return;  // true 60's half passes repeat the full pass's buttons
    const bool touch = touch_play();
    if (touch) want_context();
    const PlayContext c = play_context();
    std::lock_guard<std::mutex> lk(g_mu);
    const int was_latched = g_machine.latched();
    const bool was_zl = g_machine.zl_latched();
    const uint32_t puts = g_machine.put_aways();
    touchplay::In in;
    in.buttons = p.buttons, in.lx = p.lx, in.ly = p.ly, in.touch = touch, in.step = step();
    const uint32_t before = p.buttons;
    p.buttons = g_machine.filter(in, c, play_options());
    if (log_on()) {
        if (g_machine.latched() != was_latched)
            LOG("[touchplay] item tap: %s (step %llu)", g_machine.latched() >= 0 ? "latched" : "let go", (unsigned long long)step());
        if (g_machine.zl_latched() != was_zl) LOG("[touchplay] ZL latch %s (step %llu)", g_machine.zl_latched() ? "on" : "off", (unsigned long long)step());
        if (g_machine.put_aways() != puts) LOG("[touchplay] put away: hand %04X (step %llu)", c.item_in_hand, (unsigned long long)step());
        static uint32_t last_in = 0, last_out = 0;  // each change of what touch play does to the pad
        if ((before != p.buttons || last_in != last_out) && (before != last_in || p.buttons != last_out))
            LOG("[touchplay] pad %04X -> game %04X (step %llu)", before, p.buttons, (unsigned long long)step());
        last_in = before, last_out = p.buttons;
    }
}

// ---- once per logic step, after the PlayContext was read (play_context.cpp) ----
void context_touch_step(const PlayContext& c) {
    ctxbtn::Options o;
    o.fast_forward = fast_forward() && fast_forward_button() == input::kZR;
    ctxbtn::Buttons b = ctxbtn::context_buttons(c, o);
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_aim_only.get() && g_machine.aim_latched()) touchplay::aim_only(b, c, g_machine.latched());
    g_rings.step(b, c, c.step);
    g_held = (g_machine.zl_latched() ? 1u << ctxbtn::kZL : 0) |
             (g_machine.latched() >= 0 ? 1u << (ctxbtn::kX + g_machine.latched()) : 0);
    const bool hide = context_buttons_on() && controls_shown() && b.active;
    if (log_on() && hide != g_hide_cluster.load()) LOG("[touchplay] the game's buttons %s", hide ? "hidden" : "shown");
    g_hide_cluster.store(hide, std::memory_order_relaxed);
    if (log_on()) {
        static int32_t rings = 0;
        if (g_rings.count() != rings) rings = g_rings.count(), LOG("[touchplay] golden rings on A (%d)", rings);
        if (touch_play()) log_buttons(b);
    }
    g_buttons = b;
}

bool context_touch_hide_cluster() { return g_hide_cluster.load(std::memory_order_relaxed); }
bool context_touch_aim_latched() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_machine.aim_latched();
}

void context_touch_pack(int32_t* out, int n) {
    want_context();
    ctxtouch::Extra e;
    ctxbtn::Buttons b;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        b = g_buttons;
        e.aim_latched = g_machine.aim_latched();
        e.zl_latched = g_machine.zl_latched();
        e.held = g_held;
        e.ring[ctxbtn::kA] = g_rings.count();
    }
    e.enabled = context_buttons_on();
    e.cluster_hidden = g_hide_cluster.load(std::memory_order_relaxed);
    e.item_names_a = item_names_on_hud();
    e.link_poses = link_poses::enabled();
    e.assets_generation = g_assets_generation.load(std::memory_order_relaxed);
    ctxtouch::pack(b, e, out, n);
}

// 0 not ready yet, -1 failed, > 0 the generation of the files in dir (re-exported when the dir changes or
// the pack the game opened changed: game_layouts drops its cache, and the export runs again)
int context_touch_assets(const std::string& dir) {
    std::lock_guard<std::mutex> lk(g_assets_mu);
    const int gen = g_assets_generation.load();
    if (dir == g_assets_dir && (gen != 0 || g_assets_running)) return g_assets_running ? 0 : gen;
    if (g_assets_running) return 0;
    g_assets_dir = dir;
    g_assets_running = true;
    g_assets_generation.store(0);
    std::thread([dir] {
        const int r = export_assets(dir);
        std::lock_guard<std::mutex> lk(g_assets_mu);
        g_assets_running = false;
        static int next = 0;
        g_assets_generation.store(r > 0 ? ++next : r < 0 ? -1 : 0);
        if (r == 0) g_assets_dir.clear();  // ask again: the pack wasn't open yet
    }).detach();
    return 0;
}

// tests on the Mac: WWHD_CONTEXT_ASSETS=<dir> exports once the game opened its pack (interp.cpp's frame
// boundary calls this; it returns at once otherwise)
void context_touch_service() {
    static const char* dir = getenv("WWHD_CONTEXT_ASSETS");
    static bool done = false;
    if (!dir || !*dir || done) return;
    static uint64_t last = 0;
    if (step() < last + 30) return;  // about once a second
    last = step();
    const int r = context_touch_assets(dir);
    if (r != 0) done = true;
}
}  // namespace mods

#ifdef __ANDROID__
#include <jni.h>
extern "C" JNIEXPORT jint JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeContextButtons(JNIEnv* env, jclass, jintArray out) {
    int32_t v[mods::ctxtouch::kPackLen];
    mods::context_touch_pack(v, mods::ctxtouch::kPackLen);
    if (out) {
        const jsize n = env->GetArrayLength(out);
        env->SetIntArrayRegion(out, 0, n < mods::ctxtouch::kPackLen ? n : mods::ctxtouch::kPackLen, reinterpret_cast<const jint*>(v));
    }
    return mods::ctxtouch::kPackVersion;
}
extern "C" JNIEXPORT jint JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeContextAssets(JNIEnv* env, jclass, jstring dir) {
    if (!dir) return -1;
    const char* s = env->GetStringUTFChars(dir, nullptr);
    if (!s) return -1;
    const std::string d(s);
    env->ReleaseStringUTFChars(dir, s);
    return mods::context_touch_assets(d);
}
#endif
