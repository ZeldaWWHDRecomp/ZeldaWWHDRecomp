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
//             stage + location, address, size), the vertex input it bound (fetch shader + <= 2 attribute buffers),
//             and optionally an index range of its own (count and index address; the kind's primitive and index type)
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
// one site off (GRASS, TREE, WOOD, FLOWER, MAGMA, STAR, HOUSI, WAVE, MOYA, VRKUMO, RAIN, SEA, LINE, JPA);
// WWHD_NATIVE_DRAW_JOURNAL=0 turns the register journal's check off (draw.cpp); WWHD_NATIVE_DRAW_VERIFY=N draws
// every Nth replayable instance through the full path and compares what it resolved with the packet (mismatches
// logged, "[native-draw] VERIFY MISMATCH").
//
// Shader-rewrite batches (WWHD_NATIVE_DRAW_BATCH; Vulkan; on by default in this build (cmake WWHD_NATIVE_DRAW_BATCH,
// ON), WWHD_NATIVE_DRAW_BATCH=0 turns them off): the replayable instances of a kind with the kind's own vertex input and
// index range (a static mesh whose instances differ only in their instance blocks) are not drawn one by one but
// collected and drawn as one instanced draw with a native shader pair: the kind's translated vertex (and pixel)
// program, rewritten so that the instance blocks' entries it reads come from a per-instance vertex stream (each
// instance's 16-byte block entries, raw as the support uniforms copy them) instead of the support uniforms. Every other
// operation of the program stays as translated (same operations, order and precision). The collected batch is drawn
// before any other command, at the end of each command chunk, and when an instance of another packet or kind comes. A
// kind whose program the rewrite cannot take (an instance block read as a buffer, instance-rate vertex input, its
// pipeline variant not compiled yet) replays its instances one by one.
// Not bit-exact: on GPUs whose automatic texture-level (mip LOD) choice depends on how the draws are split (Apple),
// one instanced draw can pick another level than one draw per instance at some pixels. That difference is accepted
// per kind, and only where it is the only one: a site is enabled (batch_site_on; WWHD_NATIVE_DRAW_BATCH_SITES lists
// them, default GRASS,FLOWER) only after its gate proved, for each of its kinds, that with every texture read at
// explicit LOD 0 the batched picture is bit-identical to the unbatched one. Every other kind refuses (logged once).
// Multi-draw batches (below) are exact and take precedence: an instance with a stream or an index range of its own
// never joins a shader-rewrite batch.
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include <cstdint>

namespace native_draw {

// the sites (port_native_draw.h: the same values)
enum Site : uint32_t {
    kSiteGrass = 1, kSiteTree = 2, kSiteWood = 3, kSiteFlower = 4, kSiteMagma = 5,
    kSiteStar = 6, kSiteHousi = 7, kSiteWave = 8, kSiteMoya = 9, kSiteVrkumo = 10, kSiteRain = 11, kSiteSea = 12,
    kSiteLine = 13, kSiteJpa = 14, kSites = 15
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

// OP_PACKET_DRAW payload: id, kind, layout (block count | has-vertex << 8 | buffer count << 12 | has-range << 16), then
// per block {block key, address, size}, then the vertex input {SQ_PGM_START_FS word 0, word 1, then per buffer
// {attribute index, address, size, stride}}, then the index range {count, index address}
constexpr uint32_t kDrawHead = 3, kBlockWords = 3, kVertexHead = 2, kBufferWords = 4, kRangeWords = 2;
constexpr uint32_t kMaxDrawWords =
    kDrawHead + kMaxBlocks * kBlockWords + kVertexHead + kMaxBuffers * kBufferWords + kRangeWords;

// render thread: bumped by every register change and every command other than register writes and packet draws
// (gx2_core.cpp); a packet's lowered state is valid only while it is unchanged
extern uint64_t g_epoch;
// render thread: the register journal. Every register write that changes a register (gx2_core.cpp apply_regs, the
// ALU constants excepted) records the register and its old value; g_journalBumps counts the epoch bumps those writes
// made. A lowered state whose epoch moved only by journalled writes is still valid when every register those writes
// changed holds its value from the lowering again (draw.cpp packet_draw: the register file is the same; a site that
// sets per-object state and resets it after each object, magma balls and 3D line objects, keeps its lowered state).
// A ring: entries older than kJournal writes are gone (the check then fails).
struct RegChange {
    uint32_t reg, old;
};
constexpr uint32_t kJournal = 4096;
extern RegChange g_journal[kJournal];
extern uint64_t g_journalPos, g_journalBumps;
inline void journal(uint32_t reg, uint32_t old) { g_journal[g_journalPos++ % kJournal] = {reg, old}; }

const Packet* packet(uint32_t id);  // render thread: the defined packet, or nullptr
// render thread: OP_PACKET_DEFINE. A definition the same as the packet's current one, word for word, changes nothing:
// the packet's lowered state stays valid (g_epoch and a collected multi-draw batch stay as they are)
void define(const uint32_t* p, uint32_t n);
// render thread: OP_PACKET_DRAW (kind < meshCount) after its register effects were applied: the packet path, or the
// full draw. RANGE: the instance's own index range (the kind's primitive and index type), or null: the kind's mesh
void draw(const uint32_t* regs, const Packet& pk, uint32_t kind, const Mesh* range);
void frame_end();  // render thread, at each swap: the periodic counter line
bool batch_on();   // shader-rewrite batches on (packets on, WWHD_NATIVE_DRAW_BATCH not 0)
bool batch_site_on(uint32_t site);  // the site's kinds may be batched (WWHD_NATIVE_DRAW_BATCH_SITES)
// render thread: an instanced batch is collected (gfxvk); flush_batch draws it now
extern bool g_batchPending;
void flush_batch();
void count_batch(uint32_t site, uint32_t instances);  // one batch draw of INSTANCES instances

// fallback / lowering reasons, counted per frame and site
enum Reason : uint32_t {
    kReplayed, kLowered, kVerified,
    kNotVulkan, kStereo, kFramegen, kGrassDistance, kBackdrop, kClouds, kRefused, kBufferChange,
    kBlockSize, kFetchChange, kStrideChange, kRangeChange, kMdi, kBatched,
    kRestored,  // (not a fallback) the lowered state kept by the register journal (registers changed and set back)
    kReasons
};
void count(uint32_t site, Reason r);
// Multi-draw batches (WWHD_NATIVE_DRAW_MDI=1 with packets on; off by default; Vulkan): consecutive replayable instances
// of one packet kind that bind the same blocks and differ only in their own vertex stream, or only in their own index
// range, are drawn as one indirect multi-draw (gfxvk::packet_mdi_flush): same pipeline, shaders, descriptors and bytes
// as replaying them one by one.
// The collected batch is drawn before any other command, at the end of each command chunk, and before an instance
// that cannot join it. Register writes (OP_SET_REGS) leave it open: they record nothing, and one that changes a
// register ends the packet's lowered state (g_epoch), so no later instance joins the batch.
bool mdi_on();
extern bool g_mdiPending;  // render thread: a multi-draw batch is collected
void flush_mdi();
void count_mdi(uint32_t site, uint32_t instances, bool indirect);  // one batch draw of INSTANCES instances
void verify_mismatch(const char* what);
uint32_t verify_every();  // WWHD_NATIVE_DRAW_VERIFY (0: off)

}  // namespace native_draw
#endif
