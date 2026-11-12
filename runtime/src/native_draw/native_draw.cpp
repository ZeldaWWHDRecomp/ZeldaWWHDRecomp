// Native draw packets (gx2/native_draw.h): the decompiled sites' entry points (game thread) and the render thread's
// packet table and dispatch. Decomp build, fast mode, WWHD_NATIVE_DRAW=ON only.
#include "../../third_party/cemu/cemu_shim.h"  // (as the gx2 sources get it: -include; for gx2_regs.h)
#include "gx2/native_draw.h"
#include "gx2/native_coverage.h"
#include "gx2/gx2_cmd.h"
#include "gx2/gx2_regs.h"
#include "runtime.h"
#include "framegen.h"
#include "cloud_proto.h"
#include "mods/mod_actor_shadow_proof.h"
#include "gfx/renderer.h"
#include "gfx/stereo.h"
#include "aspect.h"
#ifdef WWHD_HAS_VULKAN
#include "gfx/vulkan/api.h"
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <string>

namespace gfxvk {
bool gamepad_backdrop_capture_active();
}

namespace native_draw {

uint64_t g_epoch = 1;
RegChange g_journal[kJournal];
uint64_t g_journalPos = 0, g_journalBumps = 0;

// ---------------------------------------------------------------- switches
static int env_int(const char* name, int fallback) {
    const char* e = std::getenv(name);
    return e && *e ? std::atoi(e) : fallback;
}
static const char* const kSiteNames[kSites] = {"", "grass", "tree", "wood", "flower", "magma",
                                               "star", "housi", "wave", "moya", "vrkumo", "rain", "sea", "line", "jpa",
                                               "layout", "text", "post", "shadow", "aglquad", "aglpair", "sun"};
static bool master_on() {
    static const bool on = env_int("WWHD_NATIVE_DRAW", 0) != 0;
    return on;
}
// WWHD_NATIVE_DRAW=1 and WWHD_NATIVE_DRAW_<SITE> (default 1)
static bool site_on(uint32_t site) {
    static const std::array<bool, kSites> on = [] {
        std::array<bool, kSites> v{};
        char line[400];
        int k = std::snprintf(line, sizeof line, "[native-draw] packets %s:", master_on() ? "on (WWHD_NATIVE_DRAW=1)"
                                                                                       : "off (WWHD_NATIVE_DRAW=0)");
        for (uint32_t s = 1; s < kSites; ++s) {
            char name[48] = "WWHD_NATIVE_DRAW_";
            for (const char* c = kSiteNames[s]; *c && std::strlen(name) < sizeof name - 1; ++c) {
                const size_t n = std::strlen(name);
                name[n] = char(*c - 'a' + 'A');
                name[n + 1] = 0;
            }
            v[s] = master_on() && env_int(name, 1) != 0;
            if (master_on()) k += std::snprintf(line + k, sizeof line - k, " %s %s", kSiteNames[s], v[s] ? "on" : "off");
        }
        LOG("%s", line);
        return v;
    }();
    return site < kSites && on[site];
}
// shader-rewrite batches (native_draw.h): on by default in this build (cmake WWHD_NATIVE_DRAW_BATCH sets
// WWHD_NATIVE_DRAW_BATCH_DEFAULT=1), WWHD_NATIVE_DRAW_BATCH=0/1 overrides
#ifndef WWHD_NATIVE_DRAW_BATCH_DEFAULT
#define WWHD_NATIVE_DRAW_BATCH_DEFAULT 0
#endif
bool batch_on() {
    static const bool on = [] {
        const bool v = master_on() && env_int("WWHD_NATIVE_DRAW_BATCH", WWHD_NATIVE_DRAW_BATCH_DEFAULT) != 0;
        if (master_on()) LOG("[native-draw] shader-rewrite batches %s (WWHD_NATIVE_DRAW_BATCH=%d)", v ? "on" : "off", v ? 1 : 0);
        return v;
    }();
    return on;
}
// the sites whose kinds have the LOD-0 proof (native_draw.h): WWHD_NATIVE_DRAW_BATCH_SITES, a comma list of site
// names (default "grass,flower,vrkumo,jpa"; any case)
bool batch_site_on(uint32_t site) {
    static const std::array<bool, kSites> on = [] {
        std::array<bool, kSites> v{};
        const char* e = std::getenv("WWHD_NATIVE_DRAW_BATCH_SITES");
        std::string list = e ? e : "grass,flower,vrkumo,jpa";
        for (auto& c : list) c = char(std::tolower(uint8_t(c)));
        std::string names;
        for (uint32_t s = 1; s < kSites; ++s) {
            const std::string n = kSiteNames[s];
            for (size_t at = list.find(n); at != std::string::npos; at = list.find(n, at + 1))
                if ((at == 0 || list[at - 1] == ',') && (at + n.size() == list.size() || list[at + n.size()] == ','))
                    v[s] = true;
            if (v[s]) names += (names.empty() ? "" : ",") + n;
        }
        if (batch_on()) LOG("[native-draw] shader-rewrite batch sites: %s", names.empty() ? "none" : names.c_str());
        return v;
    }();
    return site < kSites && on[site];
}
bool j3d_on() {
    static const bool on = [] {
        const bool v = master_on() && env_int("WWHD_NATIVE_DRAW_J3D", 0) != 0;
        if (master_on()) LOG("[native-draw] J3D material cache %s (WWHD_NATIVE_DRAW_J3D=%d)", v ? "on" : "off", v ? 1 : 0);
        return v;
    }();
    return on;
}
uint32_t verify_every() {
    static const uint32_t n = uint32_t(std::max(0, env_int("WWHD_NATIVE_DRAW_VERIFY", 0)));
    return n;
}

// ---------------------------------------------------------------- game thread: the sites
// j3d::RenderUniformBindEntries (JSystem/J3DGraph/RenderUniformBindings.cpp): a RenderUniformPair's bind (its vtable
// +0x2C) binds the pair's previous buffer (mBuffers at +0x10, 28 bytes each: CPU storage +4, size +12; mPreviousIndex
// +0x4C) at the locations of one slot of the material's table (RenderUniformLocations_l: count +12, bindings +16,
// 20 bytes each: vertex +12, pixel +14, geometry +16), in the order pixel, vertex, geometry
static int pair_slot(uint32_t fn) {
    switch (fn) {
    case 0x027FF75C: return 0;  // bindFrame
    case 0x027FF88C: return 2;  // bindMatrix
    case 0x027FF9C0: return 3;  // bindMaterial
    case 0x027FFB48: return 4;  // bindScene
    default: return -1;
    }
}
static bool slot_locations(uint32_t locations, uint32_t slot, int32_t loc[3]) {
    if (!locations || ld32(locations + 12) <= slot) return false;  // (the bind then reads guest address 0's record)
    const uint32_t entry = ld32(locations + 16) + slot * 20;
    for (int i = 0; i < 3; ++i) loc[i] = int16_t(ld16(entry + 12 + 2 * i));
    return true;
}
struct SiteState {
    bool open = false, defined = false;
    Packet pk;
    uint32_t locations = 0;  // the material's location table (pair binds)
    // the pending instance: its effects since the last instance, in the site's order
    uint32_t words[kMaxDrawWords];
    uint32_t blocks = 0;
    bool vertex = false, noFetch = false;  // (noFetch: attribute buffers alone, no fetch shader words)
    uint32_t buffers = 0;
    uint32_t vertexWords[kVertexHead + kMaxBuffers * kBufferWords];
    uint32_t regRanges = 0, regUsed = 0;  // the register values: ranges, words used in regWords (with their heads)
    uint32_t regWords[kMaxRegRanges * kRegHead + kMaxRegWords];
    // the packet last emitted (OP_PACKET_DEFINE) and the command stream segment it went into (list_serial)
    Packet emitted;
    uint64_t emittedSerial = 0;
};
static thread_local SiteState t_sites[kSites];
static SiteState* site_state(uint32_t site) { return site < kSites ? &t_sites[site] : nullptr; }

// the pending effects as one instance (KIND, or kNoDraw: effects only); RANGE: its own index range {count, address}
static void emit_instance(SiteState& s, uint32_t kind, const uint32_t* range = nullptr) {
    uint32_t n = kDrawHead + s.blocks * kBlockWords;
    s.words[0] = s.pk.id;
    s.words[1] = kind;
    s.words[2] = s.blocks | uint32_t(s.vertex) << 8 | uint32_t(s.vertex && s.noFetch) << 9 | s.buffers << 12 |
                 uint32_t(range != nullptr) << 16 | s.regRanges << 20;
    if (s.vertex) {
        const uint32_t head = s.noFetch ? 0 : kVertexHead, v = head + s.buffers * kBufferWords;
        std::memcpy(s.words + n, s.vertexWords + (kVertexHead - head), v * 4);
        n += v;
    }
    if (range) {
        s.words[n++] = range[0];
        s.words[n++] = range[1];
    }
    if (s.regRanges) {
        std::memcpy(s.words + n, s.regWords, s.regUsed * 4);
        n += s.regUsed;
    }
    gx2::emit(gx2::OP_PACKET_DRAW, s.words, n);
    if (kind != kNoDraw)  // (coverage: the instance's site in the "lr" field; its mesh or own index range)
        NATIVE_COVERAGE(kPacket, s.pk.site, range ? range[0] : s.pk.mesh[kind].count,
                        range ? range[1] : s.pk.mesh[kind].indexAddr);
    s.blocks = 0;
    s.vertex = s.noFetch = false;
    s.buffers = 0;
    s.regRanges = s.regUsed = 0;
}
static void flush(SiteState& s) {
    if (s.defined && (s.blocks || s.vertex || s.regRanges)) emit_instance(s, kNoDraw);
}
static bool declared(const Packet& pk, uint32_t key) {
    for (uint32_t i = 0; i < pk.blockCount; ++i)
        if (pk.block[i] == key) return true;
    return false;
}
static void add_block(SiteState& s, uint32_t key, uint32_t addr, uint32_t size) {
    if (s.blocks == kMaxBlocks) flush(s);
    uint32_t* w = s.words + kDrawHead + s.blocks++ * kBlockWords;
    w[0] = key;
    w[1] = addr;
    w[2] = size;
    gx2::note_uniform_block(addr, size);  // as the GX2Set*UniformBlock call would
}

}  // namespace native_draw

