// Native depth shaders for J3D's depth-only pass: the families and their native programs (j3d_depth.h). Decomp build
// fast mode, WWHD_NATIVE_DRAW=ON only.
//
// A family is a set of J3D depth programs with the same structure: the same instruction sequence once temporaries are
// renumbered and every remapped uniform is named by what it reads (uniform block location and vec4). Membership is
// decided by that normalised text's hash, so a member reads exactly the inputs its family's native program reads, in
// the same arithmetic order: the native program reproduces the translated arithmetic (the decompiled GLSL is the
// specification) with its own interface: the position (and skinning) streams as they are, a view block (the slice's
// view 3x4 and projection rows), a model block (3 rows) or the skinning palette, and no colour output.
#include "../../third_party/cemu/cemu_shim.h"  // (as the gx2 sources get it: -include)
#include "native_draw/j3d_depth.h"
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"
#include "gfx/vulkan/backend.h"
#include "gfx/vulkan/shaders.h"
#include "gfx/vulkan/deferred_dispatch.h"
#include "gfx/vulkan/pass.h"
#include "gfx/vulkan/vertex_formats.h"
#include "gfx/vulkan/clip_emulation.h"
#include "gx2/gx2_cmd.h"
#include "runtime.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Latte;

namespace native_draw::j3d_depth {
namespace {
using gfxvk::R;
float f32(uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; }

// ---------------------------------------------------------------- structure hash
// The program's main body as tokens: comments dropped, R<n>f/R<n>i renumbered by first appearance, uf_remapped<S>[i]
// named U<location>_<vec4> (its remap table), everything else (operators, constants, block reads) kept; FNV-1a over the
// tokens joined by single spaces (the same hash can be computed offline from a WWHD_J3D_DEPTH_DUMP program dump).
uint64_t structure_hash(const gfxvk::vk::Shader* s, std::string* textOut = nullptr) {
    const std::string& t = s->source_text();
    size_t i = t.find("void main()");
    if (i == std::string::npos) return 0;
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> remap;  // array index -> (location, vec4)
    for (const auto& g : s->dec->list_remappedUniformEntries_bufferGroups)
        for (const auto& e : g.entries) remap[e.mappedIndexOffset / 16] = {g.kcacheBankIdOffset / 28u, e.indexOffset / 16u};
    std::unordered_map<std::string, int> regs;
    std::string out;
    std::vector<std::string> toks;
    const size_t n = t.size();
    auto word = [](char c) { return std::isalnum(uint8_t(c)) || c == '_'; };
    while (i < n) {
        const char c = t[i];
        if (c == '/' && i + 1 < n && t[i + 1] == '/') { while (i < n && t[i] != '\n') ++i; continue; }
        if (std::isspace(uint8_t(c))) { ++i; continue; }
        if (std::isalpha(uint8_t(c)) || c == '_') {
            size_t j = i; while (j < n && word(t[j])) ++j;
            toks.push_back(t.substr(i, j - i)); i = j; continue;
        }
        if (std::isdigit(uint8_t(c))) {
            size_t j = i; while (j < n && (word(t[j]) || t[j] == '.')) ++j;
            toks.push_back(t.substr(i, j - i)); i = j; continue;
        }
        toks.push_back(std::string(1, c)); ++i;
    }
    for (size_t k = 0; k < toks.size(); ++k) {
        std::string tok = toks[k];
        const size_t L = tok.size();
        if (L >= 3 && tok[0] == 'R' && (tok[L - 1] == 'f' || tok[L - 1] == 'i') &&
            std::all_of(tok.begin() + 1, tok.end() - 1, [](char ch) { return std::isdigit(uint8_t(ch)); })) {
            auto [it, fresh] = regs.emplace(tok, int(regs.size()));
            tok = "r" + std::to_string(it->second) + tok[L - 1];
        } else if ((tok == "uf_remappedVS" || tok == "uf_remappedPS") && k + 3 < toks.size() && toks[k + 1] == "[" &&
                   toks[k + 3] == "]") {
            const uint32_t idx = uint32_t(std::strtoul(toks[k + 2].c_str(), nullptr, 10));
            auto it = remap.find(idx);
            tok = it == remap.end() ? "U?" + toks[k + 2]
                                    : "U" + std::to_string(it->second.first) + "_" + std::to_string(it->second.second);
            k += 3;
        }
        if (!out.empty()) out += ' ';
        out += tok;
    }
    if (textOut) *textOut = out;
    uint64_t h = 0xCBF29CE484222325ull;
    for (char ch : out) h = (h ^ uint8_t(ch)) * 0x100000001B3ull;
    return h;
}

// ---------------------------------------------------------------- families
enum Kind { kRigid, kSkin2, kSkin3 };
struct Family {
    const char* name;
    Kind kind;
    uint64_t vs;  // structure hash of the vertex program
};
// pixel programs without texture reads, discards or depth output (their colour is masked off): drawn without one
bool colour_only(const gfxvk::vk::Shader* ps) {
    if (!ps->dec || ps->dec->textureUnitListCount) return false;
    const std::string& t = ps->source_text();
    return t.find("discard") == std::string::npos && t.find("gl_FragDepth") == std::string::npos;
}
std::vector<Family>& families() {
    static std::vector<Family> f = [] {
        std::vector<Family> v;
        // the structure hashes of the families (from WWHD_J3D_DEPTH_DUMP dumps of 7 scenes); a
        // WWHD_J3D_DEPTH_FAMILIES=name:kind:hash,... list replaces them (kind 0 rigid, 1 two-bone skin, 2 three-bone skin)
        if (const char* e = std::getenv("WWHD_J3D_DEPTH_FAMILIES")) {
            static std::deque<std::string> names;  // (stable addresses: the families keep c_str pointers)
            std::string s(e);
            size_t p = 0;
            while (p < s.size()) {
                size_t q = s.find(',', p);
                if (q == std::string::npos) q = s.size();
                const std::string item = s.substr(p, q - p);
                const size_t a = item.find(':'), b = item.find(':', a + 1);
                if (a != std::string::npos && b != std::string::npos) {
                    names.push_back(item.substr(0, a));
                    v.push_back({names.back().c_str(), Kind(std::atoi(item.c_str() + a + 1)),
                                 std::strtoull(item.c_str() + b + 1, nullptr, 16)});
                }
                p = q + 1;
            }
        }
        if (v.empty())
            v = {{"rigid", kRigid, 0x8541df56ac5fa6d2ull},    // 59.7 % of the 7 scenes' depth-only draws, 101 programs
                 {"skin2", kSkin2, 0x84d3e3ea689eacf7ull},    // 6.9 %, 15 programs
                 {"skin3", kSkin3, 0x0e74a2cd1f98602aull}};  // 4.7 %, 6 programs
        return v;
    }();
    return f;
}
struct Classified { const Family* family = nullptr; uint64_t vsHash = 0; };
std::unordered_map<uint64_t, Classified> g_byShader;  // vertex shader key -> its family
std::map<uint64_t, std::pair<uint64_t, std::string>> g_unknown;  // structure hash -> (samples, a program's text)

// ---------------------------------------------------------------- native programs
const char* kHeader = R"glsl(#version 450
layout(location = 0) in uvec4 aPos;
invariant gl_Position;
float be(uint v) { return uintBitsToFloat((v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24)); }
float mul_nonIEEE1(float a, float b) { return (b == 0.0 ? 0.0 : a) * b; }
// the slice's view: the view block's rows 0-2 (view 3x4) and 27-30 (projection)
layout(set = 0, binding = 0, std140) uniform View { vec4 view[3]; vec4 proj[4]; };
)glsl";
// the end of every family: view, projection, the decompiler's SET_POSITION (Vulkan depth range); CLAMP: the emulated
// near-plane depth clamp variant (the device lacks depthClamp)
std::string tail(bool clamp) {
    return std::string(R"glsl(
    vec4 w = vec4(world, 1.0);
    vec3 v = vec3(dot(w, view[0]), dot(w, view[1]), dot(w, view[2]));
    vec4 e = vec4(v, 1.0);
    gl_Position = vec4(dot(e, proj[0]), dot(e, proj[1]), dot(e, proj[2]), dot(e, proj[3]));
    gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
)glsl") + (clamp ? "    if (gl_Position.w > 0.0) gl_Position.z = max(gl_Position.z, 0.0);\n" : "") + "}\n";
}
std::string vertex_source(Kind k, bool clamp) {
    std::string s = kHeader;
    if (k == kRigid) {
        s += R"glsl(layout(set = 0, binding = 1, std140) uniform Model { vec4 model[3]; };
void main() {
    vec4 p = vec4(be(aPos.x), be(aPos.y), be(aPos.z), 1.0);
    vec3 world = vec3(dot(p, model[0]), dot(p, model[1]), dot(p, model[2]));
)glsl";
    } else if (k == kSkin3) {
        // three bones: as two, plus the third bone's rows weighted by z and added last (all three of its terms the
        // GPU's MUL_IEEE)
        s += R"glsl(layout(set = 0, binding = 1, std140) uniform Palette { vec4 palette[4096]; };
layout(location = 1) in uvec4 aWeight;
layout(location = 2) in uvec4 aIndex;
uint swap(uint v) { return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24); }
void main() {
    vec4 p = vec4(be(aPos.x), be(aPos.y), be(aPos.z), 1.0);
    float w0 = be(aWeight.x), w1 = be(aWeight.y), w2 = be(aWeight.z);
    int i0 = int(swap(aIndex.x)) * 3, i1 = int(swap(aIndex.y)) * 3, i2 = int(swap(aIndex.z)) * 3;
    int a0 = floatBitsToInt(dot(p, palette[i0])), a1 = floatBitsToInt(dot(p, palette[i0 + 1]));
    int a2 = floatBitsToInt(dot(p, palette[i0 + 2]));
    int b0 = floatBitsToInt(dot(p, palette[i1])), b1 = floatBitsToInt(dot(p, palette[i1 + 1]));
    int b2 = floatBitsToInt(dot(p, palette[i1 + 2]));
    int c0 = floatBitsToInt(dot(p, palette[i2])), c1 = floatBitsToInt(dot(p, palette[i2 + 1]));
    int c2 = floatBitsToInt(dot(p, palette[i2 + 2]));
    int tx = floatBitsToInt(w0 * intBitsToFloat(a0)), ty = floatBitsToInt(w0 * intBitsToFloat(a1));
    int tz = floatBitsToInt(w0 * intBitsToFloat(a2));
    int ux = floatBitsToInt(mul_nonIEEE1(intBitsToFloat(b0), w1)), uy = floatBitsToInt(w1 * intBitsToFloat(b1));
    int uz = floatBitsToInt(mul_nonIEEE1(intBitsToFloat(b2), w1));
    int sx = floatBitsToInt(intBitsToFloat(tx) + intBitsToFloat(ux)), sy = floatBitsToInt(intBitsToFloat(ty) + intBitsToFloat(uy));
    int sz = floatBitsToInt(intBitsToFloat(tz) + intBitsToFloat(uz));
    int vx = floatBitsToInt(w2 * intBitsToFloat(c0)), vy = floatBitsToInt(w2 * intBitsToFloat(c1));
    int vz = floatBitsToInt(w2 * intBitsToFloat(c2));
    vec3 world = vec3(intBitsToFloat(sx) + intBitsToFloat(vx), intBitsToFloat(sy) + intBitsToFloat(vy),
                      intBitsToFloat(sz) + intBitsToFloat(vz));
)glsl";
    } else {
        // two bones: rows 3i..3i+2 of the palette per index; weights x, y (the translated sequence: the second bone's
        // x and z terms are the GPU's MUL, its y term and the first bone's MUL_IEEE)
        s += R"glsl(layout(set = 0, binding = 1, std140) uniform Palette { vec4 palette[4096]; };
layout(location = 1) in uvec4 aWeight;
layout(location = 2) in uvec4 aIndex;
uint swap(uint v) { return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24); }
void main() {
    vec4 p = vec4(be(aPos.x), be(aPos.y), be(aPos.z), 1.0);
    float w0 = be(aWeight.x), w1 = be(aWeight.y);
    int i0 = int(swap(aIndex.x)) * 3, i1 = int(swap(aIndex.y)) * 3;
    // (each product kept as its bits before the sum, as the translated program does: no fused multiply-add)
    int a0 = floatBitsToInt(dot(p, palette[i0])), a1 = floatBitsToInt(dot(p, palette[i0 + 1]));
    int a2 = floatBitsToInt(dot(p, palette[i0 + 2]));
    int b0 = floatBitsToInt(dot(p, palette[i1])), b1 = floatBitsToInt(dot(p, palette[i1 + 1]));
    int b2 = floatBitsToInt(dot(p, palette[i1 + 2]));
    int tx = floatBitsToInt(w0 * intBitsToFloat(a0)), ty = floatBitsToInt(w0 * intBitsToFloat(a1));
    int tz = floatBitsToInt(w0 * intBitsToFloat(a2));
    int ux = floatBitsToInt(mul_nonIEEE1(intBitsToFloat(b0), w1)), uy = floatBitsToInt(w1 * intBitsToFloat(b1));
    int uz = floatBitsToInt(mul_nonIEEE1(intBitsToFloat(b2), w1));
    vec3 world = vec3(intBitsToFloat(tx) + intBitsToFloat(ux), intBitsToFloat(ty) + intBitsToFloat(uy),
                      intBitsToFloat(tz) + intBitsToFloat(uz));
)glsl";
    }
    return s + tail(clamp);
}
const char* kFragment = "#version 450\nvoid main() {}\n";

