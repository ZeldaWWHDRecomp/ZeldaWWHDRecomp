#pragma once
// Register state groups: what a register write can change in a renderer's resolved draw state.
//
// The Vulkan renderer's continued-draw fast path (gfx/vulkan/draw.cpp, WWHD_VK_FASTPATH) reuses the
// previous draw's shaders, render targets, pipeline and texture/sampler bindings while only
// "continued" groups changed: ALU constants, uniform-block and vertex-buffer pointers, and the
// per-draw dynamic words (viewport, scissor, blend colour, stencil/alpha reference, point size, index
// restart), all of which the draw re-reads every time. A value change in any other group bumps
// g_full_state_version (gx2_core.cpp), which ends the continuation. The classification is
// deliberately coarse on the breaking side: only words proven to be read per draw (draw.cpp
// record/index stages, vk::pack_uniforms_into, bind_stage uniforms, the vertex loop) and nowhere in
// shader/pipeline/target/texture resolution are continued (state_groups_test checks the known
// readers).
#include <cstdint>
#include "gx2_regs.h"

namespace gx2 {

enum StateGroup : uint8_t {
    kSgAluConst,      // SQ_ALU_CONSTANT*: uniform registers (packed per draw)
    kSgUniformBlock,  // SQ_{VS,PS,GS} uniform-block resources (uniform buffer pointer and size)
    kSgVertexBuffer,  // SQ_VTX_ATTRIBUTE_BLOCK words 0/1 (vertex buffer pointer and size)
    kSgDynamic,       // viewport, scissor, blend colour, stencil/alpha reference, point size, index restart
    kSgShader,        // SQ_PGM_*, SQ_CONFIG, VGT_GS_MODE, streamout
    kSgFetch,         // vertex semantics, vertex buffer strides/formats, VS->PS interface (SPI_*)
    kSgTargets,       // CB_COLORn_*, DB_DEPTH_* (render targets)
    kSgOutput,        // blend/depth/stencil/raster/clip control, write masks
    kSgTexture,       // SQ_TEX_RESOURCE texture units
    kSgSampler,       // SQ_TEX_SAMPLER, sampler border colours
    kSgOther,         // everything else; context and save-state loads
    kSgCount
};
// groups whose changes a continued draw picks up by itself
constexpr uint32_t kSgContinuedMask = 1u << kSgAluConst | 1u << kSgUniformBlock | 1u << kSgVertexBuffer | 1u << kSgDynamic;
// table value for DB_STENCILREFMASK(_BF): the reference byte is dynamic, the masks are pipeline state
constexpr uint8_t kSgStencilRefMask = kSgCount;

inline const char* state_group_name(int g) {
    static const char* const names[kSgCount] = {"ALU constants", "uniform blocks", "vertex buffers", "dynamic",
                                                "shader", "fetch/interface", "targets", "output", "textures",
                                                "samplers", "other"};
    return g >= 0 && g < kSgCount ? names[g] : "?";
}

// group of a register (kSgStencilRefMask: depends on the changed bits, see state_group_change)
inline uint8_t state_group(uint32_t reg) {
    using Latte::REGADDR;
    auto in = [reg](uint32_t first, uint32_t count) { return reg >= first && reg < first + count; };
    if (in(mmSQ_ALU_CONSTANT0_0, 0x1000)) return kSgAluConst;
    for (uint32_t base : {uint32_t(mmSQ_VTX_UNIFORM_BLOCK_START), uint32_t(mmSQ_PS_UNIFORM_BLOCK_START),
                          uint32_t(mmSQ_GS_UNIFORM_BLOCK_START)})
        if (in(base, 7 * 16)) return kSgUniformBlock;
    if (in(mmSQ_VTX_ATTRIBUTE_BLOCK_START, 7 * 16))  // word 2 holds the stride (pipeline key, fetch)
        return (reg - mmSQ_VTX_ATTRIBUTE_BLOCK_START) % 7 < 2 ? kSgVertexBuffer : kSgFetch;
    for (uint32_t base : {uint32_t(REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS), uint32_t(REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS),
                          uint32_t(REGADDR::SQ_TEX_RESOURCE_WORD0_N_GS)})
        if (in(base, 7 * LATTE_NUM_MAX_TEX_UNITS)) return kSgTexture;
    if (in(REGADDR::SQ_TEX_SAMPLER_WORD0_0, 3 * 3 * LATTE_NUM_MAX_TEX_UNITS)) return kSgSampler;
    if (in(REGADDR::TD_PS_SAMPLER0_BORDER_RED, 0x200)) return kSgSampler;  // PS, VS, GS border colours
    switch (reg) {
    case REGADDR::PA_CL_VPORT_XSCALE: case REGADDR::PA_CL_VPORT_XOFFSET: case REGADDR::PA_CL_VPORT_YSCALE:
    case REGADDR::PA_CL_VPORT_YOFFSET: case REGADDR::PA_CL_VPORT_ZSCALE: case REGADDR::PA_CL_VPORT_ZOFFSET:
    case REGADDR::PA_SC_GENERIC_SCISSOR_TL: case REGADDR::PA_SC_GENERIC_SCISSOR_BR:
    case REGADDR::CB_BLEND_RED: case REGADDR::CB_BLEND_GREEN: case REGADDR::CB_BLEND_BLUE: case REGADDR::CB_BLEND_ALPHA:
    case REGADDR::SX_ALPHA_REF: case REGADDR::PA_SU_POINT_SIZE:
    case REGADDR::VGT_MULTI_PRIM_IB_RESET_INDX: case REGADDR::VGT_MULTI_PRIM_IB_RESET_EN:
        return kSgDynamic;
    case REGADDR::DB_STENCILREFMASK: case REGADDR::DB_STENCILREFMASK_BF:
        return kSgStencilRefMask;
    case REGADDR::SQ_CONFIG: case REGADDR::VGT_GS_MODE: case REGADDR::VGT_STRMOUT_EN:
        return kSgShader;
    case REGADDR::CB_COLOR_CONTROL: case REGADDR::CB_TARGET_MASK: case mmCB_SHADER_MASK: case mmCB_SHADER_CONTROL:
    case REGADDR::DB_DEPTH_CONTROL: case mmDB_SHADER_CONTROL: case REGADDR::SX_ALPHA_TEST_CONTROL:
    case REGADDR::PA_CL_CLIP_CNTL: case REGADDR::PA_SU_SC_MODE_CNTL: case REGADDR::PA_CL_VTE_CNTL:
    case REGADDR::PA_SU_POLY_OFFSET_FRONT_SCALE: case REGADDR::PA_SU_POLY_OFFSET_FRONT_OFFSET:
    case REGADDR::PA_SU_POLY_OFFSET_CLAMP: case mmPA_SU_POLY_OFFSET_DB_FMT_CNTL:
        return kSgOutput;
    case REGADDR::PA_CL_VS_OUT_CNTL:
        return kSgFetch;
    default: break;
    }
    if (in(REGADDR::CB_BLEND0_CONTROL, 8)) return kSgOutput;
    if (in(mmDB_DEPTH_SIZE, 6) || in(mmCB_COLOR0_BASE, 0x38)) return kSgTargets;  // DB_DEPTH_* (and kDepthSlicesReg), CB_COLORn_BASE..MASK
    if (in(REGADDR::SQ_VTX_SEMANTIC_0, 32) || in(mmSPI_VS_OUT_ID_0, 0x32)) return kSgFetch;  // SPI_VS_OUT_ID .. SPI_INPUT_Z
    if (in(REGADDR::SQ_PGM_START_PS, 0x28)) return kSgShader;  // SQ_PGM_START/SIZE/RESOURCES/EXPORTS/CF_OFFSET
    if (in(REGADDR::VGT_STRMOUT_BUFFER_SIZE_0, 16)) return kSgShader;
    return kSgOther;
}

// group a value change belongs to
inline uint8_t state_group_change(uint8_t tableGroup, uint32_t old, uint32_t value) {
    if (tableGroup == kSgStencilRefMask) return ((old ^ value) & 0xFFFFFF00u) ? kSgOutput : kSgDynamic;
    return tableGroup;
}

// render thread (gx2_core.cpp): bumped on every register value change outside kSgContinuedMask, on
// context loads and on save-state loads; groups changed since the last take_dirty_groups()
extern uint64_t g_full_state_version;
extern uint32_t g_dirty_groups;
inline uint32_t take_dirty_groups() {
    uint32_t g = g_dirty_groups;
    g_dirty_groups = 0;
    return g;
}

}  // namespace gx2
