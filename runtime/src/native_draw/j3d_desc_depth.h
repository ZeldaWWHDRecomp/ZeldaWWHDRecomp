// Native depth programs for described J3D draws (stage D2 of NATIVE-J3D-DESC.md). Decomp build, fast mode, cmake
// WWHD_NATIVE_DRAW=ON only; Vulkan; Mac only until the phone gate. Code: native_draw/j3d_desc_depth.cpp, the family core
// in native_draw/j3d_depth_family.{h,cpp}.
//
// A pass-0 description (native_j3d_desc.h Desc with pass 0) whose define image is the canonical depth-only state
// (CB_TARGET_MASK 0, DB_DEPTH_CONTROL 0x16: depth LESS + write, no stencil) and whose translated programs (what the
// define resolved) are a proven depth family draws with that family's native program:
//   - define (once per what the description path defines: desc version x env class x targets): choose() classifies
//     from the define's own register image and programs, never from the live register file. It makes the pipeline
//     (by a compact raster key, shared by every state with the same family, streams and raster words), the static
//     part of the descriptor set (material textures/samplers, support scalars) and the per-draw block plan (which of the
//     draw's block refs feeds which binding).
//   - per draw: bind() takes the draw's block refs (OP_J3D_DESC_DRAW) through the description path's block upload and
//     gives a pipeline, one descriptor set per (state, submission slot) and its dynamic offsets. No register reads, no
//     strings, no allocation once warm.
// Family membership is the structure-hash pair; with WWHD_NATIVE_J3D_DEPTH_BIASFREE=1 (recommended, the phone needs it)
// the bias-free hashes and their proven list (60 generated families, 0 mismatches in 7 scenes + 26 states), so the
// Mac and the phone classify the same descriptions into the same families. The 14 zero-texel families stay translated.
//
// Shadow slices (WWHD_NATIVE_J3D_DESC_DEPTH_INST=1, needs the device's shaderOutputLayer; off by default): the pass-0
// cascade slices share one description, so the 3 draws of a (state, mesh), one per slice, become one instanced draw
// into a layered pass over the slices (gl_Layer per instance). The collected draws are flushed at the first command
// that is not a canonical depth-only described draw into the same cascade surface (see inst_* below and the D2 section
// of NATIVE-J3D-DESC.md for the exactness argument).
//
// Switches: WWHD_NATIVE_J3D_DESC_DEPTH (with WWHD_NATIVE_J3D_DESC=2; fast build default on, =0 off; with
// WWHD_NATIVE_J3D_DEPTH_BIASFREE, default on, =0 off), WWHD_NATIVE_J3D_DESC_DEPTH_INST=1 (default off).
// Counters: one "[j3d-desc-depth]" line per 300 frames.
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include <cstdint>
#include "gfx/vulkan/loader.h"
#include "gx2/native_j3d_desc.h"

