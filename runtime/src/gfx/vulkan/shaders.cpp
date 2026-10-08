// Real Latte microcode -> Cemu GLSL -> SPIR-V for the Vulkan backend.
#include "shaders.h"
#include "mods/cemu_pack.h"
#include "mods/shader_interface.h"
#include "graphic_pack_hash.h"
#include "gfx/area_sample.h"
#include "exact_state_memo.h"
#include "render_prof.h"
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "gx2/gx2.h"
#include "gx2/gx2_regs.h"
#include "ppc.h"
#include "util/helpers/StringBuf.h"
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <memory>
#include <filesystem>
#include <glslang/build_info.h>
#include "platform/host.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <future>
#include <unordered_map>

LatteDecompilerShader* FinishDecompiledShader(LatteDecompilerOutput_t& output);
LatteFetchShader* LatteShaderRecompiler_createFetchShader(LatteFetchShader::CacheHash hash,
    uint32* regs, uint32* code, uint32 size);

void log_msg(const char* fmt, ...);  // runtime.h (not included here: the Cemu headers)
namespace gfxvk::vk {
using Latte::REGADDR;
namespace {
std::unordered_map<uint64_t, std::unique_ptr<Shader>> shaders;
std::unordered_map<uint64_t, LatteFetchShader*> fetchShaders;
struct ProgramHash { uint64_t hash = 0, frame = ~uint64_t{0}; };
std::unordered_map<uint64_t, ProgramHash> programHashes;
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
// Disk entries retain exact GLSL to verify hash collisions and translator changes.
// Bump this compile recipe whenever compilation options/defaults change.
constexpr char cacheRecipe[] = "WWVKSC02:glsl450:vk1.1:spv1.3:noopt:resources1:"
    GLSLANG_VERSION_FLAVOR;
constexpr size_t maxCacheBytes = 128u * 1024u * 1024u;
constexpr size_t maxSourceBytes = 4u * 1024u * 1024u;
constexpr size_t maxSpirvWords = 1024u * 1024u;
constexpr size_t maxCacheEntries = 65536;
struct DiskShader { std::string source; std::vector<uint32_t> words; bool vertex; bool fromDisk = false; };
std::unordered_map<uint64_t, DiskShader> diskShaders;
std::string diskPath;
bool diskInitialized = false, diskDirty = false;
uint64_t diskChangedFrame = 0, diskAttemptFrame = 0, diskRevision = 0;
size_t diskBytes = 40;
uint64_t cache_checksum(const uint8_t* p, size_t n) {
    uint64_t h=14695981039346656037ull;
    for(size_t i=0;i<n;++i) h=(h^p[i])*1099511628211ull;
    return h;
}
uint64_t cache_fingerprint() {
    return cache_checksum(reinterpret_cast<const uint8_t*>(cacheRecipe),sizeof(cacheRecipe)) ^
        (uint64_t(GLSLANG_VERSION_MAJOR)<<48) ^ (uint64_t(GLSLANG_VERSION_MINOR)<<32) ^
        (uint64_t(GLSLANG_VERSION_PATCH)<<16);
}
void cache_put(std::vector<uint8_t>& out,uint64_t value,size_t bytes) {
    for(size_t i=0;i<bytes;++i) out.push_back(uint8_t(value>>(i*8)));
}
bool cache_take(const std::vector<uint8_t>& in,size_t& offset,uint64_t& value,size_t bytes) {
    if(offset>in.size() || bytes>in.size()-offset) return false;
    value=0; for(size_t i=0;i<bytes;++i) value|=uint64_t(in[offset++])<<(i*8);
    return true;
}
bool cache_spirv_valid(const std::vector<uint32_t>& words) {
    if(words.size()<5 || words.size()>maxSpirvWords || words[0]!=0x07230203 ||
       words[1]!=0x00010300 || !words[3] || words[3]>0x400000 || words[4]!=0) return false;
    for(size_t i=5;i<words.size();) {
        size_t length=words[i]>>16;
        if(!length || length>words.size()-i) return false;
        i+=length;
    }
    return words.size()>5;
}
bool decode_disk_cache(const std::vector<uint8_t>& file,
                      std::unordered_map<uint64_t,DiskShader>& result) {
    // 8-byte schema, fingerprint, payload length, checksum, record count.
    if(file.size()<40 || file.size()>maxCacheBytes || memcmp(file.data(),"WWVKSC02",8)) return false;
    size_t pos=8; uint64_t fingerprint,length,checksum,count;
    if(!cache_take(file,pos,fingerprint,8) || fingerprint!=cache_fingerprint() ||
       !cache_take(file,pos,length,8) || length!=file.size()-40 ||
       !cache_take(file,pos,checksum,8) || checksum!=cache_checksum(file.data()+40,file.size()-40) ||
       !cache_take(file,pos,count,8) || count>maxCacheEntries) return false;
    std::unordered_map<uint64_t,DiskShader> decoded;
    for(uint64_t i=0;i<count;++i) {
        uint64_t key,stage,sourceLength,wordCount;
        if(!cache_take(file,pos,key,8) || !cache_take(file,pos,stage,4) || stage>1 ||
           !cache_take(file,pos,sourceLength,4) || !sourceLength || sourceLength>maxSourceBytes ||
           !cache_take(file,pos,wordCount,4) || wordCount<5 || wordCount>maxSpirvWords ||
           sourceLength>file.size()-pos || wordCount>(file.size()-pos-sourceLength)/4) return false;
        DiskShader entry;
        entry.vertex=stage!=0;
        entry.fromDisk=true;
        entry.source.assign(reinterpret_cast<const char*>(file.data()+pos),size_t(sourceLength));
        pos+=sourceLength;
        entry.words.reserve(size_t(wordCount));
        for(uint64_t j=0;j<wordCount;++j) {
            uint64_t word; if(!cache_take(file,pos,word,4)) return false;
            entry.words.push_back(uint32_t(word));
        }
        if(!cache_spirv_valid(entry.words) || !decoded.emplace(key,std::move(entry)).second) return false;
    }
    if(pos!=file.size()) return false;
    result=std::move(decoded); return true;
}
void initialize_disk_cache() {
    if(diskInitialized) return;
    diskInitialized=true;
    auto start=std::chrono::steady_clock::now();
    try {
        const char* overridePath=getenv("WWHD_VK_SHADER_CACHE");
        const char* general=getenv("WWHD_SHADER_CACHE");
        if((overridePath && !strcmp(overridePath,"0")) ||
           (!overridePath && general && !strcmp(general,"0"))) return;
        std::string dir=overridePath ? overridePath : host::config_dir()+"/shadercache/vulkan-shaders";
        if(dir.empty()) return;
        diskPath=dir+"/spirv.bin";
        std::error_code ec;
        auto length=std::filesystem::file_size(diskPath,ec);
        if(ec || length<40 || length>maxCacheBytes) return;
        std::vector<uint8_t> bytes(static_cast<size_t>(length));
        FILE* file=fopen(diskPath.c_str(),"rb");
        if(!file) return;
        bool read=fread(bytes.data(),1,bytes.size(),file)==bytes.size() && fgetc(file)==EOF && !ferror(file);
        fclose(file);
        if(read && decode_disk_cache(bytes,diskShaders)) { ++stats.diskLoads; diskBytes=bytes.size(); }
    } catch(...) { diskShaders.clear(); }
    stats.diskLoadNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-start).count();
}

uint64_t program_hash(uint32_t address, uint32_t size, uint64_t frame) {
    // Include size: the same address can refer to different program ranges.
    auto& entry = programHashes[(uint64_t(address) << 32) | size];
    if (frame == ~uint64_t{0} || entry.frame != frame) {
        entry.hash = hash_bytes(ppc_ptr(address), size);
        entry.frame = frame;
    }
    return entry.hash;
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
                 (regs[REGADDR::PA_CL_CLIP_CNTL] & (1u << 19) ? 16u : 0u); // DX clip space (half Z)
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
        regs[REGADDR::PA_CL_CLIP_CNTL] & (1u << 19),
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
uint64_t output_hash(const Shader& shader) {
    uint64_t hash = hash_bytes(shader.glsl.data(), shader.glsl.size(), shader.vertex ? 0x5151 : 0xA2A2);
    hash = hash_bytes(&shader.mapping, sizeof shader.mapping, hash);
    hash = hash_bytes(&shader.uniforms, sizeof shader.uniforms, hash);
    return hash_bytes(&shader.descriptorRanks, sizeof shader.descriptorRanks, hash);
}
bool same_output(const Shader& a, const Shader& b) {
    return a.vertex == b.vertex && a.glsl == b.glsl &&
           !std::memcmp(&a.mapping, &b.mapping, sizeof a.mapping) &&
           !std::memcmp(&a.uniforms, &b.uniforms, sizeof a.uniforms) &&
           !std::memcmp(&a.descriptorRanks, &b.descriptorRanks, sizeof a.descriptorRanks);
}
// Latte program -> GLSL and its binding metadata (no SPIR-V). Also used by the verify mode, which
// translates again under the current registers and compares.
bool decompile(Shader& shader, const uint32_t* regs, bool vertex, LatteFetchShader* fetch,
               uint32_t address, uint32_t size, uint64_t base, bool packReplacement, const PsLink& link) {
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
    uint64_t packBase=0;
    if(mods::cemu::has_shaders()) {
        packBase=cemu_pack_hash::base(ppc_ptr(address),size,regs,vertex,fetch);
        options.legacyGraphicPackUniforms=!vertex&&mods::cemu::legacy_pixel_uniforms(packBase);
    }
    const uint64_t decompilerBase=mods::cemu::has_shaders()?packBase:base;
    if (!vertex) options.areaSampledTextures = ::gfx::area_sample::units_for_pixel_shader(ppc_ptr(address), size);
    LatteDecompilerOutput_t output{};
    if (vertex) LatteDecompiler_DecompileVertexShader(decompilerBase, const_cast<uint32_t*>(regs),
        ppc_ptr(address), size, fetch, options, &output);
    else LatteDecompiler_DecompilePixelShader(decompilerBase, const_cast<uint32_t*>(regs),
        ppc_ptr(address), size, options, &output);
    if (!output.shader || output.shader->hasError || !output.shader->strBuf_shaderSource) {
        free_decompiler(output.shader); shader.error = "Latte GLSL translation failed"; return false;
    }
    if (options.areaSampledTextures && packReplacement && mods::cemu::has_shaders() &&
        !mods::cemu::shader_source(packBase, cemu_pack_hash::auxiliary(*output.shader, regs, vertex), vertex).empty()) {
        // a graphics pack replaces this shader: translate it as Cemu does, so its interface matches
        free_decompiler(output.shader);
        options.areaSampledTextures = 0;
        output = LatteDecompilerOutput_t{};
        LatteDecompiler_DecompilePixelShader(decompilerBase, const_cast<uint32_t*>(regs), ppc_ptr(address), size, options, &output);
        if (!output.shader || output.shader->hasError || !output.shader->strBuf_shaderSource) {
            free_decompiler(output.shader); shader.error = "Latte GLSL translation failed"; return false;
        }
    }
    shader.dec = FinishDecompiledShader(output);
    shader.mapping = output.resourceMappingVK;
    shader.descriptorRanks = make_descriptor_rank_plan(shader.mapping, *shader.dec);
    shader.uniforms = output.uniformOffsetsVK;
    shader.glsl = shader.dec->strBuf_shaderSource->c_str();
    if (options.areaSampledTextures && ::gfx::area_sample::rewrite(shader.glsl, options.areaSampledTextures, false) <= 0)
        fprintf(stderr, "[vulkan] pixel shader %08X: area-sampled taps not applied\n", address);
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
        if(!custom.empty()) {
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
    return true;
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
    bool built = decompile(reference, regs, vertex, fetch, address, size, base, false, link);
    ++stats.verifyChecks;
    const std::string& translated = shader.translatedGlsl.empty() ? shader.glsl : shader.translatedGlsl;
    if (!built || reference.glsl != translated ||
        std::memcmp(&reference.mapping, &shader.mapping, sizeof shader.mapping) ||
        std::memcmp(&reference.uniforms, &shader.uniforms, sizeof shader.uniforms) ||
        std::memcmp(&reference.descriptorRanks, &shader.descriptorRanks, sizeof shader.descriptorRanks)) {
        ++stats.verifyViolations;
        std::string left, right;
        first_difference(translated, reference.glsl, left, right);
        fprintf(stderr, "[vulkan shader key] VIOLATION %s %08X key %016llx legacy %016llx: %s; cached \"%s\" vs now \"%s\"\n",
                vertex ? "vs" : "ps", address, (unsigned long long)key, (unsigned long long)legacy,
                !built ? reference.error.c_str() : reference.glsl != translated ? "GLSL differs" : "binding metadata differs",
                left.c_str(), right.c_str());
    }
    free_decompiler(reference.dec);
    stats.verifyNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count();
}
}

void select_renderer() { g_renderer = std::make_unique<VulkanRenderer>(); }

static std::vector<uint32_t> compile_stage(const std::string& source, EShLanguage stage, std::string* error) {
    static std::once_flag init;
    static bool initialized = false;
    std::call_once(init, [] { initialized = glslang::InitializeProcess(); });
    if (error) error->clear();
    auto fail = [&](const std::string& reason) { if (error) *error = reason; return std::vector<uint32_t>{}; };
    if (!initialized) return fail("glslang initialization failed");
    glslang::TShader shader(stage);
    const char* text = source.c_str();
    shader.setStrings(&text, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, false, messages))
        return fail(std::string(shader.getInfoLog()) + shader.getInfoDebugLog());
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) return fail(program.getInfoLog());
    std::vector<uint32_t> words;
    glslang::SpvOptions options;
    options.disableOptimizer = true; // no SPIRV-Tools runtime dependency
    glslang::GlslangToSpv(*program.getIntermediate(stage), words, &options);
    if (words.empty()) return fail("glslang emitted empty SPIR-V");
    return words;
}

