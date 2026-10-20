#include "vertex_cpu_policy.h"
#include "../stereo_projection.h"
#include "../stereo.h"
#include "atomic_temporary.h"
// Real Latte microcode -> Cemu GLSL -> SPIR-V for the Vulkan backend.
#include "shaders.h"
#include "spirv_precision.h"
#include "pipeline_identity.h"
#include "shader_compiler.h"
#include "shader_translation.h"
#include "shader_binary.h"
#include "shader_disk_cache.h"
#include "worker_metrics.h"
#include "backend.h"
#include "vertex_formats.h"
#include "vertex_format_shader.h"
#include "headstart.h"
#include "stutter_trace.h"
#include "mods/cemu_pack.h"
#include "mods/shader_interface.h"
#include "graphic_pack_hash.h"
#include "gfx/area_sample.h"
#include "exact_state_memo.h"
#include "program_hash_cache.h"
#include "write_watch.h"
#include "render_prof.h"
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LattePSInputDefault.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "gx2/gx2.h"
#include "gx2/gx2_regs.h"
#include "ppc.h"
#include "util/helpers/StringBuf.h"
#include <memory>
#include <filesystem>
#include <glslang/build_info.h>
#include "platform/host.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <future>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <regex>

LatteDecompilerShader* FinishDecompiledShader(LatteDecompilerOutput_t& output);
LatteFetchShader* LatteShaderRecompiler_createFetchShader(LatteFetchShader::CacheHash hash,
    uint32* regs, uint32* code, uint32 size);

