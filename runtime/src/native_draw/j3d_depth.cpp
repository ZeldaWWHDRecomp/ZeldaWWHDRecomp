// Native depth shaders for J3D's depth-only pass: the debug tools (j3d_depth.h). Decomp build fast mode,
// WWHD_NATIVE_DRAW=ON only.
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
#include "runtime.h"
#include "gx2/gx2_cmd.h"
#include "fixed_seed.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <map>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace Latte;

namespace native_draw::j3d_depth {
namespace {

uint32_t g_words[5];
bool g_named = false;

struct DumpCfg {
    std::string dir;
    uint64_t from = 0, to = 0, raw = 0;
    FILE* rawFile = nullptr;
    bool done = false;
};
DumpCfg& dump_cfg() {
    static DumpCfg c = [] {
        DumpCfg c;
        const char* e = std::getenv("WWHD_J3D_DEPTH_DUMP");
        if (!e || !*e) return c;
        std::string s(e);
        unsigned long long a = 0, b = ~0ull, w = 0;
        if (const size_t k = s.find(':'); k != std::string::npos) {
            std::sscanf(s.c_str() + k + 1, "%llu:%llu:%llu", &a, &b, &w);
            s.resize(k);
        }
        std::error_code ec;
        std::filesystem::create_directories(s + "/glsl", ec);
        c.dir = s; c.from = a; c.to = b; c.raw = w;
        c.rawFile = std::fopen((s + "/raw.txt").c_str(), "w");
        LOG("[j3d-depth] dump to %s, frames %llu-%llu, raw %llu", s.c_str(), a, b, w);
        return c;
    }();
    return c;
}

uint32_t be32(uint32_t ea) {
    if (ea < 0x10000000u || ea >= 0x50000000u) return 0;
    uint32_t v;
    std::memcpy(&v, mem::ptr(ea), 4);
    return __builtin_bswap32(v);
}
// the program record's agl shader program (+0) -> its shared data (+0x7C) -> name text (+0); flags word at +0
std::string prog_name(uint32_t rec, uint32_t* flags) {
    const uint32_t sp = be32(rec);
    *flags = sp ? be32(sp) : 0;
    const uint32_t st = sp ? be32(sp + 0x7C) : 0;
    const uint32_t t = st ? be32(st) : 0;
    if (!t) return "?";
    std::string n;
    for (uint32_t i = 0; i < 64; ++i) {
        const char ch = char(*mem::ptr(t + i));
        if (!ch) break;
        n += (ch > 32 && ch < 127) ? ch : '_';
    }
    return n.empty() ? "?" : n;
}

// per (vertex shader, pixel shader, pass): what the draws of that pair look like
struct PairStat {
    uint32_t pass = 0;
    std::string name;
    uint32_t flags = 0;
    uint64_t draws = 0, frames = 0, lastFrame = ~0ull;
    std::map<std::string, uint64_t> states, fetches, textures, depths;
    std::unordered_set<uint32_t> bindings, shapes;
};
std::map<std::tuple<uint64_t, uint64_t, uint32_t>, PairStat> g_pairs;
std::unordered_set<uint64_t> g_written;
uint64_t g_frames = 0, g_lastFrame = ~0ull, g_firstFrame = ~0ull, g_draws = 0;

void write_glsl(const gfxvk::vk::Shader* s) {
    if (!s || !g_written.insert(s->key).second) return;
    const DumpCfg& c = dump_cfg();
    char name[64];
    std::snprintf(name, sizeof name, "/glsl/%016llx-%s.glsl", (unsigned long long)s->key, s->vertex ? "vs" : "ps");
    if (FILE* f = std::fopen((c.dir + name).c_str(), "w")) {
        const std::string& t = s->source_text();
        std::fwrite(t.data(), 1, t.size(), f);
        std::fclose(f);
    }
}

std::string fetch_text(const LatteFetchShader* fs, const uint32_t* r) {
    std::string out;
    char b[160];
    if (!fs) return "-";
    for (const auto& g : fs->bufferGroups) {
        std::snprintf(b, sizeof b, "%sbuf%u stride %u:", out.empty() ? "" : " | ", g.attributeBufferIndex,
                      g.getCurrentBufferStride(const_cast<uint32_t*>(r)));
        out += b;
        for (int i = 0; i < g.attribCount; ++i) {
            const auto& a = g.attrib[i];
            std::snprintf(b, sizeof b, " sem%u fmt%u off%u end%u nfa%u s%u ds%u%u%u%u type%u", a.semanticId, uint32_t(a.format),
                          a.offset, uint32_t(a.endianSwap), a.nfa, a.isSigned, a.ds[0], a.ds[1], a.ds[2], a.ds[3],
                          uint32_t(a.fetchType));
            out += b;
        }
    }
    return out.empty() ? "-" : out;
}

std::string shader_text(const gfxvk::vk::Shader* s, const uint32_t* r) {
    if (!s || !s->dec) return "-";
    std::string out;
    char b[160];
    const auto* d = s->dec;
    std::snprintf(b, sizeof b, "mode %u", d->uniformMode);
    out += b;
    for (int i = 0; i < 16; ++i)
        if (s->mapping.uniformBuffersBindingPoint[i] >= 0) { std::snprintf(b, sizeof b, " buf%d", i); out += b; }
    for (const auto& g : d->list_remappedUniformEntries_bufferGroups) {
        std::snprintf(b, sizeof b, " remap(kc%u:", g.kcacheBankIdOffset);
        out += b;
        for (const auto& e : g.entries) { std::snprintf(b, sizeof b, " %u>%u", e.indexOffset, e.mappedIndexOffset); out += b; }
        out += ")";
    }
    const uint32_t texbase = s->vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
    for (int i = 0; i < d->textureUnitListCount; ++i) {
        const uint32_t u = d->textureUnitList[i];
        if (u >= LATTE_NUM_MAX_TEX_UNITS) continue;
        const uint32_t* w = r + texbase + u * 7;
        const uint32_t si = d->textureUnitSamplerAssignment[u];
        const uint32_t* sw = si < 18 ? r + REGADDR::SQ_TEX_SAMPLER_WORD0_0 + ((s->vertex ? 18 : 0) + si) * 3 : nullptr;
        std::snprintf(b, sizeof b, " tex%u(dim%u fmt%02x w%u h%u mip%u samp %08x %08x %08x)", u, w[0] & 7, w[1] >> 20 & 0x3F,
                      (w[0] >> 19 & 0x1FFF) + 1, (w[1] & 0x1FFF) + 1, (w[5] >> 16 & 0xF), sw ? sw[0] : 0, sw ? sw[1] : 0,
                      sw ? sw[2] : 0);
        out += b;
    }
    return out;
}

// the vertex program's uniform blocks (remapped groups and bound buffers): location:size:address:content hash
std::string vs_blocks(const gfxvk::vk::Shader* s, const uint32_t* r) {
    if (!s || !s->dec) return "-";
    std::string out;
    char b[96];
    auto one = [&](uint32_t loc) {
        const uint32_t addr = r[mmSQ_VTX_UNIFORM_BLOCK_START + loc * 7];
        const uint32_t size = std::min<uint32_t>(r[mmSQ_VTX_UNIFORM_BLOCK_START + loc * 7 + 1] + 1, 0x10000);
        const uint8_t* sub = addr ? gx2::submitted_uniform_block(addr, size) : nullptr;
        const uint8_t* src = sub ? sub : addr ? static_cast<const uint8_t*>(mem::ptr(addr)) : nullptr;
        uint64_t h = 0xCBF29CE484222325ull;
        for (uint32_t i = 0; src && i < std::min<uint32_t>(size, 8192); ++i) h = (h ^ src[i]) * 0x100000001B3ull;
        std::snprintf(b, sizeof b, "%s%u:%u:%08x:%08x", out.empty() ? "" : ",", loc, size, addr, uint32_t(h ^ (h >> 32)));
        out += b;
    };
    for (const auto& g : s->dec->list_remappedUniformEntries_bufferGroups) one(g.kcacheBankIdOffset / 28);  // (byte offset of its register words: 7 words per block)
    for (uint32_t i = 0; i < 16; ++i)
        if (s->mapping.uniformBuffersBindingPoint[i] >= 0) one(i);
    return out.empty() ? "-" : out;
}

}  // namespace

void shape(const uint32_t* p, uint32_t n) {
    g_named = n == 5;
    if (g_named) std::memcpy(g_words, p, sizeof g_words);
}

bool dump_on() { return !dump_cfg().dir.empty(); }

// the program record's name of the J3D draw named last (its material's program)
std::string program_name() {
    uint32_t flags = 0;
    return g_named || g_words[4] ? prog_name(g_words[4], &flags) : "?";
}

void note(const DrawInfo& d) {
    DumpCfg& c = dump_cfg();
    if (!g_named) return;
    g_named = false;
    if (c.dir.empty() || c.done) return;
    const uint64_t frame = fixed_seed::script_frame(d.frame);  // (as WWHD_DUMP_FRAMES counts)
    if (frame < c.from) return;
    if (frame >= c.to) {
        c.done = true;
        FILE* f = std::fopen((c.dir + "/pairs.txt").c_str(), "w");
        if (!f) return;
        std::fprintf(f, "S frames %llu draws %llu pairs %zu\n", (unsigned long long)g_frames, (unsigned long long)g_draws,
                     g_pairs.size());
        for (auto& [k, p] : g_pairs) {
            std::fprintf(f, "P %016llx %016llx pass %u draws %llu frames %llu bindings %zu shapes %zu name %s flags %08x\n",
                         (unsigned long long)std::get<0>(k), (unsigned long long)std::get<1>(k), p.pass,
                         (unsigned long long)p.draws, (unsigned long long)p.frames, p.bindings.size(), p.shapes.size(),
                         p.name.c_str(), p.flags);
            for (auto& [s, n] : p.states) std::fprintf(f, "  state %llu %s\n", (unsigned long long)n, s.c_str());
            for (auto& [s, n] : p.fetches) std::fprintf(f, "  fetch %llu %s\n", (unsigned long long)n, s.c_str());
            for (auto& [s, n] : p.textures) std::fprintf(f, "  shaders %llu %s\n", (unsigned long long)n, s.c_str());
            for (auto& [s, n] : p.depths) std::fprintf(f, "  depth %llu %s\n", (unsigned long long)n, s.c_str());
        }
        std::fclose(f);
        if (c.rawFile) std::fclose(c.rawFile);
        c.rawFile = nullptr;
        LOG("[j3d-depth] dump written: %llu frames, %llu draws, %zu program pairs", (unsigned long long)g_frames,
            (unsigned long long)g_draws, g_pairs.size());
        return;
    }
    if (d.frame != g_lastFrame) { g_lastFrame = d.frame; ++g_frames; if (g_firstFrame == ~0ull) g_firstFrame = d.frame; }
    ++g_draws;
    const uint32_t* r = d.regs;
    const uint64_t vk = d.vs ? d.vs->key : 0, pk = d.ps ? d.ps->key : 0;
    PairStat& p = g_pairs[{vk, pk, g_words[2]}];
    if (!p.draws) { p.pass = g_words[2]; p.name = prog_name(g_words[4], &p.flags); }
    ++p.draws;
    if (p.lastFrame != d.frame) { p.lastFrame = d.frame; ++p.frames; }
    p.bindings.insert(g_words[1]);
    p.shapes.insert(g_words[0]);
    write_glsl(d.vs);
    write_glsl(d.ps);
    char b[512];
    std::snprintf(b, sizeof b, "prim %u it %u db %08x cbmask %08x alpha %08x ref %08x mode %08x stencil %08x dbsh %08x clip %08x "
                  "polyoff %08x/%08x/%08x/%08x colctl %08x",
                  d.prim, d.indexType, r[REGADDR::DB_DEPTH_CONTROL], r[REGADDR::CB_TARGET_MASK], r[REGADDR::SX_ALPHA_TEST_CONTROL],
                  r[REGADDR::SX_ALPHA_REF], r[REGADDR::PA_SU_SC_MODE_CNTL], r[REGADDR::DB_STENCILREFMASK], r[mmDB_SHADER_CONTROL],
                  r[REGADDR::PA_CL_CLIP_CNTL], r[REGADDR::PA_SU_POLY_OFFSET_FRONT_SCALE], r[REGADDR::PA_SU_POLY_OFFSET_FRONT_OFFSET],
                  r[REGADDR::PA_SU_POLY_OFFSET_BACK_SCALE], r[REGADDR::PA_SU_POLY_OFFSET_BACK_OFFSET], r[REGADDR::CB_COLOR_CONTROL]);
    ++p.states[b];
    ++p.fetches[fetch_text(d.fs, r)];
    ++p.textures["vs " + shader_text(d.vs, r) + " || ps " + shader_text(d.ps, r)];
    if (d.depth) {
        std::snprintf(b, sizeof b, "%ux%u slices %u fmt %u vkfmt %d target %ux%u", d.depth->width, d.depth->height,
                      d.depth->slices, d.depth->format, int(d.depth->fmt.pixel), d.width, d.height);
        ++p.depths[b];
    } else ++p.depths["none"];
    if (c.rawFile && d.frame - g_firstFrame < c.raw)
        std::fprintf(c.rawFile, "R %llu %08x %08x %u %u %08x %016llx %016llx %u %u %08x %08x %u %08x %s\n",
                     (unsigned long long)(d.frame - g_firstFrame), g_words[0], g_words[1], g_words[2], g_words[3], g_words[4],
                     (unsigned long long)vk, (unsigned long long)pk, d.count, d.depthSlice, r[mmDB_DEPTH_BASE],
                     r[mmDB_DEPTH_VIEW], d.baseVertex, d.indexAddr, vs_blocks(d.vs, r).c_str());
}


// ---------------------------------------------------------------- proof harness (WWHD_J3D_DEPTH_PROOF)
const char* family_of(const ProofDraw& d, std::string& name);  // j3d_depth_family.cpp
bool record_native(VkCommandBuffer cmd, const ProofDraw& d, bool lod0);  // j3d_depth_family.cpp
bool record_translated_lod0(VkCommandBuffer cmd, const ProofDraw& d);    // j3d_depth_family.cpp
bool samples_texture(const ProofDraw& d);                                 // j3d_depth_family.cpp
bool has_native(const std::string& name);                       // j3d_depth_family.cpp
void report_unknown(const std::string& dir);                    // j3d_depth_family.cpp
namespace {
using gfxvk::Surface;
using gfxvk::R;

struct ProofCfg {
    uint32_t every = 0;
    std::string dir;
    bool self = false;  // WWHD_J3D_DEPTH_PROOF_SELF=1: the second proof draw is the translated one again (harness check)
    bool lod0 = false;  // WWHD_J3D_DEPTH_PROOF_LOD0=1: texture-reading draws compared with both programs reading LOD 0
    uint32_t maxPending = 64;
};
const ProofCfg& proof_cfg() {
    static const ProofCfg c = [] {
        ProofCfg c;
        const char* e = std::getenv("WWHD_J3D_DEPTH_PROOF");
        if (!e || !*e) return c;
        c.every = uint32_t(std::max(1, std::atoi(e)));
        if (const char* k = std::strchr(e, ':')) c.dir = k + 1;
        if (!c.dir.empty()) { std::error_code ec; std::filesystem::create_directories(c.dir, ec); }
        const char* sf = std::getenv("WWHD_J3D_DEPTH_PROOF_SELF");
        c.self = sf && std::atoi(sf);
        const char* l0 = std::getenv("WWHD_J3D_DEPTH_PROOF_LOD0");
        c.lod0 = l0 && std::atoi(l0);
        LOG("[j3d-depth] proof: every %u depth-only J3D draw%s%s", c.every, c.self ? " (harness check: translated twice)" : "",
            c.dir.empty() ? "" : (", mismatches to " + c.dir).c_str());
        return c;
    }();
    return c;
}

// per family: samples, samples drawn both ways and compared, covered texels, mismatching samples / texels
struct FamilyStat { uint64_t samples = 0, compared = 0, covered = 0, mismatchSamples = 0, mismatchTexels = 0, maxUlps = 0; };
std::map<std::string, FamilyStat> g_stats;     // (render thread; completions run on it too)
std::map<std::string, uint64_t> g_skipped;    // samples not drawn twice: why
uint32_t g_counter = 0, g_pending = 0, g_dumped = 0;
uint64_t g_reportFrame = 0;

void report(uint64_t frame) {
    if (frame < g_reportFrame + 300) return;
    g_reportFrame = frame;
    for (auto& [f, st] : g_stats)
        LOG("[j3d-depth] proof %s: samples %llu, compared %llu, covered texels %llu, MISMATCHING samples %llu (texels %llu, "
            "max %llu ulps)", f.c_str(), (unsigned long long)st.samples, (unsigned long long)st.compared,
            (unsigned long long)st.covered, (unsigned long long)st.mismatchSamples, (unsigned long long)st.mismatchTexels,
            (unsigned long long)st.maxUlps);
    for (auto& [why, n] : g_skipped) LOG("[j3d-depth] proof skipped: %s %llu", why.c_str(), (unsigned long long)n);
    report_unknown(proof_cfg().dir);
}

// private targets: (format, size) -> two depth surfaces (+ colour surfaces of the draw's colour formats)
struct Targets { std::unique_ptr<Surface> depth[2]; std::unique_ptr<Surface> color[2][8]; };
std::map<std::tuple<int, uint32_t, uint32_t, uint64_t>, Targets> g_targets;
std::unique_ptr<Surface> private_surface(const Surface& like, uint32_t w, uint32_t h) {
    auto s = std::make_unique<Surface>();
    s->format = like.format;
    s->fmt = like.fmt;
    s->isDepth = like.isDepth;
    s->dim = 1;  // 2D
    s->width = w; s->height = h; s->slices = 1; s->mips = 1;
    gfxvk::create_surface_image(s.get(), true, {w, h, 1});
    return s;
}
Targets& targets_for(const ProofDraw& d) {
    uint64_t colorKey = 0;
    for (int i = 0; i < 8; ++i) colorKey = colorKey * 131 + (d.colors[i] ? uint64_t(d.colors[i]->fmt.pixel) + 1 : 0);
    const uint32_t w = d.info.width, h = d.info.height;
    Targets& t = g_targets[{int(d.info.depth->fmt.pixel), w, h, colorKey}];
    if (!t.depth[0])
        for (int k = 0; k < 2; ++k) {
            t.depth[k] = private_surface(*d.info.depth, w, h);
            for (int i = 0; i < 8; ++i) if (d.colors[i]) t.color[k][i] = private_surface(*d.colors[i], w, h);
        }
    return t;
}

// one proof draw into target K: CLEAR (depth 1.0), the draw, then a copy of the depth image into BUF at OFFSET
using Record = std::function<void(VkCommandBuffer)>;
void draw_into(Targets& t, int k, const ProofDraw& d, const Record& rec, gfxvk::Buffer& buf, VkDeviceSize offset) {
    using namespace gfxvk;
    Surface* depth = t.depth[k].get();
    transition_image(depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    std::array<VkRenderingAttachmentInfo, 8> att{};
    uint32_t ncolor = 0;
    for (int i = 0; i < 8; ++i) {
        att[i] = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        att[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (Surface* c = t.color[k][i].get()) {
            transition_image(c, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            att[i].imageView = layer_view(c, 0);
            ncolor = i + 1;
        }
    }
    VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    da.imageView = layer_view(depth, 0);
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    da.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.extent = {d.info.width, d.info.height};
    ri.layerCount = 1;
    ri.colorAttachmentCount = ncolor;
    ri.pColorAttachments = att.data();
    ri.pDepthAttachment = &da;
    ri.pStencilAttachment = depth->fmt.stencil ? &da : nullptr;
    VkCommandBuffer cmd = command_buffer();
    begin_pass(cmd, ri, [&] {
        PassBegin pass;
        pass.formats.colorCount = ncolor;
        for (uint32_t i = 0; i < ncolor; ++i)
            if (Surface* c = t.color[k][i].get()) { pass.formats.colors[i] = c->fmt.pixel; pass.layouts[i] = c->layout; }
        pass.formats.depth = depth->fmt.pixel;
        pass.formats.stencil = depth->fmt.stencil;
        pass.layouts[8] = depth->layout;
        return pass;
    });
    rec(cmd);
    end_pass(cmd);
    transition_image(depth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.bufferOffset = offset;
    region.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    region.imageExtent = {d.info.width, d.info.height, 1};
    record::vkCmdCopyImageToBuffer(cmd, depth->image, depth->layout, buf.buffer, 1, &region);
}

// the draw's own translated programs and binds, as it drew them
void record_translated(VkCommandBuffer cmd, const ProofDraw& d) {
    using namespace gfxvk;
    const uint32_t* r = d.info.regs;
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d.pipeline);
    record::vkCmdSetViewport(cmd, 0, 1, &d.viewport);
    record::vkCmdSetScissor(cmd, 0, 1, &d.scissor);
    record::vkCmdSetBlendConstants(cmd, reinterpret_cast<const float*>(r + REGADDR::CB_BLEND_RED));
    record::vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_BIT, r[REGADDR::DB_STENCILREFMASK] & 255);
    record::vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_BACK_BIT, r[REGADDR::DB_STENCILREFMASK_BF] & 255);
    record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d.layout, 0, 2, d.sets, d.offsetCount, d.offsets);
    for (uint32_t i = 0; i < d.vertexBindCount; ++i)
        record::vkCmdBindVertexBuffers(cmd, d.vertexBinds[i].binding, 1, &d.vertexBinds[i].buffer, &d.vertexBinds[i].offset);
    record::vkCmdBindIndexBuffer(cmd, d.indexBuffer, d.indexOffset, d.indexType);
    record::vkCmdDrawIndexed(cmd, d.drawCount, 1, 0, int32_t(d.info.baseVertex), 0);
}

// ordered distance between two depth words of FORMAT (float formats: by their bits, which order like the values for
// non-negative depth; unorm: the integer)
uint64_t ulps(uint32_t a, uint32_t b) { return a > b ? uint64_t(a - b) : uint64_t(b - a); }

}  // namespace

namespace {
void run_pair(const ProofDraw& d, const std::string& family, const std::function<bool(VkCommandBuffer)>& first,
              const std::function<bool(VkCommandBuffer)>& second);
}
bool proof_wanted() {
    const ProofCfg& c = proof_cfg();
    if (!c.every || !g_named || g_words[2] != 0) return false;
    return ++g_counter % c.every == 0 && g_pending < c.maxPending;
}


void proof(const ProofDraw& d) {
    using namespace gfxvk;
    const ProofCfg& c = proof_cfg();
    const uint32_t* r = d.info.regs;
    report(d.info.frame);
    auto skip = [](const char* why) { ++g_skipped[why]; };
    // the depth-only state the native family stands for (the sea's 20 colour draws in pass 0 stay translated)
    if (r[REGADDR::CB_TARGET_MASK] != 0) return skip("colour writes on");
    if (r[REGADDR::DB_DEPTH_CONTROL] != 0x16) return skip("depth control other than test + write, LESS, no stencil");
    if (d.refused) return skip(d.refused);
    if (!d.indexed || !d.pipeline || !d.info.depth || !d.info.depth->fmt.depth) return skip("draw form");
    std::string family;
    if (const char* why = family_of(d, family)) return skip(why);
    const bool lod = c.lod0 && samples_texture(d);  // (the tolerance rule's proof: both programs read LOD 0)
    if (lod) family += " @LOD0";
    FamilyStat& st = g_stats[family];
    ++st.samples;
    if (!c.self && !has_native(family)) return;  // (counted: a structure without a native program yet)
    run_pair(d, family, [&](VkCommandBuffer cmd) {
        if (lod) return record_translated_lod0(cmd, d);
        record_translated(cmd, d);
        return true;
    }, [&](VkCommandBuffer cmd) {
        if (c.self) {
            if (lod) return record_translated_lod0(cmd, d);
            record_translated(cmd, d);
            return true;
        }
        return record_native(cmd, d, lod);
    });
}

// a described depth draw (j3d_desc_depth.h, WWHD_NATIVE_J3D_DESC_DEPTH=1): D's translated binds against the description
// path's native binding (SECOND records it on the same streams); WWHD_J3D_DEPTH_PROOF_SELF=1: translated twice
bool proof_desc_wanted() {
    const ProofCfg& c = proof_cfg();
    if (!c.every) return false;
    return ++g_counter % c.every == 0 && g_pending < c.maxPending;
}
void proof_desc(const ProofDraw& d, const std::string& family, const std::function<bool(VkCommandBuffer)>& second) {
    const ProofCfg& c = proof_cfg();
    report(d.info.frame);
    if (!d.indexed || !d.pipeline || !d.info.depth || !d.info.depth->fmt.depth) { ++g_skipped["draw form"]; return; }
    ++g_stats[family].samples;
    run_pair(d, family, [&](VkCommandBuffer cmd) { record_translated(cmd, d); return true; },
             [&](VkCommandBuffer cmd) {
                 if (c.self) { record_translated(cmd, d); return true; }
                 return second(cmd);
             });
}
// two recorders of one draw (the instanced program against the per-slice native one): FIRST into target 0, SECOND into 1
void proof_pair(const ProofDraw& d, const std::string& family, const std::function<bool(VkCommandBuffer)>& first,
                const std::function<bool(VkCommandBuffer)>& second) {
    report(d.info.frame);
    if (!d.indexed || !d.info.depth || !d.info.depth->fmt.depth) { ++g_skipped["draw form"]; return; }
    ++g_stats[family].samples;
    run_pair(d, family, first, second);
}

namespace {
void run_pair(const ProofDraw& d, const std::string& family, const std::function<bool(VkCommandBuffer)>& first,
              const std::function<bool(VkCommandBuffer)>& second) {
    using namespace gfxvk;
    const ProofCfg& c = proof_cfg();
    Targets& t = targets_for(d);
    const uint32_t texels = d.info.width * d.info.height;
    const bool d16 = d.info.depth->fmt.pixel == VK_FORMAT_D16_UNORM;  // (the cascade array: 1024x1024x3 D16)
    const VkDeviceSize bytes = (VkDeviceSize(texels) * (d16 ? 2 : 4) + 255) & ~VkDeviceSize(255);
    end_encoder();  // (the next draw reopens its pass with LOAD)
    Buffer buf = create_readback_buffer(bytes * 2);
    bool nativeOk = true;
    draw_into(t, 0, d, [&](VkCommandBuffer cmd) { nativeOk = first(cmd); }, buf, 0);
    draw_into(t, 1, d, [&](VkCommandBuffer cmd) {
        if (nativeOk) nativeOk = second(cmd);
    }, buf, bytes);
    derive_dependency(buf.use, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    transition_buffer(buf, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
    ++g_pending;
    const uint32_t w = d.info.width, h = d.info.height;
    const int format = int(d.info.depth->fmt.pixel);
    const uint64_t frame = d.info.frame;
    std::string dir = c.dir;
    R.completions.push_back([buf, texels, bytes, d16, w, h, format, family, nativeOk, frame, dir] {
        --g_pending;
        FamilyStat& st = g_stats[family];
        if (!nativeOk) { ++g_skipped["proof draw not recorded: " + family]; return; }
        const uint8_t* base = static_cast<const uint8_t*>(buf.mapped);
        auto word = [&](int k, uint32_t i) -> uint32_t {
            const uint8_t* p = base + VkDeviceSize(k) * bytes;
            if (d16) { uint16_t v; std::memcpy(&v, p + size_t(i) * 2, 2); return v; }
            uint32_t v; std::memcpy(&v, p + size_t(i) * 4, 4); return v;
        };
        uint64_t covered = 0, bad = 0, worst = 0;
        // D24S8 copies hold the depth in the low 24 bits; the high byte is undefined
        const uint32_t mask = format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_X8_D24_UNORM_PACK32 ? 0xFFFFFFu : ~0u;
        const uint32_t clear = d16 ? 0xFFFFu : format == VK_FORMAT_D24_UNORM_S8_UINT ? 0xFFFFFFu : 0x3F800000u;
        for (uint32_t i = 0; i < texels; ++i) {
            const uint32_t x = word(0, i) & mask, y = word(1, i) & mask;
            covered += x != clear || y != clear;
            if (x != y) { ++bad; worst = std::max(worst, ulps(x, y)); }
        }
        ++st.compared;
        st.covered += covered;
        if (bad) {
            ++st.mismatchSamples;
            st.mismatchTexels += bad;
            st.maxUlps = std::max(st.maxUlps, worst);
            if (!dir.empty() && g_dumped < 16) {
                char name[256];
                std::snprintf(name, sizeof name, "%s/mismatch-%llu-%u-%s.bin", dir.c_str(), (unsigned long long)frame, g_dumped++,
                              family.c_str());
                if (FILE* f = std::fopen(name, "wb")) {
                    const uint32_t hdr[3] = {w, h, uint32_t(format)};
                    std::fwrite(hdr, 4, 3, f);
                    std::fwrite(base, 1, size_t(bytes) * 2, f);
                    std::fclose(f);
                }
            }
        }
    });
    defer_buffer(buf);
}
}  // namespace

}  // namespace native_draw::j3d_depth
