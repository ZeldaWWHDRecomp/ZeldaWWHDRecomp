// Cloud research prototype: snapshot of the game's sky clouds for the renderer (cloud_proto.cpp).
#pragma once
#include <cstdint>

#include "sky_frame.h"

namespace cloud_proto {
using sky::SkyFrame;

enum Mode : int { kOff = 0, kSdf, kVolume, kHide, kMark, kCombo };
int mode();  // the Cloud style setting (Original: kOff, Toon: kSdf), or WWHD_CLOUDS_PROTO for research
int tier();  // 1 Lite, 2 High (0: Original)

struct Cloud {
    float pos[3];   // VRKUMO_EFF position: camera-relative direction * distance (x, z on a 15000 disc)
    float height;   // per-cloud random vertical stretch (-0.09..0.09)
    float alpha;    // the game's fade (0..max alpha)
    float falloff;  // 1 near the zenith/centre .. 0 at the disc edge (size and colour blend)
    int32_t index;  // slot 0..99 (shape seed)
};

struct Snapshot {
    float eye[3], center[3];
    float viewRot[12];  // the view's camera matrix (3x4, rows), View +0x1E4
    float fovy, bank;
    float viewNear, viewFar, viewAspect;  // View +0xCC/+0xD0/+0xD8 (projection, for depth reconstruction)
    float sunPos[3], sunAlpha, moonPos[3];
    int32_t sunVisible;
    float cloudColor[3], centerColor[3], fogColor[3];  // palette (0..1)
    float cloudScale, centerScale, strength, curTime, phase;
    // drawVrkumo's size constants (.rodata): sizeBase, sizeGain, variation, pitchScale, sideGain,
    // pitchGain, starSizeBase, farPlane
    float k[8];
    int32_t count;  // mVrkumoCount (weather)
    int32_t n;      // visible clouds in c[]
    Cloud c[100];
    // cloud shadows: each game cloud anchored in the world (it drifts with the wind but no longer follows
    // the camera), camera-relative at this frame: x, z, radius, alpha
    int32_t nShadow;
    float shadow[100][4];
};
constexpr uint32_t kRing = 32;
const Snapshot* snapshot(uint32_t seq);
// render thread: the snapshot whose draws are being executed (0: outside drawVrkumo), and whether
// the prototype's clouds were drawn for it
extern uint32_t g_active_seq;
extern bool g_drawn;
extern void (*g_on_end)();  // OP_CLOUDS_END hook of the renderer (timing), may be null

// per-frame sky values for the renderers' sky passes: sky_frame.h
inline void sky_frame(const Snapshot& s, float aspect, float hazeReduction, SkyFrame& out) {
    sky::Inputs in{};
    for (int i = 0; i < 3; i++) in.eye[i] = s.eye[i], in.sunPos[i] = s.sunPos[i], in.fog[i] = s.fogColor[i];
    for (int i = 0; i < 12; i++) in.viewRot[i] = s.viewRot[i];
    in.fovy = s.fovy, in.curTime = s.curTime, in.sunVisible = s.sunVisible != 0;
    sky::frame(in, aspect, hazeReduction, out);
}

}  // namespace cloud_proto
