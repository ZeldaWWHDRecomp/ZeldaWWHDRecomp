// SPDX-License-Identifier: MPL-2.0
// The keys the Metal renderer names shaders and fetch shaders by in its recipe files (metal_draw.mm "shader keys",
// shaders.bin, the head start's type 1/2 records): the wide key of a stage's state and the fetch shader key. Shared,
// so that the Vulkan setup preparation (vulkan/headstart.cpp) resolves the head start's Metal pipeline recipes with
// exactly the derivation the Metal renderer uses. Pure functions of the registers and the guest programs.
#pragma once
#include <cstdint>
#include <cstring>

#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "ppc.h"

namespace gfx::mtlkey {

// fast 64-bit hash over 8-byte words (tail handled bytewise)
inline uint64_t hash_bytes(const void* data, size_t n, uint64_t h = 0x9E3779B97F4A7C15ull) {
    const uint8_t* p = (const uint8_t*)data;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t v;
        memcpy(&v, p + i, 8);
        h = (h ^ v) * 0xFF51AFD7ED558CCDull;
        h ^= h >> 32;
    }
    for (; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h ^ (h >> 29);
}

constexpr uint32_t kStageWords = 400;
// the words a stage's wide key hashes (texBase: REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS or _PS); returns their count
inline uint32_t stage_words(const uint32_t* regs, uint32_t texBase, uint32_t* buf) {
    uint32_t n = 0;
    auto put = [&](uint32_t first, uint32_t count) { memcpy(&buf[n], &regs[first], count * 4); n += count; };
    put(mmSQ_VTX_SEMANTIC_0, 32);
    put(mmSPI_VS_OUT_ID_0, 10);
    put(mmSPI_VS_OUT_CONFIG, 1);
    put(mmPA_CL_VS_OUT_CNTL, 1);
    put(mmSPI_PS_IN_CONTROL_0, 2);
    put(mmSPI_PS_INPUT_CNTL_0, 32);
    put(Latte::REGADDR::SQ_CONFIG, 1);
    put(mmCB_SHADER_MASK, 1);
    put(mmCB_SHADER_CONTROL, 1);
    put(mmDB_SHADER_CONTROL, 1);
    put(mmSPI_INPUT_Z, 1);
    put(Latte::REGADDR::SX_ALPHA_TEST_CONTROL, 1);
    put(Latte::REGADDR::PA_CL_VTE_CNTL, 1);
    put(Latte::REGADDR::PA_CL_CLIP_CNTL, 1);
    put(Latte::REGADDR::CB_COLOR_CONTROL, 1);
    put(Latte::REGADDR::CB_TARGET_MASK, 1);
    put(mmCB_COLOR0_INFO, 8);
    uint32_t cbBase[8];
    for (int i = 0; i < 8; i++) {
        cbBase[i] = regs[mmCB_COLOR0_BASE + i] & ~0xFFu;
        buf[n++] = cbBase[i] != 0;
    }
    buf[n++] = regs[Latte::REGADDR::DB_DEPTH_CONTROL] & 0x83;
    // texture types and formats; sampling a bound render target switches to framebuffer fetch
    for (int t = 0; t < 18; t++) {
        const uint32_t* w = &regs[texBase + t * 7];
        buf[n++] = (w[0] & 7) | (w[4] & 0x300);
        buf[n++] = w[1] & 0x3F00000;
        uint32_t fb = 0, base = w[2] << 8;
        if (base)
            for (int i = 0; i < 8; i++)
                if (base == cbBase[i]) fb = 1 + i;
        buf[n++] = fb;
    }
    // depth-compare samplers
    for (int i = 0; i < 18 * 3; i++) buf[n++] = regs[Latte::REGADDR::SQ_TEX_SAMPLER_WORD0_0 + i * 3] & 0xF8000000;
    return n;
}

// a stage program's hash seed (metal_draw.mm get_shader_uncached: program hash ^ 0x1111 vertex / 0x2222 pixel)
inline uint64_t program_seed(const void* program, uint32_t size, bool vertex) {
    return hash_bytes(program, size) ^ (vertex ? 0x1111 : 0x2222);
}

// the wide key a recipe names a shader by: the stage state hashed with the program seed; vertex shaders also with
// the fetch shader key
inline uint64_t wide_key(const uint32_t* regs, bool vertex, uint64_t seed, uint64_t fetchKey) {
    uint32_t buf[kStageWords];
    const uint32_t n = stage_words(regs, vertex ? Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS, buf);
    const uint64_t key = hash_bytes(buf, n * 4, seed);
    return vertex ? key ^ fetchKey * 31 : key;
}

// strides decide between vertex-descriptor fetch and manual fetch in the shader: part of the fetch key
inline uint64_t mix_strides(const uint32_t* regs, uint64_t h) {
    for (uint32_t b = 0; b < 16; b++) h = (h ^ ((regs[mmSQ_VTX_ATTRIBUTE_BLOCK_START + b * 7 + 2] >> 11) & 0xFFFF)) * 1099511628211ull;
    return h;
}

// The fetch shader's program at regs[SQ_PGM_START_FS] (guest memory): the bytes its key hashes. WWFA ("our" attribute
// table): the table's own hash; otherwise the program bytes. false: no or an unusable fetch shader.
struct FetchProgram {
    bool attributeTable = false;  // "WWFA0001"
    uint32_t prog = 0, size = 0;  // the hashed bytes (not attributeTable)
    uint32_t count = 0;           // attributeTable: entries
    uint64_t tableHash = 0;
};
inline bool fetch_program(const uint32_t* regs, FetchProgram& out) {
    out = FetchProgram{};
    const uint32_t prog = regs[mmSQ_PGM_START_FS] << 8;
    if (!prog) return false;
    out.prog = prog;
    if (uint64_t(prog) + 32 <= 0x100000000ull && !memcmp(ppc_ptr(prog), "WWFA0001", 8)) {
        const uint8_t* p = (const uint8_t*)ppc_ptr(prog);
        auto word = [](const uint8_t* q) { return uint32_t(q[0]) | uint32_t(q[1]) << 8 | uint32_t(q[2]) << 16 | uint32_t(q[3]) << 24; };
        out.count = word(p + 16);
        if (out.count > 64) return false;
        out.attributeTable = true;
        out.tableHash = uint64_t(word(p + 24)) | uint64_t(word(p + 28)) << 32;
        return true;
    }
    // either our compact encoding (from GX2InitFetchShaderEx) or real fetch shader microcode shipped with the game
    const bool ours = ld32(prog) == 0x57574653;
    out.size = ours ? 16 + ld32(prog + 4) * 16 : regs[mmSQ_PGM_START_FS + 1] << 3;
    return out.size <= 0x1000;
}
inline uint64_t fetch_key(const uint32_t* regs, const FetchProgram& f) {
    return mix_strides(regs, f.attributeTable ? f.tableHash : hash_bytes(ppc_ptr(f.prog), f.size));
}

}  // namespace gfx::mtlkey
