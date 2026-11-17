// Batched vertex-shader variants (draw runs, gfx/vulkan/batch_draw.h): the interface of batch_glsl.cpp.
// Written for ZeldaWWHDRecomp (MPL-2.0). It declares what batch_glsl.cpp (based on VR-WakerHD's draw batching by
// @mkooner, contributed under MPL-2.0 in discussion #535) implements, so the names below are the ones that file uses.
//
// rewrite() turns a translated vertex shader into a variant that draws a whole same-state run (draws that differ only
// in some uniform block addresses) as one draw with one instance per piece. Only declarations change: a per-piece
// uniform block is declared as an array with one element per piece, and its old name is redefined to pick the
// element of the current instance (gl_InstanceIndex). The support block (the translator's register / remapped-entry
// block) is handled the same way when a per-piece block was remapped into it. The guest instance ID is read as 0,
// as in each piece's own one-instance draw. The shader body is left as translated.
#pragma once
#include <cstdint>
#include <string>

namespace gfxvk::vk::batch {

// upper bound on the instances (pieces) of one draw of a variant; the device's uniform range may lower it
constexpr uint32_t kMaxPieces = 256;

// Input: which blocks are per piece. Output (filled by rewrite): the array length and each block's per-piece stride.
struct Variant {
    uint32_t blocks = 0;          // in: bit i set = guest vertex uniform block i differs per piece
    bool support = false;         // in: the support block differs per piece too
    uint32_t supportBytes = 0;    // in: that block's size in bytes (std140, multiple of 16) = its per-piece stride
    uint32_t pieces = 0;          // out: element count given to every per-piece array
    uint32_t vec4s[16] = {};      // out: per-piece stride of each per-piece guest block, in vec4s
};

// Rewrites `glsl` into `out`. maxRange is the device's maxUniformBufferRange (all pieces of one block must fit in one
// binding). Returns false, with a reason in *why when given, if the source does not have the expected layout.
bool rewrite(const std::string& glsl, Variant& variant, uint32_t maxRange, std::string& out, std::string* why = nullptr);

}  // namespace gfxvk::vk::batch
