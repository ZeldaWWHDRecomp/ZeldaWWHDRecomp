#pragma once
// Register classes: what a register write has to do, looked up per register in tables built once
// (gx2_core.cpp, on the first register command, when the renderer is chosen) instead of walking
// range lists for every written word. Per register: its state group (state_groups.h), whether its
// changes leave shader translation alone (shader_irrelevant), whether the render-thread profiler's
// draw classifier counts it as a buffer/constant word (rprof::fast_class_reg); with a shader key
// mask, the bits of it that count for the shader key. register_class_test checks every register
// against the functions the tables are built from.
// Idea from the GreenNaugahyde Android fork (v0.9.5), which looks register kinds up by register.
#include <cstdint>
#include <vector>

#include "gx2_regs.h"
#include "shader_key_dirty.h"
#include "state_groups.h"

namespace gx2 {

// Register writes that can change how shaders are translated bump g_shader_state_gen; these don't:
// uniforms, uniform/vertex buffer addresses (the vertex stride does), and with addressMemo (Vulkan,
// WWHD_VK_SHADER_ADDRESS_MEMO=1) texture address words, which bind_stage resolves freshly.
inline bool shader_irrelevant_reg(uint32_t reg, bool addressMemo) {
    using Latte::REGADDR;
    if (addressMemo)
        for (uint32_t base : {uint32_t(REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS),
                              uint32_t(REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS),
                              uint32_t(REGADDR::SQ_TEX_RESOURCE_WORD0_N_GS)})
            if (reg >= base && reg < base + 7 * 18) {
                uint32_t word = (reg - base) % 7;
                if (word == 2 || word == 3) return true;
            }
    if (reg >= mmSQ_ALU_CONSTANT0_0 && reg < mmSQ_ALU_CONSTANT0_0 + 0x1000) return true;
    for (uint32_t base : {uint32_t(mmSQ_VTX_UNIFORM_BLOCK_START), uint32_t(mmSQ_PS_UNIFORM_BLOCK_START),
                          uint32_t(mmSQ_GS_UNIFORM_BLOCK_START)})
        if (reg >= base && reg < base + 7 * 16) return true;
    if (reg >= mmSQ_VTX_ATTRIBUTE_BLOCK_START && reg < mmSQ_VTX_ATTRIBUTE_BLOCK_START + 7 * 16) {
        uint32_t w = (reg - mmSQ_VTX_ATTRIBUTE_BLOCK_START) % 7;
        return w != 2;  // word 2 holds the stride
    }
    return false;
}

enum : uint8_t {
    kRcGroup = 0x0F,             // state group (state_groups.h; kSgStencilRefMask included)
    kRcShaderIrrelevant = 0x10,  // shader_irrelevant_reg()
    kRcProfFast = 0x20,          // rprof::fast_class_reg()
};
static_assert(kSgStencilRefMask <= kRcGroup, "state groups must fit the class table");

struct RegisterClasses {
    uint8_t cls[kNumRegs];
    template <class ProfFast>
    void build(bool addressMemo, ProfFast profFast) {
        for (uint32_t r = 0; r < kNumRegs; ++r)
            cls[r] = uint8_t(state_group(r)) | (shader_irrelevant_reg(r, addressMemo) ? kRcShaderIrrelevant : 0) |
                     (profFast(r) ? kRcProfFast : 0);
    }
};

// Shader key bits per register for a key mask (shader_key_changed() by table): the mask's bits, or
// all bits where the mask does not cover the register (then any change counts). Empty: no mask.
struct ShaderKeyBits {
    std::vector<uint32_t> bits;
    void build(bool (*keyMask)(uint32_t reg, uint32_t& mask)) {
        bits.clear();
        if (!keyMask) return;
        bits.assign(kNumRegs, ~0u);
        for (uint32_t r = 0; r < kNumRegs; ++r) {
            uint32_t m;
            if (keyMask(r, m)) bits[r] = m;
        }
    }
    // a change of a register (old != value) changes the shader key
    bool changed(uint32_t reg, uint32_t old, uint32_t value) const {
        return bits.empty() || ((old ^ value) & bits[reg]) != 0;
    }
};

}  // namespace gx2
