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
#include "native_draw/j3d_depth_family.h"
#include "runtime.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Latte;

namespace native_draw::j3d_depth {
namespace fam {
using gfxvk::R;
float f32(uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; }

// ---------------------------------------------------------------- the sampler LOD-bias term (WWHD_NATIVE_J3D_DEPTH_BIASFREE=1)
// A device without sampler LOD bias (MoltenVK) gets the bias in the program text (LatteDecompilerEmitGLSL.cpp,
// uf_tex<N>LodBias); one with it (Adreno) applies it in the sampler. The structure is the same program either way:
// with the switch the hash reads the text as the device with sampler bias gets it, so one proof list covers both.
// The emitted forms (no spaces) and what they are without the bias:
//   texture(...,bias) / textureOffset(...,ivec2(..),bias)        pixel SAMPLE      -> the call without ",bias"
//   ,(lod+bias)                                                     SAMPLE_L / _LB    -> ,lod
//   textureLod(...,bias) / textureLodOffset(...,bias,ivec2(..))     SAMPLE_LZ (pixel) -> 0.0 in its place
//   gradH.xy*exp2(bias),gradV.xy*exp2(bias)                         SAMPLE_G          -> gradH.xy,gradV.xy
// A vertex program's textureLod at the bias is SAMPLE (texture() without it) or SAMPLE_LZ: not told apart, left as it
// is (that program keeps its device's hash). Returns false when a bias term stays.
bool bias_free(std::string& t, bool pixel) {
    if (t.find("LodBias") == std::string::npos) return true;
    auto is_bias = [&](size_t k, size_t* end) {  // "uf_tex<N>LodBias" at k
        if (t.compare(k, 6, "uf_tex") != 0) return false;
        size_t j = k + 6;
        while (j < t.size() && std::isdigit(uint8_t(t[j]))) ++j;
        if (j == k + 6 || t.compare(j, 7, "LodBias") != 0) return false;
        *end = j + 7;
        return true;
    };
    // the open parenthesis matching the one before position k (k: just past an argument), or npos
    auto open_of = [&](size_t k) {
        int depth = 0;
        for (size_t i = k; i-- > 0;) {
            if (t[i] == ')') ++depth;
            else if (t[i] == '(') { if (depth == 0) return i; --depth; }
            else if (t[i] == ';' || t[i] == '{' || t[i] == '}') return std::string::npos;
        }
        return std::string::npos;
    };
    auto callee = [&](size_t open) {
        size_t b = open;
        while (b > 0 && (std::isalnum(uint8_t(t[b - 1])) || t[b - 1] == '_')) --b;
        return t.substr(b, open - b);
    };
    bool clean = true;
    std::string out;
    out.reserve(t.size());
    size_t i = 0, e = 0;
    // pass 1: the gradient and (lod+bias) forms
    while (i < t.size()) {
        if (t.compare(i, 13, "gradH.xy*exp2") == 0) {
            const size_t a = i + 14;  // past "gradH.xy*exp2("
            size_t a2 = 0, b2 = 0;
            if (t[i + 13] == '(' && is_bias(a, &a2) && t.compare(a2, 1, ")") == 0 &&
                t.compare(a2 + 1, 15, ",gradV.xy*exp2(") == 0 && is_bias(a2 + 16, &b2) && t.compare(b2, 1, ")") == 0) {
                out += "gradH.xy,gradV.xy";
                i = b2 + 1;
                continue;
            }
        }
        if (t[i] == '+' && is_bias(i + 1, &e) && e < t.size() && t[e] == ')') {
            // ",(" lod "+bias)": drop the "(" written before the lod in OUT and the "+bias)"
            int depth = 0;
            size_t o = std::string::npos;
            for (size_t k = out.size(); k-- > 0;) {
                if (out[k] == ')') ++depth;
                else if (out[k] == '(') { if (depth == 0) { o = k; break; } --depth; }
            }
            if (o != std::string::npos && o > 0 && out[o - 1] == ',') {
                out.erase(o, 1);
                i = e + 1;
                continue;
            }
        }
        out += t[i++];
    }
    t.swap(out);
    out.clear();
    // pass 2: the bias as an argument of its own
    i = 0;
    while (i < t.size()) {
        if (t[i] == ',' && is_bias(i + 1, &e) && e < t.size() && (t[e] == ')' || t[e] == ',')) {
            std::string f;
            if (const size_t o = open_of(i); o != std::string::npos) f = callee(o);
            if ((f == "texture" || f == "textureOffset") && t[e] == ')') {
                i = e;  // drop ",bias"
                continue;
            }
            if ((f == "textureLod" || f == "textureLodOffset") && pixel) {
                out += ",0.0";
                i = e;
                continue;
            }
            clean = false;
        }
        out += t[i++];
    }
    t.swap(out);
    return clean && t.find("LodBias") == std::string::npos;
}
bool biasfree_on() {
    static const bool on = [] {
        const char* e = std::getenv("WWHD_NATIVE_J3D_DEPTH_BIASFREE");
        const bool v = e && *e ? std::atoi(e) != 0 : true;  // default on (=0: off)
        if (v) LOG("[j3d-depth] structure hashes without the sampler LOD-bias term (WWHD_NATIVE_J3D_DEPTH_BIASFREE=1)");
        return v;
    }();
    return on;
}


// ---------------------------------------------------------------- structure hash
// The program's main body as tokens: comments dropped, R<n>f/R<n>i renumbered by first appearance, uf_remapped<S>[i]
// named U<location>_<vec4> (its remap table), everything else (operators, constants, block reads) kept; FNV-1a over the
// tokens joined by single spaces (the same hash can be computed offline from a WWHD_J3D_DEPTH_DUMP program dump).
uint64_t structure_hash(const gfxvk::vk::Shader* s, std::string* textOut = nullptr) {
    const std::string& text = s->source_text();
    size_t i = text.find("void main()");
    if (i == std::string::npos) return 0;
    std::string freeText;  // (WWHD_NATIVE_J3D_DEPTH_BIASFREE=1) the main body as a device with sampler LOD bias gets it
    if (biasfree_on()) {
        freeText = text.substr(i);
        const bool clean = bias_free(freeText, !s->vertex);
        if (!clean) LOG("[j3d-depth] %s program %016llx: a LOD-bias term stays in its structure hash", s->vertex ? "vertex" : "pixel",
                        (unsigned long long)s->key);
        i = 0;
    }
    const std::string& t = biasfree_on() ? freeText : text;
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

Native g_native;

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

// the raster, depth and attachment state of a proof pipeline: as the translated pipeline has them (cull, front face,
// polygon offset, depth clamp from the draw's registers; depth LESS + write; the private pass's attachment formats,
// colour writes off); appended to KEY
// what a depth pipeline is made for: the draw's registers (raster state), its fetch layout, the pass's attachment formats
PipeIn pipe_in(const ProofDraw& d) {
    PipeIn p;
    p.regs = d.info.regs;
    p.fs = d.info.fs;
    for (int i = 0; i < 8; ++i)
        if (d.colors[i]) { p.colors[i] = d.colors[i]->fmt.pixel; p.ncolor = i + 1; }
    p.depth = d.info.depth->fmt.pixel;
    p.stencil = d.info.depth->fmt.stencil;
    return p;
}
struct Fixed {
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    std::array<VkFormat, 8> colorFormats{};
    uint32_t ncolor = 0;
};
Fixed fixed_state(const PipeIn& d, std::string& key) {
    using namespace gfxvk;
    const uint32_t* r = d.regs;
    Fixed f;
    LATTE_PA_SU_SC_MODE_CNTL pm;
    std::memcpy(&pm, r + REGADDR::PA_SU_SC_MODE_CNTL, 4);
    auto& rs = f.rs;
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
    f.colorFormats = d.colors;
    f.ncolor = d.ncolor;
    char b[160];
    std::snprintf(b, sizeof b, "|%08x|%08x|%08x|%08x|%08x|%d|%d", r[REGADDR::PA_SU_SC_MODE_CNTL], r[REGADDR::PA_CL_CLIP_CNTL],
                  r[REGADDR::PA_SU_POLY_OFFSET_FRONT_OFFSET], r[REGADDR::PA_SU_POLY_OFFSET_FRONT_SCALE],
                  r[REGADDR::PA_SU_POLY_OFFSET_CLAMP], int(d.depth), int(d.stencil));
    key += b;
    for (uint32_t i = 0; i < f.ncolor; ++i) key += "," + std::to_string(int(f.colorFormats[i]));
    return f;
}

std::map<std::string, VkPipeline> g_pipelines;  // every proof pipeline, by its key
VkPipeline create_pipeline(const PipeIn& d, VkPipelineLayout layout, const std::vector<uint32_t>& vsWords,
                           const std::vector<uint32_t>& psWords, const std::vector<VkVertexInputBindingDescription>& bindings,
                           const std::vector<VkVertexInputAttributeDescription>& attributes, std::string key,
                           const char** why) {
    using namespace gfxvk;
    const Fixed f = fixed_state(d, key);
    if (auto it = g_pipelines.find(key); it != g_pipelines.end()) {
        if (!it->second) *why = "proof pipeline failed";
        return it->second;
    }
    auto module = [](const std::vector<uint32_t>& w) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = w.size() * 4;
        ci.pCode = w.data();
        VkShaderModule m = VK_NULL_HANDLE;
        if (w.empty() || vkCreateShaderModule(R.device, &ci, nullptr, &m) != VK_SUCCESS) return VkShaderModule(VK_NULL_HANDLE);
        return m;
    };
    VkShaderModule vs = module(vsWords), fs = module(psWords);
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
        bs.attachmentCount = f.ncolor;
        bs.pAttachments = cb.data();
        // the dynamic state every J3D pipeline has (the material path keeps it bound across its draws)
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
                                VK_DYNAMIC_STATE_STENCIL_REFERENCE};
        VkPipelineDynamicStateCreateInfo dsi{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dsi.dynamicStateCount = 4;
        dsi.pDynamicStates = dyn;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = f.ncolor;
        rendering.pColorAttachmentFormats = f.colorFormats.data();
        rendering.depthAttachmentFormat = d.depth;
        rendering.stencilAttachmentFormat = d.stencil ? d.depth : VK_FORMAT_UNDEFINED;
        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline_targets(ci, rendering);
        ci.stageCount = 2;
        ci.pStages = stages;
        ci.pVertexInputState = &vi;
        ci.pInputAssemblyState = &ia;
        ci.pViewportState = &vp;
        ci.pRasterizationState = &f.rs;
        ci.pMultisampleState = &ms;
        ci.pDepthStencilState = &ds;
        ci.pColorBlendState = &bs;
        ci.pDynamicState = &dsi;
        ci.layout = layout;
        if (vkCreateGraphicsPipelines(R.device, R.pipelineCache, 1, &ci, nullptr, &pipe) != VK_SUCCESS) pipe = VK_NULL_HANDLE;
    }
    if (vs) vkDestroyShaderModule(R.device, vs, nullptr);
    if (fs) vkDestroyShaderModule(R.device, fs, nullptr);
    LOG("[j3d-depth] proof pipeline %s%s", key.c_str(), pipe ? "" : ": FAILED");
    g_pipelines.emplace(key, pipe);
    if (!pipe) *why = "proof pipeline failed";
    return pipe;
}

void set_dynamic(VkCommandBuffer cmd, const ProofDraw& d) {
    using namespace gfxvk;
    const uint32_t* r = d.info.regs;
    record::vkCmdSetViewport(cmd, 0, 1, &d.viewport);
    record::vkCmdSetScissor(cmd, 0, 1, &d.scissor);
    record::vkCmdSetBlendConstants(cmd, reinterpret_cast<const float*>(r + REGADDR::CB_BLEND_RED));
    record::vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_BIT, r[REGADDR::DB_STENCILREFMASK] & 255);
    record::vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_BACK_BIT, r[REGADDR::DB_STENCILREFMASK_BF] & 255);
}