namespace native_draw::j3ddesc::depth {

bool on();       // WWHD_NATIVE_J3D_DESC_DEPTH=1 (Vulkan only)
bool inst_on();  // WWHD_NATIVE_J3D_DESC_DEPTH_INST=1 and the device writes gl_Layer from the vertex stage

// ---------------------------------------------------------------- define (render thread)
// what the description path's define made, for one (desc version, env class, targets)
struct Query {
    const Desc* desc = nullptr;
    const Env* env = nullptr;
    // the define's register image: env image, then the desc segments, over the pass's ambient snapshot (the file the
    // resolve-only define ran on). Read only here, never kept.
    const uint32_t* image = nullptr;
    const void* vs = nullptr;     // gfxvk::vk::Shader the define resolved (translated vertex program)
    const void* ps = nullptr;     // gfxvk::vk::Shader (translated pixel program)
    const void* fetch = nullptr;  // LatteFetchShader
    int depthFormat = 0;          // VkFormat of the pass's depth target (0: none)
    bool depthStencil = false;
    int colorFormats[8] = {};
    uint32_t colorCount = 0;
    const uint8_t* psSupport = nullptr;  // the pixel program's support bytes the define made (alpha ref, LOD bias, scales)
    uint32_t psSupportSize = 0;
    struct Image { uint32_t binding; uint64_t sampler, view; uint32_t layout; };
    const Image* images = nullptr;  // the pixel stage's static image writes (material units) the define resolved
    uint32_t imageCount = 0;
};
struct State;                     // owned here until reset()
State* choose(const Query& q);    // nullptr: the description draws with its translated programs (reason counted)

// ---------------------------------------------------------------- per draw (render thread)
struct Slice { uint64_t buffer = 0, offset = 0, size = 0; };
// the description path's uniform block upload: once per (address, submitted-uniform epoch, submission), as the full
// draw and B2's replay upload them; {} when the block cannot be read
using Upload = Slice (*)(uint32_t addr, uint32_t size);
struct Draw {
    const uint32_t* blocks = nullptr;  // OP_J3D_DESC_DRAW's blocks {stage << 16 | location, address, size}
    uint32_t blockCount = 0;
    Upload upload = nullptr;
};
struct Binding {
    uint64_t pipeline = 0, layout = 0, set = 0;
    const uint32_t* offsets = nullptr;  // dynamic offsets (valid until the next bind())
    uint32_t offsetCount = 0;
};
// false: this draw takes the translated path (counted by reason)
bool bind(State* s, const Draw& d, Binding& out);

// ---------------------------------------------------------------- proof (WWHD_J3D_DEPTH_PROOF=N with the description path)
// every Nth native depth draw of the description path also has its translated sets made; after the draw, both are
// drawn into private depth targets and compared word for word (j3d_depth.h proof_desc; counted per family)
bool proof_wanted();
struct ProofIn {
    VkPipeline pipeline = VK_NULL_HANDLE;  // the translated state's
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSet sets[2]{};
    const uint32_t* offsets = nullptr;
    uint32_t offsetCount = 0;
    VkBuffer vb[16]{};
    VkDeviceSize vbo[16]{};
    uint32_t vbBinding[16]{};
    uint32_t vbCount = 0;
    VkBuffer ib = VK_NULL_HANDLE;
    VkDeviceSize ibo = 0;
    VkIndexType indexType = VK_INDEX_TYPE_UINT32;
    uint32_t count = 0;
    VkViewport viewport{};
    VkRect2D scissor{};
    const uint32_t* blend = nullptr;  // 4 words
    uint32_t stencilFront = 0, stencilBack = 0;
    void* colors[8]{};  // gfxvk::Surface*
    void* depth = nullptr;
    uint32_t depthSlice = 0, width = 0, height = 0;
    const void* vs = nullptr;
    const void* ps = nullptr;
    const void* fs = nullptr;
    Binding native;  // what the draw drew with
};
void proof(State* s, const ProofIn& p);

// ---------------------------------------------------------------- shadow-slice instancing (WWHD_NATIVE_J3D_DESC_DEPTH_INST=1)
// The description path hands each native depth draw of pass 0 to inst_collect() instead of drawing it; inst_flush()
// draws everything collected (one instanced draw per (state, mesh) group with 2+ slices, else one draw) in one layered
// pass over the cascade's layers, and must be called with no render pass open, before any command that is not a
// collected draw touches the cascade or reads it (the next pass begin on another target, a copy or expand, a clear of
// the cascade, a non-canonical draw into it, a frame end, a save-state load). Exact: see NATIVE-J3D-DESC.md D2.
struct Target {
    VkImage image = VK_NULL_HANDLE;  // the cascade (a 2D array depth image)
    uint64_t surface = 0;            // identity of the surface (gfxvk::Surface*)
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool stencil = false;
    uint32_t layer = 0;              // the slice this draw renders into
    uint32_t width = 0, height = 0;  // the slice pass's render area (equal for every slice of one flush)
    VkViewport viewport{};
    VkRect2D scissor{};
};
struct Geometry {  // the draw's vertex and index binds, as the description path resolved them for the draw
    uint32_t mesh = 0;  // mesh id (group key with the state)
    VkBuffer vertex[16]{};
    VkDeviceSize vertexOffset[16]{};
    uint32_t vertexBinding[16]{};
    uint32_t vertexCount = 0;
    VkBuffer index = VK_NULL_HANDLE;
    VkDeviceSize indexOffset = 0;
    VkIndexType indexType = VK_INDEX_TYPE_UINT32;
    uint32_t indexCount = 0;
    int32_t baseVertex = 0;
};
// whether the state's draws are collected (instancing on, the state has its instanced variant)
bool inst_wanted(const State* s);
// false: not collectable (the caller draws it now; with something collected for another target it flushes first, which
// ends the open pass). PROOF (INST proof, WWHD_J3D_DEPTH_PROOF=N: every Nth collected draw, proof_wanted()): the
// draw's plain native binding (bind()); the flush then also draws this instance with it and with the instanced program
// into private targets and compares them word for word (stats as inst:<family>)
bool inst_collect(State* s, const Draw& d, const Geometry& g, const Target& t, const Binding* proof = nullptr);
bool inst_pending();                  // something is collected
uint64_t inst_target();               // the collected draws' cascade surface (0: none)
void inst_flush(VkCommandBuffer cmd); // draws and clears the collection (no pass open; leaves none open)

void frame_end();  // render thread, at each swap: the counter line (the caller flushes first)
void reset();      // a save-state load or shader reset: states, sets and pipelines dropped (after a flush)

// backend hook (device creation, gfx/vulkan/backend.cpp, WWHD_NATIVE_DRAW builds): whether to enable shaderOutputLayer
bool want_layer_output();
void set_layer_output(bool enabled);

}  // namespace native_draw::j3ddesc::depth
#endif
