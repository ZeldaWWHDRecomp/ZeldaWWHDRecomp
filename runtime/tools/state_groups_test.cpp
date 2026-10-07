// Register state groups (runtime/src/gx2/state_groups.h) of the Vulkan continued-draw fast path.
// Every register the full draw resolution reads (shader key state_hash() in gfx/vulkan/shaders.cpp,
// the pipeline key in draw.cpp pipeline(), render targets in surfaces.cpp color_target/depth_target,
// texture units and samplers in bind_stage, program/fetch addresses, vertex strides) must end a
// continuation; the words a continued draw re-reads itself (uniforms, uniform blocks, vertex buffer
// pointers, viewport/scissor/blend colour/stencil and alpha reference, point size, index restart)
// must not.
#include "state_groups.h"
#include "gx2.h"

#include <cassert>
#include <cstdio>

using namespace gx2;
using Latte::REGADDR;

static int failures = 0;
static bool continued(uint32_t reg, uint32_t old = 0, uint32_t value = 1) {
    const uint8_t g = state_group_change(state_group(reg), old, value);
    if (g >= kSgCount) {
        std::printf("register %04X: invalid group %u\n", reg, g);
        ++failures;
    }
    return (kSgContinuedMask >> g) & 1;
}
static void breaking(uint32_t first, uint32_t count, const char* what) {
    for (uint32_t r = first; r < first + count; ++r)
        if (continued(r)) {
            std::printf("%s: register %04X (%s) is continued but read by the full resolution\n", what, r,
                        state_group_name(state_group(r)));
            ++failures;
        }
}
static void continues(uint32_t first, uint32_t count, const char* what) {
    for (uint32_t r = first; r < first + count; ++r)
        if (!continued(r)) {
            std::printf("%s: register %04X (%s) ends continuations\n", what, r, state_group_name(state_group(r)));
            ++failures;
        }
}