std::vector<uint32_t> words(const std::string& src, bool vertex, const char* what) {
    std::string error;
    auto w = gfxvk::vk::compile_glsl(src, vertex, &error);
    if (w.empty()) LOG("[j3d-depth] %s %s program did not compile: %s", what, vertex ? "vertex" : "pixel", error.c_str());
    return w;
}

// the hand-written families' pipeline (their streams at locations 0-2)
VkPipeline pipeline(const PipeIn& d, Kind kind, const char** why, VkPipelineLayout dynLayout = VK_NULL_HANDLE,
                    const std::vector<uint32_t>* vsOverride = nullptr, const char* tag = "") {
    using namespace gfxvk;
    const uint32_t* r = d.regs;
    ensure_layout();
    if (!g_native.layout) { *why = "native layout"; return VK_NULL_HANDLE; }
    const bool clampVariant = !R.enabledFeatures.depthClamp && (r[REGADDR::PA_CL_CLIP_CNTL] & (1u << 27));
    std::vector<Stream> streams{{0, 0}};
    if (kind == kSkin2 || kind == kSkin3) streams = {{0, 0}, {2, 1}, {4, 2}};
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    std::string key = "hand:" + std::to_string(int(kind)) + ":" + std::to_string(int(clampVariant)) + ":";
    for (const Stream& s : streams) {
        const LatteParsedFetchShaderBufferGroup* g = nullptr;
        const auto* a = attribute(d.fs, s.semantic, &g);
        if (!a) { *why = "stream missing"; return VK_NULL_HANDLE; }
        if (a->fetchType != LatteConst::VertexFetchType2::VERTEX_DATA) { *why = "instance stream"; return VK_NULL_HANDLE; }
        const VkFormat native = vk::vertex_format(a->format);
        if (R.vertexFormats.fallback(native)) { *why = "vertex format fallback"; return VK_NULL_HANDLE; }
        const uint32_t stride = (r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g->attributeBufferIndex * 7 + 2] >> 11) & 0xFFFF;
        if (std::none_of(bindings.begin(), bindings.end(), [&](const auto& b) { return b.binding == g->attributeBufferIndex; }))
            bindings.push_back({g->attributeBufferIndex, stride, VK_VERTEX_INPUT_RATE_VERTEX});
        attributes.push_back({s.location, g->attributeBufferIndex, native, a->offset});
        key += std::to_string(s.location) + "/" + std::to_string(g->attributeBufferIndex) + "/" + std::to_string(int(native)) +
               "/" + std::to_string(a->offset) + "/" + std::to_string(stride) + ",";
    }
    struct Words { std::vector<uint32_t> vs, ps; };
    static std::map<int, Words> compiled;  // (kind, clamp variant)
    Words& w = compiled[int(kind) * 2 + int(clampVariant)];
    if (w.vs.empty()) { w.vs = words(vertex_source(kind, clampVariant), true, "native"); w.ps = words(kFragment, false, "native"); }
    if (dynLayout)
        return create_pipeline(d, dynLayout, vsOverride ? *vsOverride : w.vs, w.ps, bindings, attributes, key + "dyn:" + tag, why);
    return create_pipeline(d, g_native.layout, w.vs, w.ps, bindings, attributes, key, why);
}

