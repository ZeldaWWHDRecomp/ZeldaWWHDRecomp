// SPDX-License-Identifier: MPL-2.0
// The linkage words of a shader key, shared by the Vulkan and Metal renderers (docs/vulkan.md,
// "Shader keys"): the registers the decompiler reads for every program of a stage. The program's
// own analysis (its texture units, exports, colour outputs) adds the variant words in each renderer.
#pragma once
#include <cstdint>
#include <cstring>
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"

namespace gfx {
constexpr size_t kShaderLinkageWords = 3 + GPU7_PS_MAX_INPUTS + 32 + 4;

// The PS input table (latte_support.cpp LatteShader_CreatePSInputTable): input count, position
// import, parameter generation, and per input its semantic, flat and noperspective bits.
inline size_t gather_ps_input_table(const uint32_t* regs, uint32_t* out) {
    size_t count = 0;
    const uint32_t control0 = regs[mmSPI_PS_IN_CONTROL_0];
    out[count++] = control0 & 0x03FFFD3Fu;
    const uint32_t inputs = control0 & 0x3F;
    for (uint32_t i = 0; i < inputs && i < GPU7_PS_MAX_INPUTS; ++i) out[count++] = regs[mmSPI_PS_INPUT_CNTL_0 + i] & 0x14FFu;
    return count;
}

// packWords: the extra words Cemu graphics-pack shader names hash (whole VTE word, rect primitives).
// vertexFlags: a renderer's own vertex-shader variant bits, added to the flags word (Vulkan: 64, the
// emulated depth clamp; bits 1-32 are taken here).
inline size_t gather_shader_linkage(const uint32_t* regs, bool vertex, const LatteFetchShader* fetch, bool packWords,
                                    uint32_t* out, uint32_t vertexFlags = 0) {
    using Latte::REGADDR;
    size_t count = 0;
    uint32_t flags = (regs[REGADDR::VGT_GS_MODE] & 3) != 0 ? 1u : 0u;
    if (vertex) {
        // Each fetch attribute is imported into the register after the first SQ_VTX_SEMANTIC slot
        // holding its semantic id (LatteDecompiler_emitAttributeImport, analyzer relative reads);
        // the other slots are never read. One byte per attribute, 0xFF when unmapped.
        uint32_t attributes = 0;
        if (fetch)
            for (const auto& group : fetch->bufferGroups) attributes += group.attribCount;
        if (attributes <= 4 * 32) {
            uint32_t packed = 0, bytes = 0;
            if (fetch)
                for (const auto& group : fetch->bufferGroups)
                    for (int i = 0; i < group.attribCount; ++i) {
                        uint32_t location = 0xFF;
                        for (uint32_t f = 0; f < 32; ++f)
                            if (regs[mmSQ_VTX_SEMANTIC_0 + f] == group.attrib[i].semanticId) { location = f; break; }
                        packed |= location << (8 * (bytes++ & 3));
                        if ((bytes & 3) == 0) { out[count++] = packed; packed = 0; }
                    }
            if (bytes & 3) out[count++] = packed;
            out[count++] = attributes;
        } else {
            std::memcpy(out + count, regs + mmSQ_VTX_SEMANTIC_0, 32 * sizeof(uint32_t));
            count += 32;
        }
        const uint32_t vte = regs[REGADDR::PA_CL_VTE_CNTL];
        const uint32_t primitive = regs[REGADDR::VGT_PRIMITIVE_TYPE] & 0x3F;
        flags |= (primitive == 1 ? 2u : 0u) | (regs[mmVGT_STRMOUT_EN] ? 4u : 0u) |
                 ((vte & 0x15) != 0x15 ? 8u : 0u) |                       // any viewport scale off
                 (regs[REGADDR::PA_CL_CLIP_CNTL] & (1u << 19) ? 16u : 0u) | // DX clip space (half Z)
                 vertexFlags;
        if (packWords) {
            // graphics-pack shader names hash the whole VTE word and rect primitives
            out[count++] = vte;
            out[count++] = primitive == 0x11 ? 1u : 0u;
        }
    } else {
        // PS input table; every input is declared. Vertex shaders only need where their exports
        // land (the variant words).
        count += gather_ps_input_table(regs, out + count);
        out[count++] = regs[mmSPI_PS_IN_CONTROL_1] & 0x1FF00u;  // front-face import
        out[count++] = regs[mmCB_SHADER_MASK];
        const uint32_t alpha = regs[REGADDR::SX_ALPHA_TEST_CONTROL];
        out[count++] = alpha & 8 ? alpha & 0xF : 0;
        flags |= regs[mmSPI_INTERP_CONTROL_0] & (1u << 1) ? 32u : 0u;  // point sprite
    }
    out[count++] = flags;
    return count;
}
}  // namespace gfx