void log_msg(const char* fmt, ...);  // runtime.h (not included here: the Cemu headers)
namespace gfxvk::vk {
using Latte::REGADDR;
UniformInputPlan make_uniform_input_plan(const Shader& shader) {
    UniformInputPlan plan;
    const auto& offsets = shader.uniforms;
    plan.bytes = (std::max(offsets.offset_endOfBlock, 16) + 15) & ~15;
    if (!shader.dec) return plan;
    auto add = [&](uint32_t reg, uint32_t source, int destination, size_t bytes, bool indirect) {
        if (destination < 0 || size_t(destination) + bytes > plan.bytes || !bytes) return;
        plan.readsGuestMemory |= indirect;
        if (!plan.spans.empty()) {
            auto& last = plan.spans.back();
            if (last.indirect == indirect && last.reg == reg &&
                last.source + last.bytes == source && last.destination + last.bytes == uint32_t(destination)) {
                last.bytes += uint32_t(bytes);
                return;
            }
        }
        plan.spans.push_back({reg, source, uint32_t(destination), uint32_t(bytes), indirect});
    };
    const uint32_t alu = mmSQ_ALU_CONSTANT0_0 + (shader.vertex ? 0x400 : 0);
    const uint32_t block = shader.vertex ? mmSQ_VTX_UNIFORM_BLOCK_START : mmSQ_PS_UNIFORM_BLOCK_START;
    if (offsets.offset_remapped >= 0) {
        for (const auto& entry : shader.dec->list_remappedUniformEntries_register)
            add(alu, (entry.indexOffset / 4) * 4, offsets.offset_remapped + entry.mappedIndexOffset, 16, false);
        for (const auto& group : shader.dec->list_remappedUniformEntries_bufferGroups)
            for (const auto& entry : group.entries)
                add(block + group.kcacheBankIdOffset / 4, entry.indexOffset,
                    offsets.offset_remapped + entry.mappedIndexOffset, 16, true);
    }
    add(alu, 0, offsets.offset_uniformRegister, size_t(offsets.count_uniformRegister) * 16, false);
    // Missing guest blocks were zero in the original fresh payload. Preserve
    // that behavior without assuming malformed/overlapping metadata is safe.
    for (size_t i = 0; i < plan.spans.size(); ++i)
        for (size_t j = 0; j < i; ++j) {
            const auto& a = plan.spans[i];
            const auto& b = plan.spans[j];
            if (a.destination < b.destination + b.bytes && b.destination < a.destination + a.bytes)
                return plan;
        }
    plan.ready = true;
    return plan;
}
// Only this detached CPU result is shared with pipeline workers. Shader objects,
// cache maps, counters and disk-cache publication stay on the render thread.
bool shader_binary_ready(const std::shared_ptr<ShaderBinary>& binary) {
    return !binary || binary->ready();
}
std::vector<uint32_t> shader_binary_words(const std::shared_ptr<ShaderBinary>& binary) {
    if (!binary) throw std::runtime_error("missing shader binary prerequisite");
    auto result = binary->get();
    if (result.words.empty() || !result.error.empty())
        throw std::runtime_error(result.error.empty() ? "shader compilation produced no SPIR-V" : result.error);
    return std::move(result.words);
}
std::vector<uint32_t> shader_binary_words_exact(const std::shared_ptr<ShaderBinary>& binary, uint64_t frame) {
    std::exception_ptr presentationError;
    while (!shader_binary_ready(binary)) {
        if (frame != ~uint64_t{0} && !presentationError) try {
            begin_pipeline_pending_frame();
            present_completed_frame_while_pipeline_pending();
        } catch (...) { presentationError = std::current_exception(); }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto words = shader_binary_words(binary);
    if (presentationError) std::rethrow_exception(presentationError);
    return words;
}
namespace {
std::unordered_map<uint64_t, std::unique_ptr<Shader>> shaders;
std::unordered_map<uint64_t, LatteFetchShader*> fetchShaders;
// program bytes hashed again only after a write to them (program_hash_cache.h)
struct ProgramWatch {
    static bool active() { return wwatch::active(); }
    static uint64_t arm(uint32_t a, uint32_t n) { return wwatch::arm(a, n); }
    static bool written_since(uint32_t a, uint32_t n, uint64_t stamp) { return wwatch::written_since(a, n, stamp); }
    static uint64_t write_seq() { return wwatch::write_seq(); }
};
ProgramHashCache<ProgramWatch> programHashes;
struct LastShader {
    const uint32_t* regs = nullptr;
    LatteFetchShader* fetch = nullptr;
    uint64_t frame = 0, generation = 0, fsKey = 0;
    uint32_t primitive = 0;
    Shader* shader = nullptr;
};
LastShader lastShaders[2];
struct LastFetch {
    uint64_t frame = ~uint64_t{0}, key = 0;
    uint32_t address = 0, size = 0, count = 0;
    bool compact = false;
    LatteFetchShader* fetch = nullptr;
};
LastFetch lastFetch;
ShaderStats stats;
ExactStateMemo<3 + GPU7_PS_MAX_INPUTS + 32 + 4> stateMemo;  // linkage words (gather_linkage)
// Same full-byte word mixer used by the working Metal backend. memcpy keeps
// unaligned microcode/state safe; the tail still includes every remaining byte.
uint64_t hash_bytes(const void* bytes, size_t size, uint64_t hash = 0x9E3779B97F4A7C15ull) {
    const auto* p = static_cast<const uint8_t*>(bytes);
    size_t i=0;
    for (; i+8<=size; i+=8) {
        uint64_t word;
        memcpy(&word,p+i,8);
        hash=(hash^word)*0xFF51AFD7ED558CCDull;
        hash^=hash>>32;
    }
    for (; i<size; ++i) hash=(hash^p[i])*0x100000001B3ull;
    return hash^(hash>>29);
}
// Disk entries keep SPIR-V and the final GLSL's identity (hash + length), not
// the text (shader_disk_cache.h). Translator changes produce different GLSL and
// therefore a different key and identity.
// Bump this compile recipe whenever compilation options/defaults change.
constexpr char cacheRecipe[] = "WWVKSC02:glsl450:vk1.1:spv1.3:noopt:resources1:strictmul1:"
    GLSLANG_VERSION_FLAVOR;
constexpr size_t maxCacheBytes = diskcache::kMaxBytes;
constexpr size_t maxSourceBytes = diskcache::kMaxSourceBytes;
constexpr size_t maxCacheEntries = diskcache::kMaxEntries;
using DiskShader = diskcache::Entry;
std::unordered_map<uint64_t, DiskShader> diskShaders;
std::string diskPath;
bool diskInitialized = false, diskDirty = false;
uint64_t diskChangedFrame = 0, diskAttemptFrame = 0, diskRevision = 0;
size_t diskBytes = diskcache::kHeaderBytes;
uint64_t cache_checksum(const uint8_t* p, size_t n) { return diskcache::checksum(p, n); }
uint64_t cache_fingerprint() {
    return cache_checksum(reinterpret_cast<const uint8_t*>(cacheRecipe),sizeof(cacheRecipe)) ^
        (uint64_t(GLSLANG_VERSION_MAJOR)<<48) ^ (uint64_t(GLSLANG_VERSION_MINOR)<<32) ^
        (uint64_t(GLSLANG_VERSION_PATCH)<<16);
}
uint64_t source_cache_key(const std::string& text, bool vertex) {
    return cache_checksum(reinterpret_cast<const uint8_t*>(text.data()), text.size()) ^
        cache_fingerprint() ^ (vertex ? 0x9E3779B97F4A7C15ull : 0xD1B54A32D192ED03ull);
}
void initialize_disk_cache() {
    if(diskInitialized) return;
    diskInitialized=true;
    auto start=std::chrono::steady_clock::now();
    try {
        const auto overrideStorage=path_environment_utf8("WWHD_VK_SHADER_CACHE");
        const auto generalStorage=path_environment_utf8("WWHD_SHADER_CACHE");
        const char* overridePath=overrideStorage ? overrideStorage->c_str() : nullptr;
        const char* general=generalStorage ? generalStorage->c_str() : nullptr;
        if((overridePath && !strcmp(overridePath,"0")) ||
           (!overridePath && general && !strcmp(general,"0"))) return;
        std::string dir=overridePath ? overridePath : host::config_dir()+"/shadercache/vulkan-shaders";
        if(dir.empty()) return;
        diskPath=dir+"/spirv.bin";
        std::error_code ec;
        auto length=std::filesystem::file_size(std::filesystem::u8path(diskPath),ec);
        if(ec || length<40 || length>maxCacheBytes) return;
        std::vector<uint8_t> bytes(static_cast<size_t>(length));
        FILE* file=open_utf8_file(diskPath,"rb");
        if(!file) return;
        bool read=fread(bytes.data(),1,bytes.size(),file)==bytes.size() && fgetc(file)==EOF && !ferror(file);
        fclose(file);
        if(read) {
            const auto decoded=diskcache::decode(bytes,cache_fingerprint(),diskShaders);
            if(decoded.ok) {
                ++stats.diskLoads; diskBytes=decoded.bytes;
                // An older file (with GLSL text) is rewritten smaller at the next checkpoint.
                if(decoded.legacy) { ++diskRevision; diskDirty=true; diskChangedFrame=0; }
            }
        }
    } catch(...) { diskShaders.clear(); }
    stats.diskLoadNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-start).count();
}

uint64_t program_hash(uint32_t address, uint32_t size, uint64_t frame) {
    // Include size: the same address can refer to different program ranges. Hashed again on a new
    // frame only if the program's pages were written (WWHD_VK_PROGRAM_HASH_WATCH=0: every frame).
    static const bool watch = [] {
        const char* e = std::getenv("WWHD_VK_PROGRAM_HASH_WATCH");
        return !e || std::strcmp(e, "0") != 0;
    }();
    static const bool verify = std::getenv("WWHD_VK_PROGRAM_HASH_VERIFY") != nullptr;
    programHashes.watchEnabled = watch;
    programHashes.verify = verify;
    static const bool report = std::getenv("WWHD_VK_STATS") || std::getenv("WWHD_VK_CPU_ONLY_STATS");
    static uint64_t reportedFrame = 0;
    if (report && frame != ~uint64_t{0} && frame >= reportedFrame + 600) {
        const auto& st = programHashes.stats;
        if (reportedFrame)
            ::log_msg("[vulkan program hash] %zu programs; lookups %llu, hashed %llu (every frame while written: %llu), "
                      "unchanged by write watch %llu, same frame %llu; became dynamic %llu; verified %llu, mismatches %llu; hashed %.1f MiB, skipped %.1f MiB",
                      programHashes.size(), (unsigned long long)st.lookups, (unsigned long long)st.hashes,
                      (unsigned long long)st.dynamicHashes, (unsigned long long)st.watchedHits,
                      (unsigned long long)st.frameHits, (unsigned long long)st.becameDynamic,
                      (unsigned long long)st.verified, (unsigned long long)st.mismatches,
                      st.hashedBytes / 1048576.0, st.skippedBytes / 1048576.0);
        reportedFrame = frame;
    }
    return programHashes.get(address, size, frame, [&] { return hash_bytes(ppc_ptr(address), size); });
}
// Shader keys (two levels, as Cemu's LatteShader.cpp base and auxiliary hashes, but exact about
// what our GLSL path reads; docs/vulkan.md, "Shader keys"):
// - linkage: the program, the fetch shader and the registers the decompiler reads for every
//   program of the stage: for vertex shaders the input location each fetch attribute resolves to
//   (not the 32 raw SQ_VTX_SEMANTIC words), viewport-transform/half-Z/points/streamout; for pixel
//   shaders the PS input table, output mask, alpha test and front-face import;
// - variant: per used texture unit its dimension and integer format; for vertex shaders, per
//   exported parameter, its semantic id and the PS input it resolves to (location, flat,
//   noperspective) instead of the whole PS input table and SPI_VS_OUT_ID; streamout strides of
//   buffers it writes; for pixel shaders, the inputs the draw's vertex shader does not feed and
//   their DEFAULT_VAL (ps_link: those inputs are constants in the translation). The units and
//   exports come from the program, so they are known once its first variant has been translated.
// Render-target formats, buffer addresses, samplers and units the program does not sample are not
// read by the GLSL translation (the Metal-only render-target-texture check is the one that does)
// and are not in the key; the pipeline key has the attachment formats.
constexpr size_t kLinkageWords = 3 + GPU7_PS_MAX_INPUTS + 32 + 4;
// Depth clamp where the device has none (Mali-G52 and other GPUs without the depthClamp feature, or
// WWHD_VK_FORCE_DEPTH_CLAMP_EMULATION=1): the game draws its shadow maps with far-plane clipping off
// (PA_CL_CLIP_CNTL ZCLIP_FAR_DISABLE, which the pipeline turns into depth clamp), so casters in front
// of a shadow cascade's near plane still land in the map at its nearest depth. Without the feature
// the pipeline cannot clamp; the vertex shader is translated in a variant that clamps the position's
// depth at the near plane, per vertex: exact for casters wholly in front of it, a triangle crossing
// the plane comes out a little too deep (at worst a little less shadow). The far plane still clips:
// nothing beyond it shadows anything in range, and big ground triangles clamped there per vertex
// came out too near. The variant is part of the shader key (gather_linkage flag 64), so the shader
// caches keep both. Approach from the GreenNaugahyde Android fork (v0.9.5, issue #45 there).
bool depth_clamp_variant_wanted(const uint32_t* regs) {
    return !R.enabledFeatures.depthClamp && (regs[REGADDR::PA_CL_CLIP_CNTL] & (1u << 27));  // ZCLIP_FAR_DISABLE
}
// the vertex shader variant: SET_POSITION (the Vulkan define of the decompiler's header) clamps z
bool clamp_position_depth(std::string& glsl) {
    static const std::string def = "#define SET_POSITION(_v) ";
    const size_t d = glsl.find(def);
    const size_t eol = d == std::string::npos ? d : glsl.find_first_of("\r\n", d);
    if (eol == std::string::npos) return false;
    glsl.insert(eol, "; if (gl_Position.w > 0.0) gl_Position.z = max(gl_Position.z, 0.0)");
    return true;
}
size_t gather_linkage(const uint32_t* regs, bool vertex, const LatteFetchShader* fetch, uint32_t* out) {
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
                 (depth_clamp_variant_wanted(regs) ? 64u : 0u);               // emulated depth clamp
        if (mods::cemu::has_shaders()) {
            // graphics-pack shader names hash the whole VTE word and rect primitives
            out[count++] = vte;
            out[count++] = primitive == 0x11 ? 1u : 0u;
        }
    } else {
        // PS input table (latte_support.cpp LatteShader_CreatePSInputTable): input count, position
        // import, parameter generation, and per input its semantic, flat and noperspective bits;
        // every input is declared. Vertex shaders only need where their exports land (variant_hash).
        const uint32_t control0 = regs[mmSPI_PS_IN_CONTROL_0];
        out[count++] = control0 & 0x03FFFD3Fu;
        const uint32_t inputs = std::min<uint32_t>(control0 & 0x3F, GPU7_PS_MAX_INPUTS);
        for (uint32_t i = 0; i < inputs; ++i) out[count++] = regs[mmSPI_PS_INPUT_CNTL_0 + i] & 0x14FFu;
        out[count++] = regs[mmSPI_PS_IN_CONTROL_1] & 0x1FF00u;  // front-face import
        out[count++] = regs[mmCB_SHADER_MASK];
        const uint32_t alpha = regs[REGADDR::SX_ALPHA_TEST_CONTROL];
        out[count++] = alpha & 8 ? alpha & 0xF : 0;
        flags |= regs[mmSPI_INTERP_CONTROL_0] & (1u << 1) ? 32u : 0u;  // point sprite
    }
    out[count++] = flags;
    return count;
}
// What the program reads beyond the linkage, recorded from its first translation.
struct ProgramUse {
    bool known = false;
    uint8_t unitCount = 0;
    std::array<uint8_t, LATTE_NUM_MAX_TEX_UNITS> units{};
    uint32_t exports = 0;   // vertex shader parameter exports (outputParameterMask)
    bool streamout = false; // vertex shader writes a streamout buffer
    Shader* failed = nullptr;
};
std::unordered_map<uint64_t, ProgramUse> programUses;  // linkage key -> use
std::unordered_map<uint64_t, Shader*> variants;         // full key -> shader (may be shared)
std::unordered_multimap<uint64_t, Shader*> shadersByOutput;
std::unordered_multimap<uint64_t, Shader*> shadersByModule;  // SPIR-V + resource mapping
// A pixel shader translated for the draw's vertex shader: the semantic ids that vertex shader
// exports (its output parameters through SPI_VS_OUT_ID) and, for the key, the PS inputs none of
// them feeds. Those inputs are constants in the translation, the GPU's default value for them
// (SPI_PS_INPUT_CNTL DEFAULT_VAL, which the linkage words leave out, so the variant words have it
// for these inputs; LatteDecompilerOptions::linkPSInputsToVS): a declared input with no output
// made the Adreno driver refuse the pipeline.
struct PsLink {
    bool linked = false;
    std::bitset<256> exports;
    uint32_t unfed = 0;  // bit f: PS input f has no vertex shader output
};
PsLink ps_link(const uint32_t* regs, const Shader* vs) {
    PsLink link;
    if (!vs || !vs->dec) return link;
    link.linked = true;
    const uint32_t mask = vs->dec->outputParameterMask;
    for (uint32_t i = 0; i < 32; ++i)
        if (mask & (1u << i)) link.exports.set((regs[mmSPI_VS_OUT_ID_0 + i / 4] >> (8 * (i % 4))) & 0xFF);
    const uint32_t control0 = regs[mmSPI_PS_IN_CONTROL_0];
    const uint32_t inputs = std::min<uint32_t>(control0 & 0x3F, GPU7_PS_MAX_INPUTS);
    const uint32_t position = (control0 >> 8) & 1 ? (control0 >> 10) & 0x1F : 0xFFFFFFFFu;
    for (uint32_t f = 0; f < inputs; ++f)
        if (f != position && !link.exports.test(regs[mmSPI_PS_INPUT_CNTL_0 + f] & 0xFF)) link.unfed |= 1u << f;
    return link;
}
uint64_t variant_hash(const uint32_t* regs, bool vertex, const ProgramUse& use, uint64_t linkage,
                      const PsLink& link) {
    std::array<uint32_t, LATTE_NUM_MAX_TEX_UNITS + 32 + 4 + 1 + GPU7_PS_MAX_INPUTS> words;
    size_t count = 0;
    const uint32_t base = vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
    for (uint32_t i = 0; i < use.unitCount; ++i) {
        const uint32_t* w = regs + base + use.units[i] * 7;
        // dimension (sampler type) and NUM_FORMAT_ALL == INT (integer sampler); analyzer.cpp
        words[count++] = (w[0] & 7) | (((w[4] >> 8) & 3) == 1 ? 8u : 0u);
    }
    if (vertex) {
        // Per exported parameter: its semantic id (SPI_VS_OUT_ID) and the PS input it lands in,
        // as _getVertexShaderOutParamSemanticId and _emitVSExports resolve it against the PS input
        // table; an export without a PS input is skipped whatever its semantic.
        if (use.exports) {
            const uint32_t control0 = regs[mmSPI_PS_IN_CONTROL_0];
            const uint32_t inputs = std::min<uint32_t>(control0 & 0x3F, GPU7_PS_MAX_INPUTS);
            const uint32_t position = (control0 >> 8) & 1 ? (control0 >> 10) & 0x1F : 0xFFFFFFFFu;
            for (uint32_t i = 0; i < 32; ++i) {
                if (!(use.exports & (1u << i))) continue;
                const uint32_t semantic = (regs[mmSPI_VS_OUT_ID_0 + i / 4] >> (8 * (i % 4))) & 0xFF;
                uint32_t word = 0x7FFFFFFFu;
                for (uint32_t f = 0; f < inputs; ++f) {
                    if (f == position) continue;  // LATTE_ANALYZER_IMPORT_INDEX_SPIPOSITION never matches
                    const uint32_t control = regs[mmSPI_PS_INPUT_CNTL_0 + f];
                    if ((control & 0xFF) != semantic) continue;
                    word = 0x80000000u | semantic | (f << 8) | (control & (1u << 10) ? 1u << 16 : 0) |
                           (control & (1u << 12) ? 1u << 17 : 0);
                    break;
                }
                words[count++] = word;
            }
        }
        if (use.streamout)
            for (uint32_t buffer = 0; buffer < 4; ++buffer)
                words[count++] = regs[mmVGT_STRMOUT_VTX_STRIDE_0 + buffer * 4];
    } else {
        // the inputs that read their default value, and that value (DEFAULT_VAL)
        words[count++] = link.linked ? link.unfed : 0xFFFFFFFFu;
        for (uint32_t f = 0; f < GPU7_PS_MAX_INPUTS; ++f)
            if (link.unfed & (1u << f)) words[count++] = (regs[mmSPI_PS_INPUT_CNTL_0 + f] >> 8) & 3;
    }
    return hash_bytes(words.data(), count * sizeof(uint32_t), linkage ^ 0xC2B2AE3D27D4EB4Full);
}
// The key before the narrowing (all 18 units, all 54 samplers, render-target words): kept for
// the verify mode (WWHD_VK_SHADER_KEY_VERIFY), which checks that the narrow key never gives one
// shader to two of these keys that translate differently.
uint64_t legacy_state_hash(const uint32_t* regs, uint64_t hash, bool vertex) {
    std::array<uint32_t,105+5*LATTE_NUM_MAX_TEX_UNITS> state;
    size_t count=0;
    auto append = [&](const uint32_t* words,size_t length) {
        memcpy(state.data()+count,words,length*sizeof(uint32_t));
        count+=length;
    };
    auto put = [&](uint32_t first,uint32_t length) { append(regs+first,length); };
    put(mmSQ_VTX_SEMANTIC_0, 32);
    put(mmSPI_VS_OUT_ID_0, 10);
    put(mmSPI_VS_OUT_CONFIG, 1); put(mmPA_CL_VS_OUT_CNTL, 1);
    put(mmSPI_PS_IN_CONTROL_0, 2); put(mmSPI_PS_INPUT_CNTL_0, 32);
    uint32_t primitiveState[] = {regs[REGADDR::VGT_PRIMITIVE_TYPE] & 0x3F,
        regs[mmSPI_INTERP_CONTROL_0] & (1u << 1), regs[mmVGT_STRMOUT_EN]};
    append(primitiveState,3);
    if (regs[mmVGT_STRMOUT_EN])
        for (uint32_t buffer = 0; buffer < 4; ++buffer)
            put(mmVGT_STRMOUT_VTX_STRIDE_0 + buffer * 4, 1);
    put(REGADDR::VGT_GS_MODE, 1); put(REGADDR::SQ_CONFIG, 1); put(mmCB_SHADER_MASK, 1);
    put(mmCB_SHADER_CONTROL, 1); put(mmDB_SHADER_CONTROL, 1);
    put(mmSPI_INPUT_Z, 1); put(REGADDR::SX_ALPHA_TEST_CONTROL, 1);
    uint32_t transformState[] = {regs[REGADDR::PA_CL_VTE_CNTL] & 0x3F,
        regs[REGADDR::PA_CL_CLIP_CNTL] & ((1u << 19) | (depth_clamp_variant_wanted(regs) ? 1u << 27 : 0u)),
        regs[REGADDR::DB_DEPTH_CONTROL] & 0x83};
    append(transformState,3);
    put(REGADDR::CB_COLOR_CONTROL, 1); put(REGADDR::CB_TARGET_MASK, 1);
    put(mmCB_COLOR0_INFO, 8);
    uint32_t base = vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
    for (uint32_t t = 0; t < LATTE_NUM_MAX_TEX_UNITS; ++t) {
        const auto* w = regs + base + t * 7;
        uint32_t relevant[] = {(w[0] & 7) | (w[4] & 0x300), w[1] & 0x3F00000};
        append(relevant,2);
    }
    for (uint32_t t = 0; t < LATTE_NUM_MAX_TEX_UNITS * 3; ++t) {
        uint32_t compare = regs[REGADDR::SQ_TEX_SAMPLER_WORD0_0 + t * 3] & 0xF8000000;
        append(&compare,1);
    }
    return hash_bytes(state.data(),count*sizeof(uint32_t),hash);
}
// Shader-cache miss histogram (render_prof.h, H6 of the performance research): for each new variant
// of a program that already has variants, which of the words legacy_state_hash() covers (the key
// before the narrowing) differ from the
// nearest existing variant. Texture and sampler words are split into units/samplers this shader
// samples and ones it does not. Same words as legacy_state_hash(), fixed layout.
enum MissGroup : uint8_t {
    kMgSemantic, kMgVsOutId, kMgVsOutConfig, kMgVsOutCntl, kMgPsInControl, kMgPsInputCntl, kMgPrimitive,
    kMgPointSprite, kMgStreamout, kMgGsMode, kMgSqConfig, kMgCbShaderMask, kMgCbShaderControl,
    kMgDbShaderControl, kMgInputZ, kMgAlphaTest, kMgVteCntl, kMgClipCntl, kMgDepthControl, kMgCbColorControl,
    kMgTargetMask, kMgColorInfo, kMgTexUsed, kMgTexUnused, kMgSamplerUsed, kMgSamplerUnused, kMgFetch, kMgCount
};
const char* const missGroupNames[kMgCount] = {
    "VTX_SEMANTIC", "SPI_VS_OUT_ID", "SPI_VS_OUT_CONFIG", "PA_CL_VS_OUT_CNTL", "SPI_PS_IN_CONTROL",
    "SPI_PS_INPUT_CNTL", "primitive type", "point sprite", "streamout", "VGT_GS_MODE", "SQ_CONFIG",
    "CB_SHADER_MASK", "CB_SHADER_CONTROL", "DB_SHADER_CONTROL", "SPI_INPUT_Z", "alpha test", "PA_CL_VTE_CNTL",
    "PA_CL_CLIP_CNTL", "DB_DEPTH_CONTROL", "CB_COLOR_CONTROL", "CB_TARGET_MASK", "CB_COLOR_INFO",
    "tex words (used units)", "tex words (unused units)", "sampler compare (used)", "sampler compare (unused)",
    "fetch shader"};
void miss_words(const uint32_t* regs, bool vertex, uint64_t fsKey, std::vector<uint32_t>& words,
                std::vector<uint8_t>* groups, const LatteDecompilerShader* dec) {
    words.clear();
    if (groups) groups->clear();
    auto put = [&](uint32_t value, uint8_t group) {
        words.push_back(value);
        if (groups) groups->push_back(group);
    };
    auto range = [&](uint32_t first, uint32_t n, uint8_t group) { for (uint32_t i = 0; i < n; ++i) put(regs[first + i], group); };
    range(mmSQ_VTX_SEMANTIC_0, 32, kMgSemantic); range(mmSPI_VS_OUT_ID_0, 10, kMgVsOutId);
    range(mmSPI_VS_OUT_CONFIG, 1, kMgVsOutConfig); range(mmPA_CL_VS_OUT_CNTL, 1, kMgVsOutCntl);
    range(mmSPI_PS_IN_CONTROL_0, 2, kMgPsInControl); range(mmSPI_PS_INPUT_CNTL_0, 32, kMgPsInputCntl);
    put(regs[REGADDR::VGT_PRIMITIVE_TYPE] & 0x3F, kMgPrimitive);
    put(regs[mmSPI_INTERP_CONTROL_0] & (1u << 1), kMgPointSprite);
    put(regs[mmVGT_STRMOUT_EN], kMgStreamout);
    for (uint32_t b = 0; b < 4; ++b) put(regs[mmVGT_STRMOUT_EN] ? regs[mmVGT_STRMOUT_VTX_STRIDE_0 + b * 4] : 0, kMgStreamout);
    range(REGADDR::VGT_GS_MODE, 1, kMgGsMode); range(REGADDR::SQ_CONFIG, 1, kMgSqConfig);
    range(mmCB_SHADER_MASK, 1, kMgCbShaderMask); range(mmCB_SHADER_CONTROL, 1, kMgCbShaderControl);
    range(mmDB_SHADER_CONTROL, 1, kMgDbShaderControl); range(mmSPI_INPUT_Z, 1, kMgInputZ);
    range(REGADDR::SX_ALPHA_TEST_CONTROL, 1, kMgAlphaTest);
    put(regs[REGADDR::PA_CL_VTE_CNTL] & 0x3F, kMgVteCntl);
    put(regs[REGADDR::PA_CL_CLIP_CNTL] & (1u << 19), kMgClipCntl);
    put(regs[REGADDR::DB_DEPTH_CONTROL] & 0x83, kMgDepthControl);
    range(REGADDR::CB_COLOR_CONTROL, 1, kMgCbColorControl); range(REGADDR::CB_TARGET_MASK, 1, kMgTargetMask);
    range(mmCB_COLOR0_INFO, 8, kMgColorInfo);
    std::array<bool, LATTE_NUM_MAX_TEX_UNITS> usedUnit{};
    std::array<bool, LATTE_NUM_MAX_TEX_UNITS * 3> usedSampler{};
    if (dec)
        for (int i = 0; i < dec->textureUnitListCount; ++i) {
            const uint32_t unit = dec->textureUnitList[i];
            if (unit >= LATTE_NUM_MAX_TEX_UNITS) continue;
            usedUnit[unit] = true;
            const uint32_t sampler = dec->textureUnitSamplerAssignment[unit];
            if (sampler < 18) usedSampler[(vertex ? 18 : 0) + sampler] = true;
        }
    uint32_t base = vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
    for (uint32_t t = 0; t < LATTE_NUM_MAX_TEX_UNITS; ++t) {
        const auto* w = regs + base + t * 7;
        const uint8_t g = usedUnit[t] ? kMgTexUsed : kMgTexUnused;
        put((w[0] & 7) | (w[4] & 0x300), g);
        put(w[1] & 0x3F00000, g);
    }
    for (uint32_t t = 0; t < LATTE_NUM_MAX_TEX_UNITS * 3; ++t)
        put(regs[REGADDR::SQ_TEX_SAMPLER_WORD0_0 + t * 3] & 0xF8000000, usedSampler[t] ? kMgSamplerUsed : kMgSamplerUnused);
    put(uint32_t(fsKey), kMgFetch);
    put(uint32_t(fsKey >> 32), kMgFetch);
}
std::unordered_map<uint64_t, std::vector<std::vector<uint32_t>>> missVariants;  // program base -> variants
void record_shader_variant(const uint32_t* regs, bool vertex, uint64_t base, uint64_t fsKey,
                           const LatteDecompilerShader* dec) {
    if (!rprof::enabled()) return;
    std::vector<uint32_t> words;
    std::vector<uint8_t> groups;
    miss_words(regs, vertex, vertex ? fsKey : 0, words, &groups, dec);
    auto& variants = missVariants[base];
    const char* names[kMgCount];
    int count = 0;
    bool onlyUnused = false;
    if (!variants.empty()) {
        const std::vector<uint32_t>* nearest = nullptr;
        size_t best = SIZE_MAX;
        for (const auto& v : variants) {
            size_t d = 0;
            for (size_t i = 0; i < v.size() && i < words.size(); ++i) d += v[i] != words[i];
            if (d < best) { best = d; nearest = &v; }
        }
        std::array<bool, kMgCount> differs{};
        for (size_t i = 0; i < words.size() && i < nearest->size(); ++i)
            if ((*nearest)[i] != words[i]) differs[groups[i]] = true;
        onlyUnused = best > 0;
        for (int g = 0; g < kMgCount; ++g)
            if (differs[g]) {
                names[count++] = missGroupNames[g];
                if (g != kMgTexUnused && g != kMgSamplerUnused) onlyUnused = false;
            }
        if (!count) names[count++] = "none (equal words)";
    }
    rprof::shader_variant(variants.empty(), onlyUnused, names, count);
    variants.push_back(std::move(words));
}
void free_decompiler(LatteDecompilerShader* shader) {
    if (!shader) return;
    delete shader->strBuf_shaderSource;
    delete shader;
}
// WWHD_VK_SHADER_KEY_VERIFY=1 checks every distinct pre-narrowing key once; N > 1 checks one in N.
uint32_t key_verify_rate() {
    static const uint32_t rate = [] {
        const char* value = getenv("WWHD_VK_SHADER_KEY_VERIFY");
        if (!value || !*value) return 0u;
        long parsed = strtol(value, nullptr, 10);
        return parsed > 0 ? uint32_t(std::min<long>(parsed, 1 << 20)) : 0u;
    }();
    return rate;
}
// Verification modes keep each shader's final GLSL text so collisions and
// key violations can be compared and shown byte for byte. Off by default:
// shaders otherwise keep only the text's identity (hash + length).
bool retain_shader_source() {
    static const bool retain = [] {
        const char* value = getenv("WWHD_VK_SHADER_SOURCE_VERIFY");
        return (value && !strcmp(value, "1")) || key_verify_rate() != 0;
    }();
    return retain;
}
uint64_t output_hash(const Shader& shader) {
    const uint64_t source[3]{shader.sourceId.low, shader.sourceId.high, shader.sourceId.length};
    uint64_t hash = hash_bytes(source, sizeof source, shader.vertex ? 0x5151 : 0xA2A2);
    hash = hash_bytes(&shader.mapping, sizeof shader.mapping, hash);
    hash = hash_bytes(&shader.uniforms, sizeof shader.uniforms, hash);
    return hash_bytes(&shader.descriptorRanks, sizeof shader.descriptorRanks, hash);
}
uint64_t portable_output_identity(const Shader& shader) {
    PortableShaderIdentity id(shader.vertex, uint32_t(shader.kind), shader.source_text());
    const auto& m=shader.mapping;
    id.words(m.textureUnitToBindingPoint); id.words(m.relBindingPointToRelTextureUnit);
    id.word(uint32_t(m.textureUnitBaseBindingPoint)); id.word(uint32_t(m.textureUnitCount));
    id.word(uint32_t(m.uniformVarsBufferBindingPoint)); id.words(m.uniformBuffersBindingPoint);
    id.word(uint32_t(m.tfStorageBindingPoint)); id.words(m.attributeMapping);
    id.word(uint32_t(m.setIndex)); id.word(uint32_t(m.verticesPerInstanceBinding));
    id.word(uint32_t(m.indexBufferBinding)); id.word(uint32_t(m.indexTypeBinding));
    const auto& u=shader.uniforms;
    id.word(uint32_t(u.offset_remapped)); id.word(uint32_t(u.offset_uniformRegister));
    id.word(uint32_t(u.count_uniformRegister)); id.word(uint32_t(u.offset_alphaTestRef));
    id.word(uint32_t(u.offset_pointSize)); id.word(uint32_t(u.offset_fragCoordScale));
    id.words(u.offset_framebufferFetchSize); id.word(uint32_t(u.offset_windowSpaceToClipSpaceTransform));
    id.words(u.offset_texScale); id.word(uint32_t(u.offset_verticesPerInstance));
    id.words(u.offset_streamoutBufferBase); id.word(uint32_t(u.offset_endOfBlock));
    const auto& r=shader.descriptorRanks;
    id.words(r.blocks); id.words(r.textures); id.word(r.support); id.word(r.count); id.word(r.valid ? 1 : 0);
    return id.value();
}
bool same_output(const Shader& a, const Shader& b) {
    if (a.vertex != b.vertex || !(a.sourceId == b.sourceId) ||
        std::memcmp(&a.mapping, &b.mapping, sizeof a.mapping) ||
        std::memcmp(&a.uniforms, &b.uniforms, sizeof a.uniforms) ||
        std::memcmp(&a.descriptorRanks, &b.descriptorRanks, sizeof a.descriptorRanks)) return false;
    // Verification modes still have both texts: confirm the identity byte for byte.
    if (retain_shader_source() && a.source_text() != b.source_text()) {
        fprintf(stderr, "[vulkan] shader source hash collision (%u bytes); not shared\n",
                unsigned(a.sourceId.length));
        return false;
    }
    return true;
}
// A translation job owns every byte read by the legacy decompiler. Fetch
// groups contain interior pointers, so a shallow fetch copy is insufficient.
struct TranslationInput {
    std::array<uint32_t, gx2::kNumRegs> regs;
    std::vector<uint8_t> program;
    LatteFetchShader fetch;
    std::vector<std::vector<LatteParsedFetchShaderAttribute>> attributes;
    bool hasFetch = false;
    TranslationInput(const uint32_t* sourceRegs, uint32_t address, uint32_t size,
                     const LatteFetchShader* sourceFetch) {
        std::copy_n(sourceRegs, regs.size(), regs.begin());
        const auto* bytes = static_cast<const uint8_t*>(ppc_ptr(address));
        program.assign(bytes, bytes + size);
        if (!sourceFetch) return;
        hasFetch = true;
        fetch.key = sourceFetch->key;
        fetch.attributeBufferMask = sourceFetch->attributeBufferMask;
        fetch.vkPipelineHashFragment = sourceFetch->vkPipelineHashFragment;
        fetch.mtlFetchVertexManually = sourceFetch->mtlFetchVertexManually;
        fetch.m_cacheHash = sourceFetch->m_cacheHash;
        fetch.m_isRegistered = false;
        attributes.reserve(sourceFetch->bufferGroups.size() + sourceFetch->bufferGroupsInvalid.size());
        auto copyGroups = [&](const auto& source, auto& destination) {
            destination.reserve(source.size());
            for (const auto& group : source) {
                if (group.attribCount < 0 || (group.attribCount && !group.attrib))
                    throw std::runtime_error("invalid fetch translation snapshot");
                attributes.emplace_back();
                auto& owned = attributes.back();
                if (group.attribCount) owned.assign(group.attrib, group.attrib + group.attribCount);
                destination.push_back(group);
                destination.back().attrib = owned.data();
            }
        };
        copyGroups(sourceFetch->bufferGroups, fetch.bufferGroups);
        copyGroups(sourceFetch->bufferGroupsInvalid, fetch.bufferGroupsInvalid);
    }
};
std::mutex decompilerMutex;
// Latte program -> GLSL and its binding metadata (no SPIR-V). Also used by the verify mode, which
// translates again under the current registers and compares.
bool decompile(Shader& shader, const uint32_t* regs, bool vertex, LatteFetchShader* fetch,
               uint32_t address, uint32_t size, uint64_t base, bool packReplacement, const PsLink& link,
               uint8_t* program) {
    std::lock_guard translationLock(decompilerMutex);
    if (!g_renderer || g_renderer->GetType() != RendererAPI::Vulkan) {
        shader.error = "Vulkan renderer was not selected before shader translation"; return false;
    }
    if ((regs[Latte::REGADDR::VGT_GS_MODE] & 3) != 0) {
        shader.error = "geometry shaders are not implemented by this Vulkan backend"; return false;
    }
    if (vertex && !fetch) { shader.error = "vertex shader has no fetch program"; return false; }
    LatteShader_UpdatePSInputs(const_cast<uint32_t*>(regs));
    LatteDecompilerOptions options;
    if (!vertex && link.linked) {
        options.linkPSInputsToVS = true;
        options.vsOutputSemantics = link.exports;
    }
    // the GPU's MUL/MULADD give 0*anything=0 (rsqrt(0)*0 is NaN otherwise: black letter in the Rito
    // mail sorting game); Cemu's default too. WWHD_STRICT_MUL=0 turns it off for comparisons
    static const bool strictMul = !getenv("WWHD_STRICT_MUL") || strcmp(getenv("WWHD_STRICT_MUL"), "0");
    options.strictMul = strictMul;
    uint64_t packBase=0;
    if(mods::cemu::has_shaders()) {
        packBase=cemu_pack_hash::base(program,size,regs,vertex,fetch);
        options.legacyGraphicPackUniforms=!vertex&&mods::cemu::legacy_pixel_uniforms(packBase);
    }
    const uint64_t decompilerBase=mods::cemu::has_shaders()?packBase:base;
    const auto fixes = vertex ? ::gfx::area_sample::Program{} : ::gfx::area_sample::pixel_program(program, size);
    options.areaSampledTextures = fixes.units;
    LatteDecompilerOutput_t output{};
    if (vertex) LatteDecompiler_DecompileVertexShader(decompilerBase, const_cast<uint32_t*>(regs),
        program, size, fetch, options, &output);
    else LatteDecompiler_DecompilePixelShader(decompilerBase, const_cast<uint32_t*>(regs),
        program, size, options, &output);
    if (!output.shader || output.shader->hasError || !output.shader->strBuf_shaderSource) {
        free_decompiler(output.shader); shader.error = "Latte GLSL translation failed"; return false;
    }
    if (options.areaSampledTextures && packReplacement && mods::cemu::has_shaders() &&
        !mods::cemu::shader_source(packBase, cemu_pack_hash::auxiliary(*output.shader, regs, vertex), vertex).empty()) {
        // a graphics pack replaces this shader: translate it as Cemu does, so its interface matches
        free_decompiler(output.shader);
        options.areaSampledTextures = 0;
        output = LatteDecompilerOutput_t{};
        LatteDecompiler_DecompilePixelShader(decompilerBase, const_cast<uint32_t*>(regs), program, size, options, &output);
        if (!output.shader || output.shader->hasError || !output.shader->strBuf_shaderSource) {
            free_decompiler(output.shader); shader.error = "Latte GLSL translation failed"; return false;
        }
    }
    shader.dec = FinishDecompiledShader(output);
    shader.mapping = output.resourceMappingVK;
    shader.descriptorRanks = make_descriptor_rank_plan(shader.mapping, *shader.dec);
    shader.uniforms = output.uniformOffsetsVK;
    shader.uniformInputs = make_uniform_input_plan(shader);
    shader.glsl = shader.dec->strBuf_shaderSource->c_str();
    if (options.areaSampledTextures && ::gfx::area_sample::rewrite(shader.glsl, fixes, false) <= 0)
        fprintf(stderr, "[vulkan] pixel shader %08X: upscaling fix not applied\n", address);
    if (vertex) {
        // Depth-only and shaded variants must rasterize identical positions.
        // Match Metal's [[invariant]] position output for multipass depth tests.
        auto main = shader.glsl.find("void main(");
        if (main != std::string::npos)
            shader.glsl.insert(main, "invariant gl_Position;\n");
    }
    if(packReplacement && mods::cemu::has_shaders()) {
        auto packAux=cemu_pack_hash::auxiliary(*shader.dec,regs,vertex);
        auto custom=mods::cemu::shader_source(packBase,packAux,vertex);
        // graphics-pack vertex shaders decode raw guest attributes themselves: not with CPU-converted ones
        bool convertedInputs = false;
        if (vertex) for (const auto& group : fetch->bufferGroups)
            convertedInputs |= cpu_vertex_count(group, R.vertexDiagnostics.cpuClasses) != 0;
        if(vertex&&!custom.empty()&&convertedInputs) {
            fprintf(stderr,"[cemu-pack] %016llx_%016llx_vs skipped: Convert vertices on the CPU is on\n",
                (unsigned long long)packBase,(unsigned long long)packAux);
            custom.clear();
        }
        if(!custom.empty()) {
            if(!vertex&&link.unfed) {
                // the inputs the translation made constants (ps_link), with the same values
                const char* values[32]={};
                for(uint32_t f=0;f<GPU7_PS_MAX_INPUTS&&f<32;f++)
                    if(link.unfed&(1u<<f))values[f]=LattePSInputDefaultGLSL(regs[mmSPI_PS_INPUT_CNTL_0+f]);
                custom=mods::cemu::const_pixel_inputs(std::move(custom),values);
            }
            std::string error;
            auto replacement=compile_glsl(custom,vertex,&error);
            std::string originalError;
            auto original=compile_glsl(shader.glsl,vertex,&originalError);
            bool accepted=!replacement.empty()&&!original.empty()&&
                mods::cemu::compatible_shader_interface(original,replacement,error);
            if(accepted && key_verify_rate()) shader.translatedGlsl=shader.glsl;
            if(accepted)shader.glsl=std::move(custom);
            if(error.empty()&&!accepted)error=originalError.empty()?"Shader compilation failed":originalError;
            mods::cemu::report_shader(packBase,packAux,vertex,accepted,error);
            fprintf(stderr,"[cemu-pack] %016llx_%016llx_%s %s%s\n",
                (unsigned long long)packBase,(unsigned long long)packAux,vertex?"vs":"ps",
                accepted?"applied":"rejected: ",accepted?"":error.c_str());
        }
    }
    if (vertex && depth_clamp_variant_wanted(regs)) {
        static int applied = 0, missed = 0;
        if (clamp_position_depth(shader.glsl)) {
            if (++applied <= 5 || (applied & (applied - 1)) == 0)
                fprintf(stderr, "[vulkan] vertex shader %08X: near-plane depth clamp variant (%d so far)\n", address, applied);
        } else if (missed++ < 10)  // a graphics-pack vertex shader without the decompiler's SET_POSITION: drawn unclamped
            fprintf(stderr, "[vulkan] vertex shader %08X: depth clamp emulation not applied (no SET_POSITION)\n", address);
    }
    if (vertex && R.vertexFormats.fallbackMask) {
        uint32_t tailLocation = vertex_tail_location(shader.mapping.attributeMapping);
        // Rewrite the input declaration, leaving every existing endian/sign/
        // normalization decoder intact. Vulkan supplies w=1 for native RGB.
        // Apply after pack replacement too: packs use the same raw interface.
        for (const auto& group : fetch->bufferGroups) {
            for (int i = 0; i < group.attribCount; ++i) {
                const auto& attribute = group.attrib[i];
                if (cpu_vertex_selected(attribute, R.vertexDiagnostics.cpuClasses) || !R.vertexFormats.fallback(vertex_format(attribute.format))) continue;
                const auto location = shader.mapping.attributeMapping[attribute.semanticId];
                if (location < 0) continue;
                const auto name = "attrDataSem" + std::to_string(attribute.semanticId);
                if (!rewrite_vertex_fallback_input(shader.glsl, name, location, tailLocation) ||
                    (!shader.translatedGlsl.empty() &&
                     !rewrite_vertex_fallback_input(shader.translatedGlsl, name, location, tailLocation))) {
                    shader.error = "cannot adapt graphics-pack vertex input for RGB fallback";
                    return false;
                }
                ++tailLocation;
            }
        }
    }
    // Rewrites and graphics-pack validation above have consumed the original
    // decompiler text. Keep its metadata, but release this independent copy;
    // exact aliases, verification and debug export use glsl/translatedGlsl.
    delete shader.dec->strBuf_shaderSource;
    shader.dec->strBuf_shaderSource = nullptr;
    return true;
}
bool translate_owned(Shader& shader, const uint32_t* regs, bool vertex,
                     LatteFetchShader* fetch, uint32_t address, uint32_t size,
                     uint64_t base, bool packReplacement, const PsLink& link,
                     uint64_t frame, uint64_t key) {
    auto input = std::make_shared<TranslationInput>(regs, address, size, fetch);
    auto work = [&, input, vertex, address, size, base, packReplacement, link, frame, key] {
        const auto cpu = stutter::trace().enabled() ? stutter::thread_cpu_now() : 0;
        const auto start = stutter::now();
        // Never let a borrowed fetch pointer escape the owned job, including
        // a translation or diagnostic allocation exception.
        struct FetchLease {
            Shader& shader;
            ~FetchLease() { if (shader.dec) shader.dec->compatibleFetchShader = nullptr; }
        } lease{shader};
        try {
            const bool ok = decompile(shader, input->regs.data(), vertex,
                input->hasFetch ? &input->fetch : nullptr, address, size, base,
                packReplacement, link, input->program.data());
            stutter::trace().add("shader_translation", frame, key, start, stutter::now(),
                ok ? 0 : -1, stutter::cpu_elapsed(cpu));
        } catch (...) {
            free_decompiler(shader.dec);
            shader.dec = nullptr;
            throw;
        }
    };
    const char* option = std::getenv("WWHD_VK_ASYNC_TRANSLATION");
    if (option && !std::strcmp(option, "0")) work();
    else {
        auto done = submit_shader_translation(work);
        std::exception_ptr presentationError;
        const auto requested = stutter::now();
        while (done.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
            if (frame != ~uint64_t{0} && !presentationError) try {
                begin_pipeline_pending_frame();
                present_completed_frame_while_pipeline_pending();
            } catch (...) { presentationError = std::current_exception(); }
        }
        done.get();
        stutter::trace().add("translation_wait", frame, key, requested, stutter::now(),
            shader.dec ? 0 : -1);
        if (presentationError) std::rethrow_exception(presentationError);
    }
    if (shader.dec) shader.dec->compatibleFetchShader = fetch;
    return shader.dec && shader.error.empty();
}
std::unordered_map<uint64_t, uint64_t> verifiedKeys;  // legacy key -> full narrow key
void first_difference(const std::string& a, const std::string& b, std::string& left, std::string& right) {
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    auto line = [&](const std::string& s) {
        size_t begin = s.rfind('\n', at ? at - 1 : 0);
        begin = begin == std::string::npos || at == 0 ? 0 : begin + 1;
        size_t end = s.find('\n', at);
        return s.substr(begin, std::min<size_t>((end == std::string::npos ? s.size() : end) - begin, 160));
    };
    left = line(a); right = line(b);
}
void verify_key(const uint32_t* regs, bool vertex, LatteFetchShader* fetch, uint64_t fsKey,
                uint32_t address, uint32_t size, uint64_t base, uint64_t key, const Shader& shader,
                const PsLink& link) {
    const uint32_t rate = key_verify_rate();
    if (!rate || !shader.dec) return;
    uint64_t legacy = legacy_state_hash(regs, base, vertex) ^ (vertex ? fsKey * 31 : 0);
    if (vertex && mods::cemu::has_shaders())
        legacy ^= uint64_t(regs[REGADDR::PA_CL_VTE_CNTL]) * 0x9E3779B97F4A7C15ull;
    auto [it, inserted] = verifiedKeys.emplace(legacy, key);
    if (!inserted) {
        if (it->second != key) ++stats.verifySplitKeys;  // narrower is allowed to be finer, never coarser
        return;
    }
    if (rate > 1 && ((legacy * 0x9E3779B97F4A7C15ull) >> 40) % rate) return;
    auto started = std::chrono::steady_clock::now();
    Shader reference;
    reference.vertex = vertex;
    bool built = translate_owned(reference, regs, vertex, fetch, address, size, base, false, link, ~uint64_t{0}, key);
    ++stats.verifyChecks;
    const std::string& translated = shader.translatedGlsl.empty() ? shader.source_text() : shader.translatedGlsl;
    if (!built || reference.source_text() != translated ||
        std::memcmp(&reference.mapping, &shader.mapping, sizeof shader.mapping) ||
        std::memcmp(&reference.uniforms, &shader.uniforms, sizeof shader.uniforms) ||
        std::memcmp(&reference.descriptorRanks, &shader.descriptorRanks, sizeof shader.descriptorRanks)) {
        ++stats.verifyViolations;
        std::string left, right;
        first_difference(translated, reference.source_text(), left, right);
        fprintf(stderr, "[vulkan shader key] VIOLATION %s %08X key %016llx legacy %016llx: %s; cached \"%s\" vs now \"%s\"\n",
                vertex ? "vs" : "ps", address, (unsigned long long)key, (unsigned long long)legacy,
                !built ? reference.error.c_str() : reference.source_text() != translated ? "GLSL differs" : "binding metadata differs",
                left.c_str(), right.c_str());
    }
    free_decompiler(reference.dec);
    stats.verifyNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count();
}
}

