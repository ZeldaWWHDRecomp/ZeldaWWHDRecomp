// Fog reads see the fog their own thread set last, not another core's fog of the moment.
//
// The game keeps one "current fog" object per view (found through the view's fog list, 3E9B9280 on
// the Outset cliff). Every fog change goes through one setter, 0255F668: GxFog_set (0255F84C, the
// scene's normal fog from the environment light +0xB8C), GxFog_sea_set (0255F8A0, the sea fog from
// +0xB94) and the line, wave and effect materials that call it with their own colour. The environment
// uniforms (027FDA54) copy that object's colour, start and end into the scene blocks the GPU uses.
//
// Two cores do this at once: the main thread paints the sea (daSea_setUniforms 0246C7C8, daBg_Draw
// 02079D08, drawWave 02574D38 -> draw_wave_packet 02582650, which switch to the sea fog, copy it and
// switch back), while WorkerMgr/Worker2() paints the layers (its line packets set the normal fog,
// lineSetMaterial1 025EC090, and its shadow pass 02736C10 -> 027FDA54 copies it). A copy that ran
// while the other core had just set its own fog took that one: the sea fog in the shadow pass's
// scene constants, or the normal fog in the wave's. Which one changed from run to run, and the sea
// and grass with it (#178; Outset cliff 6-387 px between captures).
//
// The Wii U shares the object the same way; its fixed timing only makes the outcome stable. In each
// thread's own program order the copy reads the fog that thread set right before (the shadow pass
// draws no sea; the wave packet copies the sea fog its drawWave set). So each thread records what its
// own setter wrote (site at 0255F7F8, where 0255F668 stores into the object: r31 = the object, the
// colour already converted on the stack, start and end from the environment light +0xFD0 / +0xFD4 as
// the setter reads them next), and the copy (site at 027FDF78, inside 027FDA54 after its lookup and
// "fog enabled" checks) reads that record when it copies the same object: r31 is pointed at a private
// copy that holds the fields 027FDA54 reads there (+0xF4, +0x104, +0x114..+0x120); the function's
// epilogue restores r31. A thread that has not set this object's fog reads the shared object as before.
//
// Safe: only those reads change, and only to what the reading thread itself wrote. Nothing is written
// to the shared object, the setters run unchanged, and the private copies live in host scratch memory
// (not part of save states). WWHD_FOG_PIN=0 turns it off.
#include <cstdlib>
#include <cstring>

#include "guest_addr.h"
#include "runtime.h"

namespace {
bool pin_on() {
    static const bool on = [] { const char* e = getenv("WWHD_FOG_PIN"); return !e || atoi(e) != 0; }();
    return on;
}
// what this thread's fog setter last wrote into a fog object
struct FogRecord {
    uint32_t object = 0;
    uint32_t start = 0, end = 0, colour[4] = {};
};
thread_local FogRecord t_fog;
}  // namespace

// 0255F668 (the fog setter), at its first store into the fog object: r31 = the object, the colour
// (4 floats, from GXColor_to_f32) at r1+0xC..+0x18; it stores start and end from the environment
// light (+0xFD0, +0xFD4) right after.
extern "C" void site_0255F7F8(Cpu* c) {
    if (!pin_on()) return;
    const uint32_t object = c->r[31], sp = c->r[1];
    if (!object) return;
    if (!ld32(GD(0x104773B4))) return;  // environment light not set up yet (dKy_getEnvlight 02555D0C)
    const uint32_t env = GD(0x10475A68);
    t_fog.object = object;
    for (uint32_t i = 0; i < 4; i++) t_fog.colour[i] = ld32(sp + 0xC + 4 * i);
    t_fog.start = ld32(env + 0xFD0);
    t_fog.end = ld32(env + 0xFD4);
}

namespace interp_fx { bool in_shadow_pass(); }

// 027FDA54 (environment uniforms), before it copies the fog object r31
extern "C" void site_027FDF78(Cpu* c) {
    if (!pin_on()) return;
    FogRecord f;
    if (interp_fx::in_shadow_pass()) {
        // the shadow pass sets no fog itself: the scene's normal fog, as GxFog_set computes it
        if (!ld32(GD(0x104773B4))) return;
        const uint32_t env = GD(0x10475A68);
        float divisor;
        { const uint32_t w = ld32(GD(0x1004F104)); memcpy(&divisor, &w, 4); }  // GXColor_to_f32's 255.0f
        for (uint32_t i = 0; i < 4; i++) {
            const float v = (float)((double)(float)ld8(env + 0xB8C + i) / (double)divisor);
            memcpy(&f.colour[i], &v, 4);
        }
        f.start = ld32(env + 0xFD0);
        f.end = ld32(env + 0xFD4);
    } else {
        if (!t_fog.object || c->r[31] != t_fog.object) return;
        f = t_fog;
    }
    thread_local uint32_t copy = 0;  // per thread: the cores paint at the same time
    if (!copy) copy = mem::host_alloc(0x130);
    st32(copy + 0xF4, f.start);
    st32(copy + 0x104, f.end);
    for (uint32_t i = 0; i < 4; i++) st32(copy + 0x114 + 4 * i, f.colour[i]);
    c->r[31] = copy;
}
