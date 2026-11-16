// Native depth programs for described J3D draws, and shadow-slice instancing (j3d_desc_depth.h). Decomp build fast mode,
// WWHD_NATIVE_DRAW=ON only. The family core (hashes, proven lists, programs, pipelines) is j3d_depth_family.cpp.
#include "../../third_party/cemu/cemu_shim.h"  // (as the gx2 sources get it: -include)
#include "native_draw/j3d_desc_depth.h"
#include "native_draw/j3d_desc_depth_inst.h"
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"
#include "native_draw/j3d_depth_family.h"
#include "native_draw/j3d_depth.h"
#include "gfx/vulkan/backend.h"
#include "gfx/vulkan/shaders.h"
#include "gfx/vulkan/deferred_dispatch.h"
#include "gx2/gx2_cmd.h"
#include "runtime.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Latte;

namespace native_draw::j3ddesc::depth {
namespace {
namespace fam = native_draw::j3d_depth::fam;
using gfxvk::R;
constexpr uint32_t kMaxBind = 48;
constexpr uint32_t kMaxInst = 4;        // slices per instanced draw (the cascade has 3)
constexpr uint32_t kLayerBinding = 60;  // the instanced variant's layer table {ivec4 fam_layer[kMaxInst]}

bool env_on(const char* n, bool fallback = false) {
    const char* e = std::getenv(n);
    return e && *e ? std::atoi(e) != 0 : fallback;
}
bool g_layerOutput = false;  // the device writes gl_Layer from the vertex stage (backend hook)

// ---------------------------------------------------------------- counters
struct Counts {
    std::map<std::string, uint64_t> refused, failed;
    uint64_t hand = 0, gen = 0, reused = 0, draws = 0, setsWritten = 0, setsOwn = 0;
    uint64_t collected = 0, groups = 0, instanced = 0, instDraws = 0, flushes = 0, notCollected = 0;
    std::map<std::string, uint64_t> instRefused;
} g_counts;
uint64_t g_countFrame = 0;

// ---------------------------------------------------------------- per-state descriptor sets (one per submission slot)
struct Ubo { uint32_t binding; VkBuffer buffer; VkDeviceSize offset, range; };
struct Img { uint32_t binding; VkDescriptorImageInfo info; };
struct SlotSet {
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint64_t gen = ~0ull;  // the submission it was last bound in
    bool written = false;
    std::array<VkBuffer, kMaxBind> buffers{};
    std::array<VkDeviceSize, kMaxBind> ranges{};
};
constexpr uint32_t kSetsPerSlot = 4;  // a state's draws in one submission may see up to this many buffer sets
struct SetCache { std::vector<std::array<SlotSet, kSetsPerSlot>> slots; };
// the slot's set: one with the same buffers and ranges as it is (images are static per state), else one not bound yet in
// this submission rewritten, else a set of the submission's pool for this draw (counted "own": an allocation). Up to
// kSetsPerSlot sets per (state, submission slot): blocks that move to another upload buffer within one submission (the
// upload ring's next buffer) find their set again from the next frame on. Dynamic offsets carry the per-draw part.
VkDescriptorSet slot_set_for(SetCache& sc, VkDescriptorSetLayout layout, const Ubo* ubo, uint32_t nubo, const Img* img,
                             uint32_t nimg) {
    using namespace gfxvk;
    const size_t slot = R.activeSubmission;
    if (sc.slots.size() <= slot) sc.slots.resize(slot + 1);
    auto& entries = sc.slots[slot];
    SlotSet* ss = nullptr;
    SlotSet* spare = nullptr;  // (not bound in this submission: may be rewritten)
    for (SlotSet& e : entries) {
        bool same = e.written;
        for (uint32_t i = 0; i < nubo && same; ++i) same = e.buffers[i] == ubo[i].buffer && e.ranges[i] == ubo[i].range;
        if (same) { ss = &e; break; }
        if (!spare && e.gen != R.submissionGeneration) spare = &e;
    }
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (ss) set = ss->set;
    else {
        ss = spare ? spare : &entries[0];
        if (!spare) {
            ss = nullptr;  // (bound earlier in this submission: kept; this draw takes a set of the submission)
            VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            a.descriptorPool = R.descriptorPool;
            a.descriptorSetCount = 1;
            a.pSetLayouts = &layout;
            if (vkAllocateDescriptorSets(R.device, &a, &set) != VK_SUCCESS) return VK_NULL_HANDLE;
        } else {
            if (!ss->set) ss->set = fam::slot_set(layout);
            set = ss->set;
        }
        if (!set) return VK_NULL_HANDLE;
        std::array<VkWriteDescriptorSet, kMaxBind + 16> w{};
        std::array<VkDescriptorBufferInfo, kMaxBind> bi{};
        uint32_t nw = 0;
        for (uint32_t i = 0; i < nubo; ++i) {
            bi[i] = {ubo[i].buffer, 0, ubo[i].range};
            w[nw] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w[nw].dstSet = set, w[nw].dstBinding = ubo[i].binding, w[nw].descriptorCount = 1;
            w[nw].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, w[nw].pBufferInfo = &bi[i];
            ++nw;
        }
        // images: static per state, written with the set (a fresh set needs them; a kept one has them)
        for (uint32_t i = 0; i < nimg && (!ss || !ss->written); ++i) {
            w[nw] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w[nw].dstSet = set, w[nw].dstBinding = img[i].binding, w[nw].descriptorCount = 1;
            w[nw].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, w[nw].pImageInfo = &img[i].info;
            ++nw;
        }
        vkUpdateDescriptorSets(R.device, nw, w.data(), 0, nullptr);
        if (ss) {
            for (uint32_t i = 0; i < nubo; ++i) ss->buffers[i] = ubo[i].buffer, ss->ranges[i] = ubo[i].range;
            ss->written = true;
            ++g_counts.setsWritten;
        } else ++g_counts.setsOwn;
    }
    if (ss) ss->gen = R.submissionGeneration;
    return set;
}

// ---------------------------------------------------------------- states
// what feeds a binding of the family's set
enum NeedKind : uint8_t {
    kWhole,     // the draw's block at (stage, location), whole (RANGE: at most that many bytes)
    kHandView,  // the hand-written families' view: location 2's rows 0-2 and 27-30 (7 vec4), repacked
    kPacked,    // instanced variant: the block's first VEC4S vec4 of every instance, packed (fam_v<loc>[i * n + k])
    kSupport,   // the support scalars (static per state)
    kLayers,    // instanced variant: the layer table
};
struct Need {
    uint32_t stageLoc = 0, binding = 0, range = 0, vec4s = 0;
    NeedKind kind = kWhole;
};
struct Variant {
    VkPipeline pipe = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    std::vector<Need> needs;  // in binding order
    SetCache sets;
    const char* why = nullptr;  // (instanced variant) why there is none
};
}  // namespace
struct State {  // (declared in the header)
    const fam::Family* hand = nullptr;
    fam::Generated* gen = nullptr;
    uint64_t vsHash = 0, psHash = 0;
    Variant plain, inst;
    uint32_t ncolor = 0;  // the pass's colour targets (the plain pipeline's formats; the instanced one has none)
    std::vector<Img> images;  // static
    bool support = false;
    std::array<float, 33 * 4> sv{};
    uint64_t supGen = ~0ull;
    gfxvk::UploadSlice sup{};
};
namespace {
std::deque<State> g_states;
std::unordered_multimap<uint64_t, State*> g_byKey;  // choose(): states by their inputs (state_key)
// a state's inputs: family, hashes, pipeline (the core's per-key pipeline: family, streams, raster words, formats), the
// block plan, support scalars and static images
uint64_t state_key(const State& s) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    mix(uint64_t(reinterpret_cast<uintptr_t>(s.hand))), mix(uint64_t(reinterpret_cast<uintptr_t>(s.gen)));
    mix(s.vsHash), mix(s.psHash), mix(uint64_t(reinterpret_cast<uintptr_t>(s.plain.pipe))), mix(s.ncolor);
    for (const Need& n : s.plain.needs) mix(uint64_t(n.stageLoc) << 32 | n.binding), mix(uint64_t(n.range) << 8 | n.kind);
    for (const Img& i : s.images)
        mix(i.binding), mix(uint64_t(reinterpret_cast<uintptr_t>(i.info.sampler))),
            mix(uint64_t(reinterpret_cast<uintptr_t>(i.info.imageView))), mix(uint64_t(i.info.imageLayout));
    if (s.support) for (float f : s.sv) { uint32_t b; std::memcpy(&b, &f, 4); mix(b); }
    return h;
}
bool same_state(const State& a, const State& b) {
    if (a.hand != b.hand || a.gen != b.gen || a.vsHash != b.vsHash || a.psHash != b.psHash || a.plain.pipe != b.plain.pipe ||
        a.plain.layout != b.plain.layout || a.ncolor != b.ncolor || a.support != b.support ||
        a.plain.needs.size() != b.plain.needs.size() || a.images.size() != b.images.size())
        return false;
    for (size_t i = 0; i < a.plain.needs.size(); ++i) {
        const Need &x = a.plain.needs[i], &y = b.plain.needs[i];
        if (x.stageLoc != y.stageLoc || x.binding != y.binding || x.range != y.range || x.vec4s != y.vec4s || x.kind != y.kind)
            return false;
    }
    for (size_t i = 0; i < a.images.size(); ++i)
        if (a.images[i].binding != b.images[i].binding || a.images[i].info.sampler != b.images[i].info.sampler ||
            a.images[i].info.imageView != b.images[i].info.imageView || a.images[i].info.imageLayout != b.images[i].info.imageLayout)
            return false;
    return !a.support || std::memcmp(a.sv.data(), b.sv.data(), sizeof a.sv) == 0;
}
struct Hashed { const fam::Family* family = nullptr; uint64_t hash = 0; };
std::unordered_map<uint64_t, Hashed> g_vs;     // vertex shader key -> structure hash, hand-written family
std::unordered_map<uint64_t, uint64_t> g_ps;   // pixel shader key -> structure hash

State* refuse(const char* why) {
    ++g_counts.refused[why];
    return nullptr;
}

// the support scalars of the generated programs (FamS {fam_s; fam_lod[16]; fam_tsc[16]}), from the define's bytes
bool support_from(State& s, const gfxvk::vk::Shader* ps, const uint8_t* sup, uint32_t n) {
    if (!sup) return false;
    auto get = [&](int32_t off, float* dst, int count) {
        if (off >= 0 && uint32_t(off) + 4u * count <= n) std::memcpy(dst, sup + off, 4u * count);
    };
    get(ps->uniforms.offset_alphaTestRef, &s.sv[0], 1);
    for (int u = 0; u < 16; ++u) {
        get(ps->uniforms.offset_texLodBias[u], &s.sv[4 + u * 4], 1);
        get(ps->uniforms.offset_texScale[u], &s.sv[4 + 64 + u * 4], 2);
    }
    return true;
}

// ---------------------------------------------------------------- the layered-instance variant
// Same arithmetic, same streams; the per-slice blocks come from a packed array indexed by the instance, and the
// instance's slice is written to gl_Layer. Hand-written: View {view[3]; proj[4]} -> fam_view[7 * kMaxInst];
// generated: every remapped vertex block fam_v<L>[n] -> fam_v<L>[n * kMaxInst] (buffer-mode blocks and the pixel
// stage stay shared: equal in every instance or the draws are not merged).
size_t main_body(const std::string& t) {
    size_t m = t.find("void main()");
    if (m == std::string::npos) return std::string::npos;
    m = t.find('{', m);
    return m == std::string::npos ? m : m + 1;
}
bool inst_text(std::string& t, bool hand, std::map<uint32_t, uint32_t>& packed) {
    const size_t nl = t.find('\n');
    if (t.compare(0, 9, "#version ") != 0 || nl == std::string::npos) return false;
    t.insert(nl + 1, "#extension GL_ARB_shader_viewport_layer_array : require\n");
    std::string pre = "int fam_i = gl_InstanceIndex;\n    gl_Layer = fam_layer[fam_i].x;\n";
    if (hand) {
        const std::string decl = "uniform View { vec4 view[3]; vec4 proj[4]; };";
        const size_t d = t.find(decl);
        if (d == std::string::npos) return false;
        t.replace(d, decl.size(), "uniform View { vec4 fam_view[" + std::to_string(7 * kMaxInst) + "]; };");
        t = std::regex_replace(t, std::regex(R"(\bview\[)"), "fam_view[fam_i * 7 + ");
        t = std::regex_replace(t, std::regex(R"(\bproj\[)"), "fam_view[fam_i * 7 + 3 + ");
    } else {
        // declarations first (kept apart while the reads are rewritten)
        const std::regex declRe(R"(uniform FamV(\d+) \{ ivec4 fam_v\d+\[(\d+)\]; \};)");
        std::smatch m;
        std::string out, rest = t;
        std::vector<std::string> decls;
        while (std::regex_search(rest, m, declRe)) {
            const uint32_t loc = uint32_t(std::stoul(m[1].str())), n = uint32_t(std::stoul(m[2].str()));
            packed[loc] = n;
            out += m.prefix().str() + "@DECL" + std::to_string(decls.size()) + "@";
            decls.push_back("uniform FamV" + m[1].str() + " { ivec4 fam_v" + m[1].str() + "[" + std::to_string(n * kMaxInst) + "]; };");
            rest = m.suffix().str();
        }
        out += rest;
        for (const auto& [loc, n] : packed)
            out = std::regex_replace(out, std::regex("\\bfam_v" + std::to_string(loc) + "\\["),
                                     "fam_v" + std::to_string(loc) + "[fam_i * " + std::to_string(n) + " + ");
        for (size_t i = 0; i < decls.size(); ++i) {
            const std::string mark = "@DECL" + std::to_string(i) + "@";
            out.replace(out.find(mark), mark.size(), decls[i]);
        }
        t.swap(out);
    }
    const size_t body = main_body(t);
    if (body == std::string::npos) return false;
    t.insert(body, "\n    " + pre);
    t.insert(t.find("void main()"), "layout(set = 0, binding = " + std::to_string(kLayerBinding) +
                                        ", std140) uniform FamLayer { ivec4 fam_layer[" + std::to_string(kMaxInst) + "]; };\n");
    return true;
}
void make_inst(State& s, fam::PipeIn in, bool clampVariant) {
    using namespace gfxvk;
    Variant& v = s.inst;
    if (!g_layerOutput) { v.why = "no gl_Layer from the vertex stage"; return; }
    if (R.renderPasses) { v.why = "render-pass path (layered passes on dynamic rendering only)"; return; }
    std::string t = s.hand ? fam::vertex_source(s.hand->kind, clampVariant) : s.gen->vsText;
    std::map<uint32_t, uint32_t> packed;
    if (t.empty() || !inst_text(t, s.hand != nullptr, packed)) { v.why = "vertex program rewrite"; return; }
    const std::vector<uint32_t> vsWords = fam::words(t, true, "instanced depth");
    if (vsWords.empty()) { v.why = "instanced program did not compile"; return; }
    std::vector<VkDescriptorSetLayoutBinding> b;
    if (s.hand) {
        b = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
             {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}};
        v.needs = {{2u, 0, 7 * 16, 7, kPacked}, s.plain.needs[1]};
        v.needs[0].kind = kHandView;  // (packed per instance from the view's repack)
    } else {
        b = s.gen->bindings;
        for (const Need& n : s.plain.needs) {
            Need m = n;
            const uint32_t loc = n.stageLoc & 0xFFFF;
            if (n.kind == kWhole && (n.stageLoc >> 16) == 0 && packed.count(loc)) {
                m.kind = kPacked;
                m.vec4s = packed[loc];
                m.range = packed[loc] * 16 * kMaxInst;
            }
            v.needs.push_back(m);
        }
    }
    b.push_back({kLayerBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr});
    v.needs.push_back({0, kLayerBinding, 16 * kMaxInst, 0, kLayers});
    if (!fam::dyn_layout(b, v.setLayout, v.layout)) { v.why = "instanced layout"; return; }
    in.colors = {};  // the layered pass has the depth attachment only (colour writes are off in this state anyway)
    in.ncolor = 0;
    const char* why = nullptr;
    v.pipe = s.hand ? fam::pipeline(in, s.hand->kind, &why, v.layout, &vsWords, "inst:")
                    : fam::generated_pipeline(*s.gen, in, s.vsHash, s.psHash, false, &why, true, v.layout, &vsWords, "inst:");
    if (!v.pipe) v.why = why ? why : "instanced pipeline";
}