// every attribute the program reads, at the locations of MAPPING (as the translated pipeline fetches them)
bool all_streams(const PipeIn& d, const int8_t* mapping, std::vector<VkVertexInputBindingDescription>& bindings,
                 std::vector<VkVertexInputAttributeDescription>& attributes, std::string& key, const char** why) {
    using namespace gfxvk;
    const uint32_t* r = d.regs;
    if (R.vertexDiagnostics.cpuClasses) { *why = "CPU-converted vertex classes"; return false; }
    for (const auto& g : d.fs->bufferGroups) {
        bool used = false;
        for (int i = 0; i < g.attribCount; ++i) {
            const auto& a = g.attrib[i];
            const int loc = mapping[a.semanticId];
            if (loc < 0) continue;
            if (a.fetchType != LatteConst::VertexFetchType2::VERTEX_DATA) { *why = "instance stream"; return false; }
            const VkFormat native = vk::vertex_format(a.format);
            if (native == VK_FORMAT_UNDEFINED || R.vertexFormats.fallback(native)) { *why = "vertex format"; return false; }
            attributes.push_back({uint32_t(loc), g.attributeBufferIndex, native, a.offset});
            key += std::to_string(loc) + "/" + std::to_string(int(native)) + "/" + std::to_string(a.offset) + ",";
            used = true;
        }
        if (!used) continue;
        const uint32_t stride = (r[mmSQ_VTX_ATTRIBUTE_BLOCK_START + g.attributeBufferIndex * 7 + 2] >> 11) & 0xFFFF;
        bindings.push_back({g.attributeBufferIndex, stride, VK_VERTEX_INPUT_RATE_VERTEX});
        key += "b" + std::to_string(g.attributeBufferIndex) + "/" + std::to_string(stride) + ";";
    }
    return true;
}

// ---------------------------------------------------------------- LOD 0 (the tolerance rule's proof)
// every implicit-LOD sample of a pixel texture unit (texture(unit, coord[, bias])) read at LOD 0 instead:
// textureLod(unit, coord, 0.0). False when the program has another implicit-LOD lookup (offset, projection, gather).
bool lod0(std::string& t) {
    if (t.find("textureOffset(") != std::string::npos || t.find("textureProj(") != std::string::npos ||
        t.find("textureGather") != std::string::npos)
        return false;
    std::string out;
    size_t i = 0;
    for (;;) {
        size_t k = t.find("texture(", i);
        if (k == std::string::npos) break;
        if (k && (std::isalnum(uint8_t(t[k - 1])) || t[k - 1] == '_')) { out += t.substr(i, k + 8 - i); i = k + 8; continue; }
        // split the arguments at top-level commas
        size_t j = k + 8;
        int depth = 0;
        std::vector<std::string> args(1);
        for (; j < t.size(); ++j) {
            const char c = t[j];
            if (c == '(') ++depth;
            if (c == ')') { if (!depth) break; --depth; }
            if (c == ',' && !depth) { args.emplace_back(); continue; }
            args.back() += c;
        }
        if (j >= t.size() || args.size() < 2 || args[0].find("textureUnitPS") == std::string::npos) return false;
        out += t.substr(i, k - i) + "textureLod(" + args[0] + "," + args[1] + ", 0.0)";
        i = j + 1;
    }
    out += t.substr(i);
    t = out;
    return true;
}

// ---------------------------------------------------------------- generated family programs
// A family without a hand-written program gets one made from its first member's translated programs: the
// arithmetic is that text unchanged (it is the specification), the interface is replaced: every remapped uniform
// read becomes a read of the block it names (own uniform buffers per block location, the whole guest block), the
// buffer-mode blocks and the textures move to the native set, the support scalars (alpha test reference, the
// emulated sampler LOD bias, texture scales) to one material block. Nothing per program remains in the interface.
std::map<std::pair<uint64_t, uint64_t>, Generated> g_generated;


bool rewrite_stage(const gfxvk::vk::Shader* s, std::string& t, std::vector<uint32_t>& blocks, std::vector<uint32_t>& units,
                   bool& support, std::string& why) {
    const bool vertex = s->vertex;
    const char* S = vertex ? "VS" : "PS";
    const uint32_t base = vertex ? 0 : kPsBlockBase;
    t = s->source_text();
    // the translation's support block (the VULKAN branch's declaration)
    if (size_t k = t.find("uniform ufBlock"); k != std::string::npos) {
        const size_t from = t.rfind('\n', k) + 1, to = t.find("};", k);
        if (to == std::string::npos) { why = "support block"; return false; }
        t.erase(from, to + 2 - from);
    }
    // buffer-mode blocks: to the native set
    std::vector<uint32_t> buffers;
    for (size_t k = 0; (k = t.find("UNIFORM_BUFFER_LAYOUT(", k)) != std::string::npos;) {
        const size_t close = t.find(')', k);
        const std::string name = std::string("uniformBlock") + S;
        const size_t nm = t.find(name, close);
        if (close == std::string::npos || nm == std::string::npos || nm > close + 32) { k = close; continue; }
        const uint32_t loc = uint32_t(std::atoi(t.c_str() + nm + name.size()));
        const std::string repl = "layout(set = 0, binding = " + std::to_string(base + loc) + ", std140)";
        t.replace(k, close + 1 - k, repl);
        buffers.push_back(loc);
        blocks.push_back(loc);
        k += repl.size();
    }
    // remapped reads -> the block they name
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> remap;
    for (const auto& g : s->dec->list_remappedUniformEntries_bufferGroups)
        for (const auto& e : g.entries) remap[e.mappedIndexOffset / 16] = {g.kcacheBankIdOffset / 28u, e.indexOffset / 16u};
    std::map<uint32_t, uint32_t> remapped;  // location -> vec4 count
    const std::string arr = std::string("uf_remapped") + S + "[";
    for (size_t k = 0; (k = t.find(arr, k)) != std::string::npos;) {
        const size_t close = t.find(']', k);
        const uint32_t idx = uint32_t(std::atoi(t.c_str() + k + arr.size()));
        auto it = remap.find(idx);
        std::string repl;
        if (it == remap.end()) repl = "ivec4(0)";  // (the inactive branch's declaration: never compiled)
        else {
            const auto [loc, vec] = it->second;
            if (std::find(buffers.begin(), buffers.end(), loc) != buffers.end())
                repl = "floatBitsToInt(uf_block" + std::string(S) + std::to_string(loc) + "[" + std::to_string(vec) + "])";
            else {
                repl = "fam_" + std::string(vertex ? "v" : "p") + std::to_string(loc) + "[" + std::to_string(vec) + "]";
                remapped[loc] = std::max(remapped[loc], vec + 1);
            }
        }
        t.replace(k, close + 1 - k, repl);
        k += repl.size();
    }
    // support scalars
    auto replace_all = [&](const std::string& from, const std::string& to) {
        for (size_t k = 0; (k = t.find(from, k)) != std::string::npos; k += to.size()) { t.replace(k, from.size(), to); support = true; }
    };
    replace_all("uf_alphaTestRef", "fam_s.x");
    for (int u = 15; u >= 0; --u) {
        replace_all("uf_tex" + std::to_string(u) + "LodBias", "fam_lod[" + std::to_string(u) + "].x");
        replace_all("uf_tex" + std::to_string(u) + "Scale", "fam_tsc[" + std::to_string(u) + "].xy");
    }
    // textures: to the native set
    for (size_t k = 0; (k = t.find("TEXTURE_LAYOUT(", k)) != std::string::npos;) {
        const size_t close = t.find(')', k), semi = t.find(';', close);
        const std::string unit = std::string("textureUnit") + S;
        const size_t nm = t.find(unit, close);
        if (close == std::string::npos || semi == std::string::npos || nm == std::string::npos || nm > semi) { k = close; continue; }
        if (vertex) { why = "vertex textures"; return false; }
        const uint32_t u = uint32_t(std::atoi(t.c_str() + nm + unit.size()));
        const std::string repl = "layout(set = 0, binding = " + std::to_string(kTextureBase + u) + ")";
        t.replace(k, close + 1 - k, repl);
        units.push_back(u);
        k += repl.size();
    }
    // anything else of the translation's interface: not supported
    for (size_t k = 0; (k = t.find("uf_", k)) != std::string::npos; ++k)
        if (t.compare(k, 8, "uf_block") != 0) { why = "support uniform " + t.substr(k, 24); return false; }
    std::string decl;
    for (const auto& [loc, n] : remapped) {
        decl += "layout(set = 0, binding = " + std::to_string(base + loc) + ", std140) uniform Fam" + (vertex ? "V" : "P") +
                std::to_string(loc) + " { ivec4 fam_" + (vertex ? "v" : "p") + std::to_string(loc) + "[" + std::to_string(n) + "]; };\n";
        blocks.push_back(loc);
    }
    if (support && !vertex)
        decl += "layout(set = 0, binding = " + std::to_string(kSupportBinding) +
                ", std140) uniform FamS { vec4 fam_s; vec4 fam_lod[16]; vec4 fam_tsc[16]; };\n";
    const size_t main = t.find("void main()");
    if (main == std::string::npos) { why = "no main"; return false; }
    t.insert(main, decl);
    return true;
}