using namespace native_draw;

// the generic site entry points (decomp: port_native_draw.h)
extern "C" unsigned wwhd_native_begin(unsigned site, unsigned pass, unsigned variant, unsigned material,
                                      unsigned program, unsigned texture) {
    SiteState* s = site_state(site);
    if (!s) return 0;
    s->open = s->defined = false;
    s->blocks = s->buffers = 0;
    s->vertex = s->noFetch = false;
    s->regRanges = s->regUsed = 0;
    const uint32_t slot = variant * 8 + pass;
    if (!site_on(site) || pass >= 8 || slot >= kIdsPerSite) return 0;
    Packet& pk = s->pk;
    pk = Packet{};
    pk.id = site * kIdsPerSite + slot;
    pk.site = site;
    pk.pass = pass;
    pk.variant = variant;
    pk.material = material;
    pk.program = program;
    pk.texture = texture;
    s->locations = 0;
    s->open = true;
    return 1;
}
// the instance blocks of a pair bind slot (RenderUniformBindEntries) of the location table LOCATIONS. Zero: not
// declared (the slot is not in the table, another table, or too many blocks); the site's binds of it stay its own
extern "C" unsigned wwhd_native_pair_slot(unsigned site, unsigned locations, unsigned slot) {
    SiteState* s = site_state(site);
    if (!s || !s->open || s->defined) return 0;
    int32_t loc[3];
    if ((s->locations && s->locations != locations) || !slot_locations(locations, slot, loc)) return 0;
    uint32_t keys[3], n = 0, add = 0;
    for (uint32_t st : {1u, 0u, 2u})
        if (loc[st] != -1) {
            keys[n] = block_key(st, uint32_t(loc[st]));
            add += !declared(s->pk, keys[n]);
            ++n;
        }
    if (s->pk.blockCount + add > kMaxBlocks) return 0;
    for (uint32_t i = 0; i < n; ++i)
        if (!declared(s->pk, keys[i])) s->pk.block[s->pk.blockCount++] = keys[i];
    s->locations = locations;
    return 1;
}
// the instance blocks of uniform pair PAIR's bind (its vtable +0x2C) with the location table LOCATIONS: the slot its
// bind function binds. Zero: not declared (another bind function, or as wwhd_native_pair_slot)
extern "C" unsigned wwhd_native_declare_pair(unsigned site, unsigned pair, unsigned locations) {
    const uint32_t vt = pair ? ld32(pair + 0xC) : 0;
    const int slot = vt ? pair_slot(ld32(vt + 0x2C)) : -1;
    return slot >= 0 ? wwhd_native_pair_slot(site, locations, uint32_t(slot)) : 0;
}
// an instance block bound by a direct GX2Set{Vertex,Pixel,Geometry}UniformBlock call (stage 0, 1, 2)
extern "C" unsigned wwhd_native_declare_block(unsigned site, unsigned stage, unsigned location) {
    SiteState* s = site_state(site);
    if (!s || !s->open || s->defined || stage > 2) return 0;
    const uint32_t key = block_key(stage, location);
    if (declared(s->pk, key)) return 1;
    if (s->pk.blockCount == kMaxBlocks) return s->open = false, 0;
    s->pk.block[s->pk.blockCount++] = key;
    return 1;
}
extern "C" void wwhd_native_mesh(unsigned site, unsigned kind, unsigned prim, unsigned count, unsigned indexType,
                                 unsigned indexAddr) {
    SiteState* s = site_state(site);
    if (!s || !s->open || s->defined) return;
    if (kind >= kMaxKinds) { s->open = false; return; }
    // (index address 0: a non-indexed draw, GX2DrawEx with first vertex 0; its index type is 0, as OP_DRAW draws it)
    s->pk.mesh[kind] = {prim, count, indexAddr ? indexType : 0u, indexAddr};
    s->pk.meshCount = std::max<uint32_t>(s->pk.meshCount, kind + 1);
}
// the packet is complete: defined (OP_PACKET_DEFINE). Nonzero: the site's draws become its instances
extern "C" unsigned wwhd_native_commit(unsigned site) {
    SiteState* s = site_state(site);
    if (!s || !s->open || !s->pk.meshCount) return s ? s->open = false, 0 : 0;
    static_assert(sizeof(Packet) == kPacketWords * 4);
    // (the same definition in the same command stream segment: already held by the render thread when the instances
    // come)
    const uint64_t serial = list_serial();
    if (serial != s->emittedSerial || std::memcmp(&s->emitted, &s->pk, sizeof(Packet)) != 0) {
        gx2::emit(gx2::OP_PACKET_DEFINE, reinterpret_cast<const uint32_t*>(&s->pk), kPacketWords);
        s->emitted = s->pk;
        s->emittedSerial = serial;
    }
    s->defined = true;
    return 1;
}
// an instance register range (before the commit): GX2Set{Vertex,Pixel}UniformReg(OFFSET, COUNT, ...) calls of STAGE
// (0 vertex, 1 pixel) inside it become instance values. Zero: not declared (an absent location, or too many ranges)
extern "C" unsigned wwhd_native_declare_regs(unsigned site, unsigned stage, unsigned offset, unsigned count) {
    SiteState* s = site_state(site);
    if (!s || !s->open || s->defined || stage > 1 || (offset & 0x8000)) return 0;
    count &= ~3u;  // (as the GX2 call: uniform_regs)
    if (offset >= 0x400 || !count) return 0;
    count = std::min<uint32_t>(count, 0x400 - offset);
    if (reg_declared(s->pk, stage, offset, count)) return 1;
    if (s->pk.regCount == kMaxRegRanges) return 0;
    s->pk.reg[s->pk.regCount] = reg_key(stage, offset);
    s->pk.regWords[s->pk.regCount++] = count;
    return 1;
}
// a checked register range (before the commit): WHAT 0 pixel texture unit INDEX (its resource and sampler words), 1
// blend target INDEX (CB_BLEND<INDEX>_CONTROL), 2 attribute buffer INDEX. The site's own calls setting them between
// instances keep the lowered state; a replay compares them. Zero: not declared
extern "C" unsigned wwhd_native_declare_check(unsigned site, unsigned what, unsigned index) {
    SiteState* s = site_state(site);
    if (!s || !s->open || s->defined) return 0;
    uint32_t keys[2], n = 0;
    switch (what) {
    case 0:
        if (index >= 18) return 0;
        keys[n++] = check_key(Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS + index * 7, 7, true);
        keys[n++] = check_key(Latte::REGADDR::SQ_TEX_SAMPLER_WORD0_0 + (Latte::SAMPLER_BASE_INDEX_PIXEL + index) * 3, 3, true);
        break;
    case 1:
        if (index >= 8) return 0;
        keys[n++] = check_key(Latte::REGADDR::CB_BLEND0_CONTROL + index, 1, true);
        break;
    case 2:
        if (index >= 16) return 0;
        keys[n++] = check_key(mmSQ_VTX_ATTRIBUTE_BLOCK_START + index * 7, 7, false);
        break;
    default: return 0;
    }
    if (s->pk.checkCount + n > kMaxChecks) return 0;
    for (uint32_t i = 0; i < n; ++i) s->pk.check[s->pk.checkCount++] = keys[i];
    return 1;
}
// where the site would call PAIR's bind (vtable +0x2C) with LOCATIONS: nonzero, the bind is the instance's (its
// blocks pend for the next instance); zero, the site binds it itself (after the pending effects)
extern "C" unsigned wwhd_native_pair(unsigned site, unsigned pair, unsigned locations) {
    SiteState* s = site_state(site);
    if (!s || !s->defined) return 0;
    const uint32_t vt = pair ? ld32(pair + 0xC) : 0;
    const int slot = vt ? pair_slot(ld32(vt + 0x2C)) : -1;
    int32_t loc[3];
    bool ok = slot >= 0 && locations == s->locations && slot_locations(locations, uint32_t(slot), loc);
    for (uint32_t st = 0; ok && st < 3; ++st) ok = loc[st] == -1 || declared(s->pk, block_key(st, uint32_t(loc[st])));
    if (!ok) {
        flush(*s);
        return 0;
    }
    const uint32_t buffer = pair + 0x10 + ld32(pair + 0x4C) * 28;
    const uint32_t data = ld32(buffer + 4), size = ld32(buffer + 12);
    for (uint32_t st : {1u, 0u, 2u})
        if (loc[st] != -1) add_block(*s, block_key(st, uint32_t(loc[st])), data, size);
    return 1;
}
// a direct GX2Set{Vertex,Pixel,Geometry}UniformBlock(LOCATION, SIZE, DATA) call (STAGE 0, 1, 2)
extern "C" unsigned wwhd_native_block(unsigned site, unsigned stage, unsigned location, unsigned size, unsigned data) {
    SiteState* s = site_state(site);
    if (!s || !s->defined) return 0;
    const uint32_t key = block_key(stage, location);
    if (stage > 2 || !declared(s->pk, key)) {
        flush(*s);
        return 0;
    }
    add_block(*s, key, data, size);
    return 1;
}
// where the site would call GX2Set{Vertex,Pixel}UniformReg(OFFSET, COUNT, VALUES) (STAGE 0 vertex, 1 pixel): nonzero,
// the values (read now, by value, as GX2 reads them) are the instance's; zero, the site makes the call itself (after
// the pending effects): outside the declared ranges, or a layout projection (aspect.cpp tags that upload)
extern "C" unsigned wwhd_native_regs(unsigned site, unsigned stage, unsigned offset, unsigned count, unsigned values) {
    SiteState* s = site_state(site);
    if (!s || !s->defined) return 0;
    if (offset & 0x8000) return 1;  // (GX2: no effect)
    count &= ~3u;
    if (offset + count > 0x400) count = 0x400 - std::min<uint32_t>(offset, 0x400);
    if (stage > 1 || count > kMaxRegWords || (stage == 0 && count == 16 && aspect::tagged_projection()) ||
        !reg_declared(s->pk, stage, offset, count)) {
        flush(*s);
        return 0;
    }
    if (!count) return 1;
    if (s->regRanges == kMaxRegRanges || s->regUsed - s->regRanges * kRegHead + count > kMaxRegWords)
        flush(*s);  // (the pending effects as an effects-only instance first)
    uint32_t* w = s->regWords + s->regUsed;
    w[0] = reg_key(stage, offset);
    w[1] = count;
    for (uint32_t i = 0; i < count; ++i) w[kRegHead + i] = ld32(values + 4 * i);
    s->regUsed += kRegHead + count;
    ++s->regRanges;
    return 1;
}
// where the site would call agl::VertexInputSet::bind (027BFE5C) on SHAPE: GX2SetFetchShader(+0xCC), then
// GX2SetAttribBuffer(i, bytes, stride, data) for each of its mBufferCount (+0xC0) buffers (mBuffers +0xC4; a
// VertexBufferPrefix_l: mData +0x140, mStride +0x144, mBytes +0x14C) that is set
extern "C" unsigned wwhd_native_shape(unsigned site, unsigned shape) {
    SiteState* s = site_state(site);
    if (!s || !s->defined) return 0;
    const int32_t count = shape ? int32_t(ld32(shape + 0xC0)) : -1;
    if (count < 0 || count > int32_t(kMaxBuffers)) {
        flush(*s);
        return 0;
    }
    if (s->vertex) flush(*s);  // (a second bind (or attribute buffers alone) before a draw: those effects first)
    const uint32_t fs = shape + 0xCC, buffers = ld32(shape + 0xC4);
    uint32_t* w = s->vertexWords;
    w[0] = ld32(fs + 0x0C) >> 8;
    w[1] = ld32(fs + 0x08) >> 3;
    uint32_t n = 0;
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t src = ld32(buffers + uint32_t(i) * 4);
        if (!src) continue;
        uint32_t* b = w + kVertexHead + n++ * kBufferWords;
        b[0] = uint32_t(i);
        b[1] = ld32(src + 0x140);
        b[2] = ld32(src + 0x14C);
        b[3] = ld32(src + 0x144);
    }
    s->vertex = true;
    s->noFetch = false;
    s->buffers = n;
    return 1;
}
// where the site would call GX2SetAttribBuffer(INDEX, SIZE, STRIDE, DATA) alone (the fetch shader stays as it is):
// nonzero, the buffer is part of the instance's vertex input
extern "C" unsigned wwhd_native_attrib(unsigned site, unsigned index, unsigned size, unsigned stride, unsigned data) {
    SiteState* s = site_state(site);
    if (!s || !s->defined) return 0;
    if (s->vertex && !s->noFetch) flush(*s);  // (a fetch shader bind pending: its effects first)
    for (uint32_t i = 0; s->vertex && i < s->buffers; ++i)
        if (s->vertexWords[kVertexHead + i * kBufferWords] == index) { flush(*s); break; }
    if (s->buffers == kMaxBuffers) flush(*s);
    uint32_t* b = s->vertexWords + kVertexHead + s->buffers++ * kBufferWords;
    b[0] = index;
    b[1] = data;
    b[2] = size;
    b[3] = stride;
    s->vertex = s->noFetch = true;
    return 1;
}
// where the site would call GX2DrawIndexedEx with mesh KIND's arguments: nonzero, the instance is drawn (with the
// effects pending since the last one)
extern "C" unsigned wwhd_native_draw(unsigned site, unsigned kind) {
    SiteState* s = site_state(site);
    if (!s || !s->defined || kind >= s->pk.meshCount) {
        if (s) flush(*s);
        return 0;
    }
    mod_actor_shadow_proof::draw_submitted(s->pk.mesh[kind].count);
    emit_instance(*s, kind);
    return 1;
}
// where the site would call GX2DrawIndexedEx with mesh KIND's primitive and index type but an index range of its own
// (COUNT indices at INDEXADDR): nonzero, the instance is drawn (with the effects pending since the last one)
extern "C" unsigned wwhd_native_draw_range(unsigned site, unsigned kind, unsigned count, unsigned indexAddr) {
    SiteState* s = site_state(site);
    if (!s || !s->defined || kind >= s->pk.meshCount) {
        if (s) flush(*s);
        return 0;
    }
    mod_actor_shadow_proof::draw_submitted(count);
    const uint32_t range[2] = {count, indexAddr};
    emit_instance(*s, kind, range);
    return 1;
}
// a site's experiment switch: option 1 = WWHD_NATIVE_DRAW_<SITE>_SPLIT (draws regrouped by mesh kind; 0 unless set)
extern "C" unsigned wwhd_native_option(unsigned site, unsigned option) {
    static std::array<int, kSites> split = [] {
        std::array<int, kSites> v{};
        for (uint32_t s = 1; s < kSites; ++s) {
            char name[64] = "WWHD_NATIVE_DRAW_";
            size_t n = std::strlen(name);
            for (const char* c = kSiteNames[s]; *c && n < 40; ++c) name[n++] = char(*c - 'a' + 'A');
            std::memcpy(name + n, "_SPLIT", 7);
            v[s] = env_int(name, 0);
            if (v[s]) LOG("[native-draw] %s: draws regrouped by mesh kind (%s=%d, experiment)", kSiteNames[s], name, v[s]);
        }
        return v;
    }();
    return option == 1 && site < kSites && site_on(site) ? unsigned(split[site] != 0) : 0;
}
// the J3D shape draw that follows (j3d::ShapePacket::submitShape): its key words (native_draw.h, J3D material cache).
// Nonzero: named (the site's GX2 calls and its draw stay its own)
extern "C" unsigned wwhd_native_j3d(unsigned shape, unsigned binding, unsigned pass, unsigned view, unsigned program) {
    if (!j3d_on()) return 0;
    const uint32_t w[5] = {shape, binding, pass, view, program};
    gx2::emit(gx2::OP_J3D_SHAPE, w, 5);
    return 1;
}
// 0: no packet; 1: begun, its mesh and commit still to come (a callee completes it); 2: defined
extern "C" unsigned wwhd_native_state(unsigned site) {
    const SiteState* s = site_state(site);
    return !s || !s->open ? 0 : s->defined ? 2 : 1;
}
// the site's pass ends (or it leaves the packet): the pending effects
extern "C" void wwhd_native_end(unsigned site) {
    SiteState* s = site_state(site);
    if (!s) return;
    flush(*s);
    s->open = s->defined = false;
}