void select_renderer() {
    finish_shader_translation_worker();
    std::lock_guard lock(decompilerMutex);
    g_renderer = std::make_unique<VulkanRenderer>();
}

std::vector<uint32_t> compile_glsl(const std::string& source, bool vertex, std::string* error) {
    auto result = compile_shader(source, vertex ? ShaderStage::Vertex : ShaderStage::Fragment,
                                 ~uint64_t{0}, 0, {}, async_shaders_enabled());
    if (error) *error = std::move(result.error);
    return std::move(result.words);
}
std::vector<uint32_t> compile_compute(const std::string& source, std::string* error) {
    auto result = compile_shader(source, ShaderStage::Compute, ~uint64_t{0}, 0, {}, async_shaders_enabled());
    if (error) *error = std::move(result.error);
    return std::move(result.words);
}

DescriptorRankPlan make_descriptor_rank_plan(const LatteDecompilerShaderResourceMapping& mapping,
                                             const LatteDecompilerShader& shader) {
    DescriptorRankPlan plan;
    static_assert(17 + LATTE_NUM_MAX_TEX_UNITS < DescriptorRankPlan::unused);
    struct Binding { int value; uint8_t kind, slot; };
    std::array<Binding, 17 + LATTE_NUM_MAX_TEX_UNITS> bindings{};
    size_t count = 0;
    auto add = [&](int value, uint8_t kind, uint8_t slot) {
        if (value >= 0) bindings[count++] = {value, kind, slot};
    };
    for (uint8_t i = 0; i < 16; ++i) add(mapping.uniformBuffersBindingPoint[i], 0, i);
    add(mapping.uniformVarsBufferBindingPoint, 1, 0);
    for (uint8_t i = 0; i < LATTE_NUM_MAX_TEX_UNITS; ++i)
        add(mapping.textureUnitToBindingPoint[i], 2, i);
    // Unexpected duplicate texture writes retain the original preparation/sort.
    std::array<bool, LATTE_NUM_MAX_TEX_UNITS> seen{};
    if (shader.textureUnitListCount < 0 || shader.textureUnitListCount > LATTE_NUM_MAX_TEX_UNITS)
        return plan;
    for (int i = 0; i < shader.textureUnitListCount; ++i) {
        const auto unit = shader.textureUnitList[i];
        if (unit >= LATTE_NUM_MAX_TEX_UNITS) return plan;
        if (mapping.textureUnitToBindingPoint[unit] >= 0) {
            if (seen[unit]) return plan;
            seen[unit] = true;
        }
    }
    std::sort(bindings.begin(), bindings.begin() + count,
              [](const auto& a, const auto& b) { return a.value < b.value; });
    for (size_t rank = 0; rank < count; ++rank) {
        if (rank && bindings[rank-1].value == bindings[rank].value) return DescriptorRankPlan{};
        const auto& binding = bindings[rank];
        if (binding.kind == 0) plan.blocks[binding.slot] = uint8_t(rank);
        else if (binding.kind == 1) plan.support = uint8_t(rank);
        else plan.textures[binding.slot] = uint8_t(rank);
    }
    plan.count = uint8_t(count);
    plan.valid = true;
    return plan;
}

