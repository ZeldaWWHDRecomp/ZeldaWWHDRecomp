// Decomp build: the port points of the decompiled camera code (the decompilation's port_hooks.h PORT_CAMERA_F,
// PORT_CAMERA_W, PORT_CAMERA_AT, PORT_PROJECTION; tools/decomp_build/port_sites.py). Each runs its instruction hook's
// handler (mods/camera.cpp, mods/targeting_letterbox.cpp, aspect.cpp) through port_points::run. In a file of its own so
// that the tests that link one of those files alone do not need the others.
#include <cstdint>

#include "port_points.h"
#include "runtime.h"
extern "C" {
void site_02171B1C(Cpu*); void site_024F9DA4(Cpu*); void site_0250074C(Cpu*); void site_02506C68(Cpu*);
void site_02506C80(Cpu*);                                                    // camera.cpp: the right stick's y
void site_024FFA98(Cpu*); void site_025020E0(Cpu*);                          // aspect.cpp: the 16:9 aspect stored
void site_024FF980(Cpu*); void site_024FF4C0(Cpu*); void site_024FF4E4(Cpu*);  // targeting_letterbox.cpp
void site_024FC080(Cpu*); void site_024FF8C0(Cpu*);
}
namespace {
struct CameraPoint { uint32_t site; void (*handler)(Cpu*); int8_t obj, value; };  // registers (value: FPR for floats)
constexpr CameraPoint kCameraFloats[] = {
    {0x02171B1C, site_02171B1C, -1, 1}, {0x024F9DA4, site_024F9DA4, -1, 1}, {0x0250074C, site_0250074C, -1, 1},
    {0x02506C68, site_02506C68, -1, 1}, {0x02506C80, site_02506C80, -1, 1}, {0x024FFA98, site_024FFA98, -1, 0},
    {0x025020E0, site_025020E0, -1, 0}, {0x024FF980, site_024FF980, 31, 11},
};
constexpr CameraPoint kCameraWords[] = {
    {0x024FF4C0, site_024FF4C0, 31, 5}, {0x024FF4E4, site_024FF4E4, 31, 5}, {0x024FC080, site_024FC080, 31, 3},
    {0x024FF8C0, site_024FF8C0, 31, -1},
};
}  // namespace
extern "C" float wwhd_camera_float(Cpu* c, unsigned sp, unsigned site, unsigned obj, float value) {
    for (const CameraPoint& p : kCameraFloats) {
        if (p.site != site) continue;
        port_points::run(c, sp, p.handler,
                         [&](Cpu& r) {
                             if (p.obj >= 0) r.r[p.obj] = obj;
                             r.f[p.value].ps0 = r.f[p.value].ps1 = value;  // an lfs result: both halves
                         },
                         [&](Cpu& r) { value = (float)r.f[p.value].ps0; });
        return value;
    }
    port_points::unknown("camera", site);
}
extern "C" unsigned wwhd_camera_word(Cpu* c, unsigned sp, unsigned site, unsigned obj, unsigned value) {
    for (const CameraPoint& p : kCameraWords) {
        if (p.site != site) continue;
        port_points::run(c, sp, p.handler,
                         [&](Cpu& r) {
                             r.r[p.obj] = obj;
                             if (p.value >= 0) r.r[p.value] = value;
                         },
                         [&](Cpu& r) { if (p.value >= 0) value = r.r[p.value]; });
        return value;
    }
    port_points::unknown("camera", site);
}

// view_setup / camera_draw: the projection call's four float arguments (f1..f4)
extern "C" {
void site_024F811C(Cpu*); void site_024F8168(Cpu*); void site_024FFD60(Cpu*); void site_0250007C(Cpu*);
}
extern "C" void wwhd_projection(Cpu* c, unsigned sp, unsigned site, float* f) {
    PpcFunc h = site == 0x024F811C ? site_024F811C : site == 0x024F8168 ? site_024F8168 :
                site == 0x024FFD60 ? site_024FFD60 : site == 0x0250007C ? site_0250007C : nullptr;
    if (!h) port_points::unknown("projection", site);
    port_points::run(c, sp, h,
                     [&](Cpu& r) { for (int i = 0; i < 4; i++) r.f[1 + i].ps0 = r.f[1 + i].ps1 = f[i]; },
                     [&](Cpu& r) { for (int i = 0; i < 4; i++) f[i] = (float)r.f[1 + i].ps0; });
}