// ---------------------------------------------------------------- per-draw block uploads
const uint32_t* find_block(const Draw& d, uint32_t stageLoc) {
    for (uint32_t i = 0; i < d.blockCount; ++i)
        if (d.blocks[i * 3] == stageLoc) return d.blocks + i * 3;
    return nullptr;
}
// a block's bytes as the GPU reads them at this point of the command stream (the submitted copy, else guest memory)
const uint8_t* host_block(uint32_t addr, uint32_t size) {
    if (const uint8_t* sub = gx2::submitted_uniform_block(addr, size)) return sub;
    return static_cast<const uint8_t*>(mem::ptr(addr));
}
// the hand-written families' view (rows 0-2 and 27-30): once per submission per address for submitted blocks
gfxvk::UploadSlice view_upload(uint32_t addr, uint32_t size, bool* ok) {
    using namespace gfxvk;
    *ok = false;
    if (!addr || size < 31 * 16) return {};
    const uint8_t* sub = gx2::submitted_uniform_block(addr, size);
    static uint32_t lastAddr = 0;
    static uint64_t lastId = ~0ull, lastGen = ~0ull;
    static UploadSlice last{};
    if (sub && addr == lastAddr && lastId == gx2::submitted_uniforms_id() && lastGen == R.submissionGeneration) {
        *ok = true;
        return last;
    }
    const uint8_t* view = sub ? sub : static_cast<const uint8_t*>(mem::ptr(addr));
    const VkDeviceSize align = std::max<VkDeviceSize>(256, R.properties.limits.minUniformBufferOffsetAlignment);
    UploadSlice vs = allocate_upload(7 * 16, align);
    std::memcpy(vs.mapped, view, 3 * 16);
    std::memcpy(static_cast<uint8_t*>(vs.mapped) + 48, view + 27 * 16, 4 * 16);
    vs.size = 7 * 16;
    if (sub) lastAddr = addr, lastId = gx2::submitted_uniforms_id(), lastGen = R.submissionGeneration, last = vs;
    else lastAddr = 0;
    *ok = true;
    return vs;
}
bool support_slice(State& s, Ubo& u, uint32_t binding) {
    using namespace gfxvk;
    if (s.supGen != R.submissionGeneration) {
        const VkDeviceSize align = std::max<VkDeviceSize>(256, R.properties.limits.minUniformBufferOffsetAlignment);
        s.sup = allocate_upload(sizeof s.sv, align);
        if (!s.sup.mapped) return false;
        std::memcpy(s.sup.mapped, s.sv.data(), sizeof s.sv);
        s.sup.size = sizeof s.sv;
        s.supGen = R.submissionGeneration;
    }
    u = {binding, s.sup.buffer, s.sup.offset, s.sup.size};
    return true;
}
// a shared (not per-instance) binding of the draw: whole block, hand view, support
bool shared_ubo(State& s, const Need& n, const Draw& d, Ubo& u, const char** why) {
    using namespace gfxvk;
    if (n.kind == kSupport) return support_slice(s, u, n.binding) || (*why = "support upload", false);
    const uint32_t* b = find_block(d, n.stageLoc);
    if (!b || !b[1]) { *why = "block missing"; return false; }
    const uint32_t size = std::min<uint32_t>(b[2], 0x10000);
    if (n.kind == kHandView) {
        bool ok = false;
        const UploadSlice v = view_upload(b[1], size, &ok);
        if (!ok) { *why = "view upload"; return false; }
        u = {n.binding, v.buffer, v.offset, 7 * 16};
        return true;
    }
    if (!d.upload) { *why = "no block upload"; return false; }
    const Slice sl = d.upload(b[1], size);
    if (!sl.buffer) { *why = "block upload"; return false; }
    const VkDeviceSize maxRange = R.properties.limits.maxUniformBufferRange;
    u = {n.binding, reinterpret_cast<VkBuffer>(uintptr_t(sl.buffer)), VkDeviceSize(sl.offset),
         std::min<VkDeviceSize>(n.range ? std::min<uint64_t>(n.range, sl.size) : sl.size, maxRange)};
    return true;
}