LatteFetchShader* get_fetch_shader(const uint32_t* regs, uint64_t* keyOut, uint64_t frame) {
    ++stats.fetchLookups;
    if (keyOut) *keyOut = 0;
    static const bool memo = [] {
        const char* e = std::getenv("WWHD_VK_FETCH_MEMO");
        return e && !std::strcmp(e, "1");
    }();
    const bool cacheLast = memo && frame != ~uint64_t{0};
    if (!cacheLast) lastFetch = {};
    uint32_t address = regs[mmSQ_PGM_START_FS] << 8;
    if (!address) return nullptr;
    if (uint64_t(address) + 32 <= 0x100000000ull &&
        !std::memcmp(ppc_ptr(address), "WWFA0001", 8)) {
        const auto* p = reinterpret_cast<const uint8_t*>(ppc_ptr(address));
        auto word = [](const uint8_t* q) { return uint32_t(q[0]) | uint32_t(q[1]) << 8 |
            uint32_t(q[2]) << 16 | uint32_t(q[3]) << 24; };
        const uint32_t count = word(p + 16);
        if (count > 64) return nullptr;
        const uint64_t key = uint64_t(word(p + 8)) | uint64_t(word(p + 12)) << 32;
        if (keyOut) *keyOut = key;
        if (auto it = fetchShaders.find(key); it != fetchShaders.end()) return it->second;
        auto* fetch = gx2::decode_fetch_attributes(address, 32 + count * 52, nullptr);
        if (fetch) fetchShaders.emplace(key, fetch);
        return fetch;
    }
    bool compact = ld32(address) == 0x57574653;
    uint32_t count = compact ? ld32(address + 4) : 0;
    if (compact && count > 64) return nullptr;
    uint32_t size = compact ? 16 + count * 16 : regs[mmSQ_PGM_START_FS + 1] << 3;
    if (!size || size > 0x1000 || uint64_t(address) + size > 0x100000000ull) return nullptr;
    // Header reads and all range validation above remain fresh on every draw.
    // The body follows the existing once-per-frame program_hash contract.
    if (cacheLast && lastFetch.fetch && lastFetch.frame == frame &&
        lastFetch.address == address && lastFetch.size == size &&
        lastFetch.compact == compact && lastFetch.count == count) {
        ++stats.fetchLastHits;
        if (keyOut) *keyOut = lastFetch.key;
        return lastFetch.fetch;
    }
    uint64_t key = program_hash(address, size, frame);
    if (keyOut) *keyOut = key;
    auto remember = [&](LatteFetchShader* fetch) {
        if (cacheLast && fetch)
            lastFetch = {frame, key, address, size, count, compact, fetch};
        return fetch;
    };
    if (auto it = fetchShaders.find(key); it != fetchShaders.end()) return remember(it->second);
    auto* fetch = compact ? gx2::build_fetch_shader(address)
        : LatteShaderRecompiler_createFetchShader(key, const_cast<uint32_t*>(regs),
                                                  reinterpret_cast<uint32_t*>(ppc_ptr(address)), size);
    if (!compact) gx2::normalize_fetch_shader(fetch);
    fetchShaders.emplace(key, fetch);
    return remember(fetch);
}