std::vector<uint32_t> compile_glsl(const std::string& source, bool vertex, std::string* error) {
    return compile_stage(source, vertex ? EShLangVertex : EShLangFragment, error);
}
std::vector<uint32_t> compile_compute(const std::string& source, std::string* error) {
    return compile_stage(source, EShLangCompute, error);
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
    fetchShaders.emplace(key, fetch);
    return remember(fetch);
}

Shader* translate(const uint32_t* regs, bool vertex, LatteFetchShader* fetch, uint64_t fsKey,
                  uint64_t frame, uint64_t stateGeneration, const Shader* linkedVs) {
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
    const uint64_t seed = base ^ (vertex ? fsKey * 31 : 0);
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
    if (!decompile(*shader, regs, vertex, fetch, address, size, base, true, link)) {
        // Failures are per linkage: the variant words need the program's analysis.
        shader->key = shader->pipelineId = linkage ^ 0xFA17EDull;
        use.failed = shader;
        shaders.emplace(shader->key, std::move(owned));
        fprintf(stderr, "[vulkan] shader %08X: %s\n", address, shader->error.c_str());
        return remember(shader);
    }
    record_shader_variant(regs, vertex, base, fsKey, shader->dec);
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
    initialize_disk_cache();
    // Guest variants retain distinct metadata, but identical final GLSL shares
    // compilation even with persistence disabled. Exact source checks collisions.
    uint64_t spirvKey=cache_checksum(reinterpret_cast<const uint8_t*>(shader->glsl.data()),shader->glsl.size()) ^
        cache_fingerprint() ^ (vertex ? 0x9E3779B97F4A7C15ull : 0xD1B54A32D192ED03ull);
    auto cached=diskShaders.find(spirvKey);
    if(cached!=diskShaders.end() && cached->second.vertex==vertex && cached->second.source==shader->glsl) {
        shader->spirv=cached->second.words;
        if(cached->second.fromDisk) ++stats.diskHits;
        else ++stats.spirvReuseHits;
    } else {
        auto compileStart=std::chrono::steady_clock::now();
        shader->spirv = compile_glsl(shader->glsl, vertex, &shader->error);
        ++stats.spirvCompiles;
        stats.spirvCompileNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-compileStart).count();
        if(shader->glsl.size()<=maxSourceBytes &&
           cache_spirv_valid(shader->spirv) && diskShaders.size()<maxCacheEntries) {
            size_t oldBytes=cached==diskShaders.end() ? 0 :
                20+cached->second.source.size()+cached->second.words.size()*4;
            size_t newBytes=20+shader->glsl.size()+shader->spirv.size()*4;
            if(newBytes<=maxCacheBytes-(diskBytes-oldBytes)) {
                diskShaders.insert_or_assign(spirvKey,DiskShader{shader->glsl,shader->spirv,vertex,false});
                diskBytes=diskBytes-oldBytes+newBytes;
                ++diskRevision;
                diskDirty=!diskPath.empty();
                diskChangedFrame=frame;
            }
        }
    }
    // Pipelines need only the module and its resource mapping: shaders whose SPIR-V and mapping
    // are identical (their GLSL can still differ, e.g. in comments) share pipelines; their
    // draw-time metadata (uniform remapping, samplers) stays their own.
    shader->pipelineId = shader->key;
    if (!shader->spirv.empty()) {
        uint64_t module = hash_bytes(shader->spirv.data(), shader->spirv.size() * 4, vertex ? 0x77 : 0x88);
        module = hash_bytes(&shader->mapping, sizeof shader->mapping, module);
        bool shared = false;
        for (auto [it, end] = shadersByModule.equal_range(module); it != end; ++it)
            if (it->second->spirv == shader->spirv &&
                !std::memcmp(&it->second->mapping, &shader->mapping, sizeof shader->mapping)) {
                shader->pipelineId = it->second->pipelineId;
                ++stats.moduleAliases;
                shared = true;
                break;
            }
        if (!shared) shadersByModule.emplace(module, shader);
    }
    if (!shader->error.empty()) fprintf(stderr, "[vulkan] shader %08X: %s\n", address, shader->error.c_str());
    return shader;
}