// ---------------------------------------------------------------- shadow-slice instancing: the collection
struct Entry {
    uint32_t layer = 0;
    uint32_t staged = 0;  // offset of this instance's packed bytes in g_stage (per kPacked/kHandView need, in need order)
    bool proof = false;   // INST proof: also drawn per slice with its plain binding (below) at the flush
    VkPipeline plainPipe = VK_NULL_HANDLE;
    VkPipelineLayout plainLayout = VK_NULL_HANDLE;
    VkDescriptorSet plainSet = VK_NULL_HANDLE;
    std::array<uint32_t, kMaxBind> plainOffsets{};
    uint32_t plainOffsetCount = 0;
};
struct Group {
    State* state = nullptr;
    Geometry geo;
    std::array<Ubo, kMaxBind> shared{};  // per need (kPacked/kLayers entries unused)
    uint32_t nshared = 0;
    std::array<Entry, kMaxInst> inst{};
    uint32_t count = 0;
};
struct Collection {
    uint64_t surface = 0;
    Target target;
    std::vector<Group> groups;
    std::unordered_map<uint64_t, std::vector<uint32_t>> byKey;  // (state, mesh) -> groups
    std::vector<uint8_t> stage;
    uint32_t minLayer = ~0u, maxLayer = 0;
    uint64_t layers = 0;  // bit per collected layer
    void clear() {  // (keeps the capacity: no allocation once warm)
        groups.clear();
        for (auto& [k, v] : byKey) v.clear();
        stage.clear();
        surface = 0;
        minLayer = ~0u, maxLayer = 0, layers = 0;
    }
} g_col, g_flushing;  // (the flush draws from g_flushing: a submission inside it finds nothing collected)
// flush points (counters): where the collection was drawn
std::map<std::string, uint64_t> g_flushAt;
struct ArrayView { VkImage image; uint32_t base, count; VkImageView view; };
std::vector<ArrayView> g_views;
uint64_t g_viewsGen = 0;
VkImageView array_view(const Target& t, uint32_t base, uint32_t count) {
    using namespace gfxvk;
    if (g_viewsGen != g_surface_struct_gen) {  // (a surface's image may have changed: its views are not reused; the old
        g_views.clear();                       // views stay alive, a few per surface rebuild)
        g_viewsGen = g_surface_struct_gen;
    }
    for (const ArrayView& a : g_views)
        if (a.image == t.image && a.base == base && a.count == count) return a.view;
    VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ci.image = t.image;
    ci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ci.format = t.format;
    ci.subresourceRange = {VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT | (t.stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)), 0, 1, base,
                           count};
    VkImageView v = VK_NULL_HANDLE;
    if (vkCreateImageView(R.device, &ci, nullptr, &v) != VK_SUCCESS) return VK_NULL_HANDLE;
    g_views.push_back({t.image, base, count, v});
    return v;
}
bool same_target(const Target& a, const Target& b) {
    return a.surface == b.surface && a.image == b.image && a.width == b.width && a.height == b.height &&
           std::memcmp(&a.viewport, &b.viewport, sizeof a.viewport) == 0 && std::memcmp(&a.scissor, &b.scissor, sizeof a.scissor) == 0;
}
bool same_geometry(const Geometry& a, const Geometry& b) {
    if (a.mesh != b.mesh || a.vertexCount != b.vertexCount || a.index != b.index || a.indexOffset != b.indexOffset ||
        a.indexType != b.indexType || a.indexCount != b.indexCount || a.baseVertex != b.baseVertex)
        return false;
    for (uint32_t i = 0; i < a.vertexCount; ++i)
        if (a.vertex[i] != b.vertex[i] || a.vertexOffset[i] != b.vertexOffset[i] || a.vertexBinding[i] != b.vertexBinding[i])
            return false;
    return true;
}
}  // namespace