namespace {
std::vector<Shader*> pendingShaderRecords;
// Pending compilations by source key: identical final GLSL (same identity)
// shares one compile. Only the identity is kept; the compiler job owns its copy.
std::unordered_map<uint64_t, std::pair<ShaderSourceId, std::weak_ptr<ShaderBinary>>> pendingBinarySources;
void publish_shader_binary(Shader& shader, uint64_t frame) {
    if (!shader.binary) return;
    auto binary = shader.binary;
    auto result = binary->get();
    shader.error = std::move(result.error);
    if (!binary->accounted) {
        binary->accounted = true;
        ++stats.spirvCompiles;
        stats.spirvCompileNs += result.compileNs;
    }
    const auto key = shader.spirvKey;
    auto cached = diskShaders.find(key);
    if (cached != diskShaders.end() && cached->second.vertex == shader.vertex &&
        cached->second.source == shader.sourceId && *cached->second.words == result.words) {
        // Another variant with the same final GLSL already published these words.
        shader.spirvWords = cached->second.words;
    } else {
        shader.spirvWords = result.words.empty() ? nullptr :
            std::make_shared<const std::vector<uint32_t>>(std::move(result.words));
        if (shader.spirvWords && shader.sourceId.length <= maxSourceBytes &&
            diskcache::spirv_valid(*shader.spirvWords) && diskShaders.size() < maxCacheEntries) {
            const size_t oldBytes = cached == diskShaders.end() ? 0 :
                diskcache::record_bytes(cached->second.words->size());
            const size_t newBytes = diskcache::record_bytes(shader.spirvWords->size());
            if (newBytes <= maxCacheBytes - (diskBytes - oldBytes)) {
                DiskShader entry;
                entry.source = shader.sourceId; entry.words = shader.spirvWords; entry.vertex = shader.vertex;
                diskShaders.insert_or_assign(key, std::move(entry));
                diskBytes = diskBytes - oldBytes + newBytes;
                ++diskRevision; diskDirty = !diskPath.empty(); diskChangedFrame = frame;
            }
        }
    }
    // Only this compilation's source lease has ended. A hash collision or a
    // newer submission may have replaced the map entry while it was pending.
    release_pending_shader_source(pendingBinarySources, key, binary);
    shader.binary.reset();
}
void poll_shader_binaries(uint64_t frame, bool drain = false) {
    size_t retained = 0;
    for (auto* shader : pendingShaderRecords) {
        if (drain || shader_binary_ready(shader->binary)) publish_shader_binary(*shader, frame);
        else pendingShaderRecords[retained++] = shader;
    }
    pendingShaderRecords.resize(retained);
}
}
// Both guest shaders and draw-array variants use the same cache and owned
// asynchronous compiler jobs. No variant may bypass persistence or wait on a
// utility compile from the render thread.
static bool request_cached_binary(Shader& shader, uint64_t frame, bool required = true) {
    initialize_disk_cache();
    // Guest variants retain distinct metadata, but identical final GLSL shares
    // compilation even with persistence disabled. The source identity (XXH3-128
    // + length, besides the FNV key) decides sharing; the text is not kept.
    const uint64_t spirvKey = shader.spirvKey;
    auto cached=diskShaders.find(spirvKey);
    if(cached!=diskShaders.end() && cached->second.vertex==shader.vertex && cached->second.source==shader.sourceId) {
        shader.spirvWords=cached->second.words;
        // the cached words are shared with the disk cache: strip a private copy
        if(full_precision_enabled() && shader.spirvWords) {
            auto words=std::make_shared<std::vector<uint32_t>>(*shader.spirvWords);
            note_relaxed_precision_stripped(strip_relaxed_precision(*words));
            shader.spirvWords=std::move(words);
        }
        if(cached->second.fromDisk) ++stats.diskHits;
        else ++stats.spirvReuseHits;
        if (shader.drawArrayCount && stutter::trace().enabled()) {
            const auto now = stutter::now();
            stutter::trace().add("draw_array_cache_hit", frame, spirvKey, now, now, 0);
        }
    } else {
        auto pending = pendingBinarySources.find(spirvKey);
        if (pending != pendingBinarySources.end() && pending->second.first == shader.sourceId)
            shader.binary = pending->second.second.lock();
        if (shader.binary) ++stats.spirvReuseHits;
        else {
            std::optional<ShaderCompileTicket> ticket;
            while (!(ticket = request_shader_compile(shader.glsl,
                shader.vertex ? ShaderStage::Vertex : ShaderStage::Fragment, frame, shader.key,
                async_shaders_enabled()))) {
                // Optional batching must not hold up a drawable base shader when
                // the compiler queue is full. Keep its GLSL and retry later.
                if (!required) return false;
                if (frame != ~uint64_t{0}) {
                    begin_pipeline_pending_frame();
                    present_completed_frame_while_pipeline_pending();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            shader.binary = std::make_shared<ShaderBinary>(std::move(*ticket));
            pendingBinarySources.insert_or_assign(spirvKey,
                std::make_pair(shader.sourceId, std::weak_ptr<ShaderBinary>(shader.binary)));
        }
        if (frame == ~uint64_t{0} || !async_shaders_enabled()) {
            (void)shader_binary_words_exact(shader.binary, frame);
            publish_shader_binary(shader, frame);
        } else pendingShaderRecords.push_back(&shader);
    }
    return true;
}

// A native instance index selects a draw's constants from one immutable upload
// reservation. Only single-instance, per-vertex-fetch draws select this variant;
// the guest's instance-ID input consequently remains zero.
// Opt-in (WWHD_VK_DRAW_ARRAYS=1): the variant is a second compiled shader, which
// drivers need not rasterize bit for bit like the guest's (edge pixels can differ),
// and it adds pipelines that existing caches and the prebuilt set do not cover.
static void make_draw_array_variant(Shader& shader) {
    static const bool enabled = [] {
        const char* e = std::getenv("WWHD_VK_DRAW_ARRAYS");
        return e && std::strcmp(e, "0");
    }();
    constexpr uint32_t count = 16;
    if (!enabled || !shader.vertex || mods::cemu::has_shaders() ||
        shader.mapping.uniformVarsBufferBindingPoint < 0 || shader.mapping.tfStorageBindingPoint >= 0 ||
        shader.uniformInputs.bytes * count > R.properties.limits.maxUniformBufferRange)
        return;
    for (int binding : shader.mapping.uniformBuffersBindingPoint) if (binding >= 0) return;
    const auto marker = shader.glsl.find("uniform ufBlock");
    if (marker == std::string::npos) return;
    const auto begin = shader.glsl.rfind('\n', marker);
    const auto open = shader.glsl.find('{', marker);
    const auto end = shader.glsl.find("};", open);
    if (begin == std::string::npos || open == std::string::npos || end == std::string::npos) return;
    auto members = shader.glsl.substr(open + 1, end - open - 1);
    std::vector<std::string> names;
    const std::regex declaration(R"(uniform\s+\w+\s+(\w+))");
    for (std::sregex_iterator i(members.begin(), members.end(), declaration), last; i != last; ++i)
        names.push_back((*i)[1].str());
    if (names.empty()) return;
    members = std::regex_replace(members, std::regex(R"(\buniform\s+)"), "");
    std::string block = "\nstruct WWHD_DrawUniforms {" + members + "};\nlayout(std140, set = 0, binding = " +
        std::to_string(shader.mapping.uniformVarsBufferBindingPoint) +
        ") uniform ufBlock { WWHD_DrawUniforms wwhdDrawUniforms[" + std::to_string(count) + "]; };\n";
    for (const auto& name : names)
        block += "#define " + name + " (wwhdDrawUniforms[gl_InstanceIndex]." + name + ")\n";
    auto source = shader.glsl;
    source.replace(begin, end + 2 - begin, block);
    const std::string instanceDefine = "#define gl_InstanceID gl_InstanceIndex";
    const auto instance = source.find(instanceDefine);
    if (instance == std::string::npos) return;
    source.replace(instance, instanceDefine.size(), "#define gl_InstanceID 0");
    auto variant = std::make_shared<Shader>(shader);
    variant->glsl = std::move(source);
    variant->retainedSource.reset();
    variant->sourceId = shader_source_id(variant->glsl);
    variant->spirvKey = source_cache_key(variant->glsl, true);
    variant->key = variant->spirvKey;
    variant->pipelineId = portable_output_identity(*variant);
    variant->binary.reset();
    variant->spirvWords.reset();
    variant->error.clear();
    variant->drawArrayCount = count;
    variant->drawArrayBase = &shader;
    shader.drawArrayVariant = std::move(variant);
}

// Stereo and ineligible draws never need this binary. Keep its source until the
// first eligible mono draw (including a later stereo -> mono switch), then hand
// it to the compiler without blocking the base draw. Select it only after the
// render-thread poll has published its completed binary (ready() alone also
// accepts pending jobs, which would stall the draw's pipeline prerequisites).
Shader* draw_array_variant(Shader& shader, uint64_t frame) {
    auto* variant = shader.drawArrayVariant.get();
    if (!variant) return &shader;
    if (!variant->glsl.empty()) {
        const auto started = stutter::trace().enabled() ? stutter::now() : 0;
        if (!request_cached_binary(*variant, frame, frame == ~uint64_t{0})) return &shader;
        if (started) stutter::trace().add("draw_array_request", frame, variant->spirvKey,
                                        started, stutter::now(), 0);
        if (retain_shader_source())
            variant->retainedSource = std::make_shared<const std::string>(std::move(variant->glsl));
        std::string{}.swap(variant->glsl);
    }
    return variant->ready() && !variant->binary ? variant : &shader;
}

Shader* translate(const uint32_t* regs, bool vertex, LatteFetchShader* fetch, uint64_t fsKey,
                  uint64_t frame, uint64_t stateGeneration, const Shader* linkedVs) {
    poll_shader_binaries(frame);
    ++stats.lookups;
    auto& last = lastShaders[vertex ? 0 : 1];
    uint32_t primitive = regs[REGADDR::VGT_PRIMITIVE_TYPE] & 0x3F;
    bool cacheLast = frame != ~uint64_t{0} && stateGeneration != ~uint64_t{0};
    if (!cacheLast) { last = {}; stateMemo.reset(); }
    if (cacheLast && last.shader && last.regs == regs && last.frame == frame &&
        last.generation == stateGeneration && last.fsKey == fsKey &&
        last.fetch == fetch && last.primitive == primitive) {
        ++stats.lastHits;
        return last.shader;
    }
    auto remember = [&](Shader* shader) {
        if (cacheLast) last = {regs, fetch, frame, stateGeneration, fsKey, primitive, shader};
        return shader;
    };
    uint32_t start = vertex ? mmSQ_PGM_START_VS : mmSQ_PGM_START_PS;
    uint32_t address = regs[start] << 8, size = regs[start + 1] << 3;
    if (!address || !size || size > 0x100000 || uint64_t(address) + size > 0x100000000ull) return nullptr;
    uint64_t base = program_hash(address, size, frame) ^ (vertex ? 0x1111 : 0x2222);
    // Level 1: program, fetch shader and linkage.
    std::array<uint32_t, kLinkageWords> words;
    const size_t count = gather_linkage(regs, vertex, fetch, words.data());
    const uint64_t seed = base ^ (vertex ? (fsKey * 31) ^ R.vertexFormats.shader_salt() : 0) ^
                          R.vertexDiagnostics.shader_salt();
    ++stats.stateHashLookups;
    static const bool memoEnabled = [] {
        const char* value = getenv("WWHD_VK_SHADER_STATE_MEMO");
        return value && !strcmp(value,"1");
    }();
    uint64_t linkage;
    if (memoEnabled && cacheLast && stateMemo.find(vertex, words.data(), count, seed, linkage)) {
        ++stats.stateHashMemoHits;
    } else {
        stats.stateHashBytes += count * sizeof(uint32_t);
        linkage = hash_bytes(words.data(), count * sizeof(uint32_t), seed);
        if (memoEnabled && cacheLast) stateMemo.remember(vertex, words.data(), count, seed, linkage);
    }
    // Level 2: the units and exports this program uses; for a pixel shader, the inputs its
    // vertex shader does not feed.
    const PsLink link = vertex ? PsLink{} : ps_link(regs, linkedVs);
    auto& use = programUses[linkage];
    if (use.failed) return remember(use.failed);
    if (use.known) {
        const uint64_t key = variant_hash(regs, vertex, use, linkage, link);
        if (auto it = variants.find(key); it != variants.end()) {
            ++stats.variantHits;
            verify_key(regs, vertex, fetch, fsKey, address, size, base, key, *it->second, link);
            return remember(it->second);
        }
    }
    auto started = std::chrono::steady_clock::now();
    struct CompileTimer {
        std::chrono::steady_clock::time_point start;
        ~CompileTimer() {
            ++stats.compiles;
            stats.compileNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count();
        }
    } timer{started};
    auto owned = std::make_unique<Shader>();
    Shader* shader = owned.get();
    shader->vertex = vertex;
    shader->kind = gfx::program_kind(ppc_ptr(address), size, vertex);
    const bool decompiled = translate_owned(*shader, regs, vertex, fetch, address, size,
        base, true, link, frame, linkage);
    if (!decompiled) {
        // Failures are per linkage: the variant words need the program's analysis.
        shader->key = shader->pipelineId = linkage ^ 0xFA17EDull;
        use.failed = shader;
        std::string{}.swap(shader->glsl);
        shaders.emplace(shader->key, std::move(owned));
        fprintf(stderr, "[vulkan] shader %08X: %s\n", address, shader->error.c_str());
        return remember(shader);
    }
    // Final GLSL identity: aliasing, the SPIR-V cache and pending compiles use
    // it, so the text can be released once the compile has been requested.
    shader->sourceId = shader_source_id(shader->glsl);
    shader->spirvKey = source_cache_key(shader->glsl, vertex);
    record_shader_variant(regs, vertex, base, fsKey, shader->dec);
    record_headstart_shader(regs, vertex, !vertex && linkedVs && linkedVs->dec);
    if (!use.known) {
        const auto& dec = *shader->dec;
        use.unitCount = uint8_t(std::clamp(int(dec.textureUnitListCount), 0, int(LATTE_NUM_MAX_TEX_UNITS)));
        for (uint32_t i = 0; i < use.unitCount; ++i)
            use.units[i] = std::min<uint8_t>(dec.textureUnitList[i], LATTE_NUM_MAX_TEX_UNITS - 1);
        use.exports = vertex ? dec.outputParameterMask : 0;
        use.streamout = vertex && dec.hasStreamoutBufferWrite;
        use.known = true;
    }
    const uint64_t key = variant_hash(regs, vertex, use, linkage, link);
    if (link.unfed) {
        static int reported = 0;
        if (reported < 20) {
            ++reported;
            ::log_msg("[vulkan] pixel shader %08X: %d inputs without a vertex shader output read their default value",
                      address, __builtin_popcount(link.unfed));
        }
    }
    stats.decompileNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-started).count();
    if (key_verify_rate()) {
        uint64_t legacy = legacy_state_hash(regs, base, vertex) ^ (vertex ? fsKey * 31 : 0);
        if (vertex && mods::cemu::has_shaders())
            legacy ^= uint64_t(regs[REGADDR::PA_CL_VTE_CNTL]) * 0x9E3779B97F4A7C15ull;
        verifiedKeys.emplace(legacy, key);
    }
    // Different keys can still translate to the same shader (e.g. a PS input table entry that the
    // program does not read). Such a shader is shared, and so are its pipelines (PR #46).
    const uint64_t output = output_hash(*shader);
    for (auto [it, end] = shadersByOutput.equal_range(output); it != end; ++it)
        if (it->second->kind == shader->kind && same_output(*it->second, *shader)) {
            ++stats.variantAliases;
            free_decompiler(shader->dec);
            variants.emplace(key, it->second);
            return remember(it->second);
        }
    shader->key = key;
    shaders.emplace(key, std::move(owned));
    variants.emplace(key, shader);
    shadersByOutput.emplace(output, shader);
    remember(shader);
    request_cached_binary(*shader, frame);
    // Stable before SPIR-V publication, and identical on cold/warm runs. Never
    // mutate a pipeline key when a pending compiler result becomes available.
    // Exact GLSL+metadata aliases above still share the same Shader object.
    // Computed from the full text, so the recorded identity is unchanged.
    shader->pipelineId = portable_output_identity(*shader);
    make_draw_array_variant(*shader);
    // The compiler job owns its own copy of the text; keep it here only for
    // the verification modes.
    if (retain_shader_source())
        shader->retainedSource = std::make_shared<const std::string>(std::move(shader->glsl));
    std::string{}.swap(shader->glsl);
    if (!shader->error.empty()) fprintf(stderr, "[vulkan] shader %08X: %s\n", address, shader->error.c_str());
    return shader;
}

const Shader* find_pipeline_shader(uint64_t pipelineId, bool vertex) {
    if (const auto* found = resolve_pipeline_shader_identity(pipelineId, vertex, shaders, variants,
        [](const Shader& a, const Shader& b) { return same_output(a, b); })) return found;
    for (const auto& [key, shader] : shaders)
        if (shader->drawArrayVariant && shader->drawArrayVariant->pipelineId == pipelineId && vertex)
            // Explicit head-start preparation needs the same lazy binary as a
            // draw. Complete it here, before gameplay, for recorded recipes.
            return draw_array_variant(*shader, ~uint64_t{0});
    return nullptr;
}

namespace {
using DiskSnapshot = diskcache::Snapshot;
struct DiskSaveResult { uint64_t revision=0, bytes=0, ns=0; bool saved=false; };
std::future<DiskSaveResult> diskSaveJob;
DiskSnapshot snapshot_disk_shaders() {
    auto start=std::chrono::steady_clock::now();
    DiskSnapshot snapshot;
    snapshot.reserve(diskShaders.size());
    for(const auto& entry:diskShaders) snapshot.push_back(entry);
    stats.diskSnapshotNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-start).count();
    return snapshot;
}
// Worker receives only immutable owned data; no shader maps, metadata, stats,
// Vulkan objects, or renderer state are accessed from this thread.
DiskSaveResult write_shader_snapshot(DiskSnapshot snapshot,std::string path,uint64_t revision) {
    auto start=std::chrono::steady_clock::now();
    DiskSaveResult result; result.revision=revision;
    auto finish=[&] {
        result.ns=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-start).count();
        return result;
    };
    std::string temporary;
    try {
        // Shared SPIR-V words are immutable; the snapshot only holds references.
        const auto bytes=diskcache::encode(snapshot,cache_fingerprint());
        if(bytes.empty()) return finish();
        std::error_code ec;
        auto parent=std::filesystem::u8path(path).parent_path();
        if(!parent.empty()) std::filesystem::create_directories(parent,ec);
        if(ec) return finish();
        FILE* file=open_atomic_temporary(path,temporary);
        if(!file) return finish();
        bool wrote=fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
        if(fclose(file)!=0) wrote=false;
        result.saved=wrote && host::replace_file(temporary,path);
        if(result.saved) result.bytes=bytes.size();
    } catch(...) { result.saved=false; }
    if(!result.saved && !temporary.empty()) { std::error_code ec; std::filesystem::remove(std::filesystem::u8path(temporary),ec); }
    return finish();
}
bool finish_disk_save(bool wait) {
    if(!diskSaveJob.valid() || (!wait && diskSaveJob.wait_for(std::chrono::seconds(0))!=std::future_status::ready))
        return false;
    try {
        auto result=diskSaveJob.get();
        stats.diskSaveNs+=result.ns;
        if(result.saved) {
            ++stats.diskSaves; stats.diskSavedBytes=result.bytes;
            diskDirty=diskRevision!=result.revision;
        } else diskDirty=true;
        return result.saved;
    } catch(...) { diskDirty=true; return false; }
}
} // namespace
bool shader_cache_dirty() { return diskDirty; }
uint64_t shader_cache_changed_frame() { return diskChangedFrame; }
bool save_shader_cache() {
    finish_shader_translation_worker();
    finish_shader_compiler();
    poll_shader_binaries(~uint64_t{0}, true);
    save_headstart_recording();
    // Explicit tools/shutdown contract is synchronous: join the old immutable
    // snapshot, then save any revisions compiled while that job was running.
    bool saved=finish_disk_save(true);
    if(!diskDirty || diskPath.empty()) return saved;
    try {
        auto snapshot=snapshot_disk_shaders();
        auto result=write_shader_snapshot(std::move(snapshot),diskPath,diskRevision);
        stats.diskSaveNs+=result.ns;
        if(result.saved) {
            diskDirty=false; ++stats.diskSaves; stats.diskSavedBytes=result.bytes;
        }
        return result.saved;
    } catch(...) { return false; }
}
void checkpoint_shader_cache(uint64_t frame) {
    poll_shader_binaries(frame);
    finish_disk_save(false);
    if(diskSaveJob.valid() || !diskDirty || diskPath.empty() || diskChangedFrame==~uint64_t{0} ||
       frame<diskChangedFrame || frame-diskChangedFrame<120 ||
       frame<diskAttemptFrame || frame-diskAttemptFrame<120) return;
    diskAttemptFrame=frame;
    try {
        auto snapshot=snapshot_disk_shaders();
        diskSaveJob=std::async(std::launch::async,write_shader_snapshot,
                              std::move(snapshot),diskPath,diskRevision);
    } catch(...) { /* Keep dirty for a bounded retry; cache failure is nonfatal. */ }
}