VkShaderModule module(const std::string& src, bool vertex) {
    std::string error;
    auto words = gfxvk::vk::compile_glsl(src, vertex, &error);
    if (words.empty()) {
        LOG("[j3d-depth] native %s program did not compile: %s", vertex ? "vertex" : "fragment", error.c_str());
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = words.size() * 4;
    ci.pCode = words.data();
    VkShaderModule m = VK_NULL_HANDLE;
    if (gfxvk::vkCreateShaderModule(R.device, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

struct Native {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::map<std::string, VkPipeline> pipelines;
} g_native;

void ensure_layout() {
    if (g_native.device == R.device && g_native.layout) return;
    g_native = {};
    g_native.device = R.device;
    VkDescriptorSetLayoutBinding b[2]{{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                                      {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = 2;
    ci.pBindings = b;
    gfxvk::vkCreateDescriptorSetLayout(R.device, &ci, nullptr, &g_native.set);
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1;
    li.pSetLayouts = &g_native.set;
    gfxvk::vkCreatePipelineLayout(R.device, &li, nullptr, &g_native.layout);
}

// the vertex streams the family reads: (semantic, location)
struct Stream { uint32_t semantic, location; };
const LatteParsedFetchShaderAttribute* attribute(const LatteFetchShader* fs, uint32_t semantic,
                                                  const LatteParsedFetchShaderBufferGroup** group) {
    for (const auto& g : fs->bufferGroups)
        for (int i = 0; i < g.attribCount; ++i)
            if (g.attrib[i].semanticId == semantic) { *group = &g; return &g.attrib[i]; }
    return nullptr;
}

// the native pipeline for this draw (family kind, streams, raster and depth state as the translated pipeline has them,
// the private pass's attachment formats); null with WHY set when it cannot be made
VkPipeline pipeline(const ProofDraw& d, Kind kind, const char** why) {
    using namespace gfxvk;
    const uint32_t* r = d.info.regs;
    ensure_layout();
    if (!g_native.layout) { *why = "native layout"; return VK_NULL_HANDLE; }
    const bool clampVariant = !R.enabledFeatures.depthClamp && (r[REGADDR::PA_CL_CLIP_CNTL] & (1u << 27));
    std::vector<Stream> streams{{0, 0}};
    if (kind == kSkin2 || kind == kSkin3) streams = {{0, 0}, {2, 1}, {4, 2}};
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    char key[512];
    int kn = std::snprintf(key, sizeof key, "%d:%d:", int(kind), int(clampVariant));
    for (const Stream& s : streams) {
        const LatteParsedFetchShaderBufferGroup* g = nullptr;
        const auto* a = attribute(d.info.fs, s.semantic, &g);
        if (!a) { *why = "stream missing"; return VK_NULL_HANDLE; }
        if (a->fetchType != LatteConst::VertexFetchType2::VERTEX_DATA) { *why = "instance stream"; return VK_NULL_HANDLE; }
        const VkFormat native = vk::vertex_format(a->format);
        if (R.vertexFormats.fallback(native)) { *why = "vertex format fallback"; return VK_NULL_HANDLE; }
        const uint32_t stride = (r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g->attributeBufferIndex * 7 + 2] >> 11) & 0xFFFF;
        if (std::none_of(bindings.begin(), bindings.end(), [&](const auto& b) { return b.binding == g->attributeBufferIndex; }))
            bindings.push_back({g->attributeBufferIndex, stride, VK_VERTEX_INPUT_RATE_VERTEX});
        attributes.push_back({s.location, g->attributeBufferIndex, native, a->offset});
        kn += std::snprintf(key + kn, sizeof key - kn, "%u/%u/%d/%u/%u,", s.location, g->attributeBufferIndex, int(native),
                            a->offset, stride);
    }
    LATTE_PA_SU_SC_MODE_CNTL pm;
    std::memcpy(&pm, r + REGADDR::PA_SU_SC_MODE_CNTL, 4);
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.depthClampEnable = (r[REGADDR::PA_CL_CLIP_CNTL] & (1u << 27)) && R.enabledFeatures.depthClamp;
    rs.lineWidth = 1;
    rs.cullMode = (pm.get_CULL_FRONT() ? VK_CULL_MODE_FRONT_BIT : 0) | (pm.get_CULL_BACK() ? VK_CULL_MODE_BACK_BIT : 0);
    rs.frontFace = pm.get_FRONT_FACE() == LATTE_PA_SU_SC_MODE_CNTL::E_FRONTFACE::CCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                                                                     : VK_FRONT_FACE_CLOCKWISE;
    rs.depthBiasEnable = pm.get_OFFSET_FRONT_ENABLED();
    rs.depthBiasConstantFactor = f32(r[REGADDR::PA_SU_POLY_OFFSET_FRONT_OFFSET]);
    rs.depthBiasSlopeFactor = f32(r[REGADDR::PA_SU_POLY_OFFSET_FRONT_SCALE]) / 16;
    rs.depthBiasClamp = rs.depthBiasEnable ? f32(r[REGADDR::PA_SU_POLY_OFFSET_CLAMP]) : 0.0f;
    std::array<VkFormat, 8> colorFormats{};
    uint32_t ncolor = 0;
    for (int i = 0; i < 8; ++i)
        if (d.colors[i]) { colorFormats[i] = d.colors[i]->fmt.pixel; ncolor = i + 1; }
    kn += std::snprintf(key + kn, sizeof key - kn, "|%08x|%08x|%08x|%08x|%08x|%d|%d", r[REGADDR::PA_SU_SC_MODE_CNTL],
                        r[REGADDR::PA_CL_CLIP_CNTL], r[REGADDR::PA_SU_POLY_OFFSET_FRONT_OFFSET],
                        r[REGADDR::PA_SU_POLY_OFFSET_FRONT_SCALE], r[REGADDR::PA_SU_POLY_OFFSET_CLAMP],
                        int(d.info.depth->fmt.pixel), int(d.info.depth->fmt.stencil));
    for (uint32_t i = 0; i < ncolor; ++i) kn += std::snprintf(key + kn, sizeof key - kn, ",%d", int(colorFormats[i]));
    if (auto it = g_native.pipelines.find(key); it != g_native.pipelines.end()) {
        if (!it->second) *why = "native pipeline failed";
        return it->second;
    }
    VkShaderModule vs = module(vertex_source(kind, clampVariant), true), fs = module(kFragment, false);
    VkPipeline pipe = VK_NULL_HANDLE;
    if (vs && fs) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (int i = 0; i < 2; ++i) {
            stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
            stages[i].module = i ? fs : vs;
            stages[i].pName = "main";
        }
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = uint32_t(bindings.size());
        vi.pVertexBindingDescriptions = bindings.data();
        vi.vertexAttributeDescriptionCount = uint32_t(attributes.size());
        vi.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
        std::array<VkPipelineColorBlendAttachmentState, 8> cb{};  // (colour writes off, as the draw's mask)
        VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        bs.attachmentCount = ncolor;
        bs.pAttachments = cb.data();
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dsi{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dsi.dynamicStateCount = 2;
        dsi.pDynamicStates = dyn;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = ncolor;
        rendering.pColorAttachmentFormats = colorFormats.data();
        rendering.depthAttachmentFormat = d.info.depth->fmt.pixel;
        rendering.stencilAttachmentFormat = d.info.depth->fmt.stencil ? d.info.depth->fmt.pixel : VK_FORMAT_UNDEFINED;
        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline_targets(ci, rendering);
        ci.stageCount = 2;
        ci.pStages = stages;
        ci.pVertexInputState = &vi;
        ci.pInputAssemblyState = &ia;
        ci.pViewportState = &vp;
        ci.pRasterizationState = &rs;
        ci.pMultisampleState = &ms;
        ci.pDepthStencilState = &ds;
        ci.pColorBlendState = &bs;
        ci.pDynamicState = &dsi;
        ci.layout = g_native.layout;
        if (vkCreateGraphicsPipelines(R.device, R.pipelineCache, 1, &ci, nullptr, &pipe) != VK_SUCCESS) pipe = VK_NULL_HANDLE;
    }
    if (vs) vkDestroyShaderModule(R.device, vs, nullptr);
    if (fs) vkDestroyShaderModule(R.device, fs, nullptr);
    LOG("[j3d-depth] native pipeline %s%s", key, pipe ? "" : ": FAILED");
    g_native.pipelines.emplace(key, pipe);
    if (!pipe) *why = "native pipeline failed";
    return pipe;
}

// a vertex uniform block's bytes as the draw read them (the submitted copy, else guest memory)
const uint8_t* block(const uint32_t* r, uint32_t loc, uint32_t* size) {
    const uint32_t addr = r[mmSQ_VTX_UNIFORM_BLOCK_START + loc * 7];
    *size = std::min<uint32_t>(r[mmSQ_VTX_UNIFORM_BLOCK_START + loc * 7 + 1] + 1, 0x10000);
    if (!addr) return nullptr;
    if (const uint8_t* sub = gx2::submitted_uniform_block(addr, *size)) return sub;
    return static_cast<const uint8_t*>(mem::ptr(addr));
}

}  // namespace

const char* family_of(const ProofDraw& d, std::string& name) {
    const auto* vs = d.info.vs;
    const auto* ps = d.info.ps;
    if (!vs || !vs->dec || !ps || !ps->dec) return "no programs";
    if (vs->source_text().empty()) return "no program text (WWHD_VK_SHADER_SOURCE_VERIFY=1)";
    if (vs->clipVarying || ps->clipVarying) return "depth clipping in shaders";
    auto [it, fresh] = g_byShader.try_emplace(vs->key);
    if (fresh) {
        it->second.vsHash = structure_hash(vs);
        for (const Family& f : families())
            if (f.vs == it->second.vsHash) it->second.family = &f;
    }
    const Classified& c = it->second;
    if (!c.family) {
        auto& u = g_unknown[c.vsHash];
        if (!u.first++) {
            std::string text;
            structure_hash(vs, &text);
            u.second = text;
            LOG("[j3d-depth] vertex structure %016llx not a family (pixel program %s)", (unsigned long long)c.vsHash,
                colour_only(ps) ? "colour only" : "with texture/discard");
        }
        char b[64];
        std::snprintf(b, sizeof b, "no family (%016llx)", (unsigned long long)c.vsHash);
        name = b;
        return nullptr;  // counted under its hash, drawn translated twice (harness check) or not at all
    }
    if (!colour_only(ps)) return "family vertex program with a texture-reading pixel program";
    name = c.family->name;
    return nullptr;
}

// whether the sample has a native program (else the proof records it but does not compare)
bool has_native(const std::string& name) {
    for (const Family& f : families()) if (name == f.name) return true;
    return false;
}

bool record_native(VkCommandBuffer cmd, const ProofDraw& d) {
    using namespace gfxvk;
    const Family* fam = nullptr;
    if (auto it = g_byShader.find(d.info.vs->key); it != g_byShader.end()) fam = it->second.family;
    if (!fam) return false;
    const char* why = nullptr;
    VkPipeline pipe = pipeline(d, fam->kind, &why);
    if (!pipe) return false;
    const uint32_t* r = d.info.regs;
    // view (location 2: rows 0-2, 27-30) and model (location 1: rows 0-2) / palette (location 1, whole)
    uint32_t viewSize = 0, modelSize = 0;
    const uint8_t* view = block(r, 2, &viewSize);
    const uint8_t* model = block(r, 1, &modelSize);
    if (!view || !model || viewSize < 31 * 16 || modelSize < 48) return false;
    UploadSlice vs = allocate_upload(7 * 16, 256);
    std::memcpy(vs.mapped, view, 3 * 16);
    std::memcpy(static_cast<uint8_t*>(vs.mapped) + 48, view + 27 * 16, 4 * 16);
    const uint32_t mbytes = fam->kind == kRigid ? 48 : modelSize;
    UploadSlice ms = allocate_upload(mbytes, 256);
    std::memcpy(ms.mapped, model, mbytes);
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = R.descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &g_native.set;
    if (vkAllocateDescriptorSets(R.device, &alloc, &set) != VK_SUCCESS) return false;
    VkDescriptorBufferInfo bi[2]{{vs.buffer, vs.offset, 7 * 16}, {ms.buffer, ms.offset, mbytes}};
    VkWriteDescriptorSet w[2]{{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    for (int i = 0; i < 2; ++i) {
        w[i].dstSet = set;
        w[i].dstBinding = uint32_t(i);
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[i].pBufferInfo = &bi[i];
    }
    vkUpdateDescriptorSets(R.device, 2, w, 0, nullptr);
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    record::vkCmdSetViewport(cmd, 0, 1, &d.viewport);
    record::vkCmdSetScissor(cmd, 0, 1, &d.scissor);
    record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_native.layout, 0, 1, &set, 0, nullptr);
    for (uint32_t i = 0; i < d.vertexBindCount; ++i)
        record::vkCmdBindVertexBuffers(cmd, d.vertexBinds[i].binding, 1, &d.vertexBinds[i].buffer, &d.vertexBinds[i].offset);
    record::vkCmdBindIndexBuffer(cmd, d.indexBuffer, d.indexOffset, d.indexType);
    record::vkCmdDrawIndexed(cmd, d.drawCount, 1, 0, int32_t(d.info.baseVertex), 0);
    return true;
}

// every 300 frames from the proof's report: the structures seen that are no family yet (first text written to DIR)
void report_unknown(const std::string& dir) {
    for (auto& [h, u] : g_unknown) {
        LOG("[j3d-depth] structure %016llx (no family): %llu samples", (unsigned long long)h, (unsigned long long)u.first);
        if (!dir.empty() && !u.second.empty()) {
            char path[512];
            std::snprintf(path, sizeof path, "%s/structure-%016llx.txt", dir.c_str(), (unsigned long long)h);
            if (FILE* f = std::fopen(path, "w")) { std::fputs(u.second.c_str(), f); std::fclose(f); }
            u.second.clear();
        }
    }
}

}  // namespace native_draw::j3d_depth