// ---------------------------------------------------------------- switches
bool on() {
    static const bool v = [] {
        const bool o = env_on("WWHD_NATIVE_J3D_DESC_DEPTH", true);  // default on (=0: off)
        if (o) LOG("[j3d-desc-depth] native depth programs for described depth-only J3D draws on (WWHD_NATIVE_J3D_DESC_DEPTH=1%s)",
                   fam::biasfree_on() ? ", bias-free family hashes" : "; WWHD_NATIVE_J3D_DEPTH_BIASFREE=1 recommended");
        return o;
    }();
    return v;
}
bool want_layer_output() { return env_on("WWHD_NATIVE_J3D_DESC_DEPTH_INST"); }
void set_layer_output(bool enabled) {
    g_layerOutput = enabled;
    if (want_layer_output()) LOG("[j3d-desc-depth] shadow-slice instancing: gl_Layer from the vertex stage %s", enabled ? "on" : "NOT available");
}
bool inst_on() {
    static const bool v = on() && want_layer_output();
    return v && g_layerOutput;
}

// ---------------------------------------------------------------- define
State* choose(const Query& q) {
    using namespace gfxvk;
    if (!on() || !q.desc || q.desc->pass != 0 || !q.image) return nullptr;
    const uint32_t* r = q.image;
    if (r[REGADDR::CB_TARGET_MASK] != 0 || r[REGADDR::DB_DEPTH_CONTROL] != 0x16) return refuse("not depth-only");
    const auto* vs = static_cast<const vk::Shader*>(q.vs);
    const auto* ps = static_cast<const vk::Shader*>(q.ps);
    const auto* fs = static_cast<const LatteFetchShader*>(q.fetch);
    if (!vs || !vs->dec || !ps || !ps->dec || !fs) return refuse("no programs");
    if (vs->source_text().empty() || ps->source_text().empty()) return refuse("no program text");
    if (vs->clipVarying || ps->clipVarying) return refuse("depth clipping in shaders");
    if (!q.depthFormat) return refuse("no depth target");
    auto [vit, vfresh] = g_vs.try_emplace(vs->key);
    if (vfresh) {
        vit->second.hash = fam::structure_hash(vs, nullptr);
        for (const fam::Family& f : fam::families())
            if (f.vs == vit->second.hash) vit->second.family = &f;
    }
    auto [pit, pfresh] = g_ps.try_emplace(ps->key);
    if (pfresh) pit->second = fam::structure_hash(ps, nullptr);
    State s;
    s.vsHash = vit->second.hash;
    s.psHash = pit->second;
    if (vit->second.family && fam::colour_only(ps)) s.hand = vit->second.family;
    else {
        // proven families not sampled on the described path: the wizrobe pair (state archive/states/siren-r8.wwstate,
        // proven in mode 0) is never replayed natively by D1 (its draws stay translated), so it is unproven there;
        // drawn translated (exact) until proven on the described path
        static const std::set<std::pair<uint64_t, uint64_t>> notSampled = {
            {0x1d46893bdfbcd2c1ull, 0xd3abfbb42cd82ef0ull},  // wizrobe_all_tx
            {0x26b70fa90605042full, 0xeee2b994dc8c4f4cull},  // wizrobe_cloth_tx
        };
        if (fam::biasfree_on() && notSampled.count({s.vsHash, s.psHash})) return refuse("not sampled (described path)");
        if (!(fam::biasfree_on() ? fam::proven_biasfree() : fam::proven()).count({s.vsHash, s.psHash})) {
            static std::set<std::pair<uint64_t, uint64_t>> said;
            if (said.size() < 64 && said.insert({s.vsHash, s.psHash}).second)
                LOG("[j3d-desc-depth] depth-only description %u without a proven family: %016llx/%016llx (stays translated)",
                    q.desc->id, (unsigned long long)s.vsHash, (unsigned long long)s.psHash);
            return refuse("not a proven family");
        }
        fam::Generated& g = fam::generated(vs, ps, s.vsHash, s.psHash);
        if (!g.why.empty()) return refuse("generated program failed");
        for (int i = 0; i < LATTE_NUM_MAX_ATTRIBUTE_LOCATIONS; ++i)
            if (g.mapping[i] != vs->mapping.attributeMapping[i]) return refuse("attribute locations");
        s.gen = &g;
    }
    // the pipeline: made here from the define's image (raster words, strides) and the pass's formats; the family core
    // keeps one per (family, streams, raster words, formats), shared by every state with the same key
    fam::PipeIn in;
    in.regs = r;
    in.fs = fs;
    for (uint32_t i = 0; i < q.colorCount && i < 8; ++i) in.colors[i] = VkFormat(q.colorFormats[i]);
    in.ncolor = std::min<uint32_t>(q.colorCount, 8);
    s.ncolor = in.ncolor;
    in.depth = VkFormat(q.depthFormat);
    in.stencil = q.depthStencil;
    const char* why = nullptr;
    const bool clampVariant = !R.enabledFeatures.depthClamp && (r[REGADDR::PA_CL_CLIP_CNTL] & (1u << 27));
    if (s.hand) {
        if (!fam::native_dyn()) return refuse("native layout");
        s.plain.layout = fam::g_nativeDyn.layout, s.plain.setLayout = fam::g_nativeDyn.set;
        s.plain.pipe = fam::pipeline(in, s.hand->kind, &why, s.plain.layout, nullptr, "");
        s.plain.needs = {{2u, 0, 7 * 16, 7, kHandView}, {1u, 1, s.hand->kind == fam::kRigid ? 48u : 0u, 0, kWhole}};
    } else {
        fam::Generated& g = *s.gen;
        if (!fam::dyn_layout(g.bindings, g.setDyn, g.layoutDyn)) return refuse("generated layout");
        s.plain.layout = g.layoutDyn, s.plain.setLayout = g.setDyn;
        s.plain.pipe = fam::generated_pipeline(g, in, s.vsHash, s.psHash, false, &why, true, VK_NULL_HANDLE, nullptr, "");
        for (uint32_t loc : g.vsBlocks) s.plain.needs.push_back({loc, loc, 0, 0, kWhole});
        for (uint32_t loc : g.psBlocks) s.plain.needs.push_back({1u << 16 | loc, fam::kPsBlockBase + loc, 0, 0, kWhole});
        if (g.support) {
            if (!support_from(s, ps, q.psSupport, q.psSupportSize)) return refuse("no support bytes");
            s.support = true;
            s.plain.needs.push_back({0, fam::kSupportBinding, 0, 0, kSupport});
        }
        for (uint32_t u : g.units) {
            const int binding = ps->mapping.textureUnitToBindingPoint[u];
            const Query::Image* im = nullptr;
            for (uint32_t i = 0; i < q.imageCount; ++i)
                if (int(q.images[i].binding) == binding) { im = &q.images[i]; break; }
            if (!im) return refuse("texture missing");
            s.images.push_back({fam::kTextureBase + u, {reinterpret_cast<VkSampler>(uintptr_t(im->sampler)),
                                                        reinterpret_cast<VkImageView>(uintptr_t(im->view)), VkImageLayout(im->layout)}});
        }
        std::sort(s.plain.needs.begin(), s.plain.needs.end(), [](const Need& a, const Need& b) { return a.binding < b.binding; });
    }
    if (!s.plain.pipe) return refuse(why ? why : "pipeline");
    if (s.plain.needs.size() > kMaxBind) return refuse("too many blocks");
    // an equal state made before (the description path makes its states again, e.g. at every slice pass's first draw):
    // that one, with its slot sets and instanced variant (one State per distinct set of inputs, no growth per define)
    const uint64_t key = state_key(s);
    for (auto [it, end] = g_byKey.equal_range(key); it != end; ++it)
        if (same_state(*it->second, s)) { ++g_counts.reused; return it->second; }
    ++(s.hand ? g_counts.hand : g_counts.gen);
    g_states.push_back(std::move(s));
    State& st = g_states.back();
    g_byKey.emplace(key, &st);
    if (want_layer_output()) {
        make_inst(st, in, clampVariant);
        if (st.inst.why) ++g_counts.instRefused[st.inst.why];
    }
    return &st;
}

