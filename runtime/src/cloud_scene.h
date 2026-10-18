// Per-frame data of the toon clouds, shared by the renderers (cloud_scene.cpp).
#pragma once
#include <cstdint>
#include <vector>

#include "cloud_proto.h"

namespace cloud_scene {
// the shaders' Globals (MSL float3x3 = 3 padded columns; GLSL: three vec4), 160 bytes
struct Globals {
    float view[12];
    float proj[4], sun[4], lit[4], sky[4], params[4], tune[4], extra[4];
};
// one impostor: centre (xyz, width radius), shape (height/width, seed, alpha, distance blend), base colour (rgb, far flag)
struct Inst {
    float center[4], shape[4], base[4];
};
static_assert(sizeof(Globals) == 160 && sizeof(Inst) == 48, "shader layouts");
// `mode`: cloud_proto::Mode; `depthTested`: drawn into the full-resolution picture with the sky-only test
void build(const cloud_proto::Snapshot& s, float aspect, int mode, bool depthTested, uint64_t frame, Globals& g,
           std::vector<Inst>& sorted);
}  // namespace cloud_scene