Generated& generated(const gfxvk::vk::Shader* vs, const gfxvk::vk::Shader* ps, uint64_t vsHash, uint64_t psHash) {
    using namespace gfxvk;
    auto [it, fresh] = g_generated.try_emplace({vsHash, psHash});
    Generated& g = it->second;
    if (!fresh) return g;
    if (ps->gatherUnits) { g.why = "gathering pixel program"; return g; }
    std::string vt, pt, why;
    std::vector<uint32_t> none;
    bool vsupport = false;
    if (!rewrite_stage(vs, vt, g.vsBlocks, none, vsupport, why) || !rewrite_stage(ps, pt, g.psBlocks, g.units, g.support, why)) {
        g.why = why;
        return g;
    }
    if (vsupport) { g.why = "vertex support uniforms"; return g; }
    std::memcpy(g.mapping, vs->mapping.attributeMapping, sizeof g.mapping);
    g.vs = words(vt, true, "generated");
    g.vsText = vt;
    g.ps = words(pt, false, "generated");
    std::string pl = pt;
    if (lod0(pl)) g.psLod0 = words(pl, false, "generated LOD 0");
    if (g.vs.empty() || g.ps.empty()) { g.why = "generated program did not compile"; return g; }
    std::vector<VkDescriptorSetLayoutBinding> b;
    for (uint32_t loc : g.vsBlocks) b.push_back({loc, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr});
    for (uint32_t loc : g.psBlocks)
        b.push_back({kPsBlockBase + loc, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    if (g.support) b.push_back({kSupportBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    for (uint32_t u : g.units)
        b.push_back({kTextureBase + u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    g.bindings = b;
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = uint32_t(b.size());
    ci.pBindings = b.data();
    vkCreateDescriptorSetLayout(R.device, &ci, nullptr, &g.set);
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1;
    li.pSetLayouts = &g.set;
    vkCreatePipelineLayout(R.device, &li, nullptr, &g.layout);
    if (!g.layout) g.why = "generated layout";
    LOG("[j3d-depth] generated family %016llx/%016llx: %zu vertex blocks, %zu pixel blocks, %zu textures%s%s",
        (unsigned long long)vsHash, (unsigned long long)psHash, g.vsBlocks.size(), g.psBlocks.size(), g.units.size(),
        g.support ? ", support scalars" : "", g.psLod0.empty() ? ", no LOD-0 variant" : "");
    return g;
}

// a uniform block's bytes as the draw read them (the submitted copy, else guest memory)
const uint8_t* block(const uint32_t* r, uint32_t loc, uint32_t* size, bool vertex = true) {
    const uint32_t start = vertex ? mmSQ_VTX_UNIFORM_BLOCK_START : mmSQ_PS_UNIFORM_BLOCK_START;
    const uint32_t addr = r[start + loc * 7];
    *size = std::min<uint32_t>(r[start + loc * 7 + 1] + 1, 0x10000);
    if (!addr) return nullptr;
    if (const uint8_t* sub = gx2::submitted_uniform_block(addr, *size)) return sub;
    return static_cast<const uint8_t*>(mem::ptr(addr));
}

bool generate_on() {
    static const bool on = [] { const char* e = std::getenv("WWHD_J3D_DEPTH_GENERATE"); return e && std::atoi(e); }();
    return on;
}
const char* intern(const std::string& s) {
    static std::set<std::string> strings;
    return strings.insert(s).first->c_str();
}
std::unordered_map<uint64_t, uint64_t> g_psHash;  // pixel shader key -> its structure hash
struct Sample { const Family* hand = nullptr; Generated* gen = nullptr; uint64_t vsHash = 0, psHash = 0; };
Sample g_sample;  // the sample family_of classified last (proof() records it right after)

}  // namespace fam
using namespace fam;

std::string program_name();  // j3d_depth.cpp: the draw's program record name

const char* family_of(const ProofDraw& d, std::string& name) {
    g_sample = {};
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
    g_sample.vsHash = c.vsHash;
    if (c.family && colour_only(ps)) {
        g_sample.hand = c.family;
        name = c.family->name;
        return nullptr;
    }
    auto [pit, pfresh] = g_psHash.try_emplace(ps->key);
    if (pfresh) pit->second = structure_hash(ps);
    g_sample.psHash = pit->second;
    char b[96];
    if (generate_on()) {
        Generated& g = generated(vs, ps, c.vsHash, pit->second);
        if (!g.why.empty()) return intern("generated program: " + g.why);
        g_sample.gen = &g;
        static std::map<std::pair<uint64_t, uint64_t>, std::string> names;  // (the first member's name)
        auto& n = names[{c.vsHash, pit->second}];
        if (n.empty()) {
            std::snprintf(b, sizeof b, "gen %016llx/%016llx ", (unsigned long long)c.vsHash, (unsigned long long)pit->second);
            n = b + program_name();
        }
        name = n;
        return nullptr;
    }
    auto& u = g_unknown[c.vsHash];
    if (!u.first++) {
        std::string text;
        structure_hash(vs, &text);
        u.second = text;
    }
    std::snprintf(b, sizeof b, "no family (%016llx)", (unsigned long long)c.vsHash);
    name = b;
    return nullptr;  // counted under its hash, drawn translated twice (harness check) or not at all
}

// whether the sample family_of classified last has a native program (else the proof counts it but does not compare)
bool has_native(const std::string&) { return g_sample.hand || g_sample.gen; }
bool samples_texture(const ProofDraw& d) { return d.info.ps && d.info.ps->dec && d.info.ps->dec->textureUnitListCount; }

// the draw's translated programs with the pixel program's texture reads at LOD 0 (the tolerance rule's proof), on
// the draw's own pipeline layout, sets and binds
bool record_translated_lod0(VkCommandBuffer cmd, const ProofDraw& d) {
    using namespace gfxvk;
    struct Words { std::vector<uint32_t> w; bool tried = false; };
    static std::unordered_map<uint64_t, Words> vsw, psw;
    Words& v = vsw[d.info.vs->key];
    if (!v.tried) { v.tried = true; v.w = words(d.info.vs->source_text(), true, "translated"); }
    Words& p = psw[d.info.ps->key];
    if (!p.tried) {
        p.tried = true;
        std::string t = d.info.ps->source_text();
        if (lod0(t)) p.w = words(t, false, "translated LOD 0");
    }
    if (v.w.empty() || p.w.empty()) return false;
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    char k[64];
    std::snprintf(k, sizeof k, "tl0:%016llx:%016llx:", (unsigned long long)d.info.vs->key, (unsigned long long)d.info.ps->key);
    std::string key = k;
    const char* why = nullptr;
    const PipeIn in = pipe_in(d);
    if (!all_streams(in, d.info.vs->mapping.attributeMapping, bindings, attributes, key, &why)) return false;
    key += ":" + std::to_string(uint64_t(d.layout));
    VkPipeline pipe = create_pipeline(in, d.layout, v.w, p.w, bindings, attributes, key, &why);
    if (!pipe) return false;
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    set_dynamic(cmd, d);
    record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d.layout, 0, 2, d.sets, d.offsetCount, d.offsets);
    for (uint32_t i = 0; i < d.vertexBindCount; ++i)
        record::vkCmdBindVertexBuffers(cmd, d.vertexBinds[i].binding, 1, &d.vertexBinds[i].buffer, &d.vertexBinds[i].offset);
    record::vkCmdBindIndexBuffer(cmd, d.indexBuffer, d.indexOffset, d.indexType);
    record::vkCmdDrawIndexed(cmd, d.drawCount, 1, 0, int32_t(d.info.baseVertex), 0);
    return true;
}

namespace fam {
void bind_and_draw(VkCommandBuffer cmd, const ProofDraw& d, VkPipeline pipe, VkPipelineLayout layout, VkDescriptorSet set) {
    using namespace gfxvk;
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    set_dynamic(cmd, d);
    record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
    for (uint32_t i = 0; i < d.vertexBindCount; ++i)
        record::vkCmdBindVertexBuffers(cmd, d.vertexBinds[i].binding, 1, &d.vertexBinds[i].buffer, &d.vertexBinds[i].offset);
    record::vkCmdBindIndexBuffer(cmd, d.indexBuffer, d.indexOffset, d.indexType);
    record::vkCmdDrawIndexed(cmd, d.drawCount, 1, 0, int32_t(d.info.baseVertex), 0);
}
VkDescriptorSet allocate(VkDescriptorSetLayout layout) {
    using namespace gfxvk;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = R.descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layout;
    return vkAllocateDescriptorSets(R.device, &alloc, &set) == VK_SUCCESS ? set : VK_NULL_HANDLE;
}

// a native set's inputs: the draw's registers, its pixel program (support offsets, texture bindings), the pixel
// program's support bytes and a lookup of the image write at a pixel binding
struct SetIn {
    const uint32_t* regs = nullptr;
    const gfxvk::vk::Shader* ps = nullptr;
    const uint8_t* psSupport = nullptr;
    uint32_t psSupportSize = 0;
    std::function<const VkDescriptorImageInfo*(int binding)> image;
};
// a generated family's set: whole guest blocks per location, the support scalars, the draw's image views and samplers
VkDescriptorSet generated_set(Generated& g, const SetIn& in) {
    using namespace gfxvk;
    const uint32_t* r = in.regs;
    const auto* ps = in.ps;
    VkDescriptorSet set = allocate(g.set);
    if (!set) return VK_NULL_HANDLE;
    std::vector<VkWriteDescriptorSet> w;
    std::vector<VkDescriptorBufferInfo> bi;
    std::vector<VkDescriptorImageInfo> ii;
    bi.reserve(40);
    ii.reserve(16);
    const VkDeviceSize align = std::max<VkDeviceSize>(256, R.properties.limits.minUniformBufferOffsetAlignment);
    auto ubo = [&](uint32_t binding, const void* data, uint32_t size) {
        size = std::min<uint32_t>(size, R.properties.limits.maxUniformBufferRange);
        UploadSlice sl = allocate_upload(size, align);
        std::memcpy(sl.mapped, data, size);
        bi.push_back({sl.buffer, sl.offset, size});
        VkWriteDescriptorSet x{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        x.dstSet = set; x.dstBinding = binding; x.descriptorCount = 1;
        x.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; x.pBufferInfo = &bi.back();
        w.push_back(x);
    };
    for (uint32_t loc : g.vsBlocks) {
        uint32_t size = 0;
        const uint8_t* b = block(r, loc, &size, true);
        if (!b) return VK_NULL_HANDLE;
        ubo(loc, b, size);
    }
    for (uint32_t loc : g.psBlocks) {
        uint32_t size = 0;
        const uint8_t* b = block(r, loc, &size, false);
        if (!b) return VK_NULL_HANDLE;
        ubo(kPsBlockBase + loc, b, size);
    }
    if (g.support) {
        float sv[33 * 4]{};
        const uint8_t* sup = in.psSupport;
        const uint32_t n = in.psSupportSize;
        if (!sup) return VK_NULL_HANDLE;
        auto get = [&](int32_t off, float* dst, int count) {
            if (off >= 0 && uint32_t(off) + 4u * count <= n) std::memcpy(dst, sup + off, 4u * count);
        };
        get(ps->uniforms.offset_alphaTestRef, &sv[0], 1);
        for (int u = 0; u < 16; ++u) {
            get(ps->uniforms.offset_texLodBias[u], &sv[4 + u * 4], 1);
            get(ps->uniforms.offset_texScale[u], &sv[4 + 64 + u * 4], 2);
        }
        ubo(kSupportBinding, sv, sizeof sv);
    }
    for (uint32_t u : g.units) {
        const VkDescriptorImageInfo* img = in.image ? in.image(ps->mapping.textureUnitToBindingPoint[u]) : nullptr;
        if (!img) return VK_NULL_HANDLE;
        ii.push_back(*img);
        VkWriteDescriptorSet x{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        x.dstSet = set; x.dstBinding = kTextureBase + u; x.descriptorCount = 1;
        x.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; x.pImageInfo = &ii.back();
        w.push_back(x);
    }
    vkUpdateDescriptorSets(R.device, uint32_t(w.size()), w.data(), 0, nullptr);
    return set;
}
// a hand-written family's set: view (location 2: rows 0-2, 27-30), model (location 1: rows 0-2) or palette (whole)
VkDescriptorSet hand_set(Kind kind, const uint32_t* r) {
    using namespace gfxvk;
    uint32_t viewSize = 0, modelSize = 0;
    const uint8_t* view = block(r, 2, &viewSize);
    const uint8_t* model = block(r, 1, &modelSize);
    if (!view || !model || viewSize < 31 * 16 || modelSize < 48) return VK_NULL_HANDLE;
    UploadSlice vs = allocate_upload(7 * 16, 256);
    std::memcpy(vs.mapped, view, 3 * 16);
    std::memcpy(static_cast<uint8_t*>(vs.mapped) + 48, view + 27 * 16, 4 * 16);
    const uint32_t mbytes = kind == kRigid ? 48 : modelSize;
    UploadSlice ms = allocate_upload(mbytes, 256);
    std::memcpy(ms.mapped, model, mbytes);
    VkDescriptorSet set = allocate(g_native.set);
    if (!set) return VK_NULL_HANDLE;
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
    return set;
}
// the pipeline of a generated family for these inputs (LOD: its LOD-0 pixel program)
VkPipeline generated_pipeline(Generated& g, const PipeIn& in, uint64_t vsHash, uint64_t psHash, bool lod, const char** why,
                              bool dyn = false, VkPipelineLayout layoutOverride = VK_NULL_HANDLE,
                              const std::vector<uint32_t>* vsOverride = nullptr, const char* tag = "") {
    const std::vector<uint32_t>& pw = lod ? g.psLod0 : g.ps;
    if (pw.empty()) { *why = "no LOD-0 program"; return VK_NULL_HANDLE; }
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    char k[80];
    std::snprintf(k, sizeof k, "gen:%016llx:%016llx:%d:", (unsigned long long)vsHash, (unsigned long long)psHash, int(lod));
    std::string key = k;
    if (!all_streams(in, g.mapping, bindings, attributes, key, why)) return VK_NULL_HANDLE;
    if (layoutOverride)
        return create_pipeline(in, layoutOverride, vsOverride ? *vsOverride : g.vs, pw, bindings, attributes, key + "dyn:" + tag, why);
    if (dyn) return create_pipeline(in, g.layoutDyn, g.vs, pw, bindings, attributes, key + "dyn:", why);
    return create_pipeline(in, g.layout, g.vs, pw, bindings, attributes, key, why);
}

// a generated family's native proof draw
bool record_generated(VkCommandBuffer cmd, const ProofDraw& d, Generated& g, bool lod) {
    for (int i = 0; i < LATTE_NUM_MAX_ATTRIBUTE_LOCATIONS; ++i)
        if (g.mapping[i] != d.info.vs->mapping.attributeMapping[i]) return false;
    const char* why = nullptr;
    VkPipeline pipe = generated_pipeline(g, pipe_in(d), g_sample.vsHash, g_sample.psHash, lod, &why);
    if (!pipe) return false;
    SetIn in;
    in.regs = d.info.regs;
    in.ps = d.info.ps;
    in.psSupport = d.psSupport;
    in.psSupportSize = d.psSupportSize;
    in.image = [&d](int binding) -> const VkDescriptorImageInfo* {
        for (uint32_t i = 0; i < d.psImageCount; ++i)
            if (int(d.psImages[i].binding) == binding) return &d.psImages[i].info;
        return nullptr;
    };
    VkDescriptorSet set = generated_set(g, in);
    if (!set) return false;
    bind_and_draw(cmd, d, pipe, g.layout, set);
    return true;
}
}  // namespace fam
using namespace fam;

bool record_native(VkCommandBuffer cmd, const ProofDraw& d, bool lod) {
    if (g_sample.gen) return record_generated(cmd, d, *g_sample.gen, lod);
    const Family* fam = g_sample.hand;
    if (!fam) return false;
    const char* why = nullptr;
    VkPipeline pipe = pipeline(pipe_in(d), fam->kind, &why);
    if (!pipe) return false;
    VkDescriptorSet set = hand_set(fam->kind, d.info.regs);
    if (!set) return false;
    bind_and_draw(cmd, d, pipe, g_native.layout, set);
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


// ---------------------------------------------------------------- shared with the material paths (B2: j3d_material,
// D2: native_draw/j3d_desc_depth.cpp): the proven lists, dynamic-uniform layouts, kept set pools
namespace fam {
const std::set<std::pair<uint64_t, uint64_t>>& proven() {
    static const std::set<std::pair<uint64_t, uint64_t>> s = {
// 60 proven generated families (covered texels > 0, 0 mismatches), 14 zero-texel, 0 with mismatches
    {0x28aaefdc5033135cull, 0x36ba2076004ec111ull},  // Door_a_v (50107 samples)
    {0x0c4ce3740cd2d3eeull, 0xd6085ce911adcdc9ull},  // A_lwod_a_ha_v (42593 samples)
    {0xc7876a691ebb31bfull, 0x36ba2076004ec111ull},  // koura (36315 samples)
    {0x8dc3aa208e87c193ull, 0x36ba2076004ec111ull},  // SHMSBMAT (30913 samples)
    {0x7e05040f72ee9968ull, 0x678a6ae9f605bb70ull},  // mayuR (30606 samples)
    {0x6ea49c844ce22756ull, 0x36ba2076004ec111ull},  // m_ka1 (30346 samples)
    {0xe56c42b3fe6c9b17ull, 0xd63018954921f60dull},  // mayuLdamB (30112 samples)
    {0xd464ec21fc134062ull, 0xd6085ce911adcdc9ull},  // A_doom_a_v (18814 samples)
    {0x28920f003c976012ull, 0xdb6a853e46ba1d15ull},  // tyakusui (14312 samples)
    {0x8950ec8ef964dcbdull, 0xff2fbea617247e53ull},  // SC_lupy_outside (8836 samples)
    {0x5cb32ed7152e3560ull, 0xeee2b994dc8c4f4cull},  // SC_lupy_inside (8804 samples)
    {0xb9fbe56e645003d4ull, 0xeac78331d566a8fcull},  // V_tsp_v (7929 samples)
    {0xa9d36ebee024c35dull, 0x02417a1d1b471495ull},  // SC_eye (6636 samples)
    {0x3d527d70a864d5f0ull, 0x1eb9adbc9eea09f8ull},  // m_pg_hana (5374 samples)
    {0x506d088c3511da3cull, 0xd6085ce911adcdc9ull},  // lambert108_v,lambert11_v,m03_v,test001_l (3445 samples)
    {0x8275b83eec96836full, 0x0b0c5f8cb3e3019eull},  // ga_all (3442 samples)
    {0x693d3b08d3e7d66cull, 0xa63833cd3620b37cull},  // BoxB1,BoxC1,BoxC1_2_ (2363 samples)
    {0x8275b83eec96836full, 0x2222d8c57b2db242ull},  // me (2266 samples)
    {0x3bcd9d57e7a201a1ull, 0x36ba2076004ec111ull},  // m_pz_hair,m_pz_jewel,zelda_zhead_2_ (1448 samples)
    {0x494ef1808da3e8e6ull, 0x0b0c5f8cb3e3019eull},  // m_bpw_light1,mk_hidane (1397 samples)
    {0x1d46893bdfbcd2c1ull, 0x2222d8c57b2db242ull},  // eye,mo_e1,php02 (1370 samples)
    {0x9bdf6ad9af4b836cull, 0x2222d8c57b2db242ull},  // SP_VzouK (1033 samples)
    {0x6bebed70447550e5ull, 0x1eb9adbc9eea09f8ull},  // m_bokobaba_sita,m_ki_main (810 samples)
    {0xd41c7dcdbdfbd13cull, 0x800e90bc4ff101a1ull},  // haneR (740 samples)
    {0x58f515c9a9615417ull, 0x1eb9adbc9eea09f8ull},  // Bom1,bakM (485 samples)
    {0x0ec90e7692f66798ull, 0x2222d8c57b2db242ull},  // m_tn_me1,ne (456 samples)
    {0xca3e0eac7ec5d7d1ull, 0x2222d8c57b2db242ull},  // mo_e1_2_ (446 samples)
    {0x7e05040f72ee9968ull, 0x02417a1d1b471495ull},  // SC_eye,eye (432 samples)
    {0xd03e0f90de3b06deull, 0xfc65a3631593f832ull},  // gnd_eye_l_tx (415 samples)
    {0xb00cc7270f0154a2ull, 0x36ba2076004ec111ull},  // m_pz_body (412 samples)
    {0x580108c41f88a095ull, 0xb765e4e162a813eeull},  // hikari_ex_hana_ura (412 samples)
    {0xd4f5abd7d6b914a2ull, 0xd682be4215c5d44full},  // lambert14 (406 samples)
    {0xa8375f5992cbd44full, 0x2222d8c57b2db242ull},  // lock (386 samples)
    {0xe0e042c266a067f2ull, 0xd6085ce911adcdc9ull},  // lambert98_v_x_v (319 samples)
    {0xc8ae22b00ce6cbafull, 0xfc65a3631593f832ull},  // tuno (307 samples)
    {0x1d46893bdfbcd2c1ull, 0xaf59eec97d14902cull},  // kuro (298 samples)
    {0xc8961f8e4b9e115dull, 0xa5bceb55676eebdcull},  // E3mYOGAN_v,SC_Yedge_v (242 samples)
    {0x3bf879dc72be1975ull, 0x6ce79d333ac84e26ull},  // lock_boss (235 samples)
    {0x8cdd1fad10056aacull, 0xd6085ce911adcdc9ull},  // igaH_v (218 samples)
    {0x9a118cf57a0bb0f7ull, 0x36ba2076004ec111ull},  // body,m_dr_sippo_hihu1 (210 samples)
    {0x24c887547119d079ull, 0x1eb9adbc9eea09f8ull},  // bj_face (207 samples)
    {0x2d65a3ff8a6a7109ull, 0xcc089e8547b5b869ull},  // lambert70_v (207 samples)
    {0x1cec203c02f21e0bull, 0x2222d8c57b2db242ull},  // mi1 (187 samples)
    {0xe8c6570214fc3ea4ull, 0xc53976a2f5a4257aull},  // light (187 samples)
    {0x1d46893bdfbcd2c1ull, 0x6ce79d333ac84e26ull},  // body (176 samples)
    {0x21ec364a6a3f7039ull, 0x2222d8c57b2db242ull},  // hada (144 samples)
    {0xff51d0d389079aebull, 0x29a059e66d2db61aull},  // lambert7_2_ (119 samples)
    {0x1d46893bdfbcd2c1ull, 0xb89bf1dc4ae3a54bull},  // wizrobe_all_tx (116 samples)
    {0x6ec8db5ccfe1d36aull, 0xd6085ce911adcdc9ull},  // V_syok_v (115 samples)
    {0xabb399ac72e51910ull, 0x2222d8c57b2db242ull},  // SP_Vbtsp_v (86 samples)
    {0xc32255e329433384ull, 0x6cd6eeb0fef8850aull},  // suimen (79 samples)
    {0x1beb170cdcafdb2eull, 0x8062b5ae54f03e80ull},  // sita1_v (74 samples)
    {0x26b70fa90605042full, 0xeee2b994dc8c4f4cull},  // wizrobe_cloth_tx (61 samples)
    {0xd464ec21fc134062ull, 0x59de425c11be4e91ull},  // ochiba_v (58 samples)
    {0x8cc8323d21c3d037ull, 0xd6085ce911adcdc9ull},  // V_koke_y_v (58 samples)
    {0x2453f8a9be665c8aull, 0xd6085ce911adcdc9ull},  // A_doom_kabe_v (30 samples)
    {0x9d86bcb5159a7140ull, 0x36ba2076004ec111ull},  // dk_body_v (29 samples)
    {0xfe26078b595e1aa7ull, 0x02417a1d1b471495ull},  // m_dk_kuti_naka (6 samples)
    {0x25d362907adb0ebdull, 0x1eb9adbc9eea09f8ull},  // m_dk_kumadori1 (4 samples)
    {0xe879bb19820b9d5cull, 0xb89bf1dc4ae3a54bull},  // MsuSW_MX_v_4_ (4 samples)
    };
    return s;
}
// (WWHD_NATIVE_J3D_DEPTH_BIASFREE=1) the proven generated families by their bias-free structure hashes
const std::set<std::pair<uint64_t, uint64_t>>& proven_biasfree() {
    static const std::set<std::pair<uint64_t, uint64_t>> s = {
// 60 proven generated families (covered texels > 0, 0 mismatches), 14 zero-texel, 0 with mismatches (depth/runs/proof-bfl0, proof-bflod, proof-bfst)
    {0x28aaefdc5033135cull, 0x36ba2076004ec111ull},  // Door_a_v (50118 samples)
    {0x0c4ce3740cd2d3eeull, 0xce8df10559a228daull},  // A_lwod_a_ha_v,A_lwod_a_miki1_v (42681 samples)
    {0xc7876a691ebb31bfull, 0x36ba2076004ec111ull},  // koura (36310 samples)
    {0x8dc3aa208e87c193ull, 0x36ba2076004ec111ull},  // SHMSBMAT,ear_3_ (30881 samples)
    {0x7e05040f72ee9968ull, 0xe8d63484e27f4031ull},  // mayuR (30724 samples)
    {0x6ea49c844ce22756ull, 0x36ba2076004ec111ull},  // m_ka1 (30364 samples)
    {0xe56c42b3fe6c9b17ull, 0xd63018954921f60dull},  // mayuLdamB (30154 samples)
    {0xd464ec21fc134062ull, 0xce8df10559a228daull},  // A_doom_a_v (18820 samples)
    {0x28920f003c976012ull, 0xb28174754e25ab20ull},  // tyakusui (14285 samples)
    {0x8950ec8ef964dcbdull, 0x774d1d435b761ab4ull},  // SC_lupy_outside (8835 samples)
    {0x5cb32ed7152e3560ull, 0xeee2b994dc8c4f4cull},  // SC_lupy_inside (8802 samples)
    {0xb9fbe56e645003d4ull, 0xbbc227bca0654453ull},  // V_tsp_v (7923 samples)
    {0xa9d36ebee024c35dull, 0xc2644f2a97028618ull},  // SC_eye (6569 samples)
    {0x3d527d70a864d5f0ull, 0x83e74803dc2aed11ull},  // m_pg_hana (5371 samples)
    {0x8275b83eec96836full, 0x2d7807700c3d399dull},  // ga_all (3450 samples)
    {0x506d088c3511da3cull, 0xce8df10559a228daull},  // lambert11_v,m03_v,test001_lambert112_v,test001_light_v,test1 (3449 samples)
    {0x693d3b08d3e7d66cull, 0xa63833cd3620b37cull},  // BoxB1,BoxC1,BoxC1_2_ (2350 samples)
    {0x8275b83eec96836full, 0x0b0d83df11001cbdull},  // me (2266 samples)
    {0x3bcd9d57e7a201a1ull, 0x36ba2076004ec111ull},  // m_pz_hair,m_pz_head,zelda_zhead_2_ (1451 samples)
    {0x494ef1808da3e8e6ull, 0x2d7807700c3d399dull},  // m_bpw_light1,mk_hidane (1399 samples)
    {0x1d46893bdfbcd2c1ull, 0x0b0d83df11001cbdull},  // eye,mo_e1,php02 (1370 samples)
    {0x9bdf6ad9af4b836cull, 0x0b0d83df11001cbdull},  // SP_VzouK (1035 samples)
    {0x6bebed70447550e5ull, 0x83e74803dc2aed11ull},  // m_bokobaba_sita,m_ki_main (810 samples)
    {0xd41c7dcdbdfbd13cull, 0x800e90bc4ff101a1ull},  // haneB (738 samples)
    {0x58f515c9a9615417ull, 0x83e74803dc2aed11ull},  // Bom1_2_,bakM (490 samples)
    {0x0ec90e7692f66798ull, 0x0b0d83df11001cbdull},  // m_tn_me1,ne (456 samples)
    {0xca3e0eac7ec5d7d1ull, 0x0b0d83df11001cbdull},  // mo_e1_2_ (447 samples)
    {0x7e05040f72ee9968ull, 0xc2644f2a97028618ull},  // SC_eye,eye (432 samples)
    {0xd4f5abd7d6b914a2ull, 0x061647c84d2c0a90ull},  // lambert14 (415 samples)
    {0xb00cc7270f0154a2ull, 0x36ba2076004ec111ull},  // m_pz_body (413 samples)
    {0xd03e0f90de3b06deull, 0x6f5c10fbc6e10dcbull},  // gnd_eye_l_tx (412 samples)
    {0x580108c41f88a095ull, 0xe4ce93ed3d0ed099ull},  // hikari_ex_hana_ura (412 samples)
    {0xa8375f5992cbd44full, 0x0b0d83df11001cbdull},  // lock (388 samples)
    {0xc8ae22b00ce6cbafull, 0x6f5c10fbc6e10dcbull},  // tuno (309 samples)
    {0xe0e042c266a067f2ull, 0xce8df10559a228daull},  // lambert98_v_x_v (295 samples)
    {0x1d46893bdfbcd2c1ull, 0xea305e0366f1dfcfull},  // kuro (291 samples)
    {0xc8961f8e4b9e115dull, 0x654f3b2581ad27b9ull},  // E3mYOGAN_v,SC_Yedge_v (243 samples)
    {0x3bf879dc72be1975ull, 0xced7875d7890b56bull},  // lock_boss (236 samples)
    {0x8cdd1fad10056aacull, 0xce8df10559a228daull},  // igaH_v (218 samples)
    {0x9a118cf57a0bb0f7ull, 0x36ba2076004ec111ull},  // body,m_dr_sippo_hihu1 (210 samples)
    {0x2d65a3ff8a6a7109ull, 0x537fb8fa9f0293adull},  // lambert70_v (208 samples)
    {0x24c887547119d079ull, 0x83e74803dc2aed11ull},  // bj_face (207 samples)
    {0x1cec203c02f21e0bull, 0x0b0d83df11001cbdull},  // mi1 (187 samples)
    {0xe8c6570214fc3ea4ull, 0x731792febd963b2full},  // light (187 samples)
    {0x1d46893bdfbcd2c1ull, 0xced7875d7890b56bull},  // body (176 samples)
    {0x21ec364a6a3f7039ull, 0x0b0d83df11001cbdull},  // hada (144 samples)
    {0x1d46893bdfbcd2c1ull, 0xd3abfbb42cd82ef0ull},  // wizrobe_all_tx (116 samples)
    {0xff51d0d389079aebull, 0x9572a68378ed4263ull},  // lambert7 (115 samples)
    {0x6ec8db5ccfe1d36aull, 0xce8df10559a228daull},  // V_syok_v (115 samples)
    {0xabb399ac72e51910ull, 0x0b0d83df11001cbdull},  // SP_Vbtsp_v (83 samples)
    {0xc32255e329433384ull, 0xb2c0a38992a50bc4ull},  // suimen (79 samples)
    {0x1beb170cdcafdb2eull, 0xda6dbb7aa8729a8eull},  // sita1_v (74 samples)
    {0x26b70fa90605042full, 0xeee2b994dc8c4f4cull},  // wizrobe_cloth_tx (61 samples)
    {0xd464ec21fc134062ull, 0x600376639308f93eull},  // ochiba_v (58 samples)
    {0x8cc8323d21c3d037ull, 0xce8df10559a228daull},  // V_koke_y_v (58 samples)
    {0x2453f8a9be665c8aull, 0xce8df10559a228daull},  // A_doom_kabe_v (30 samples)
    {0x9d86bcb5159a7140ull, 0x36ba2076004ec111ull},  // dk_body_v (29 samples)
    {0xfe26078b595e1aa7ull, 0xc2644f2a97028618ull},  // m_dk_kuti_naka (6 samples)
    {0x25d362907adb0ebdull, 0x83e74803dc2aed11ull},  // m_dk_kumadori1 (4 samples)
    {0xe879bb19820b9d5cull, 0xd3abfbb42cd82ef0ull},  // MsuSW_MX_v_4_ (4 samples)
    };
    return s;
}

// the dynamic-uniform layout of a set layout's bindings
bool dyn_layout(std::vector<VkDescriptorSetLayoutBinding> b, VkDescriptorSetLayout& set, VkPipelineLayout& layout) {
    using namespace gfxvk;
    if (layout) return true;
    for (auto& x : b)
        if (x.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) x.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = uint32_t(b.size());
    ci.pBindings = b.data();
    if (vkCreateDescriptorSetLayout(R.device, &ci, nullptr, &set) != VK_SUCCESS) return false;
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1;
    li.pSetLayouts = &set;
    return vkCreatePipelineLayout(R.device, &li, nullptr, &layout) == VK_SUCCESS;
}
NativeDyn g_nativeDyn;
bool native_dyn() {
    if (g_nativeDyn.device == R.device && g_nativeDyn.layout) return true;
    g_nativeDyn = {};
    g_nativeDyn.device = R.device;
    return dyn_layout({{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                       {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}},
                      g_nativeDyn.set, g_nativeDyn.layout);
}
// the slot sets' own pools (kept: a state's sets live as long as the state)
struct SetPools { VkDevice device = VK_NULL_HANDLE; std::vector<VkDescriptorPool> pools; } g_setPools;
VkDescriptorSet slot_set(VkDescriptorSetLayout layout) {
    using namespace gfxvk;
    if (g_setPools.device != R.device) g_setPools = {R.device, {}};
    VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    a.descriptorSetCount = 1;
    a.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!g_setPools.pools.empty()) {
        a.descriptorPool = g_setPools.pools.back();
        if (vkAllocateDescriptorSets(R.device, &a, &set) == VK_SUCCESS) return set;
    }
    VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 256 * 8}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 * 4}};
    VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    ci.maxSets = 256;
    ci.poolSizeCount = 2;
    ci.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(R.device, &ci, nullptr, &pool) != VK_SUCCESS) return VK_NULL_HANDLE;
    g_setPools.pools.push_back(pool);
    a.descriptorPool = pool;
    return vkAllocateDescriptorSets(R.device, &a, &set) == VK_SUCCESS ? set : VK_NULL_HANDLE;
}
}  // namespace fam

}  // namespace native_draw::j3d_depth
