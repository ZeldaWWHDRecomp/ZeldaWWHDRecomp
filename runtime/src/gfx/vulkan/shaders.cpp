// Real Latte microcode -> Cemu GLSL -> SPIR-V for the Vulkan backend.
#include "shaders.h"
#include "exact_state_memo.h"
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

namespace gfxvk::vk {
using Latte::REGADDR;
namespace {
std::unordered_map<uint64_t, std::unique_ptr<Shader>> shaders;
std::unordered_map<uint64_t, Shader*> shaderAliases;          // keys whose translation matched a shader's
std::unordered_map<std::string, Shader*> shadersByText;       // stage letter + GLSL -> first such shader
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
ExactStateMemo<105+5*LATTE_NUM_MAX_TEX_UNITS> stateMemo;
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
// Hash all state affecting the GLSL analyzer. Buffer addresses are excluded from
// shader variants: unlike Metal framebuffer-fetch, Vulkan always samples images.
uint64_t state_hash(const uint32_t* regs, uint64_t hash, bool vertex, bool cacheLast) {
    // 105 fixed words includes all four optional streamout strides, plus two
    // texture words and three sampler words per unit. Keep the original order.
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
    // Primitive mode changes point-size emission; point-sprite interpolation
    // changes fragment inputs. Streamout enable also affects shader resources.
    uint32_t primitiveState[] = {regs[REGADDR::VGT_PRIMITIVE_TYPE] & 0x3F,
        regs[mmSPI_INTERP_CONTROL_0] & (1u << 1), regs[mmVGT_STRMOUT_EN]};
    append(primitiveState,3);
    if (regs[mmVGT_STRMOUT_EN])
        for (uint32_t buffer = 0; buffer < 4; ++buffer)
            put(mmVGT_STRMOUT_VTX_STRIDE_0 + buffer * 4, 1);
    put(REGADDR::VGT_GS_MODE, 1); put(REGADDR::SQ_CONFIG, 1); put(mmCB_SHADER_MASK, 1);
    put(mmCB_SHADER_CONTROL, 1); put(mmDB_SHADER_CONTROL, 1);
    put(mmSPI_INPUT_Z, 1); put(REGADDR::SX_ALPHA_TEST_CONTROL, 1);
    // Clip enable and depth compare/write functions belong to pipeline state;
    // only viewport transform, half-Z and attachment enable affect translation.
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
    ++stats.stateHashLookups;
    static const bool enabled = [] {
        const char* value = getenv("WWHD_VK_SHADER_STATE_MEMO");
        return value && !strcmp(value,"1");
    }();
    uint64_t result;
    if(enabled && cacheLast && stateMemo.find(vertex,state.data(),count,hash,result)) {
        ++stats.stateHashMemoHits;
        return result;
    }
    stats.stateHashBytes += count*sizeof(uint32_t);
    result = hash_bytes(state.data(),count*sizeof(uint32_t),hash);
    if(enabled && cacheLast) stateMemo.remember(vertex,state.data(),count,hash,result);
    return result;
}
void free_decompiler(LatteDecompilerShader* shader) {
    if (!shader) return;
    delete shader->strBuf_shaderSource;
    delete shader;
}
}

void select_renderer() { g_renderer = std::make_unique<VulkanRenderer>(); }

std::vector<uint32_t> compile_glsl(const std::string& source, bool vertex, std::string* error) {
    static std::once_flag init;
    static bool initialized = false;
    std::call_once(init, [] { initialized = glslang::InitializeProcess(); });
    if (error) error->clear();
    auto fail = [&](const std::string& reason) { if (error) *error = reason; return std::vector<uint32_t>{}; };
    if (!initialized) return fail("glslang initialization failed");
    EShLanguage stage = vertex ? EShLangVertex : EShLangFragment;
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
                  uint64_t frame, uint64_t stateGeneration) {
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
    uint64_t key = state_hash(regs, base, vertex, cacheLast) ^ (vertex ? fsKey * 31 : 0);
    if (auto it = shaders.find(key); it != shaders.end()) {
        ++stats.variantHits;
        return remember(it->second.get());
    }
    if (auto it = shaderAliases.find(key); it != shaderAliases.end()) {
        ++stats.variantHits;
        return remember(it->second);
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
    shader->key = key; shader->vertex = vertex;
    shaders.emplace(key, std::move(owned));
    remember(shader);
    if (!g_renderer || g_renderer->GetType() != RendererAPI::Vulkan) {
        shader->error = "Vulkan renderer was not selected before shader translation"; return shader;
    }
    if ((regs[Latte::REGADDR::VGT_GS_MODE] & 3) != 0) {
        shader->error = "geometry shaders are not implemented by this Vulkan backend"; return shader;
    }
    if (vertex && !fetch) { shader->error = "vertex shader has no fetch program"; return shader; }
    LatteShader_UpdatePSInputs(const_cast<uint32_t*>(regs));
    LatteDecompilerOptions options;
    LatteDecompilerOutput_t output{};
    if (vertex) LatteDecompiler_DecompileVertexShader(base, const_cast<uint32_t*>(regs),
        ppc_ptr(address), size, fetch, options, &output);
    else LatteDecompiler_DecompilePixelShader(base, const_cast<uint32_t*>(regs),
        ppc_ptr(address), size, options, &output);
    if (!output.shader || output.shader->hasError || !output.shader->strBuf_shaderSource) {
        free_decompiler(output.shader); shader->error = "Latte GLSL translation failed"; return shader;
    }
    shader->dec = FinishDecompiledShader(output);
    shader->mapping = output.resourceMappingVK;
    shader->descriptorRanks = make_descriptor_rank_plan(shader->mapping, *shader->dec);
    shader->uniforms = output.uniformOffsetsVK;
    shader->glsl = shader->dec->strBuf_shaderSource->c_str();
    if (vertex) {
        // Depth-only and shaded variants must rasterize identical positions.
        // Match Metal's [[invariant]] position output for multipass depth tests.
        auto main = shader->glsl.find("void main(");
        if (main != std::string::npos)
            shader->glsl.insert(main, "invariant gl_Position;\n");
    }
    // Keys include registers that do not change every shader's translation (a vertex shader's key
    // has the render target formats; each stage has the other's input/output links), so one shader
    // came back under many keys: on a Galaxy S25, 5,072 translated shaders had 686 distinct GLSL texts,
    // each with its own pipelines, and the memory they took closed the game after about 35 minutes.
    // A translation identical to an earlier one (GLSL and binding metadata) is that shader: the new key
    // points to it, and its pipelines are shared.
    {
        const std::string text = std::string(vertex ? "v" : "p") + shader->glsl;
        auto same = [&](const Shader* other) {
            return !std::memcmp(&other->mapping, &shader->mapping, sizeof shader->mapping) &&
                   !std::memcmp(&other->uniforms, &shader->uniforms, sizeof shader->uniforms) &&
                   !std::memcmp(&other->descriptorRanks, &shader->descriptorRanks, sizeof shader->descriptorRanks);
        };
        if (auto it = shadersByText.find(text); it != shadersByText.end() && same(it->second)) {
            Shader* existing = it->second;
            free_decompiler(shader->dec);
            shaders.erase(key);  // the new Shader object (shader is no longer valid)
            shaderAliases[key] = existing;
            ++stats.variantAliases;
            return remember(existing);
        }
        shadersByText.emplace(text, shader);
    }
    stats.decompileNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-started).count();
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

ShaderStats shader_stats() { return stats; }

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
    shaderAliases.clear();
    shadersByText.clear();
    reset_shader_memoization();
    // Fetch parsing allocations have shared interior pointers and no owning
    // destructor in the adapted Cemu parser. Keep its process-lifetime cache.
}
} // namespace gfxvk::vk