ShaderStats shader_stats() {
    ShaderStats result = stats;
    result.programs = programUses.size();
    result.shaders = shaders.size();
    result.variantKeys = variants.size();
    return result;
}

ShaderMemoryStats shader_memory_stats() {
    ShaderMemoryStats result;
    result.shaders = shaders.size();
    std::unordered_set<const void*> spirv;
    for (const auto& [key, shader] : shaders) {
        if (shader->dec && shader->dec->strBuf_shaderSource)
            result.decompilerText += shader->dec->strBuf_shaderSource->getLen();
        result.glslCapacity += shader->glsl.capacity();
        if (shader->retainedSource) result.retainedSourceCapacity += shader->retainedSource->capacity();
        result.verifyCapacity += shader->translatedGlsl.capacity();
        if (shader->spirvWords && spirv.insert(shader->spirvWords.get()).second)
            result.spirvCapacity += shader->spirvWords->capacity() * sizeof(uint32_t);
        if (const auto& variant = shader->drawArrayVariant) {
            result.glslCapacity += variant->glsl.capacity();
            if (variant->retainedSource)
                result.retainedSourceCapacity += variant->retainedSource->capacity();
            if (variant->spirvWords && spirv.insert(variant->spirvWords.get()).second)
                result.spirvCapacity += variant->spirvWords->capacity() * sizeof(uint32_t);
        }
    }
    result.diskEntries = diskShaders.size();
    for (const auto& [key, shader] : diskShaders)
        if (shader.words && spirv.insert(shader.words.get()).second)
            result.diskSpirvCapacity += shader.words->capacity() * sizeof(uint32_t);
    result.pendingCompiles = pendingBinarySources.size();
    return result;
}

