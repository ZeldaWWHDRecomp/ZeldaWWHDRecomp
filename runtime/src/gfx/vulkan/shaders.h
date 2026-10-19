#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <memory>
#include "../shader_identity.h"
#include "shader_source.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"
namespace stereo { struct EyeRows; }

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
struct ShaderBinary;
// CPU execution plan for a shader's uniform inputs. Built with the translation,
// so a continued draw does not walk decompiler lists or rediscover copy ranges.
struct UniformInputSpan {
    uint32_t reg = 0, source = 0, destination = 0, bytes = 0;
    bool indirect = false;
};
struct UniformInputPlan {
    std::vector<UniformInputSpan> spans;
    size_t bytes = 0;
    bool ready = false, readsGuestMemory = false;
};
bool shader_binary_ready(const std::shared_ptr<ShaderBinary>& binary);
std::vector<uint32_t> shader_binary_words(const std::shared_ptr<ShaderBinary>& binary);
std::vector<uint32_t> shader_binary_words_exact(const std::shared_ptr<ShaderBinary>& binary, uint64_t frame);
struct Shader {
    uint64_t key = 0;
    gfx::ProgramKind kind = gfx::ProgramKind::Other;
    uint64_t pipelineId = 0;  // portable canonical GLSL+metadata identity, stable before SPIR-V
    bool vertex = false;
    LatteDecompilerShader* dec = nullptr;
    LatteDecompilerShaderResourceMapping mapping;
    DescriptorRankPlan descriptorRanks;
    LatteDecompilerOutputUniformOffsets uniforms;
    UniformInputPlan uniformInputs;
    // Optional mono variant: one uniform descriptor contains several draws.
    // Guest instance ID remains zero; the native instance index selects data.
    std::shared_ptr<Shader> drawArrayVariant;
    Shader* drawArrayBase = nullptr;
    uint32_t drawArrayCount = 0;
    SpirvWords spirvWords; // shared with the SPIR-V disk cache entry
    std::shared_ptr<ShaderBinary> binary; // CPU result; immutable metadata is already published
    std::string glsl; // Mutable translation scratch, released once the compile is requested.
    // Final GLSL identity (hash + length): aliasing, disk cache and pending
    // compilations compare this. The text itself is kept (retainedSource)
    // only in the verification modes (WWHD_VK_SHADER_KEY_VERIFY,
    // WWHD_VK_SHADER_SOURCE_VERIFY=1).
    ShaderSourceId sourceId;
    uint64_t spirvKey = 0; // disk cache / pending compilation key of the final GLSL
    ShaderSource retainedSource;
    const std::string& source_text() const { return retainedSource ? *retainedSource : glsl; }
    std::string translatedGlsl;  // verify mode only: the translation a graphics pack replaced
    std::string error;
    const std::vector<uint32_t>& spirv() const {
        static const std::vector<uint32_t> none;
        return spirvWords ? *spirvWords : none;
    }
    bool ready() const { return dec && (!spirv().empty() || binary) && error.empty(); }
};
// Render-thread-only lookup; recorded pipeline identity, never program hash.
const Shader* find_pipeline_shader(uint64_t pipelineId, bool vertex);
// Resolve the optional batching binary only for an eligible mono draw.
Shader* draw_array_variant(Shader& shader, uint64_t frame);
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
// Render-thread-only diagnostic payload/capacity counters, not total allocator heap.
// SPIR-V shared by a shader and its disk entry is counted once (spirvCapacity
// for shaders, diskSpirvCapacity for disk entries no shader uses).
struct ShaderMemoryStats {
    uint64_t shaders = 0, decompilerText = 0, glslCapacity = 0, verifyCapacity = 0;
    uint64_t spirvCapacity = 0, diskEntries = 0, diskSpirvCapacity = 0;
    uint64_t pendingCompiles = 0, retainedSourceCapacity = 0;
};
ShaderMemoryStats shader_memory_stats();
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
// eyeRows: receives the rows the eye offset shifted (stereo_projection.h)
// Optional immutable guest inputs for comparing packers against the same bytes.
struct UniformReader {
    void* context;
    const void* (*read)(void*, uint32_t, size_t);
};
void pack_uniforms_into(const uint32_t* regs, const Shader& shader,
    std::vector<uint8_t>& data, float scaleX = 1.0f, float scaleY = 1.0f,
    float eyeOffset = 0, float convergence = 600, stereo::EyeRows* eyeRows = nullptr,
    const UniformReader* reader = nullptr);
// Updates an already initialized mono payload; padding and texture scales are
// retained from its resolved stage. Guest inputs and dynamic fields stay fresh.
bool update_prepared_uniforms(const uint32_t* regs, const Shader& shader,
    std::vector<uint8_t>& data, float scaleX, float scaleY, const UniformReader* reader = nullptr);
std::vector<uint8_t> pack_uniforms(const uint32_t* regs, const Shader& shader,
    float scaleX = 1.0f, float scaleY = 1.0f);
// After guest memory replacement and render drain; compiled variants stay alive.
void reset_shader_memoization();
// Call only after draw code has discarded pipelines and references to Shader.
void clear_shader_cache();
} // namespace gfxvk::vk