namespace {
using DiskSnapshot = std::vector<std::pair<uint64_t,DiskShader>>;
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
        std::vector<uint8_t> payload;
        size_t expected=0;
        for(const auto& [key,entry]:snapshot) {
            size_t bytes=20+entry.source.size()+entry.words.size()*4;
            if(bytes>maxCacheBytes-40-expected) return finish();
            expected+=bytes;
        }
        payload.reserve(expected);
        for(const auto& [key,entry]:snapshot) {
            cache_put(payload,key,8); cache_put(payload,entry.vertex,4);
            cache_put(payload,entry.source.size(),4); cache_put(payload,entry.words.size(),4);
            payload.insert(payload.end(),entry.source.begin(),entry.source.end());
            for(uint32_t word:entry.words) cache_put(payload,word,4);
        }
        std::vector<uint8_t> header={'W','W','V','K','S','C','0','2'};
        cache_put(header,cache_fingerprint(),8); cache_put(header,payload.size(),8);
        cache_put(header,cache_checksum(payload.data(),payload.size()),8);
        cache_put(header,snapshot.size(),8);
        std::error_code ec;
        auto parent=std::filesystem::path(path).parent_path();
        if(!parent.empty()) std::filesystem::create_directories(parent,ec);
        if(ec) return finish();
        temporary=path+".tmp-"+std::to_string(start.time_since_epoch().count());
        FILE* file=fopen(temporary.c_str(),"wb");
        if(!file) return finish();
        bool wrote=fwrite(header.data(),1,header.size(),file)==header.size() &&
                   fwrite(payload.data(),1,payload.size(),file)==payload.size();
        if(fclose(file)!=0) wrote=false;
        result.saved=wrote && host::replace_file(temporary,path);
        if(result.saved) result.bytes=header.size()+payload.size();
    } catch(...) { result.saved=false; }
    if(!result.saved && !temporary.empty()) { std::error_code ec; std::filesystem::remove(temporary,ec); }
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