// ---------------------------------------------------------------- per draw
bool bind(State* s, const Draw& d, Binding& out) {
    if (!s) return false;
    auto fail = [](const char* why) { ++g_counts.failed[why]; return false; };
    std::array<Ubo, kMaxBind> ubo;
    uint32_t n = 0;
    const char* why = nullptr;
    for (const Need& need : s->plain.needs)
        if (!shared_ubo(*s, need, d, ubo[n++], &why)) return fail(why);
    const VkDescriptorSet set = slot_set_for(s->plain.sets, s->plain.setLayout, ubo.data(), n, s->images.data(),
                                             uint32_t(s->images.size()));
    if (!set) return fail("descriptor set");
    static std::array<uint32_t, kMaxBind> offsets;
    for (uint32_t i = 0; i < n; ++i) {
        if (ubo[i].offset > UINT32_MAX) return fail("uniform offset");
        offsets[i] = uint32_t(ubo[i].offset);
    }
    out.pipeline = uint64_t(reinterpret_cast<uintptr_t>(s->plain.pipe));
    out.layout = uint64_t(reinterpret_cast<uintptr_t>(s->plain.layout));
    out.set = uint64_t(reinterpret_cast<uintptr_t>(set));
    out.offsets = offsets.data();
    out.offsetCount = n;
    ++g_counts.draws;
    return true;
}

// ---------------------------------------------------------------- proof
bool proof_wanted() { return native_draw::j3d_depth::proof_desc_wanted(); }
void proof(State* s, const ProofIn& p) {
    using namespace gfxvk;
    namespace jd = native_draw::j3d_depth;
    static std::vector<uint32_t> regs;  // (record_translated reads the blend constants and stencil references here)
    if (regs.empty()) regs.assign(0x10000, 0);
    if (p.blend) std::copy_n(p.blend, 4, regs.data() + REGADDR::CB_BLEND_RED);
    regs[REGADDR::DB_STENCILREFMASK] = p.stencilFront;
    regs[REGADDR::DB_STENCILREFMASK_BF] = p.stencilBack;
    jd::ProofDraw d;
    d.info.regs = regs.data();
    d.info.count = p.count;
    d.info.fs = static_cast<const LatteFetchShader*>(p.fs);
    d.info.vs = static_cast<const vk::Shader*>(p.vs);
    d.info.ps = static_cast<const vk::Shader*>(p.ps);
    d.info.depth = static_cast<Surface*>(p.depth);
    d.info.depthSlice = p.depthSlice, d.info.width = p.width, d.info.height = p.height;
    d.info.frame = R.frame;
    d.pipeline = p.pipeline;
    d.layout = p.layout;
    d.sets[0] = p.sets[0], d.sets[1] = p.sets[1];
    d.offsetCount = std::min<uint32_t>(p.offsetCount, 34);
    std::copy_n(p.offsets, d.offsetCount, d.offsets);
    for (uint32_t i = 0; i < p.vbCount && i < 16; ++i) d.vertexBinds[d.vertexBindCount++] = {p.vbBinding[i], p.vb[i], p.vbo[i], 0};
    d.indexed = true;
    d.indexBuffer = p.ib, d.indexOffset = p.ibo, d.indexType = p.indexType;
    d.drawCount = p.count;
    d.viewport = p.viewport, d.scissor = p.scissor;
    for (int i = 0; i < 8; ++i) d.colors[i] = static_cast<Surface*>(p.colors[i]);
    char name[64];
    if (s->hand) std::snprintf(name, sizeof name, "desc:%s", s->hand->name);
    else std::snprintf(name, sizeof name, "desc:%016llx/%016llx", (unsigned long long)s->vsHash, (unsigned long long)s->psHash);
    const Binding nb = p.native;
    std::array<uint32_t, kMaxBind> offs{};
    std::copy_n(nb.offsets, std::min<uint32_t>(nb.offsetCount, kMaxBind), offs.data());
    jd::proof_desc(d, name, [&](VkCommandBuffer cmd) {
        record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, reinterpret_cast<VkPipeline>(nb.pipeline));
        record::vkCmdSetViewport(cmd, 0, 1, &d.viewport);
        record::vkCmdSetScissor(cmd, 0, 1, &d.scissor);
        const VkDescriptorSet set = reinterpret_cast<VkDescriptorSet>(nb.set);
        record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, reinterpret_cast<VkPipelineLayout>(nb.layout), 0, 1,
                                        &set, nb.offsetCount, offs.data());
        for (uint32_t i = 0; i < d.vertexBindCount; ++i)
            record::vkCmdBindVertexBuffers(cmd, d.vertexBinds[i].binding, 1, &d.vertexBinds[i].buffer, &d.vertexBinds[i].offset);
        record::vkCmdBindIndexBuffer(cmd, d.indexBuffer, d.indexOffset, d.indexType);
        record::vkCmdDrawIndexed(cmd, d.drawCount, 1, 0, 0, 0);
        return true;
    });
}