int main() {
    // every register has a valid group
    for (uint32_t r = 0; r < kNumRegs; ++r) continued(r);

    // shader key (shaders.cpp state_hash)
    breaking(REGADDR::SQ_VTX_SEMANTIC_0, 32, "VTX_SEMANTIC");
    breaking(mmSPI_VS_OUT_ID_0, 10, "SPI_VS_OUT_ID");
    breaking(mmSPI_VS_OUT_CONFIG, 1, "SPI_VS_OUT_CONFIG");
    breaking(mmPA_CL_VS_OUT_CNTL, 1, "PA_CL_VS_OUT_CNTL");
    breaking(mmSPI_PS_IN_CONTROL_0, 2, "SPI_PS_IN_CONTROL");
    breaking(mmSPI_PS_INPUT_CNTL_0, 32, "SPI_PS_INPUT_CNTL");
    breaking(REGADDR::VGT_PRIMITIVE_TYPE, 1, "VGT_PRIMITIVE_TYPE");
    breaking(mmSPI_INTERP_CONTROL_0, 1, "SPI_INTERP_CONTROL_0");
    breaking(mmVGT_STRMOUT_EN, 1, "VGT_STRMOUT_EN");
    for (uint32_t b = 0; b < 4; ++b) breaking(mmVGT_STRMOUT_VTX_STRIDE_0 + b * 4, 1, "VGT_STRMOUT_VTX_STRIDE");
    breaking(REGADDR::VGT_GS_MODE, 1, "VGT_GS_MODE");
    breaking(REGADDR::SQ_CONFIG, 1, "SQ_CONFIG");
    breaking(mmCB_SHADER_MASK, 1, "CB_SHADER_MASK");
    breaking(mmCB_SHADER_CONTROL, 1, "CB_SHADER_CONTROL");
    breaking(mmDB_SHADER_CONTROL, 1, "DB_SHADER_CONTROL");
    breaking(mmSPI_INPUT_Z, 1, "SPI_INPUT_Z");
    breaking(REGADDR::SX_ALPHA_TEST_CONTROL, 1, "SX_ALPHA_TEST_CONTROL");
    breaking(REGADDR::PA_CL_VTE_CNTL, 1, "PA_CL_VTE_CNTL");
    breaking(REGADDR::PA_CL_CLIP_CNTL, 1, "PA_CL_CLIP_CNTL");
    breaking(REGADDR::DB_DEPTH_CONTROL, 1, "DB_DEPTH_CONTROL");
    breaking(REGADDR::CB_COLOR_CONTROL, 1, "CB_COLOR_CONTROL");
    breaking(REGADDR::CB_TARGET_MASK, 1, "CB_TARGET_MASK");
    breaking(mmCB_COLOR0_INFO, 8, "CB_COLOR_INFO");
    for (uint32_t base : {uint32_t(REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS), uint32_t(REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS),
                          uint32_t(REGADDR::SQ_TEX_RESOURCE_WORD0_N_GS)})
        breaking(base, 7 * LATTE_NUM_MAX_TEX_UNITS, "texture unit");
    breaking(REGADDR::SQ_TEX_SAMPLER_WORD0_0, 3 * 3 * LATTE_NUM_MAX_TEX_UNITS, "sampler");
    // programs (shaders.cpp translate/get_fetch_shader)
    for (uint32_t p : {uint32_t(mmSQ_PGM_START_VS), uint32_t(mmSQ_PGM_START_PS), uint32_t(mmSQ_PGM_START_FS)})
        breaking(p, 2, "program address/size");
    // pipeline key (draw.cpp pipeline())
    breaking(REGADDR::PA_SU_SC_MODE_CNTL, 1, "PA_SU_SC_MODE_CNTL");
    breaking(REGADDR::PA_SU_POLY_OFFSET_FRONT_SCALE, 1, "poly offset");
    breaking(REGADDR::PA_SU_POLY_OFFSET_FRONT_OFFSET, 1, "poly offset");
    breaking(REGADDR::PA_SU_POLY_OFFSET_CLAMP, 1, "poly offset");
    breaking(REGADDR::CB_BLEND0_CONTROL, 8, "CB_BLENDn_CONTROL");
    for (uint32_t b = 0; b < 16; ++b) breaking(mmSQ_VTX_ATTRIBUTE_BLOCK_START + b * 7 + 2, 5, "vertex stride/format");
    for (uint32_t reg : {uint32_t(REGADDR::DB_STENCILREFMASK), uint32_t(REGADDR::DB_STENCILREFMASK_BF)})
        for (uint32_t bit = 8; bit < 32; ++bit)
            if (continued(reg, 0x12, 0x12 | (1u << bit))) {
                std::printf("stencil mask bit %u of %04X is continued\n", bit, reg);
                ++failures;
            }
    // render targets (surfaces.cpp color_target, depth_target)
    for (uint32_t base : {uint32_t(mmCB_COLOR0_BASE), uint32_t(mmCB_COLOR0_SIZE), uint32_t(mmCB_COLOR0_VIEW),
                          uint32_t(mmCB_COLOR0_INFO), uint32_t(mmCB_COLOR0_TILE), uint32_t(mmCB_COLOR0_FRAG)})
        breaking(base, 8, "colour target");
    for (uint32_t reg : {uint32_t(mmDB_DEPTH_BASE), uint32_t(mmDB_DEPTH_SIZE), uint32_t(mmDB_DEPTH_VIEW),
                         uint32_t(mmDB_DEPTH_INFO), uint32_t(mmDB_HTILE_DATA_BASE), uint32_t(kDepthSlicesReg)})
        breaking(reg, 1, "depth target");

    // re-read by every draw
    continues(mmSQ_ALU_CONSTANT0_0, 0x1000, "ALU constants");
    for (uint32_t base : {uint32_t(mmSQ_VTX_UNIFORM_BLOCK_START), uint32_t(mmSQ_PS_UNIFORM_BLOCK_START),
                          uint32_t(mmSQ_GS_UNIFORM_BLOCK_START)})
        continues(base, 7 * 16, "uniform blocks");
    for (uint32_t b = 0; b < 16; ++b) continues(mmSQ_VTX_ATTRIBUTE_BLOCK_START + b * 7, 2, "vertex buffer pointer/size");
    continues(REGADDR::PA_CL_VPORT_XSCALE, 6, "viewport");
    continues(REGADDR::PA_SC_GENERIC_SCISSOR_TL, 2, "scissor");
    continues(REGADDR::CB_BLEND_RED, 4, "blend colour");
    continues(REGADDR::SX_ALPHA_REF, 1, "alpha reference");
    continues(REGADDR::PA_SU_POINT_SIZE, 1, "point size");
    continues(REGADDR::VGT_MULTI_PRIM_IB_RESET_INDX, 1, "restart index");
    continues(REGADDR::VGT_MULTI_PRIM_IB_RESET_EN, 1, "restart enable");
    for (uint32_t reg : {uint32_t(REGADDR::DB_STENCILREFMASK), uint32_t(REGADDR::DB_STENCILREFMASK_BF)})
        if (!continued(reg, 0x00FFFF12, 0x00FFFF34)) {
            std::printf("stencil reference of %04X ends continuations\n", reg);
            ++failures;
        }
    std::printf("state_groups_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