void pack_uniforms_into(const uint32_t* regs, const Shader& shader,
                        std::vector<uint8_t>& data, float scaleX, float scaleY) {
    const auto& offsets = shader.uniforms;
    data.resize((std::max(offsets.offset_endOfBlock, 16) + 15) & ~15);
    // Reused capacity may contain another shader's payload. Define every byte,
    // including gaps, padding, and remapped blocks with missing guest addresses.
    std::fill(data.begin(), data.end(), uint8_t{0});
    if (!shader.dec) return;
    auto copy = [&](int offset, const void* src, size_t size) {
        if (offset >= 0 && size_t(offset) + size <= data.size()) memcpy(data.data() + offset, src, size);
    };
    auto put = [&](int offset, float value) { copy(offset, &value, sizeof value); };
    auto bitsf = [](uint32_t value) { float result; memcpy(&result, &value, 4); return result; };
    uint32_t aluBase = mmSQ_ALU_CONSTANT0_0 + (shader.vertex ? 0x400 : 0);
    uint32_t blockBase = shader.vertex ? mmSQ_VTX_UNIFORM_BLOCK_START : mmSQ_PS_UNIFORM_BLOCK_START;
    if (offsets.offset_remapped >= 0) {
        for (const auto& entry : shader.dec->list_remappedUniformEntries_register)
            copy(offsets.offset_remapped + entry.mappedIndexOffset, regs + aluBase + entry.indexOffset / 4, 16);
        for (const auto& group : shader.dec->list_remappedUniformEntries_bufferGroups) {
            uint32_t address = regs[blockBase + group.kcacheBankIdOffset / 4];
            if (!address) continue;
            for (const auto& entry : group.entries)
                copy(offsets.offset_remapped + entry.mappedIndexOffset, ppc_ptr(address + entry.indexOffset), 16);
        }
    }
    if (offsets.offset_uniformRegister >= 0)
        copy(offsets.offset_uniformRegister, regs + aluBase, size_t(offsets.count_uniformRegister) * 16);
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
    for (auto& [key, shader] : shaders) free_decompiler(shader->dec);
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
