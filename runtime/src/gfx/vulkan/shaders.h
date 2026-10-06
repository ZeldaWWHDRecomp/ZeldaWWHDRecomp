#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"

struct LatteFetchShader;
namespace gfxvk::vk {
struct DescriptorRankPlan {
    static constexpr uint8_t unused = 255;
    std::array<uint8_t, 16> blocks;
    std::array<uint8_t, LATTE_NUM_MAX_TEX_UNITS> textures;
    uint8_t support = unused, count = 0;
    bool valid = false;
    DescriptorRankPlan() { blocks.fill(unused); textures.fill(unused); }
};
DescriptorRankPlan make_descriptor_rank_plan(const LatteDecompilerShaderResourceMapping& mapping,
                                             const LatteDecompilerShader& shader);
// Metadata uses the decompiler's original binding numbers and std140 byte offsets.
// Vertex descriptors occupy set 0, pixel descriptors set 1. Device modules are
// created by the draw backend; this cache owns only translation and SPIR-V.
struct Shader {
    uint64_t key = 0;
    bool vertex = false;
    LatteDecompilerShader* dec = nullptr;
    LatteDecompilerShaderResourceMapping mapping;
    DescriptorRankPlan descriptorRanks;
    LatteDecompilerOutputUniformOffsets uniforms;
    std::vector<uint32_t> spirv;
    std::string glsl;
    std::string error;
    bool ready() const { return dec && !spirv.empty() && error.empty(); }
};
void select_renderer();
// Pass the renderer frame to revalidate program bytes once per frame. Omitting
// it keeps immediate revalidation for standalone callers and shader tools.
LatteFetchShader* get_fetch_shader(const uint32_t* regs, uint64_t* keyOut, uint64_t frame = ~uint64_t{0});
Shader* translate(const uint32_t* regs, bool vertex, LatteFetchShader* fetchShader, uint64_t fsKey,
    uint64_t frame = ~uint64_t{0}, uint64_t stateGeneration = ~uint64_t{0});
// Generation must advance for every shader-relevant register write. Primitive
// mode is checked separately because draw submission writes it directly.
struct ShaderStats {
    uint64_t stateHashLookups = 0, stateHashMemoHits = 0, stateHashBytes = 0;
    uint64_t fetchLookups = 0, fetchLastHits = 0;
    uint64_t lookups = 0, lastHits = 0, variantHits = 0, variantAliases = 0, compiles = 0, compileNs = 0;
    uint64_t decompileNs = 0, spirvCompiles = 0, spirvCompileNs = 0, diskHits = 0, spirvReuseHits = 0;
    uint64_t diskLoads = 0, diskLoadNs = 0, diskSaves = 0, diskSaveNs = 0, diskSavedBytes = 0, diskSnapshotNs = 0;
};
ShaderStats shader_stats();
// Render-thread-only checkpoint schedules one immutable disk worker after quiet frames.
// Explicit save joins that worker and synchronously saves the latest revision.
// Disk cache retains SPIR-V only; decompiler metadata is always rebuilt safely.
void checkpoint_shader_cache(uint64_t frame);
bool shader_cache_dirty();
uint64_t shader_cache_changed_frame();
bool save_shader_cache();
std::vector<uint32_t> compile_glsl(const std::string& source, bool vertex, std::string* error = nullptr);
// Caller-owned CPU scratch; all active bytes are freshly zeroed and packed.
void pack_uniforms_into(const uint32_t* regs, const Shader& shader,
    std::vector<uint8_t>& data, float scaleX = 1.0f, float scaleY = 1.0f);
std::vector<uint8_t> pack_uniforms(const uint32_t* regs, const Shader& shader,
    float scaleX = 1.0f, float scaleY = 1.0f);
// After guest memory replacement and render drain; compiled variants stay alive.
void reset_shader_memoization();
// Call only after draw code has discarded pipelines and references to Shader.
void clear_shader_cache();
} // namespace gfxvk::vk
