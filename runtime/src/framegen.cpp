// 60 fps (generated), the game side (framegen.h). The renderer side: gfx/vulkan/reproj.cpp.
// From Nycolazs/WindWakerHD-Android-Plus (vk/reproj.cpp 84b0ff2) via GreenNaugahyde/ZeldaWWHDRecompAndroid
// v0.9.9 (db65d60), MPL-2.0 (their camera capture in camera_draw_orig and OP_FRAMEGEN_CAMERA).
#include "framegen.h"

#include <cstdlib>
#include <cstring>

#include "aspect.h"
#include "gfx/renderer.h"
#include "gx2/gx2_cmd.h"
#include "runtime.h"

namespace framegen {

bool requested() {
    static const bool on = [] {
        const char* e = getenv("WWHD_FRAMEGEN");
        const bool want = e && atoi(e) != 0;
        if (want && !render::vulkan()) LOG("[framegen] WWHD_FRAMEGEN=1 needs the Vulkan renderer: off");
        else if (want) LOG("[framegen] 60 fps (generated) on: an in-between frame after each real one (WWHD_FRAMEGEN=1)");
        return want && render::vulkan();
    }();
    return on;
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
