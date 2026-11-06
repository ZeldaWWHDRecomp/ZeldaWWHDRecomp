// 60 fps (generated), the game side (framegen.h). The renderer side: gfx/vulkan/reproj.cpp.
// From Nycolazs/WindWakerHD-Android-Plus (vk/reproj.cpp 84b0ff2) via GreenNaugahyde/ZeldaWWHDRecompAndroid
// v0.9.9 (db65d60), MPL-2.0 (their camera capture in camera_draw_orig and OP_FRAMEGEN_CAMERA).
#include "framegen.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

#include "aspect.h"
#include "gfx/renderer.h"
#include "gx2/gx2_cmd.h"
#include "interp.h"
#include "runtime.h"

namespace framegen {

namespace {
std::atomic<bool> g_chosen{false};
// WWHD_FRAMEGEN: -1 unset, else its value (read once)
int env_choice() {
    static const int v = [] {
        const char* e = getenv("WWHD_FRAMEGEN");
        return e && *e ? (atoi(e) != 0 ? 1 : 0) : -1;
    }();
    return v;
}
}  // namespace

bool env_override() { return env_choice() >= 0; }
bool chosen() { return env_override() ? env_choice() == 1 : g_chosen.load(std::memory_order_relaxed); }
bool available() { return render::vulkan(); }
void set_chosen(bool on) {
    if (env_override() || g_chosen.exchange(on) == on) return;
    LOG("[framegen] 60 fps (generated) %s%s", on ? "chosen" : "off", on && !available() ? " (needs the Vulkan renderer: not running)" : "");
}
bool requested() {
    if (!chosen() || !render::vulkan()) return false;
    static const bool logged = [] {
        LOG("[framegen] 60 fps (generated) on: an in-between frame after each real one%s", env_override() ? " (WWHD_FRAMEGEN=1)" : "");
        return true;
    }();
    (void)logged;
    return interp::mode() == 0;  // (frame interpolation draws its own in-between frames)
}

// Camera layout (camera_draw, 024FFC40; interp.cpp): near +0xCC, far +0xD0, fovy +0xD4, eye +0xDC,
// center +0xE8, up +0xF4. Read after camera_draw: the camera as the scene was drawn with it.
void camera_drawn(uint32_t cam, uint32_t flags) {
    if (!requested() || !cam) return;
    float v[kCameraWords - 1];
    for (int i = 0; i < 3; i++) {
        v[i] = (float)ldf32(cam + 0xDC + 4 * i);
        v[3 + i] = (float)ldf32(cam + 0xE8 + 4 * i);
        v[6 + i] = (float)ldf32(cam + 0xF4 + 4 * i);
    }
    v[9] = (float)ldf32(cam + 0xD4);
    v[10] = (float)ldf32(cam + 0xCC);
    v[11] = (float)ldf32(cam + 0xD0);
    v[12] = aspect::scene_tan_half_fovy(v[9]);
    uint32_t w[kCameraWords];
    memcpy(w, v, sizeof v);
    w[kCameraWords - 1] = flags;
    gx2::framegen_camera(w, kCameraWords);
}

}  // namespace framegen
