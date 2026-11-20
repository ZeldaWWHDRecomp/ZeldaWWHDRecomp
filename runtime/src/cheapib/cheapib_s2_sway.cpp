// cheapib stage 2, blocks-only sites (decomp build, fast mode; cheapib/stage2/PLAN.md §2). See cheapib_s2.h.
#include "cheapib/cheapib_s2.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "cheapib/cheapib.h"
#include "cheapib/render_replay.h"
#include "guest_addr.h"
#include "ppc.h"
#include "runtime.h"
#include "write_watch.h"

namespace interp {
bool hold_pass();
uint64_t logic_steps();
}

namespace cheapib {
namespace {

// Per kind: the packet's animation slots (dGrass_anm_c / dFlower_anm_c: +2 angle Y, +4 angle X / bend, +8 matrix),
// its room heads (64 data chains), its model packets (RenderMatrixUniforms, 0xA8 apart, in the order the update's
// buildShaders / prepareGraphics walks the chains, skipping clipped plants) and the data entry's fields.
// Trees (dTree_packet_c, TreePacketState::update 025CA6EC / prepareRender 025C9D68): 72 slots (dTree_anm_c 0x84: +2
// trunk Y, +4 top X, +6 top Y, +8 trunk X, +0xA cut Y, +0x18 top offset xyz; +0x24 top matrix, +0x54 trunk matrix),
// 64 room lists (dTree_room_c 0x10C: +0 count, +8 the entries; sorted by view depth on the logic pass, kept), data
// 0xA8 (+0 state: 4 clipped, 8 top landed; +2 slot; +3 top alpha; +0x10 top, +0x40 trunk draw matrix), packets: the
// trunks and opaque tops at +0xAA48, the fading tops at +0x1DB90; frame uniforms +0xAA3C and +0x1DB84.
struct Kind {
    const char* name;
    uint32_t mask;  // its WWHD_CHEAPIB_S2 bit
    uint32_t anm, anm_stride;
    int anm_n;
    uint32_t rooms, packets;
    int packets_n;
    uint32_t flags, clipped, index;  // data entry: flag byte, its clipped bit, the slot index (s8)
    uint32_t binding[2];             // the frame uniforms (RenderFrameArray: +0 count, +4 RenderFrameUniforms[])
};
constexpr Kind kKinds[kSiteKinds] = {
    {"grass", 1, 0x18F0C, 0x38, 104, 0x1A5CC, 0x1B050, 1500, 0x1, 0x2, 0x2, {0x1B044, 0}},
    {"flower", 2, 0x35BC, 0x38, 72, 0x457C, 0x469C, 200, 0x0, 0x4, 0x1, {0x4690, 0}},
    {"tree", 8, 0x2A9C, 0x84, 72, 0x4FBC, 0xAA48, 1 << 20, 0x0, 0x4, 0x2, {0xAA3C, 0x1DB84}},
    {"bush", 16, 0x1C8D8, 0x8C, 72, 0x1F038, 0x2C4B8, 400, 0x0, 0x0, 0x0, {0x2C4AC, 0x4AA64}},
};
// Bushes (dWood::Packet_c, hdWood_prepareDraw 025D0BEC, from update 025D15B0 every pass): 72 shared animations (Anm_c
// 0x8C: +0 sway matrix, +0x30 trunk matrix, +0x8A alpha; blended at the pass's fraction by interp_fx's bush hook),
// 64 rooms (Room_c 0x32C: +0 count, +8 the unit pointers; sorted by view depth on the logic pass, kept), units
// (Unit_c: +0 position, +0x10 flags: 2 culled, 4 cut; +0x14 animation, +0x18 sway draw matrix, +0x48 trunk draw
// matrix), packets: mRoomTransforms +0x2C4B8 (400), the fading leaves mFadeTransforms +0x4AA70 (200); frame uniforms
// mRoomTransformSet +0x2C4AC, mFadeTransformSet +0x4AA64.
constexpr uint32_t kBushRoomStride = 0x32C, kBushFade = 0x4AA70, kBushAlpha = 0x8A;
constexpr uint32_t kAnmAngleY = 2, kAnmAngleX = 4;
constexpr uint32_t kDataNext = 0x40;
// the matrix a plant's packet gets (part): 0 grass/flower (slot rotation, position), 1 a tree's top, 2 its trunk,
// 3 a bush's leaves (the animation's sway matrix, its translation plus the position), 4 its trunk (the animation's
// trunk matrix, the position as its translation)
enum { kPartPlant, kPartTop, kPartTrunk, kPartBush, kPartBushTrunk };
constexpr uint32_t kPartMtx[5] = {8, 0x24, 0x54, 0, 0x30};          // the slot's matrix of that part (as the update leaves it)
constexpr uint32_t kPartData[5] = {0x10, 0x10, 0x40, 0x18, 0x48};  // the data entry's draw matrix of that part
constexpr uint32_t kPartPos[5] = {4, 4, 4, 0, 0};                  // the data entry's position
constexpr uint32_t kPacketStride = 0xA8;
// RenderUniformPair: mBuffers[2] at +0x10 (agl::RenderUniformBuffer_l, 28 bytes: +0 format, +4 CPU storage),
// mPreviousIndex +0x4C (the list binds mBuffers[mPreviousIndex]); the format: +0 field array (6 bytes each: +0 type,
// +2 count, +4 byte offset); the type table 10169D50 (3 bytes each: words per element, alignment, element stride in
// words). RenderMatrixUniforms::uploadMatrixValues (027FBACC) writes field 0 = the 3x4 matrix, 3 elements.
constexpr uint32_t kPairBuffers = 0x10, kBufferSize = 28, kPairPrevious = 0x4C;
constexpr uint32_t kTypeTable = 0x10169D50;
// the matrix helpers (m_Do_mtx.cpp) and the scratch matrix the updates use (mDoMtx_stack_c::now)
constexpr uint32_t kYrotS = 0x025F1884, kXrotM = 0x025F1BF4, kYrotM = 0x025F1C28, kStack = 0x1048D0CC;
// The frame uniforms' camera part (bit 4): RenderFrameEnvironmentEntries::update (027FDA54, the sites' "binding build")
// writes RenderFrameUniforms +0x74 view (copyRenderMatrix34 028E90D4), +0xA4 projection x view
// (composeRenderFrameProjection 0272D680), +0x1FC projection (writeRenderFrameProjection 028E8970) from the 3D view
// 104B45F8 and projection 104B470C; uploadFrameValues (027FB880) puts them in fields 0 (3 rows), 1 and 8 (4 rows).
// The rest (light and fog entries, from the drawing context's environment) stays the logic pass's.
constexpr uint32_t kView = 0x104B45F8, kProj = 0x104B470C;
constexpr uint32_t kCopy34 = 0x028E90D4, kCompose = 0x0272D680, kWriteProj = 0x028E8970;
constexpr uint32_t kFramePrimary = 0x74, kFrameSecondary = 0xA4, kFrameFinal = 0x1FC;

struct Plant {
    uint32_t data;  // its data entry (position)
    uint32_t dst;   // field 0 of the logic list's buffer (CPU storage + offset)
    uint16_t slot;
    uint8_t part;
    uint8_t words, stride;  // words per row, row stride in words (4, 4)
};
struct SiteNote {
    int kind;
    uint32_t self;
    std::vector<Plant> plants;  // the moving ones (a slot)
    uint32_t frame[2] = {}, buffer[2] = {};  // the frame uniforms objects (frames[0]) and the buffers the list binds
    uint32_t cam[28] = {};                   // the view (12) and projection (16) the buffers hold now
};
struct SlotCache {
    bool valid = false;
    uint64_t angles = 0;  // the three helper angles
    uint32_t m[12];
};
struct S {
    // game thread only (the updates run in dScnPly_Draw on the game thread; the replay patch too)
    std::vector<SiteNote> sites;  // the logic pass's
    bool open = false;            // a new logic pass's first site resets the list
    uint64_t step = ~0ull;        // the logic step the list belongs to
    std::vector<SlotCache> cache[kSiteKinds];  // per slot and part
    // counters per report
    uint64_t logic_sites = 0, logic_plants = 0, still = 0, passes = 0, written = 0, computed = 0, ns = 0;
    uint64_t check_sites = 0, check_bad = 0, layout_bad = 0, cam_written = 0, dropped = 0;
    uint64_t kind_sites[kSiteKinds] = {}, kind_plants[kSiteKinds] = {};
    bool fresh = false;  // no replayed pass since the logic pass
} g;

// agl's UniformBuffer::writeWords (027BF048) for COUNT elements of field INDEX from host-order words: word-reversed
// stores into the buffer's CPU storage; false if the layout is not what the upload expects
inline bool heap_ptr(uint32_t a) { return a >= 0x10000000 && a < 0x50000000; }
bool write_field(uint32_t buffer, uint32_t index, const uint32_t* src, int count) {
    if (!heap_ptr(buffer)) return false;
    const uint32_t format = ld32(buffer), cpu = ld32(buffer + 4);
    const uint32_t fields = heap_ptr(format) ? ld32(format) : 0;
    if (!heap_ptr(cpu) || !heap_ptr(fields) || index >= ld16(format + 6)) return false;
    const uint32_t f = fields + 6 * index, type = ld8(f), offset = ld16(f + 4);
    const uint32_t types = GD(kTypeTable);
    const uint32_t words = ld8(types + 3 * type), stride = ld8(types + 3 * type + 2);
    uint32_t base = cpu + offset;
    for (int e = 0; e < count; e++, base += 4 * stride)
        for (uint32_t w = 0; w < words; w++) st32(base + 4 * w, __builtin_bswap32(*src++));
    wwatch::hint(cpu + offset, 4 * stride * count);
    return true;
}

uint32_t read_mask() {
#ifdef WWHD_DECOMP_FAST
    const char* e = render_replay::env("WWHD_CHEAPIB_S2");  // (render_replay.h)
    const uint32_t m = e ? (uint32_t)strtoul(e, nullptr, 0) : 0;
    if (m && mode() == 1) {
        LOG("[cheapib] WWHD_CHEAPIB_S2=0x%X (stage 2): replayed in-betweens write the sway matrices of the logic pass's "
            "plants (grass %s, flower %s, tree %s, bush %s; camera in their frame uniforms %s) into the buffers its lists bind", m,
            m & 1 ? "on" : "off", m & 2 ? "on" : "off", m & 8 ? "on" : "off", m & 16 ? "on" : "off", m & 4 ? "on" : "off");
        return m;
    }
    if (m) LOG("[cheapib] WWHD_CHEAPIB_S2 needs WWHD_CHEAPIB=1: off");
#endif
    return 0;
}

// WWHD_CHEAPIB_S2_DBG (debug): 1 no buffer writes, 2 no matrix helper calls
uint32_t dbg() {
    static const uint32_t d = getenv("WWHD_CHEAPIB_S2_DBG") ? (uint32_t)strtoul(getenv("WWHD_CHEAPIB_S2_DBG"), nullptr, 0) : 0;
    return d;
}
// the slot's rotation of a part at its current angles, as the update computes it: YrotS(a), XrotM(b), YrotM(c)
// (m[3], m[7], m[11]: the helpers' zeros)
const uint32_t* slot_matrix(Cpu* c, int kind, uint32_t self, int slot, int part) {
    const Kind& k = kKinds[kind];
    const uint32_t a = self + k.anm + k.anm_stride * slot;
    if (part >= kPartBush) {  // (a bush: the animation's matrix as the bush hook blended it for this pass)
        static uint32_t m[12];
        for (int i = 0; i < 12; i++) m[i] = ld32(a + kPartMtx[part] + 4 * i);
        return m;
    }
    int16_t y, x, z;
    if (part == kPartPlant) {
        y = (int16_t)ld16(a + kAnmAngleY);
        x = (int16_t)ld16(a + kAnmAngleX);
        z = int16_t(-int32_t(y));
    } else {
        const int16_t cut = (int16_t)ld16(a + 0xA);
        y = cut;
        x = (int16_t)ld16(a + (part == kPartTop ? 4 : 8));
        z = int16_t((int16_t)ld16(a + (part == kPartTop ? 6 : 2)) - cut);
    }
    const uint64_t angles = (uint64_t(uint16_t(y)) << 32) | (uint64_t(uint16_t(x)) << 16) | uint16_t(z);
    std::vector<SlotCache>& cache = g.cache[kind];
    if (cache.size() != size_t(k.anm_n) * 3) cache.assign(size_t(k.anm_n) * 3, SlotCache{});
    SlotCache& sc = cache[size_t(slot) * 3 + part];
    if (sc.valid && sc.angles == angles) return sc.m;
    if (dbg() & 2) {  // (debug: the slot's matrix as the logic update left it, no helper calls)
        for (int i = 0; i < 12; i++) sc.m[i] = ld32(a + kPartMtx[part] + 4 * i);
        sc.valid = true;
        sc.angles = angles;
        return sc.m;
    }
    const uint32_t stack = GD(kStack);
    Cpu cc = *c;
    guest_call(&cc, kYrotS, {stack, uint32_t(int32_t(y))});
    guest_call(&cc, kXrotM, {stack, uint32_t(int32_t(x))});
    guest_call(&cc, kYrotM, {stack, uint32_t(int32_t(z))});
    for (int i = 0; i < 12; i++) sc.m[i] = ld32(stack + 4 * i);
    sc.valid = true;
    sc.angles = angles;
    g.computed++;
    return sc.m;
}
// the plant's 12 matrix words: the slot's rotation, its position as the translation column (a tree's top: plus the
// slot's top offset, a single-precision add as the update's fadds)
inline void plant_words(const uint32_t* rot, uint32_t data, int part, uint32_t anm, uint32_t out[12]) {
    memcpy(out, rot, 48);
    for (int i = 0; i < 3; i++) {
        uint32_t w = ld32(data + kPartPos[part] + 4 * i);
        if (part == kPartTop || part == kPartBush) {  // (single-precision adds, as the updates' fadds)
            float p, o;
            const uint32_t ow = part == kPartTop ? ld32(anm + 0x18 + 4 * i) : rot[3 + 4 * i];
            memcpy(&p, &w, 4);
            memcpy(&o, &ow, 4);
            const float r = p + o;
            memcpy(&w, &r, 4);
        }
        out[3 + 4 * i] = w;
    }
}

}  // namespace

uint32_t s2_mask() {
    static const uint32_t m = read_mask();
    return m;
}

// decomp port point PORT_CHEAPIB_SITE: the logic pass's update of a sway site is done (game thread)
void site(int kind, uint32_t self) {
    if (!s2_mask() || kind < 0 || kind >= kSiteKinds || !(s2_mask() & kKinds[kind].mask)) return;
    if (interp::hold_pass()) return;  // (a full in-between: nothing to note)
    // (a new step's first site starts a new list: logic_done is not called on every logic pass, e.g. before
    // interpolation starts)
    if (!g.open || g.step != interp::logic_steps()) {
        g.sites.clear();
        g.open = true;
        g.step = interp::logic_steps();
    }
    const Kind& k = kKinds[kind];
    SiteNote s{kind, self, {}};
    const uint32_t types = GD(kTypeTable);
    // a packet's buffer the logic list binds, for a plant's part
    auto add = [&](uint32_t pair, uint32_t data, int slot, int part) {
        if (slot < 0 || slot >= k.anm_n) {  // (grass: a fixed turn, no sway)
            g.still++;
            return;
        }
        const uint32_t prev = ld32(pair + kPairPrevious) & 1;
        const uint32_t buffer = pair + kPairBuffers + kBufferSize * prev;
        const uint32_t format = ld32(buffer), cpu = ld32(buffer + 4);
        const uint32_t fields = format ? ld32(format) : 0;
        if (!cpu || !fields) {
            g.layout_bad++;
            return;
        }
        const uint32_t type = ld8(fields), offset = ld16(fields + 4);
        const uint8_t words = ld8(types + 3 * type), stride = ld8(types + 3 * type + 2);
        if (words * 3 != 12 || stride < words) {
            g.layout_bad++;
            return;
        }
        s.plants.push_back({data, cpu + offset, uint16_t(slot), uint8_t(part), words, stride});
    };
    if (kind == 3) {  // bushes: hdWood_prepareDraw's walk (rooms sorted on the logic pass)
        uint32_t normal = 0, fade = 0;
        for (uint32_t room = 0; room < 64; room++) {
            const uint32_t header = self + k.rooms + kBushRoomStride * room;
            const uint32_t units = ld32(header + 8), count = ld32(header);
            for (uint32_t i = 0; units && i < count && i < 200; i++) {
                const uint32_t unit = ld32(units + 4 * i);
                const uint32_t flags = ld32(unit + 0x10);
                if (flags & 2) continue;
                const uint32_t slot = ld32(unit + 0x14);
                if (slot >= 72 || normal >= 400) { g.layout_bad++; continue; }
                const uint8_t alpha = ld8(self + k.anm + k.anm_stride * slot + kBushAlpha);
                const bool cut = flags & 4;
                if (alpha == 255 && !cut) add(self + k.packets + kPacketStride * normal++, unit, int(slot), kPartBush);
                else if (alpha && !cut && fade < 200) add(self + kBushFade + kPacketStride * fade++, unit, int(slot), kPartBush);
                else if (alpha && !cut) fade++;
                if (normal < 400) add(self + k.packets + kPacketStride * normal++, unit, int(slot), kPartBushTrunk);
            }
        }
    } else if (kind == 2) {  // trees: prepareRender's walk (rooms sorted on the logic pass)
        uint32_t small = 0, fading = 0;
        for (uint32_t room = 0; room < 64; room++) {
            const uint32_t list = self + k.rooms + 0x10C * room;
            const uint32_t start = ld32(list + 8), count = ld32(list);
            for (uint32_t i = 0; start && i < count && i < 64; i++) {
                const uint32_t data = ld32(start + 4 * i);
                const uint8_t state = ld8(data);
                if (state & 4) continue;
                const int slot = (int8_t)ld8(data + 2);
                const uint8_t alpha = ld8(data + 3);
                const bool landed = state & 8;  // (the top drawn with its landed matrix: no sway)
                if (alpha == 255) {
                    if (!landed) add(self + k.packets + kPacketStride * small, data, slot, kPartTop);
                    small++;
                } else if (alpha != 0) {
                    if (!landed) add(self + 0x1DB90 + kPacketStride * fading, data, slot, kPartTop);
                    fading++;
                }
                add(self + k.packets + kPacketStride * small, data, slot, kPartTrunk);
                small++;
            }
        }
    } else {
        int n = 0;
        for (uint32_t room = 0; room < 64; room++) {
            for (uint32_t data = ld32(self + k.rooms + 4 * room); data; data = ld32(data + kDataNext)) {
                if (ld8(data + k.flags) & k.clipped) continue;
                if (n >= k.packets_n) break;
                const uint32_t pair = self + k.packets + kPacketStride * uint32_t(n++);
                add(pair, data, (int8_t)ld8(data + k.index), kPartPlant);
            }
        }
    }
    // the frame uniforms the logic list binds, and the camera in them
    if (s2_mask() & 4) {
        for (int bi = 0; bi < 2 && k.binding[bi]; bi++) {
            const uint32_t b = self + k.binding[bi];
            const uint32_t frames = ld32(b + 4);
            if ((int32_t)ld32(b) >= 1 && frames) {
                const uint32_t buffer = frames + kPairBuffers + kBufferSize * (ld32(frames + kPairPrevious) & 1);
                // (a frame uniforms object whose buffer is not set up, e.g. a set the site has not built: not noted)
                const uint32_t format = heap_ptr(frames) ? ld32(buffer) : 0;
                if (heap_ptr(format) && heap_ptr(ld32(format)) && heap_ptr(ld32(buffer + 4))) {
                    s.frame[bi] = frames;
                    s.buffer[bi] = buffer;
                } else {
                    g.layout_bad++;
                    static int lg = 4;
                    if (lg > 0 && lg--)
                        LOG("[cheapib] s2 sway: %s frame uniforms %u (count %d, object %08X): buffer format %08X, storage %08X: "
                            "not set up, its camera is not written", kKinds[kind].name, bi, (int32_t)ld32(b), frames,
                            format, heap_ptr(frames) ? ld32(buffer + 4) : 0);
                }
            }
        }
        for (int i = 0; i < 12; i++) s.cam[i] = ld32(GD(kView) + 4 * i);
        for (int i = 0; i < 16; i++) s.cam[12 + i] = ld32(GD(kProj) + 4 * i);
    }
    // the first sites: the port's matrix against the one the update just placed (data +0x10), word for word
    if (g.check_sites < 64) {
        g.check_sites++;
        for (const Plant& p : s.plants) {
            const uint32_t a = self + k.anm + k.anm_stride * p.slot;
            uint32_t rot[12], w[12];
            for (int i = 0; i < 12; i++) rot[i] = ld32(a + kPartMtx[p.part] + 4 * i);  // (as the update left it)
            plant_words(rot, p.data, p.part, a, w);
            bool bad = false;
            for (int i = 0; i < 12; i++) bad |= w[i] != ld32(p.data + kPartData[p.part] + 4 * i);
            if (bad) g.check_bad++;
        }
        if (g.check_sites == 64)
            LOG("[cheapib] s2 sway: check of the first 64 logic sites: %llu plants whose noted matrix differs from the "
                "update's, %llu with an unexpected buffer layout",
                (unsigned long long)g.check_bad, (unsigned long long)g.layout_bad);
    }
    g.logic_sites++;
    g.logic_plants += s.plants.size();
    g.kind_sites[kind]++;
    g.kind_plants[kind] += s.plants.size();
    g.sites.push_back(std::move(s));
}

// the logic pass's per-frame function returned
void s2_logic_done() {
    if (!s2_mask()) return;
    g.fresh = true;
    g.open = false;  // (the next logic pass's first site starts a new list; until then a replay uses this one)
}

// a replayed hold pass's patch (or a verified in-between: the same writes into the logic pass's buffers)
void s2_rerun(Cpu* c) {
    if (!s2_mask() || !(replaying() || verifying()) || g.open) return;
    // (only the list of this step's logic pass: a logic pass without sites, e.g. in a scene without plants, leaves the
    // older list, whose objects may be gone; writing through them corrupted the snow packet at Ice Ring Isle)
    if (g.step != interp::logic_steps()) return;
    const auto t0 = std::chrono::steady_clock::now();
    g.passes++;
    uint32_t cam[28];
    for (int i = 0; i < 12; i++) cam[i] = ld32(GD(kView) + 4 * i);
    for (int i = 0; i < 16; i++) cam[12 + i] = ld32(GD(kProj) + 4 * i);
    // the first replayed pass after a logic pass, early on: the bytes at each destination must be the logic upload's
    // (the plant's data matrix, word-reversed); a plant whose bytes differ is dropped (and logged)
    static int checks = 40;
    const bool check = g.fresh && checks > 0;
    if (check) checks--;
    g.fresh = false;
    for (SiteNote& s : g.sites) {
        if (check) {
            size_t kept = 0;
            for (const Plant& p : s.plants) {
                bool same = true;
                for (int e = 0; e < 3; e++)
                    for (int i = 0; i < p.words; i++)
                        same &= __builtin_bswap32(ld32(p.dst + 4 * (p.stride * e + i))) ==
                                ld32(p.data + kPartData[p.part] + 4 * (e * p.words + i));
                if (same) s.plants[kept++] = p;
                else {
                    static int tr = 3;
                    if (tr > 0 && tr--) {
                        char b[400];
                        int n = 0;
                        for (int i = 0; i < 16; i++) n += snprintf(b + n, sizeof b - n, " %08X", ld32(p.dst + 4 * i));
                        n += snprintf(b + n, sizeof b - n, " | data");
                        for (int i = 0; i < 12; i++) n += snprintf(b + n, sizeof b - n, " %08X", ld32(p.data + kPartData[p.part] + 4 * i));
                        LOG("[cheapib] s2 sway: %s plant data %08X (words %u stride %u): the bytes at %08X are not its "
                            "logic upload: dropped;%s", kKinds[s.kind].name, p.data, p.words, p.stride, p.dst, b);
                    }
                    g.dropped++;
                }
            }
            s.plants.resize(kept);
        }
        if ((s.frame[0] || s.frame[1]) && memcmp(cam, s.cam, sizeof cam) != 0) {
            // this pass's camera into the frame uniforms' camera fields, then those fields into the logic buffer
            for (int bi = 0; bi < 2; bi++) {
                const uint32_t f = s.frame[bi];
                if (!f) continue;
                Cpu cc = *c;
                guest_call(&cc, kCopy34, {GD(kView), f + kFramePrimary});
                guest_call(&cc, kCompose, {f + kFrameSecondary, GD(kProj), GD(kView)});
                guest_call(&cc, kWriteProj, {GD(kProj), f + kFrameFinal});
                uint32_t w[16];
                bool ok = true;
                for (int i = 0; i < 12; i++) w[i] = ld32(f + kFramePrimary + 4 * i);
                ok &= write_field(s.buffer[bi], 0, w, 3);
                for (int i = 0; i < 16; i++) w[i] = ld32(f + kFrameSecondary + 4 * i);
                ok &= write_field(s.buffer[bi], 1, w, 4);
                for (int i = 0; i < 16; i++) w[i] = ld32(f + kFrameFinal + 4 * i);
                ok &= write_field(s.buffer[bi], 8, w, 4);
                if (!ok) {
                    g.layout_bad++;
                    s.frame[bi] = 0;
                }
            }
            memcpy(s.cam, cam, sizeof cam);
            g.cam_written++;
        }
        for (const Plant& p : s.plants) {
            uint32_t w[12];
            const Kind& k = kKinds[s.kind];
            plant_words(slot_matrix(c, s.kind, s.self, p.slot, p.part), p.data, p.part,
                        s.self + k.anm + k.anm_stride * p.slot, w);
            if (dbg() & 1) continue;
            for (int e = 0; e < 3; e++)
                for (int i = 0; i < p.words; i++)
                    st32(p.dst + 4 * (p.stride * e + i), __builtin_bswap32(w[e * p.words + i]));
            wwatch::hint(p.dst, 4u * p.stride * 3);
            g.written++;
        }
    }
    g.ns += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    if (g.passes % 900 == 0) {
        auto per = [](uint64_t v, uint64_t d) { return d ? double(v) / double(d) : 0.0; };
        LOG("[cheapib] s2 sway: per replayed pass %.1f us, plants written %.1f, slot matrices computed %.1f; logic: "
            "%.1f moving plants per site (%.1f still); frame uniforms' camera written %.2f per replayed pass, bad layouts %llu, plants dropped by the check %llu",
            per(g.ns, g.passes) / 1000.0, per(g.written, g.passes), per(g.computed, g.passes),
            per(g.logic_plants, g.logic_sites), per(g.still, g.logic_sites), per(g.cam_written, g.passes),
            (unsigned long long)g.layout_bad, (unsigned long long)g.dropped);
        LOG("[cheapib] s2 sway: logic sites (moving plants per site): grass %llu (%.1f), flower %llu (%.1f), tree %llu (%.1f), bush %llu (%.1f)",
            (unsigned long long)g.kind_sites[0], per(g.kind_plants[0], g.kind_sites[0]), (unsigned long long)g.kind_sites[1],
            per(g.kind_plants[1], g.kind_sites[1]), (unsigned long long)g.kind_sites[2], per(g.kind_plants[2], g.kind_sites[2]),
            (unsigned long long)g.kind_sites[3], per(g.kind_plants[3], g.kind_sites[3]));
        for (int i = 0; i < kSiteKinds; i++) g.kind_sites[i] = g.kind_plants[i] = 0;
        g.logic_sites = g.logic_plants = g.still = g.passes = g.written = g.computed = g.ns = g.cam_written = 0;
    }
}

}  // namespace cheapib

extern "C" int wwhd_cheapib_site(int kind, unsigned self) {
    cheapib::site(kind, self);
    return 0;
}