bool update_prepared_uniforms(const uint32_t* regs, const Shader& shader,
                              std::vector<uint8_t>& data, float scaleX, float scaleY, const UniformReader* reader) {
    const auto& plan = shader.uniformInputs;
    if (!plan.ready || data.size() != plan.bytes) return false;
    for (const auto& span : plan.spans) {
        auto* destination = data.data() + span.destination;
        if (span.indirect) {
            const uint32_t address = regs[span.reg];
            if (address) memcpy(destination, reader ? reader->read(reader->context, address + span.source, span.bytes)
                                                   : ppc_ptr(address + span.source), span.bytes);
            else memset(destination, 0, span.bytes);
        } else memcpy(destination, reinterpret_cast<const uint8_t*>(regs + span.reg) + span.source, span.bytes);
    }
    const auto& offsets = shader.uniforms;
    auto copy = [&](int offset, const void* source, size_t bytes) {
        if (offset >= 0 && size_t(offset) + bytes <= data.size()) memcpy(data.data() + offset, source, bytes);
    };
    auto put = [&](int offset, float value) { copy(offset, &value, 4); };
    auto bitsf = [](uint32_t bits) { float value; memcpy(&value, &bits, 4); return value; };
    copy(offsets.offset_alphaTestRef, regs + REGADDR::SX_ALPHA_REF, 4);
    const float point = float(regs[REGADDR::PA_SU_POINT_SIZE] & 0xFFFF) / 8.0f;
    put(offsets.offset_pointSize, (point == 0 ? 0.125f : point) * scaleX);
    if (offsets.offset_windowSpaceToClipSpaceTransform >= 0) {
        const float width = 2 * bitsf(regs[REGADDR::PA_CL_VPORT_XSCALE]);
        const float height = -2 * bitsf(regs[REGADDR::PA_CL_VPORT_YSCALE]);
        put(offsets.offset_windowSpaceToClipSpaceTransform, width != 0 ? 2 / width : 0);
        put(offsets.offset_windowSpaceToClipSpaceTransform + 4, height != 0 ? 2 / height : 0);
    }
    if (offsets.offset_fragCoordScale >= 0) {
        const float scale[] = {scaleX != 0 ? 1 / scaleX : 1, scaleY != 0 ? 1 / scaleY : 1, 0, 0};
        copy(offsets.offset_fragCoordScale, scale, sizeof scale);
    }
    return true;
}

