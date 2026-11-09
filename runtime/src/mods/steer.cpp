// SPDX-License-Identifier: MPL-2.0
// Camera extras (controls plan P7; rules in steer.h): the walking direction kept while the camera turns
// (setStickData's two `sth r9, 0x6930(r31)`, @023F35BC and @023F3610: r9 = stick angle r10 + camera angle r3),
// the camera swinging behind Link (fed to camera.cpp's swipe path in degrees), the direct camera's boat speed
// (camera.cpp manual_speed) and the pinch for the field of view (Android, nativePinch).
//
// Options (settings.ini; tests: the WWHD_* variable named with each, which also keeps it from being saved):
//   steerMode (WWHD_STEER_MODE)          0 as the game (default), 1 kept while the player turns the camera,
//                                        2 kept while the stick is held
//   cameraSwing (WWHD_CAMERA_SWING)      the camera swings behind Link while he runs (default off)
//   directBoatSpeed (WWHD_DIRECT_BOAT)   the direct camera turns as fast on the boat as on foot (default on)
//   touchPinchFov (WWHD_PINCH_FOV)       Android: two fingers pinch the field of view (default on)
// WWHD_STEER_LOG=1 logs the camera, Link's facing and the kept angle ("[steer] ...").
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0); reimplemented.
#include "steer.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>

#include "guest_addr.h"
#include "mods.h"
#include "play_context.h"
#include "../aspect.h"
#include "../overlay/hostui.h"
#include "../port_points.h"
#include "../runtime.h"

namespace mods {
namespace {
bool log_on() {
    static const bool on = [] {
        const char* e = getenv("WWHD_STEER_LOG");
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
Option g_mode{"WWHD_STEER_MODE", "steerMode", steer::kOff, steer::kStickHeld};
Option g_swing{"WWHD_CAMERA_SWING", "cameraSwing", 0, 1};
Option g_boat{"WWHD_DIRECT_BOAT", "directBoatSpeed", 1, 1};
Option g_pinch{"WWHD_PINCH_FOV", "touchPinchFov", 1, 1};

steer::Options options() {
    steer::Options o;
    o.mode = g_mode.get();
    o.swing = g_swing.get() != 0;
    return o;
}

constexpr uint32_t kStickDistance = 0x6A08, kFace = 0x322;  // daPy_lk_c: mStickDistance, shape_angle.y
bool targeting(uint32_t proc) { return proc >= 0x07 && proc <= 0x0A; }  // ATN_MOVE .. SIDE_STEP

std::mutex g_mu;
steer::Direction g_direction;
steer::Swing g_swing_rule;
// what the swing needs from the walking side (the last setStickData step)
std::atomic<uint64_t> g_active_step{~0ull};
std::atomic<int> g_cam{0}, g_face{0};
std::atomic<float> g_dist{0};
std::atomic<uint64_t> g_swing_step{~0ull};  // the last step the swing turned the camera

void walk(Cpu* c) {
    const uint32_t link = c->r[31];
    const int16_t cam = int16_t(c->r[3]), stick = int16_t(c->r[10]);
    const steer::Options o = options();
    if (o.mode == steer::kOff) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_direction = {};
        return;
    }
    const float dist = u32_as_f32(ld32(link + kStickDistance));
    const uint32_t game_info = GD(0x1046F0B0);
    const bool active = dist > 0.05f && ld8(game_info + playctx::kEventMode) == 0 &&
                        !(ld32(link + playctx::kLinkModeFlg) & playctx::kModeInShip) && !targeting(ld32(link + playctx::kLinkCurProc));
    const uint64_t now = step();
    const uint64_t sw = g_swing_step.load(std::memory_order_relaxed);
    const bool swinging = sw != ~0ull && now <= sw + 8;
    int16_t offset;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        offset = g_direction.step(active, cam, stick, camera_turned_by_player(), swinging, now, o);
    }
    if (active) {
        g_cam.store(cam, std::memory_order_relaxed);
        g_face.store(int16_t(ld16(link + kFace)), std::memory_order_relaxed);
        g_dist.store(dist, std::memory_order_relaxed);
        g_active_step.store(now, std::memory_order_relaxed);
    }
    if (log_on()) {
        static uint64_t n = 0;
        if (active && n++ % 15 == 0)
            LOG("[steer] camera %.1f, Link faces %.1f, walks to %.1f (game: %.1f), kept %.1f (step %llu)", cam * steer::kDegPerUnit,
                int16_t(ld16(link + kFace)) * steer::kDegPerUnit, int16_t(stick + cam - offset) * steer::kDegPerUnit,
                int16_t(stick + cam) * steer::kDegPerUnit, offset * steer::kDegPerUnit, (unsigned long long)now);
    }
    if (offset != 0) c->r[9] = uint32_t(int32_t(int16_t(stick + cam - offset)));
}

std::atomic<float> g_pinch_start{1}, g_pinch_d0{0};
}  // namespace

int steer_mode() { return g_mode.get(); }
void set_steer_mode(int m) { g_mode.set(m); }
bool camera_swing() { return g_swing.get() != 0; }
void set_camera_swing(bool on) { g_swing.set(on); }
bool direct_boat_speed() { return g_boat.get() != 0; }
void set_direct_boat_speed(bool on) { g_boat.set(on); }
bool pinch_fov_on() { return g_pinch.get() != 0; }
void set_pinch_fov_on(bool on) { g_pinch.set(on); }

// camera.cpp filter_pad, once per logic step: the swing's degrees for the camera's angle (0 none).
// `player_camera`: the player turned the camera in this read (right stick, mouse, swipe)
float steer_swing_degrees(bool player_camera) {
    const uint64_t now = step();
    const uint64_t act = g_active_step.load(std::memory_order_relaxed);
    const bool walking = act != ~0ull && now <= act + 1;
    float deg;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        deg = g_swing_rule.step(walking, int16_t(g_face.load(std::memory_order_relaxed)), int16_t(g_cam.load(std::memory_order_relaxed)),
                                walking ? g_dist.load(std::memory_order_relaxed) : 0.0f, player_camera, now, options());
    }
    if (deg != 0) {
        g_swing_step.store(now, std::memory_order_relaxed);
        if (log_on()) {
            static uint64_t n = 0;
            if (n++ % 15 == 0) LOG("[steer] swing %.2f degrees (step %llu)", deg, (unsigned long long)now);
        }
    }
    return deg;
}