// ---------------------------------------------------------------- shadow-slice instancing
bool g_instPending = false;
bool inst_pending() { return !g_col.groups.empty(); }
uint64_t inst_target() { return g_col.groups.empty() ? 0 : g_col.surface; }
bool inst_wanted(const State* s) { return s && s->inst.pipe && inst_on(); }

bool inst_collect(State* s, const Draw& d, const Geometry& g, const Target& t, const Binding* proof) {
    auto no = [](const char* why) { ++g_counts.instRefused[why]; ++g_counts.notCollected; return false; };
    if (!s || !inst_on() || !s->inst.pipe) return no(s && s->inst.why ? s->inst.why : "no instanced variant");
    if (!g_col.groups.empty() && !same_target(g_col.target, t)) return no("another target (flush first)");
    if (g.vertexCount > 16 || t.layer >= 64) return no("geometry");
    // the shared bindings of this draw, and its per-instance bytes (read now: what the GPU would read at this draw)
    static Group probe;
    probe.state = s;
    probe.geo = g;
    probe.nshared = 0;
    const char* why = nullptr;
    const size_t stageAt = g_col.stage.size();
    for (const Need& n : s->inst.needs) {
        Ubo& u = probe.shared[probe.nshared++];
        if (n.kind == kLayers) continue;
        if (n.kind == kPacked || (n.kind == kHandView)) {
            const uint32_t* b = find_block(d, n.stageLoc);
            const uint32_t size = b ? std::min<uint32_t>(b[2], 0x10000) : 0;
            if (!b || !b[1] || size < n.vec4s * 16 || (n.kind == kHandView && size < 31 * 16)) {
                g_col.stage.resize(stageAt);
                return no("per-instance block");
            }
            const uint8_t* p = host_block(b[1], size);
            if (!p) { g_col.stage.resize(stageAt); return no("per-instance block"); }
            const size_t at = g_col.stage.size();
            g_col.stage.resize(at + n.vec4s * 16);
            if (n.kind == kHandView) {
                std::memcpy(&g_col.stage[at], p, 3 * 16);
                std::memcpy(&g_col.stage[at + 48], p + 27 * 16, 4 * 16);
            } else std::memcpy(&g_col.stage[at], p, n.vec4s * 16);
            continue;
        }
        if (!shared_ubo(*s, n, d, u, &why)) { g_col.stage.resize(stageAt); return no(why); }
    }
    // its group: same state, mesh, geometry binds and shared bindings, and not yet this slice
    const uint64_t key = uint64_t(reinterpret_cast<uintptr_t>(s)) * 0x9E3779B97F4A7C15ull ^ g.mesh;
    std::vector<uint32_t>& list = g_col.byKey[key];
    Group* grp = nullptr;
    for (uint32_t gi : list) {
        Group& c = g_col.groups[gi];
        if (c.state != s || c.count >= kMaxInst || !same_geometry(c.geo, g)) continue;
        bool same = true;
        for (uint32_t i = 0; i < probe.nshared && same; ++i) {
            const Ubo &a = c.shared[i], &b = probe.shared[i];
            same = a.buffer == b.buffer && a.offset == b.offset && a.range == b.range;
        }
        for (uint32_t i = 0; i < c.count && same; ++i) same = c.inst[i].layer != t.layer;
        if (same) { grp = &c; break; }
    }
    if (!grp) {
        list.push_back(uint32_t(g_col.groups.size()));
        g_col.groups.push_back(probe);
        grp = &g_col.groups.back();
        grp->count = 0;
        ++g_counts.groups;
    }
    Entry& e = grp->inst[grp->count++];
    e = Entry{};
    e.layer = t.layer, e.staged = uint32_t(stageAt);
    if (proof && proof->pipeline) {
        e.proof = true;
        e.plainPipe = reinterpret_cast<VkPipeline>(uintptr_t(proof->pipeline));
        e.plainLayout = reinterpret_cast<VkPipelineLayout>(uintptr_t(proof->layout));
        e.plainSet = reinterpret_cast<VkDescriptorSet>(uintptr_t(proof->set));
        e.plainOffsetCount = std::min<uint32_t>(proof->offsetCount, kMaxBind);
        std::copy_n(proof->offsets, e.plainOffsetCount, e.plainOffsets.data());
    }
    if (g_col.groups.size() == 1 && grp->count == 1) g_col.surface = t.surface, g_col.target = t;
    g_col.minLayer = std::min(g_col.minLayer, t.layer);
    g_col.maxLayer = std::max(g_col.maxLayer, t.layer);
    g_col.layers |= 1ull << t.layer;
    g_instPending = true;
    ++g_counts.collected;
    return true;
}

// ---------------------------------------------------------------- the flush points (j3d_desc_depth_inst.h)
// why a draw with this register file does not commute (nullptr: it does)
const char* commute_why(const uint32_t* r) {
    using namespace gfxvk;
    if (g_col.groups.empty()) return nullptr;
    const auto* s = reinterpret_cast<const Surface*>(uintptr_t(g_col.surface));
    if (r[mmDB_DEPTH_BASE] != s->addr) return "other target";
    // no colour written (a colour write's depth test would see the collected draws or not): a bound target (base set,
    // surfaces.cpp color_target) with a write mask
    for (int i = 0; i < 8; ++i)
        if (r[mmCB_COLOR0_BASE + i] && ((r[REGADDR::CB_TARGET_MASK] >> (4 * i)) & 15)) return "colour";
    // depth test + write, LESS (0x16) or LEQUAL (0x36), no stencil: the stored depth is the minimum over the clear value
    // and every fragment that passes, in any order (an equal depth stores equal bits)
    const uint32_t dc = r[REGADDR::DB_DEPTH_CONTROL];
    if (dc != 0x16 && dc != 0x36) return "depth control";
    return nullptr;
}
bool inst_commutes(const uint32_t* r) { return !commute_why(r); }
bool commutes_image(const uint32_t* r) {
    for (int i = 0; i < 8; ++i)
        if (r[mmCB_COLOR0_BASE + i] && ((r[REGADDR::CB_TARGET_MASK] >> (4 * i)) & 15)) return false;
    return r[REGADDR::DB_DEPTH_CONTROL] == 0x16 || r[REGADDR::DB_DEPTH_CONTROL] == 0x36;
}
void inst_before_op(uint32_t op, const uint32_t* p, uint32_t n, const uint32_t* regs) {
    using namespace gx2;
    if (g_col.groups.empty()) return;
    switch (op) {
    // register writes, descriptions and names record nothing; an invalidate of guest memory never reaches a render
    // target (gpu-written surfaces are skipped) and replaced buffer contents get new upload slices
    case OP_NOP: case OP_SET_REGS: case OP_SET_PROJ_REGS: case OP_DEBUG_CONTEXT: case OP_SUBMITTED_UNIFORMS: case OP_CALL:
    case OP_SET_CONTEXT: case OP_INVALIDATE: case OP_PACKET_DEFINE: case OP_J3D_SHAPE: case OP_PASS_BEGIN: case OP_PASS_LOAD:
    case OP_PASS_END: case OP_PASS_COPY: case OP_PASS_VIEWPORT:
    // (expands: below) their own checks: described draws (j3d_desc_vk.h), translated draws (the Vulkan draw, with their final registers),
    // depth clears (inst_before_clear, with the cleared layers)
    case OP_J3D_DESC_DRAW: case OP_DRAW: case OP_DRAW_INDEXED: case OP_CLEAR_DEPTH:
        return;
    case OP_PACKET_DRAW:  // (its effects are blocks, fetch shader and attribute buffers: the targets are the file's)
        if (n < 2 || p[1] == 0xFFFFFFFFu /* native_draw::kNoDraw: effects only */) return;
        if (const char* why = commute_why(regs)) {
            static std::map<uint32_t, uint32_t> said;  // (debug: the first few per depth control word)
            if (said.size() < 16 && !said.count(regs[REGADDR::DB_DEPTH_CONTROL]) && said.emplace(regs[REGADDR::DB_DEPTH_CONTROL], 0).second)
                LOG("[j3d-desc-depth] flush at a packet draw (%s): packet %u kind %u, DB_DEPTH_BASE %08X (cascade %08X), "
                    "DB_DEPTH_CONTROL %08X, CB_TARGET_MASK %08X", why, p[0], p[1], regs[mmDB_DEPTH_BASE],
                    reinterpret_cast<const gfxvk::Surface*>(uintptr_t(g_col.surface))->addr, regs[REGADDR::DB_DEPTH_CONTROL],
                    regs[REGADDR::CB_TARGET_MASK]);
            return inst_flush_now(why[0] == 'o' ? "packet: target" : why[0] == 'c' ? "packet: colour" : "packet: depth");
        }
        return;
    case OP_SWAP: return inst_flush_now("swap");
    case OP_COPY_SURFACE: return inst_flush_now("copy");
    case OP_EXPAND_DEPTH: case OP_EXPAND_COLOR:  // (HiZ / MSAA decompression: nothing on the host, gx2_core.cpp)
        return;
    case OP_CLEAR_COLOR: case OP_CLEAR_BUFFERS: return inst_flush_now("clear");
    case OP_FLUSH: case OP_DRAW_DONE: case OP_FENCE: return inst_flush_now("gpu sync");
    default: return inst_flush_now("other command");
    }
}
void inst_before_clear(const void* surface, uint32_t first, uint32_t num) {
    if (g_col.groups.empty() || uint64_t(reinterpret_cast<uintptr_t>(surface)) != g_col.surface) return;
    const uint64_t mask = num >= 64 ? ~0ull : ((1ull << num) - 1) << std::min<uint32_t>(first, 63);
    if (first < 64 && !(mask & g_col.layers)) return;  // (a slice's own clear before its draws: another layer)
    inst_flush_now("clear");
}
void inst_flush_now(const char* where) {
    if (g_col.groups.empty()) return;
    ++g_flushAt[where];
    gfxvk::end_encoder();
    inst_flush(gfxvk::command_buffer());
}