void pack_uniforms_into(const uint32_t* regs, const Shader& shader,
                        std::vector<uint8_t>& data, float scaleX, float scaleY,
                        float eyeOffset, float convergence, stereo::EyeRows* eyeRows,
                        const UniformReader* reader, bool useInputPlan) {
    if (stereo::layoutDrawDepth) eyeOffset = 0;
    const auto& offsets = shader.uniforms;
    data.resize((std::max(offsets.offset_endOfBlock, 16) + 15) & ~15);
    // Reused capacity may contain another shader's payload. Define every byte,
    // including gaps, padding, and remapped blocks with missing guest addresses.
    std::fill(data.begin(), data.end(), uint8_t{0});
    if (!shader.dec) return;
    // Full draws need the same input copies as continued draws. Execute the
    // translation-time, coalesced plan in mono and for unprojected stereo
    // stages instead of walking each decompiler register/block entry again.
    // Projected vertex stages retain the independent row-discovery path below.
    if (useInputPlan && (!shader.vertex || eyeOffset == 0) &&
        update_prepared_uniforms(regs, shader, data, scaleX, scaleY, reader)) {
        for (int offset : offsets.offset_texScale) {
            if (offset < 0 || size_t(offset) + 8 > data.size()) continue;
            const float scale[] = {1, 1};
            memcpy(data.data() + offset, scale, sizeof scale);
        }
        return;
    }
    auto copy = [&](int offset, const void* src, size_t size) {
        if (offset >= 0 && size_t(offset) + size <= data.size()) memcpy(data.data() + offset, src, size);
    };
    auto put = [&](int offset, float value) { copy(offset, &value, sizeof value); };
    auto bitsf = [](uint32_t value) { float result; memcpy(&result, &value, 4); return result; };
    uint32_t aluBase = mmSQ_ALU_CONSTANT0_0 + (shader.vertex ? 0x400 : 0);
    uint32_t blockBase = shader.vertex ? mmSQ_VTX_UNIFORM_BLOCK_START : mmSQ_PS_UNIFORM_BLOCK_START;
    auto projectionRow = [&](int offset, const void* bytes, size_t available) {
        if (!shader.vertex || eyeOffset == 0 || available < 64) return;
        float p[16]; memcpy(p,bytes,64);
        const float unshifted[4] = {p[0],p[1],p[2],p[3]};
        if (stereo::project_eye(p,eyeOffset,convergence)) {
            copy(offset,p,16);
            if (eyeRows && offset >= 0 && size_t(offset) + 16 <= data.size()) eyeRows->add(size_t(offset),unshifted);
        }
    };
    if (offsets.offset_remapped >= 0) {
        for (const auto& entry : shader.dec->list_remappedUniformEntries_register) {
            copy(offsets.offset_remapped + entry.mappedIndexOffset, regs + aluBase + entry.indexOffset / 4, 16);
            if (entry.indexOffset <= 4096-64)
                projectionRow(offsets.offset_remapped+entry.mappedIndexOffset,
                    regs+aluBase+entry.indexOffset/4,4096-entry.indexOffset);
        }
        for (const auto& group : shader.dec->list_remappedUniformEntries_bufferGroups) {
            uint32_t address = regs[blockBase + group.kcacheBankIdOffset / 4];
            if (!address) continue;
            const size_t blockSize = std::min<uint32_t>(regs[blockBase+group.kcacheBankIdOffset/4+1]+1,0x10000);
            for (const auto& entry : group.entries) {
                copy(offsets.offset_remapped + entry.mappedIndexOffset,
                    reader ? reader->read(reader->context, address + entry.indexOffset, 16)
                           : ppc_ptr(address + entry.indexOffset), 16);
                if (entry.indexOffset < blockSize)
                    projectionRow(offsets.offset_remapped+entry.mappedIndexOffset,
                        ppc_ptr(address+entry.indexOffset),blockSize-entry.indexOffset);
            }
        }
    }
    if (offsets.offset_uniformRegister >= 0) {
        copy(offsets.offset_uniformRegister, regs + aluBase, size_t(offsets.count_uniformRegister) * 16);
        if (shader.vertex && eyeOffset != 0)
            stereo::project_uniform_block(data.data()+offsets.offset_uniformRegister,
                size_t(offsets.count_uniformRegister)*16,eyeOffset,convergence,
                nullptr,
                eyeRows,size_t(offsets.offset_uniformRegister));
    }
    put(offsets.offset_alphaTestRef, bitsf(regs[Latte::REGADDR::SX_ALPHA_REF]));
    float point = float(regs[Latte::REGADDR::PA_SU_POINT_SIZE] & 0xFFFF) / 8.0f;
    put(offsets.offset_pointSize, (point == 0 ? 0.125f : point) * scaleX);
    if (offsets.offset_windowSpaceToClipSpaceTransform >= 0) {
        float width = 2.0f * bitsf(regs[Latte::REGADDR::PA_CL_VPORT_XSCALE]);
        float height = -2.0f * bitsf(regs[Latte::REGADDR::PA_CL_VPORT_YSCALE]);
        put(offsets.offset_windowSpaceToClipSpaceTransform, width != 0 ? 2.0f / width : 0);
        put(offsets.offset_windowSpaceToClipSpaceTransform + 4, height != 0 ? 2.0f / height : 0);
    }
    if (offsets.offset_fragCoordScale >= 0) {
        float scale[] = {scaleX != 0 ? 1.0f / scaleX : 1.0f, scaleY != 0 ? 1.0f / scaleY : 1.0f, 0, 0};
        copy(offsets.offset_fragCoordScale, scale, sizeof scale);
    }
    for (int unit = 0; unit < LATTE_NUM_MAX_TEX_UNITS; ++unit) {
        if (offsets.offset_texScale[unit] < 0) continue;
        float scale[] = {1, 1}; // Surfaces currently preserve the guest texture dimensions.
        copy(offsets.offset_texScale[unit], scale, sizeof scale);
    }
}

std::vector<uint8_t> pack_uniforms(const uint32_t* regs, const Shader& shader,
                                   float scaleX, float scaleY) {
    std::vector<uint8_t> data;
    pack_uniforms_into(regs, shader, data, scaleX, scaleY);
    return data;
}

void reset_shader_memoization() {
    stateMemo.reset();
    lastFetch = {};
    programHashes.clear();
    lastShaders[0] = {}; lastShaders[1] = {};
}

void clear_shader_cache() {
    finish_shader_translation_worker();
    finish_shader_compiler();
    for (auto& [key, shader] : shaders) free_decompiler(shader->dec);
    pendingShaderRecords.clear();
    pendingBinarySources.clear();
    shaders.clear();
    variants.clear();
    programUses.clear();
    shadersByOutput.clear();
    shadersByModule.clear();
    verifiedKeys.clear();
    reset_shader_memoization();
    // Fetch parsing allocations have shared interior pointers and no owning
    // destructor in the adapted Cemu parser. Keep its process-lifetime cache.
}
} // namespace gfxvk::vk
