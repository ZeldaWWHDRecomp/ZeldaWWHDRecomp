// Native draw packets (native renderer stage 1: P1 grass, P2 the general form). Decomp build, fast mode, cmake
// WWHD_NATIVE_DRAW=ON only; nothing here exists in any other build.
//
// A hot decompiled draw site (decomp: wwhd_src/include/port_native_draw.h) describes its pass once as a packet
// (OP_PACKET_DEFINE) and each of its draws as an instance (OP_PACKET_DRAW). Both are ordinary commands: recorded into
// the display list being built, replayed in order with everything else.
//
// The packet is an engine-level description (what is drawn: a mesh table, material, textures, the instance blocks'
// uniform locations, pass), not pre-packed register words, so the site need not change when the backend does:
//   Packet    site, pass, variant, material, program, texture, the instance blocks (stage + uniform location, <= 6),
//             meshes[kind] (<= 8: primitive, count, index type, index address, as GX2DrawIndexedEx takes them)
//   Instance  kind (or "effects only": no draw), the blocks the site bound since its last instance (<= 6:
//             stage + location, address, size), and the vertex input it bound (fetch shader + <= 2 attribute buffers)
// Kinds alternate freely (trees: crown, trunk, stump parts). An instance's vertex input is either the one its kind
// was lowered with (a static mesh) or a stream of its own (a per-draw vertex buffer with the same fetch content and
// stride), which the replay copies like the full draw does.
//
// Today the render thread lowers the packet through the existing path: the first instance of each kind is drawn by
// the full draw (the same translation, pipeline, texture and support-uniform lookups), which captures what it
// resolved; the next instances of the packet only upload their blocks (and vertex stream), bind and draw
// (gfxvk::packet_draw). Anything else that changes state in between (a register write, another command, a draw that
// is not this packet's, a new submission or frame, a texture upload, an ended pass) makes the next instance lower
// again. Port features that act per draw (stereo, 60 fps (generated), grass draw distance, GamePad backdrop capture,
// clouds) and the Metal renderer take the full draw for every instance.
//
// The instance's register effects are applied exactly as the site's skipped GX2 calls would have applied them
// (GX2Set{Pixel,Vertex,Geometry}UniformBlock, GX2SetFetchShader, GX2SetAttribBuffer, in the site's order), so the
// register file, the context shadow and the full path see the same state as without packets.
//
// Run-time switches (ON builds): WWHD_NATIVE_DRAW=1 turns packets on (off by default); WWHD_NATIVE_DRAW_<SITE>=0 keeps
// one site off (GRASS, TREE, WOOD, FLOWER, MAGMA, STAR, HOUSI, WAVE, MOYA, VRKUMO, RAIN); WWHD_NATIVE_DRAW_VERIFY=N draws
// every Nth replayable instance through the full path and compares what it resolved with the packet (mismatches
// logged, "[native-draw] VERIFY MISMATCH").
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include <cstdint>

namespace native_draw {

// the sites (port_native_draw.h: the same values)
enum Site : uint32_t {
    kSiteGrass = 1, kSiteTree = 2, kSiteWood = 3, kSiteFlower = 4, kSiteMagma = 5,
    kSiteStar = 6, kSiteHousi = 7, kSiteWave = 8, kSiteMoya = 9, kSiteVrkumo = 10, kSiteRain = 11, kSites = 12
};
// packet ids: 8 per site (by pass, or by pass and variant), the first 8 unused
constexpr uint32_t kIdsPerSite = 8;
constexpr uint32_t kMaxPackets = kSites * kIdsPerSite;
constexpr uint32_t kMaxKinds = 8;
constexpr uint32_t kMaxBlocks = 6;         // instance blocks per packet (stage + location)
constexpr uint32_t kMaxBuffers = 2;        // attribute buffers of an instance's vertex input
constexpr uint32_t kNoDraw = 0xFFFFFFFFu;  // instance kind: effects only

struct Mesh {
    uint32_t prim = 0, count = 0, indexType = 0, indexAddr = 0;  // as GX2DrawIndexedEx takes them
};
// OP_PACKET_DEFINE payload, word for word
struct Packet {
    uint32_t id = 0;
    uint32_t site = 0, pass = 0, variant = 0;
    uint32_t material = 0;  // guest material (informational; the native shader's key later)
    uint32_t program = 0;   // guest shader program (informational)
    uint32_t texture = 0;   // guest GX2Texture the site binds (informational)
    uint32_t blockCount = 0;
    uint32_t block[kMaxBlocks] = {};  // the instance blocks: block_key(stage (0 vertex, 1 pixel, 2 geometry), location)
    uint32_t meshCount = 0;
    Mesh mesh[kMaxKinds];
};
constexpr uint32_t kPacketWords = sizeof(Packet) / 4;
inline uint32_t block_key(uint32_t stage, uint32_t location) { return stage << 16 | (location & 0xFFFF); }

// OP_PACKET_DRAW payload: id, kind, layout (block count | has-vertex << 8 | buffer count << 12), then per block
// {block key, address, size}, then the vertex input {SQ_PGM_START_FS word 0, word 1, then per buffer {attribute index,
// address, size, stride}}
constexpr uint32_t kDrawHead = 3, kBlockWords = 3, kVertexHead = 2, kBufferWords = 4;
constexpr uint32_t kMaxDrawWords = kDrawHead + kMaxBlocks * kBlockWords + kVertexHead + kMaxBuffers * kBufferWords;

// render thread: bumped by every register change and every command other than register writes and packet draws
// (gx2_core.cpp); a packet's lowered state is valid only while it is unchanged
extern uint64_t g_epoch;

const Packet* packet(uint32_t id);  // render thread: the defined packet, or nullptr
void define(const uint32_t* p, uint32_t n);  // render thread: OP_PACKET_DEFINE
// render thread: OP_PACKET_DRAW (kind < meshCount) after its register effects were applied: the packet path, or the
// full draw
void draw(const uint32_t* regs, const Packet& pk, uint32_t kind);
void frame_end();  // render thread, at each swap: the periodic counter line

// fallback / lowering reasons, counted per frame and site
enum Reason : uint32_t {
    kReplayed, kLowered, kVerified,
    kNotVulkan, kStereo, kFramegen, kGrassDistance, kBackdrop, kClouds, kRefused, kBufferChange,
    kBlockSize, kFetchChange, kStrideChange, kMdi, kReasons
};
void count(uint32_t site, Reason r);
// Multi-draw batches (WWHD_NATIVE_DRAW_MDI=1 with packets on; off by default; Vulkan): consecutive replayable instances
// of one packet kind that bind the same blocks and differ only in their own vertex stream are drawn as one indirect
// multi-draw (gfxvk::packet_mdi_flush): same pipeline, shaders, descriptors and bytes as replaying them one by one.
// The collected batch is drawn before any other command, at the end of each command chunk, and before an instance
// that cannot join it.
bool mdi_on();
extern bool g_mdiPending;  // render thread: a multi-draw batch is collected
void flush_mdi();
void count_mdi(uint32_t site, uint32_t instances, bool indirect);  // one batch draw of INSTANCES instances
void verify_mismatch(const char* what);
uint32_t verify_every();  // WWHD_NATIVE_DRAW_VERIFY (0: off)

}  // namespace native_draw
#endif
