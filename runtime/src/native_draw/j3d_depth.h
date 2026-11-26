// Native depth shaders for J3D's depth-only pass (decomp build fast mode, WWHD_NATIVE_DRAW=ON only).
// J3D draws each shape into a depth-only pass (pass 0 of j3d::ShapePacket::submitShape: colour writes off, depth
// test LESS + write; the shadow-cascade slices). Its programs only place the vertices and, for some materials, test
// one texture's alpha; this file holds the debug tools for replacing them with a few native depth programs:
//   WWHD_J3D_DEPTH_DUMP=<dir>[:<from>:<to>[:<raw>]]  (debug, off by default): every J3D draw named by OP_J3D_SHAPE
//     (WWHD_NATIVE_DRAW_J3D=1) between TV frames FROM and TO (as WWHD_DUMP_FRAMES counts them): its programs' final GLSL (once per program) and
//     their state (render state, fetch layout, texture units, uniform blocks), aggregated per program pair; the
//     first RAW frames draw by draw. Needs the shader text kept (WWHD_VK_SHADER_SOURCE_VERIFY=1).
//   WWHD_J3D_DEPTH_PROOF=<every>[:<dir>]  (debug, off by default): every EVERYth depth-only J3D draw (pass 0, colour
//     writes off, depth LESS + write) is drawn twice more after its own draw, into two private depth targets of its
//     size and format, cleared to 1.0: once with its own translated programs and binds, once with the native depth
//     family program it classifies into. The two depth images are read back and compared word for word (opaque
//     families: exact; alpha-tested ones: exact with the texture read at LOD 0 in both, WWHD_J3D_DEPTH_PROOF_LOD0=1,
//     then under the tolerance rule with their own LOD). Draws continued by the fast path keep no binds of their own:
//     the alpha-tested families' proof runs with WWHD_VK_FASTPATH=0.
//     Mismatch counts per family are logged every 300 frames (and the first mismatching pairs written to DIR).
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include <cstdint>
#include <functional>
#include <string>
#include "gfx/vulkan/loader.h"

struct LatteFetchShader;
namespace gfxvk { struct Surface; namespace vk { struct Shader; } }

namespace native_draw::j3d_depth {

// render thread: the words of the last OP_J3D_SHAPE (shape, binding, pass, view, program record)
void shape(const uint32_t* p, uint32_t n);

// one J3D draw as the Vulkan draw path resolved it
struct DrawInfo {
    const uint32_t* regs = nullptr;
    uint32_t prim = 0, count = 0, indexType = 0, indexAddr = 0, baseVertex = 0;
    const LatteFetchShader* fs = nullptr;
    const gfxvk::vk::Shader* vs = nullptr;
    const gfxvk::vk::Shader* ps = nullptr;
    gfxvk::Surface* depth = nullptr;
    uint32_t depthSlice = 0, width = 0, height = 0;
    uint64_t frame = 0;
};
// the binds of a sampled draw, as the draw recorded them (gfx/vulkan/draw.cpp)
struct ProofDraw {
    DrawInfo info;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSet sets[2]{};
    uint32_t offsets[34]{};
    uint32_t offsetCount = 0;
    struct VertexBind { uint32_t binding = 0; VkBuffer buffer = VK_NULL_HANDLE; VkDeviceSize offset = 0; uint32_t stride = 0; };
    VertexBind vertexBinds[16];
    uint32_t vertexBindCount = 0;
    bool indexed = false;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceSize indexOffset = 0;
    VkIndexType indexType = VK_INDEX_TYPE_UINT32;
    uint32_t drawCount = 0;
    VkViewport viewport{};
    VkRect2D scissor{};
    gfxvk::Surface* colors[8]{};
    const uint8_t* vsSupport = nullptr;  // the vertex program's support uniform bytes (remapped block entries)
    uint32_t vsSupportSize = 0;
    const uint8_t* psSupport = nullptr;  // the pixel program's support uniform bytes (alpha test ref, LOD bias, ...)
    uint32_t psSupportSize = 0;
    struct Image { uint32_t binding = 0; VkDescriptorImageInfo info{}; };
    Image psImages[16];                  // the pixel stage's image writes (binding: textureUnitToBindingPoint)
    uint32_t psImageCount = 0;
    const char* refused = nullptr;       // the capture's reason the binds cannot be replayed
};
// a described depth draw (WWHD_NATIVE_J3D_DESC_DEPTH=1, the description path's draw): whether it is sampled (every
// Nth of WWHD_J3D_DEPTH_PROOF), and its proof: D = its translated binds (as the description path would draw it), SECOND
// records the native binding the path drew with; FAMILY names the stats line
bool proof_desc_wanted();
void proof_desc(const ProofDraw& d, const std::string& family, const std::function<bool(VkCommandBuffer)>& second);
// the same comparison for two given recorders (shadow-slice instancing: the instanced program against the per-slice
// native one); D gives the target (depth surface, size) and the draw form; sampling is the caller's (proof_desc_wanted)
void proof_pair(const ProofDraw& d, const std::string& family, const std::function<bool(VkCommandBuffer)>& first,
                const std::function<bool(VkCommandBuffer)>& second);
// top of a named draw: whether it is sampled for the proof (the draw then captures its binds)
bool proof_wanted();
// after its own draw: the two proof draws and their comparison
void proof(const ProofDraw& d);
bool dump_on();
// after the draw is recorded (debug dump)
void note(const DrawInfo& d);

}  // namespace native_draw::j3d_depth
#endif
