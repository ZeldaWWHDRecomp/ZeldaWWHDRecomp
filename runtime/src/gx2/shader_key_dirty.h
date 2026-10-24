#pragma once
#include <cstdint>
#include <initializer_list>
#include "gx2_regs.h"

namespace gx2 {
// Only these proven registers have partial/no influence on Vulkan's current
// shader key. Unknowns, program headers and vertex strides stay conservative.
inline bool vulkan_shader_key_mask(uint32_t reg, uint32_t& mask) {
    using Latte::REGADDR;
    for(uint32_t base : {uint32_t(REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS),
                         uint32_t(REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS)}) {
        if(reg >= base && reg < base + 7 * LATTE_NUM_MAX_TEX_UNITS) {
            switch((reg - base) % 7) {
            case 0: mask = 7; break;
            case 1: mask = 0x03F00000; break;
            case 4: mask = 0x300; break;
            default: mask = 0; break;
            }
            return true;
        }
    }
    constexpr uint32_t samplers = uint32_t(REGADDR::SQ_TEX_SAMPLER_WORD0_0);
    if(reg >= samplers && reg < samplers + 3 * 3 * LATTE_NUM_MAX_TEX_UNITS) {
        mask = (reg - samplers) % 3 == 0 ? 0xF8000000 : 0;
        return true;
    }
    if(reg == REGADDR::VGT_PRIMITIVE_TYPE) { mask = 0x3F; return true; }
    if(reg == mmSPI_INTERP_CONTROL_0) { mask = 1u << 1; return true; }
    if(reg == REGADDR::PA_CL_VTE_CNTL) { mask = 0x3F; return true; }
    // DX clip space; ZCLIP_FAR_DISABLE picks the vertex shader variant without device depth clamp
    if(reg == REGADDR::PA_CL_CLIP_CNTL) { mask = 1u << 19 | 1u << 27; return true; }
    if(reg == REGADDR::DB_DEPTH_CONTROL) { mask = 0x83; return true; }
    if((reg >= REGADDR::PA_CL_VPORT_XSCALE && reg <= REGADDR::PA_CL_VPORT_ZOFFSET) ||
       (reg >= REGADDR::CB_BLEND_RED && reg <= REGADDR::CB_BLEND_ALPHA) ||
       (reg >= REGADDR::CB_BLEND0_CONTROL && reg < REGADDR::CB_BLEND0_CONTROL + 8) ||
       reg == REGADDR::PA_SC_GENERIC_SCISSOR_TL || reg == REGADDR::PA_SC_GENERIC_SCISSOR_BR ||
       reg == REGADDR::DB_STENCILREFMASK || reg == REGADDR::DB_STENCILREFMASK_BF ||
       reg == REGADDR::PA_SU_SC_MODE_CNTL || reg == REGADDR::PA_SU_POLY_OFFSET_FRONT_SCALE ||
       reg == REGADDR::PA_SU_POLY_OFFSET_FRONT_OFFSET || reg == REGADDR::PA_SU_POLY_OFFSET_CLAMP) {
        mask = 0;
        return true;
    }
    return false;
}
} // namespace gx2