void steer_reset() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_direction = {};
    g_swing_rule = {};
}

// the pinch: 0 begins (returns whether the pinch is on), 1 moves, 2 ends (saves); d: the fingers' distance
bool steer_pinch(int phase, float d) {
    if (!pinch_fov_on()) return false;
    if (phase == 0) {
        g_pinch_start.store(aspect::fov_scale());
        g_pinch_d0.store(d);
        return true;
    }
    const float v = steer::pinch_fov(g_pinch_start.load(), g_pinch_d0.load(), d);
    aspect::set_fov_scale(v);
    if (phase == 2) {
        hostui::post([v] {
            hostui::set("gameplayFovScale", std::to_string(v));
            hostui::graphics_changed();
        });
        LOG("[steer] pinch: field of view %.0f%%", v * 100);
    }
    return true;
}
}  // namespace mods

extern "C" void site_023F35BC(Cpu* c) { mods::walk(c); }
extern "C" void site_023F3610(Cpu* c) { mods::walk(c); }

// Decomp build: the decompiled setStickData asks at the same place (the decompilation's port_hooks.h PORT_STEER_WALK;
// tools/decomp_build/port_sites.py; one statement there, which the compiled code has twice: both sites): Link in r31,
// the camera angle in r3, the stick angle in r10, the walking direction (their sum) in r9, which the handler may replace.
extern "C" unsigned wwhd_steer_walk(Cpu* c, unsigned sp, unsigned site, unsigned link, unsigned cam, unsigned stick,
                                    unsigned value) {
    if (site != 0x023F35BC && site != 0x023F3610) port_points::unknown("steering", site);
    port_points::run(c, sp, site == 0x023F35BC ? site_023F35BC : site_023F3610,
                     [&](Cpu& r) { r.r[31] = link; r.r[3] = cam; r.r[10] = stick; r.r[9] = value; },
                     [&](Cpu& r) { value = r.r[9]; });
    return value;
}

#ifdef __ANDROID__
#include <jni.h>
// TouchControls.nativePinch(phase, distance px): 0 begin -> 1 if the pinch is on, 1 move, 2 end
extern "C" JNIEXPORT jint JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativePinch(JNIEnv*, jclass, jint phase, jfloat d) {
    return mods::steer_pinch(phase, d) ? 1 : 0;
}
#endif