// the INST proof of a group: each sampled instance drawn into private targets with its plain per-slice binding (FIRST)
// and with the instanced program at that instance with a zero layer table (SECOND), compared word for word
static void inst_proof(const Group& grp, const Target& t, VkDescriptorSet set, const std::array<uint32_t, kMaxBind>& offsets,
                uint32_t nubo, uint32_t layerUbo, uint32_t zeroOffset) {
    using namespace gfxvk;
    namespace jd = native_draw::j3d_depth;
    State& s = *grp.state;
    for (uint32_t i = 0; i < grp.count; ++i) {
        const Entry& e = grp.inst[i];
        if (!e.proof) continue;
        if (s.ncolor) { ++g_counts.instRefused["proof: colour targets"]; continue; }
        jd::ProofDraw d;
        d.info.depth = reinterpret_cast<Surface*>(uintptr_t(t.surface));
        d.info.width = t.width, d.info.height = t.height;
        d.info.count = grp.geo.indexCount;
        d.info.frame = R.frame;
        d.pipeline = e.plainPipe;
        d.indexed = true;
        d.indexBuffer = grp.geo.index, d.indexOffset = grp.geo.indexOffset, d.indexType = grp.geo.indexType;
        d.drawCount = grp.geo.indexCount;
        d.viewport = t.viewport, d.scissor = t.scissor;
        char name[64];
        if (s.hand) std::snprintf(name, sizeof name, "inst:%s", s.hand->name);
        else std::snprintf(name, sizeof name, "inst:%016llx/%016llx", (unsigned long long)s.vsHash, (unsigned long long)s.psHash);
        auto geometry = [&](VkCommandBuffer cmd) {
            record::vkCmdSetViewport(cmd, 0, 1, &t.viewport);
            record::vkCmdSetScissor(cmd, 0, 1, &t.scissor);
            for (uint32_t v = 0; v < grp.geo.vertexCount; ++v)
                record::vkCmdBindVertexBuffers(cmd, grp.geo.vertexBinding[v], 1, &grp.geo.vertex[v], &grp.geo.vertexOffset[v]);
            record::vkCmdBindIndexBuffer(cmd, grp.geo.index, grp.geo.indexOffset, grp.geo.indexType);
        };
        std::array<uint32_t, kMaxBind> zeroOffsets = offsets;
        zeroOffsets[layerUbo] = zeroOffset;
        jd::proof_pair(d, name, [&](VkCommandBuffer cmd) {
            record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e.plainPipe);
            record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e.plainLayout, 0, 1, &e.plainSet,
                                            e.plainOffsetCount, e.plainOffsets.data());
            geometry(cmd);
            record::vkCmdDrawIndexed(cmd, grp.geo.indexCount, 1, 0, grp.geo.baseVertex, 0);
            return true;
        }, [&](VkCommandBuffer cmd) {
            record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.inst.pipe);
            record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.inst.layout, 0, 1, &set, nubo,
                                            zeroOffsets.data());
            geometry(cmd);
            record::vkCmdDrawIndexed(cmd, grp.geo.indexCount, 1, 0, grp.geo.baseVertex, i);  // (gl_InstanceIndex = i)
            return true;
        });
    }
}

