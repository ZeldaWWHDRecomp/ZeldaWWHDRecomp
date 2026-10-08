#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include "../shader_identity.h"
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
    gfx::ProgramKind kind = gfx::ProgramKind::Other;
    uint64_t pipelineId = 0;  // equal for shaders with identical SPIR-V and resource mapping
    bool vertex = false;
    LatteDecompilerShader* dec = nullptr;
    LatteDecompilerShaderResourceMapping mapping;
    DescriptorRankPlan descriptorRanks;
    LatteDecompilerOutputUniformOffsets uniforms;
    std::vector<uint32_t> spirv;
    std::string glsl;
    std::string translatedGlsl;  // verify mode only: the translation a graphics pack replaced
    std::string error;
    bool ready() const { return dec && !spirv.empty() && error.empty(); }
};
void select_renderer();
// Pass the renderer frame to revalidate program bytes once per frame. Omitting
// it keeps immediate revalidation for standalone callers and shader tools.
LatteFetchShader* get_fetch_shader(const uint32_t* regs, uint64_t* keyOut, uint64_t frame = ~uint64_t{0});
// linkedVs (pixel shaders): the draw's vertex shader; inputs it has no output for read the GPU's
// default value for them (SPI_PS_INPUT_CNTL DEFAULT_VAL), as constants.
Shader* translate(const uint32_t* regs, bool vertex, LatteFetchShader* fetchShader, uint64_t fsKey,
    uint64_t frame = ~uint64_t{0}, uint64_t stateGeneration = ~uint64_t{0}, const Shader* linkedVs = nullptr);
// Generation must advance for every shader-relevant register write (every register the key
// reads must be covered by gx2's shader_irrelevant()/vulkan_shader_key_mask()). Primitive
// mode is checked separately because draw submission writes it directly.
struct ShaderStats {
    uint64_t stateHashLookups = 0, stateHashMemoHits = 0, stateHashBytes = 0;
    uint64_t fetchLookups = 0, fetchLastHits = 0;
    uint64_t lookups = 0, lastHits = 0, variantHits = 0, compiles = 0, compileNs = 0;
    uint64_t decompileNs = 0, spirvCompiles = 0, spirvCompileNs = 0, diskHits = 0, spirvReuseHits = 0;
    uint64_t diskLoads = 0, diskLoadNs = 0, diskSaves = 0, diskSaveNs = 0, diskSavedBytes = 0, diskSnapshotNs = 0;
    // Narrow keys: programs = linkage keys, shaders = distinct translations, variantKeys = full
    // keys (several can share a shader: variantAliases). Verify mode (WWHD_VK_SHADER_KEY_VERIFY):
    // pre-narrowing keys checked, violations (a shared shader that would have translated
    // differently), and pre-narrowing keys that the narrow key splits.
    uint64_t programs = 0, shaders = 0, variantKeys = 0, variantAliases = 0, moduleAliases = 0;
    uint64_t verifyChecks = 0, verifyViolations = 0, verifySplitKeys = 0, verifyNs = 0;
};
ShaderStats shader_stats();
// Render-thread-only checkpoint schedules one immutable disk worker after quiet frames.
// Explicit save joins that worker and synchronously saves the latest revision.
// Disk cache retains SPIR-V only; decompiler metadata is always rebuilt safely.
void checkpoint_shader_cache(uint64_t frame);
bool shader_cache_dirty();
uint64_t shader_cache_changed_frame();
bool save_shader_cache();
std::vector<uint32_t> compile_compute(const std::string& source, std::string* error = nullptr);
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
