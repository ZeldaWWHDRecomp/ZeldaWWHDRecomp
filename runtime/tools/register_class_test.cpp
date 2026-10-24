// Register class tables (runtime/src/gx2/register_class.h) and thread identities (thread_identity.h): for every register, the table gives
// exactly what the functions it is built from say (state group, shader relevance with and without
// the texture-address memo, the profiler's buffer/constant class) and, for the Vulkan shader key
// mask, the same answer as shader_key_changed() for every single-bit change and a few wider ones.
#include "register_class.h"
#include "render_prof.h"
#include "gx2.h"
#include "thread_identity.h"

#include <atomic>
#include <set>
#include <thread>
#include <vector>

#include <cstdio>
#include <memory>

using namespace gx2;

static int failures = 0;
static void fail(const char* what, uint32_t reg) {
    if (++failures <= 20) std::printf("register %04X: %s\n", reg, what);
}
static bool prof_fast(uint32_t reg) { return rprof::fast_class_reg(reg); }
// shader_key_changed (backend_hooks.h), restated without the renderer headers
static bool key_changed_by_mask(uint32_t reg, uint32_t old, uint32_t value) {
    uint32_t mask;
    return !vulkan_shader_key_mask(reg, mask) || ((old ^ value) & mask) != 0;
}

int main() {
    for (bool memo : {false, true}) {
        auto classes = std::make_unique<RegisterClasses>();
        classes->build(memo, prof_fast);
        for (uint32_t r = 0; r < kNumRegs; ++r) {
            const uint8_t c = classes->cls[r];
            if ((c & kRcGroup) != state_group(r)) fail("state group differs", r);
            if (bool(c & kRcShaderIrrelevant) != shader_irrelevant_reg(r, memo)) fail("shader relevance differs", r);
            if (bool(c & kRcProfFast) != prof_fast(r)) fail("profiler class differs", r);
            if (c & ~uint8_t(kRcGroup | kRcShaderIrrelevant | kRcProfFast)) fail("stray class bits", r);
        }
    }
    // spot checks of the relevance rules themselves
    if (!shader_irrelevant_reg(mmSQ_ALU_CONSTANT0_0 + 5, false)) fail("ALU constant counted as shader-relevant", mmSQ_ALU_CONSTANT0_0 + 5);
    if (shader_irrelevant_reg(mmSQ_VTX_ATTRIBUTE_BLOCK_START + 2, false)) fail("vertex stride counted as shader-irrelevant", mmSQ_VTX_ATTRIBUTE_BLOCK_START + 2);
    if (!shader_irrelevant_reg(mmSQ_VTX_ATTRIBUTE_BLOCK_START + 7, false)) fail("vertex buffer address counted as shader-relevant", mmSQ_VTX_ATTRIBUTE_BLOCK_START + 7);
    const uint32_t texAddress = uint32_t(Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS) + 2;
    if (shader_irrelevant_reg(texAddress, false) || !shader_irrelevant_reg(texAddress, true)) fail("texture address memo rule", texAddress);

    ShaderKeyBits none;
    none.build(nullptr);
    if (!none.bits.empty() || !none.changed(Latte::REGADDR::PA_CL_CLIP_CNTL, 0, 1)) fail("no mask: every change counts", 0);
    ShaderKeyBits bits;
    bits.build(vulkan_shader_key_mask);
    for (uint32_t r = 0; r < kNumRegs; ++r) {
        for (int b = 0; b < 32; ++b) {
            const uint32_t old = 0x5A5A5A5Au, value = old ^ (1u << b);
            if (bits.changed(r, old, value) != key_changed_by_mask(r, old, value)) fail("key bit differs", r);
        }
        for (uint32_t delta : {0x3u, 0x80000001u, 0xFFFFFFFFu, 0x00F000F0u})
            if (bits.changed(r, 0x12345678u, 0x12345678u ^ delta) != key_changed_by_mask(r, 0x12345678u, 0x12345678u ^ delta))
                fail("key bits differ for a wider change", r);
    }
    // thread identities (display-list recording slots): distinct for threads alive together
    {
        constexpr int kThreads = 8;
        uintptr_t ids[kThreads + 1] = {};
        std::vector<std::thread> threads;
        std::atomic<int> arrived{0};
        for (int i = 0; i < kThreads; ++i)
            threads.emplace_back([&, i] {
                ids[i] = thread_identity();
                ++arrived;
                while (arrived < kThreads) std::this_thread::yield();  // all alive at once
                if (thread_identity() != ids[i]) fail("thread identity not stable", uint32_t(i));
            });
        for (auto& t : threads) t.join();
        ids[kThreads] = thread_identity();
        if (std::set<uintptr_t>(ids, ids + kThreads + 1).size() != kThreads + 1) fail("thread identities collide", 0);
    }
    if (failures) {
        std::printf("register_class_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("register_class_test: ok\n");
    return 0;
}