void inst_flush(VkCommandBuffer cmd) {
    using namespace gfxvk;
    if (g_col.groups.empty()) return;
    // the collection is drawn from g_flushing: an upload or submission inside this flush finds nothing collected
    std::swap(g_col, g_flushing);
    g_col.clear();
    g_instPending = false;
    Collection& col = g_flushing;
    ++g_counts.flushes;
    const Target& t = col.target;
    Surface* surface = reinterpret_cast<Surface*>(uintptr_t(col.surface));
    const uint32_t base = col.minLayer, count = col.maxLayer - col.minLayer + 1;
    const VkImageView view = array_view(t, base, count);
    const VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT | (t.stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
    const VkPipelineStageFlags tests = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkAccessFlags depthRW = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // the per-instance data of every group in one upload: per group, per packed need n * 16 * kMaxInst bytes, then the
    // layer table (16 * kMaxInst), each at the uniform offset alignment; then one zero layer table (the INST proof)
    const VkDeviceSize align = std::max<VkDeviceSize>(256, R.properties.limits.minUniformBufferOffsetAlignment);
    auto up = [&](VkDeviceSize n) { return (n + align - 1) / align * align; };
    VkDeviceSize total = up(16 * kMaxInst);
    for (const Group& grp : col.groups)
        for (const Need& n : grp.state->inst.needs)
            if (n.kind == kPacked || n.kind == kHandView || n.kind == kLayers) total += up(n.kind == kLayers ? 16 * kMaxInst : n.vec4s * 16 * kMaxInst);
    UploadSlice pack = view ? allocate_upload(total, align) : UploadSlice{};
    if (!view || !pack.mapped) {
        ++g_counts.failed[view ? "instance upload" : "array view"];
        col.clear();  // (nothing drawn: a failed flush loses the shadow casters of this pass; counted, gate must be 0)
        return;
    }
    end_encoder();
    // the cascade as a depth attachment (its tracked layout: a slice's clear leaves it a transfer destination)
    transition_image(surface, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, tests, depthRW);
    {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};  // (the slices' earlier depth writes)
        b.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        b.dstAccessMask = depthRW;
        b.oldLayout = b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = t.image;
        b.subresourceRange = {aspect, 0, 1, base, count};
        record::vkCmdPipelineBarrier(cmd, tests, tests, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {t.width, t.height}};
    ri.layerCount = count;
    ri.pDepthAttachment = &depth;
    ri.pStencilAttachment = t.stencil ? &depth : nullptr;
    record::vkCmdBeginRendering(cmd, &ri);
    record::vkCmdSetViewport(cmd, 0, 1, &t.viewport);
    record::vkCmdSetScissor(cmd, 0, 1, &t.scissor);
    uint8_t* dst = static_cast<uint8_t*>(pack.mapped);
    std::memset(dst, 0, 16 * kMaxInst);  // (the zero layer table)
    const uint32_t zeroAt = uint32_t(pack.offset);
    VkDeviceSize at = up(16 * kMaxInst);
    VkPipeline bound = VK_NULL_HANDLE;
    struct Drawn { uint32_t group; VkDescriptorSet set; std::array<uint32_t, kMaxBind> offsets; uint32_t nubo, layerUbo; };
    static std::vector<Drawn> proofs;
    proofs.clear();
    for (uint32_t gi = 0; gi < col.groups.size(); ++gi) {
        const Group& grp = col.groups[gi];
        State& s = *grp.state;
        std::array<Ubo, kMaxBind> ubo;
        uint32_t nubo = 0, layerUbo = 0;
        uint32_t staged = 0;  // offset inside each instance's staged bytes
        for (uint32_t ni = 0; ni < s.inst.needs.size(); ++ni) {
            const Need& n = s.inst.needs[ni];
            if (n.kind == kPacked || n.kind == kHandView) {
                const uint32_t bytes = n.vec4s * 16;
                for (uint32_t i = 0; i < grp.count; ++i) std::memcpy(dst + at + i * bytes, &col.stage[grp.inst[i].staged + staged], bytes);
                staged += bytes;
                ubo[nubo++] = {n.binding, pack.buffer, pack.offset + at, VkDeviceSize(bytes) * kMaxInst};
                at += up(VkDeviceSize(bytes) * kMaxInst);
            } else if (n.kind == kLayers) {
                int32_t* l = reinterpret_cast<int32_t*>(dst + at);
                std::memset(l, 0, 16 * kMaxInst);
                for (uint32_t i = 0; i < grp.count; ++i) l[i * 4] = int32_t(grp.inst[i].layer - base);
                layerUbo = nubo;
                ubo[nubo++] = {n.binding, pack.buffer, pack.offset + at, 16 * kMaxInst};
                at += up(16 * kMaxInst);
            } else ubo[nubo++] = grp.shared[ni];
        }
        // dynamic offsets carry the per-group part; the set's buffers are the pack and the shared blocks' buffers
        std::array<Ubo, kMaxBind> setUbo = ubo;
        std::array<uint32_t, kMaxBind> offsets{};
        for (uint32_t i = 0; i < nubo; ++i) offsets[i] = uint32_t(ubo[i].offset), setUbo[i].offset = 0;
        const VkDescriptorSet set = slot_set_for(s.inst.sets, s.inst.setLayout, setUbo.data(), nubo, s.images.data(),
                                                 uint32_t(s.images.size()));
        if (!set) { ++g_counts.failed["instanced set"]; continue; }
        if (s.inst.pipe != bound) record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bound = s.inst.pipe);
        record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.inst.layout, 0, 1, &set, nubo, offsets.data());
        for (uint32_t i = 0; i < grp.geo.vertexCount; ++i)
            record::vkCmdBindVertexBuffers(cmd, grp.geo.vertexBinding[i], 1, &grp.geo.vertex[i], &grp.geo.vertexOffset[i]);
        record::vkCmdBindIndexBuffer(cmd, grp.geo.index, grp.geo.indexOffset, grp.geo.indexType);
        record::vkCmdDrawIndexed(cmd, grp.geo.indexCount, grp.count, 0, grp.geo.baseVertex, 0);
        if (grp.count > 1) ++g_counts.instanced;
        ++g_counts.instDraws;
        g_counts.draws += grp.count;
        for (uint32_t i = 0; i < grp.count; ++i)
            if (grp.inst[i].proof) { proofs.push_back({gi, set, offsets, nubo, layerUbo}); break; }
    }
    record::vkCmdEndRendering(cmd);
    mark_gpu_written(surface);
    // the proofs: after the layered pass (each ends its own private pass)
    for (const Drawn& p : proofs) inst_proof(col.groups[p.group], t, p.set, p.offsets, p.nubo, p.layerUbo, zeroAt);
    col.clear();
}

// ---------------------------------------------------------------- counters, reset
void frame_end() {
    if (!on() || R.frame < g_countFrame + 300) return;
    g_countFrame = R.frame;
    auto list = [](const std::map<std::string, uint64_t>& m) {
        std::string s;
        for (auto& [k, n] : m) s += ", " + k + " " + std::to_string(n);
        return s.empty() ? std::string(" none") : s;
    };
    LOG("[j3d-desc-depth] states: %llu hand-written, %llu generated (made so far), reused at %.1f defines/frame; refused%s; "
        "native depth draws %.1f/frame; failed%s; sets written %.1f/frame, own %.1f/frame", (unsigned long long)g_counts.hand,
        (unsigned long long)g_counts.gen, g_counts.reused / 300.0, list(g_counts.refused).c_str(), g_counts.draws / 300.0, list(g_counts.failed).c_str(), g_counts.setsWritten / 300.0,
        g_counts.setsOwn / 300.0);
    if (want_layer_output()) {
        LOG("[j3d-desc-depth] slices: collected %.1f/frame, groups %.1f/frame, instanced draws %.1f/frame (of them with 2+ "
            "slices %.1f), flushes %.1f/frame, not collected %.1f/frame%s", g_counts.collected / 300.0, g_counts.groups / 300.0,
            g_counts.instDraws / 300.0, g_counts.instanced / 300.0, g_counts.flushes / 300.0, g_counts.notCollected / 300.0,
            list(g_counts.instRefused).c_str());
        LOG("[j3d-desc-depth] flush points (300 frames):%s", list(g_flushAt).c_str() + 1);
    }
    g_counts.draws = g_counts.setsWritten = g_counts.setsOwn = g_counts.reused = 0;
    g_counts.collected = g_counts.groups = g_counts.instanced = g_counts.instDraws = g_counts.flushes = g_counts.notCollected = 0;
    g_counts.failed.clear();
    g_counts.instRefused.clear();
    for (auto& [k, n] : g_flushAt) n = 0;
}
void reset() {
    // (a save-state load or shader reset: what is collected is drawn first while a command buffer records, else dropped)
    if (!g_col.groups.empty()) {
        if (gfxvk::R.recording) inst_flush_now("reset");
        else ++g_counts.failed["dropped at reset"], g_col.clear(), g_instPending = false;
    }
    g_col.byKey.clear();
    g_byKey.clear();
    g_states.clear();  // (their slot sets stay allocated in the kept pools; pipelines stay in the family core's cache)
    g_vs.clear();
    g_ps.clear();
}

}  // namespace native_draw::j3ddesc::depth