namespace native_draw {

// ---------------------------------------------------------------- render thread
static std::array<Packet, kMaxPackets> g_packets;
static std::array<bool, kMaxPackets> g_defined{};

const Packet* packet(uint32_t id) { return id < kMaxPackets && g_defined[id] ? &g_packets[id] : nullptr; }

void define(const uint32_t* p, uint32_t n) {
    if (n != kPacketWords || p[0] >= kMaxPackets) return;
    if (g_defined[p[0]] && std::memcmp(&g_packets[p[0]], p, sizeof(Packet)) == 0) return;  // (unchanged: no new state)
    flush_mdi();
    flush_batch();
    ++g_epoch;
    std::memcpy(&g_packets[p[0]], p, sizeof(Packet));
    g_defined[p[0]] = true;
}

static uint64_t g_counts[kSites][kReasons], g_instances[kSites], g_mismatches = 0, g_frames = 0;
bool mdi_on() {
    static const bool on = [] {
        const bool v = master_on() && env_int("WWHD_NATIVE_DRAW_MDI", 0) != 0;
        if (master_on()) LOG("[native-draw] multi-draw batches %s (WWHD_NATIVE_DRAW_MDI=%d)", v ? "on" : "off", v ? 1 : 0);
        return v;
    }();
    return on;
}
bool g_mdiPending = false;
void flush_mdi() {
#ifdef WWHD_HAS_VULKAN
    if (g_mdiPending) gfxvk::packet_mdi_flush();
#endif
    g_mdiPending = false;
}
static uint64_t g_mdiDraws[kSites], g_mdiIndirect[kSites], g_mdiMax[kSites];
void count_mdi(uint32_t site, uint32_t instances, bool indirect) {
    site = site < kSites ? site : 0;
    ++g_mdiDraws[site];
    g_mdiIndirect[site] += indirect;
    g_counts[site][kMdi] += instances;
    g_mdiMax[site] = std::max<uint64_t>(g_mdiMax[site], instances);
}
static uint64_t g_batchDraws[kSites], g_batchMax[kSites], g_batchStream[kSites], g_batchIndirect[kSites];
void count(uint32_t site, Reason r) { ++g_counts[site < kSites ? site : 0][r]; }
bool g_batchPending = false;
void flush_batch() {
#ifdef WWHD_HAS_VULKAN
    if (g_batchPending) gfxvk::packet_flush();
#endif
    g_batchPending = false;
}
void count_batch(uint32_t site, uint32_t instances, bool stream, bool indirect) {
    site = site < kSites ? site : 0;
    ++g_batchDraws[site];
    g_batchStream[site] += stream;
    g_batchIndirect[site] += indirect;
    g_counts[site][kBatched] += instances;
    g_batchMax[site] = std::max<uint64_t>(g_batchMax[site], instances);
}
void verify_mismatch(const char* what) {
    if (g_mismatches++ < 32) LOG("[native-draw] VERIFY MISMATCH: %s", what);
}

// run lengths (what batching could merge): an in-order run is consecutive instances of one packet and kind with
// nothing but their own effects in between (g_epoch unchanged: any other command or register change ends it, e.g. a
// site's own sampler or room-block bind); a chain is the same without the kind, its kind-split draws the number of
// distinct kinds in it (one draw per kind if a chain's instances were regrouped by kind)
static uint64_t g_runs[kSites], g_chainKinds[kSites];
static uint32_t g_lastId = ~0u, g_lastKind = ~0u, g_chainMask = 0, g_chainSite = 0;
static uint64_t g_lastEpoch = 0;
static void chain_end() {
    g_chainKinds[g_chainSite] += uint64_t(__builtin_popcount(g_chainMask));
    g_chainMask = 0;
}
static void note_run(uint32_t site, const Packet& pk, uint32_t kind) {
    const bool chain = pk.id == g_lastId && g_epoch == g_lastEpoch;
    if (!chain) {
        chain_end();
        g_chainSite = site;
    }
    if (!chain || kind != g_lastKind) ++g_runs[site];
    g_chainMask |= 1u << (kind & 31);
    g_lastId = pk.id;
    g_lastKind = kind;
}

static uint32_t g_drawnId = ~0u;  // the packet whose instance was drawn last
static bool check_on() {
    static const bool on = [] {
        const bool v = master_on() && env_int("WWHD_NATIVE_DRAW_CHECK", 1) != 0;
        if (master_on()) LOG("[native-draw] checked registers %s (WWHD_NATIVE_DRAW_CHECK=%d)", v ? "on" : "off", v ? 1 : 0);
        return v;
    }();
    return on;
}
bool projection_as_regs() {
    static const bool on = [] {
        const bool v = master_on() && env_int("WWHD_NATIVE_DRAW_PROJ", 1) != 0;
        if (master_on()) LOG("[native-draw] layout projections as register writes %s (WWHD_NATIVE_DRAW_PROJ=%d)",
                             v ? "on" : "off", v ? 1 : 0);
        return v;
    }();
    return on && stereo::frame.mode == stereo::Off;
}
bool checked_regs(uint32_t first, uint32_t n) {
    if (g_drawnId >= kMaxPackets || !g_defined[g_drawnId]) return false;
    const Packet& pk = g_packets[g_drawnId];
    if (!pk.checkCount || !check_on()) return false;
    for (uint32_t i = 0; i < pk.checkCount && i < kMaxChecks; ++i)
        if (first >= check_first(pk.check[i]) && first + n <= check_first(pk.check[i]) + check_count(pk.check[i]))
            return true;
    return false;
}
bool instance_regs(uint32_t first, uint32_t n) {
    if (g_drawnId >= kMaxPackets || !g_defined[g_drawnId]) return false;
    const Packet& pk = g_packets[g_drawnId];
    if (!pk.regCount || first >= 0x800) return false;
    const uint32_t stage = first >= 0x400 ? 0 : 1, offset = first & 0x3FF;
    return offset + n <= 0x400 && reg_declared(pk, stage, offset, n);
}

void draw(const uint32_t* regs, const Packet& pk, uint32_t kind, const Mesh* range) {
    const uint32_t site = pk.site < kSites ? pk.site : 0;
    ++g_instances[site];
    note_run(site, pk, kind);
    g_drawnId = pk.id;
    const Mesh& m = range ? *range : pk.mesh[kind];
    Reason why = kNotVulkan;
#ifdef WWHD_HAS_VULKAN
    if (render::vulkan()) {
        if (stereo::frame.mode != stereo::Off) why = kStereo;
        else if (framegen::requested()) why = kFramegen;
        else if (render::grass_distance() > 0 && pk.site == kSiteGrass) why = kGrassDistance;
        else if (gfxvk::gamepad_backdrop_capture_active()) why = kBackdrop;
        else if (cloud_proto::g_active_seq) why = kClouds;
        else {
            gfxvk::packet_draw(regs, pk, kind, range);  // replayed, or drawn the full way (counted there)
            g_lastEpoch = g_epoch;
            return;
        }
    }
#endif
    count(site, why);
    render::draw(regs, m.prim, m.count, m.indexType, m.indexAddr, 0, 1);
    g_lastEpoch = g_epoch;
}

// ---------------------------------------------------------------- J3D material cache (render thread)
uint64_t g_j3dKey = 0;
void j3d_shape(const uint32_t* p, uint32_t n) {
    if (n != 5 || !j3d_on()) { g_j3dKey = 0; return; }
    uint64_t h = 0x9E3779B97F4A7C15ull;
    for (uint32_t i = 0; i < 5; ++i) {
        h ^= p[i] + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        h *= 0xFF51AFD7ED558CCDull;
        h ^= h >> 32;
    }
    g_j3dKey = h ? h : 1;
}
static uint64_t g_j3d[kJ3dCounts];
void j3d_count(J3dCount c, uint32_t n) { g_j3d[c < kJ3dCounts ? c : 0] += n; }
static void j3d_report() {
    if (!g_j3d[kJ3dDraws]) return;
    static const char* const names[kJ3dCounts] = {
        "draws", "hits", "continued (the previous draw's state)", "no entry", "ineligible", "verified", "VERIFY MISMATCHES", "stored", "evicted",
        "changed: registers", "changed: texture words", "changed: surfaces",
        "changed: pass not open", "changed: program", "changed: fetch shader", "changed: primitive",
        "changed: settings", "changed: scissor targets", "changed: texture upload",
        "refused: state not continuable", "refused: program kind", "refused: texture lookup", "refused: pass subset",
        "refused: geometry shader", "refused: other"};
    char buf[1400];
    int k = std::snprintf(buf, sizeof buf, "[native-draw] j3d %s, last 300 frames, per frame:", j3d_on() ? "on" : "off");
    for (uint32_t c = 0; c < kJ3dCounts; ++c)
        if (g_j3d[c] && k < int(sizeof buf) - 64)
            k += std::snprintf(buf + k, sizeof buf - k, "%s %s %.1f", c ? "," : "", names[c], g_j3d[c] / 300.0);
    if (k < int(sizeof buf) - 32)
        std::snprintf(buf + k, sizeof buf - k, "; hit rate %.1f%%", 100.0 * g_j3d[kJ3dHits] / double(g_j3d[kJ3dDraws]));
    LOG("%s", buf);
    std::memset(g_j3d, 0, sizeof g_j3d);
}

// every 300 frames, per site with instances: instances, replayed, lowered and every fallback reason per frame
void frame_end() {
    ++g_frames;
    if (g_frames % 300) return;
    j3d_report();
    chain_end();
    g_lastId = ~0u;
    static const char* const names[kReasons] = {"replayed", "lowered", "verified", "not Vulkan", "stereo", "framegen",
                                                "grass distance", "backdrop", "clouds", "refused", "buffer change",
                                                "block size", "fetch change", "stride change", "range change",
                                                "in multi-draw batches", "in shader-rewrite batches",
                                                "kept by the register journal", "register values outside the packet",
                                                "checked registers changed"};
    for (uint32_t site = 0; site < kSites; ++site) {
        if (!g_instances[site]) continue;
        char buf[700];
        int k = std::snprintf(buf, sizeof buf, "[native-draw] %s %s, last 300 frames: %.1f packet instances/frame",
                              kSiteNames[site], site_on(site) ? "on" : "off", g_instances[site] / 300.0);
        for (uint32_t r = 0; r < kReasons; ++r)
            if (g_counts[site][r])
                k += std::snprintf(buf + k, sizeof buf - k, ", %s %.1f", names[r], g_counts[site][r] / 300.0);
        k += std::snprintf(buf + k, sizeof buf - k, "; runs %.1f (in order, same kind), kind-split draws %.1f",
                           g_runs[site] / 300.0, g_chainKinds[site] / 300.0);
        if (g_mdiDraws[site])
            k += std::snprintf(buf + k, sizeof buf - k, "; multi-draw batches %.1f/frame (%.1f indirect), %.1f instances each (max %llu)",
                               g_mdiDraws[site] / 300.0, g_mdiIndirect[site] / 300.0,
                               double(g_counts[site][kMdi]) / double(g_mdiDraws[site]), (unsigned long long)g_mdiMax[site]);
        if (g_batchDraws[site])
            k += std::snprintf(buf + k, sizeof buf - k,
                               "; shader-rewrite batches %.1f/frame (%.1f stream, %.1f indirect), %.1f instances each (max %llu)",
                               g_batchDraws[site] / 300.0, g_batchStream[site] / 300.0, g_batchIndirect[site] / 300.0,
                               double(g_counts[site][kBatched]) / double(g_batchDraws[site]),
                               (unsigned long long)g_batchMax[site]);
        if (verify_every())
            std::snprintf(buf + k, sizeof buf - k, ", verify mismatches %llu", (unsigned long long)g_mismatches);
        LOG("%s", buf);
    }
    std::memset(g_instances, 0, sizeof g_instances);
    std::memset(g_runs, 0, sizeof g_runs);
    std::memset(g_chainKinds, 0, sizeof g_chainKinds);
    std::memset(g_counts, 0, sizeof g_counts);
    std::memset(g_mdiDraws, 0, sizeof g_mdiDraws);
    std::memset(g_mdiIndirect, 0, sizeof g_mdiIndirect);
    std::memset(g_mdiMax, 0, sizeof g_mdiMax);
    std::memset(g_batchDraws, 0, sizeof g_batchDraws);
    std::memset(g_batchMax, 0, sizeof g_batchMax);
    std::memset(g_batchStream, 0, sizeof g_batchStream);
    std::memset(g_batchIndirect, 0, sizeof g_batchIndirect);
}

}  // namespace native_draw
