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
//             and the instance register ranges (<= 6: stage + first register + count, in words as
//             GX2Set{Vertex,Pixel}UniformReg take them; the UI's per-quad values)
//   Instance  kind (or "effects only": no draw), the blocks the site bound since its last instance (<= 6:
//             stage + location, address, size), the vertex input it bound (fetch shader + <= 2 attribute buffers),
//             optionally an index range of its own (count and index address; the kind's primitive and index type),
//             and the register values it set (<= 6 ranges, <= 64 words in all: stage + offset + count + the values,
//             copied by value at the call, as GX2 copies them), each inside a declared register range
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
// (GX2Set{Pixel,Vertex,Geometry}UniformBlock, GX2SetFetchShader, GX2SetAttribBuffer, GX2Set{Vertex,Pixel}UniformReg,
// in the site's order per kind), so the register file, the context shadow and the full path see the same state as
// without packets. Register values reach the shaders through the support uniforms (Cemu's uniform-register copy): the
// lowering draw records which support bytes come from the declared ranges, a replay patches exactly those from the
// registers (like the instance blocks' kcache entries). A layout projection (aspect.cpp's tagged upload,
// OP_SET_PROJ_REGS) is never an instance value: the site makes that call itself.
// A packet defined again with the same content in the same command stream segment is not emitted again (the game
// thread keys it by list_serial()), and a definition identical to the one held does not end the lowered
// state.
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
// them, default GRASS,FLOWER,VRKUMO,JPA) only after its gate proved, for each of its kinds, that with every texture
// read at explicit LOD 0 the batched picture is bit-identical to the unbatched one. Every other kind refuses (logged
// once). (Fewer draws also move the draw-batch submission split, WWHD_VK_DRAW_BATCH, which by itself can change a
// pixel or two in scenes above the split's draw count, batched or not; the proof of such a scene runs with it off.)
// Stream batches: a kind whose instances bring a stream of their own (a vertex buffer of the instance's) and whose
// blocks vary from instance to instance (cloud cards: a colour pair per card; particles: material, frame and matrix
// blocks per particle) is batched the same way, its instances' streams copied back to back and drawn as one indirect
// draw, one command per instance (vertexOffset: its stream, firstInstance: its entries). Only for a kind whose vertex
// shader does not use the vertex index, with one vertex buffer (as multi-draw batches).
// A batch ends at every state change: an instance whose kind's lowered state no longer holds (another texture, render
// state, a changed packet definition) draws the open batch first; nothing outside the kind's own lowered state is
// varied per instance (no per-instance textures, no batches across textures).
// Multi-draw batches (below) are exact and take precedence: an instance with an index range of its own never joins a
// shader-rewrite batch, nor does an instance a range batch takes; an instance with a stream of its own joins one only
// for a kind whose blocks vary (a multi-draw batch, which needs the same blocks, takes at most one object's instances).
//
// How the register journal and the two kinds of batches work together (packet_draw): an instance is replayable when
// its kind's lowered state holds: the epoch is unchanged, or the journal shows that every register changed since the
// lowering holds its lowered value again (kRestored). Only a replayable instance joins a batch; otherwise the open
// batch is drawn first and the kind lowered again. Order of choice for a replayable instance: an open multi-draw or
// range batch it can join; a shader-rewrite batch (enabled site, the kind's own vertex input and index range, not a
// multi-draw or range candidate); a new multi-draw or range batch; a replay of its own. At most one batch is open:
// each is drawn before the other starts. A multi-draw batch stays open across register writes and unchanged packet
// definitions (the next instance's replayable check decides), and so does a shader-rewrite batch.
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include <cstdint>

namespace native_draw {

// the sites (port_native_draw.h: the same values)
enum Site : uint32_t {
    kSiteGrass = 1, kSiteTree = 2, kSiteWood = 3, kSiteFlower = 4, kSiteMagma = 5,
    kSiteStar = 6, kSiteHousi = 7, kSiteWave = 8, kSiteMoya = 9, kSiteVrkumo = 10, kSiteRain = 11, kSiteSea = 12,
    kSiteLine = 13, kSiteJpa = 14, kSiteLayout = 15, kSites = 16
};
// packet ids: 8 per site (by pass, or by pass and variant), the first 8 unused
constexpr uint32_t kIdsPerSite = 8;
constexpr uint32_t kMaxPackets = kSites * kIdsPerSite;
constexpr uint32_t kMaxKinds = 8;
constexpr uint32_t kMaxBlocks = 6;         // instance blocks per packet (stage + location)
constexpr uint32_t kMaxBuffers = 2;        // attribute buffers of an instance's vertex input
constexpr uint32_t kNoDraw = 0xFFFFFFFFu;  // instance kind: effects only
constexpr uint32_t kMaxRegRanges = 6;      // register ranges per packet (declared) and per instance (values)
constexpr uint32_t kMaxRegWords = 64;      // register value words per instance

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
    uint32_t regCount = 0;
    uint32_t reg[kMaxRegRanges] = {};       // the instance register ranges: reg_key(stage (0 vertex, 1 pixel), offset)
    uint32_t regWords[kMaxRegRanges] = {};  // their sizes in words (multiples of 4)
};
constexpr uint32_t kPacketWords = sizeof(Packet) / 4;
inline uint32_t block_key(uint32_t stage, uint32_t location) { return stage << 16 | (location & 0xFFFF); }
inline uint32_t reg_key(uint32_t stage, uint32_t offset) { return stage << 16 | (offset & 0xFFFF); }
// the first register of a register range (GX2Set{Vertex,Pixel}UniformReg: the ALU constants, + 0x400 for vertex)
inline uint32_t reg_first(uint32_t key) { return ((key >> 16) == 0 ? 0x400u : 0u) + (key & 0xFFFF); }
// is [OFFSET, OFFSET + WORDS) of STAGE inside one of the packet's declared register ranges
inline bool reg_declared(const Packet& pk, uint32_t stage, uint32_t offset, uint32_t words) {
    for (uint32_t i = 0; i < pk.regCount && i < kMaxRegRanges; ++i)
        if ((pk.reg[i] >> 16) == stage && offset >= (pk.reg[i] & 0xFFFF) &&
            offset + words <= (pk.reg[i] & 0xFFFF) + pk.regWords[i])
            return true;
    return false;
}

// OP_PACKET_DRAW payload: id, kind, layout (block count | has-vertex << 8 | buffer count << 12 | has-range << 16 |
// register range count << 20), then per block {block key, address, size}, then the vertex input {SQ_PGM_START_FS
// word 0, word 1, then per buffer {attribute index, address, size, stride}}, then the index range {count, index
// address}, then per register range {reg_key(stage, offset), count, the values}
constexpr uint32_t kDrawHead = 3, kBlockWords = 3, kVertexHead = 2, kBufferWords = 4, kRangeWords = 2, kRegHead = 2;
constexpr uint32_t kMaxDrawWords = kDrawHead + kMaxBlocks * kBlockWords + kVertexHead + kMaxBuffers * kBufferWords +
                                   kRangeWords + kMaxRegRanges * kRegHead + kMaxRegWords;

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

// game threads: changes at every display list begin, end and call (gx2_core.cpp), so a site's packet defined in one
// command stream segment is defined again in the next
uint64_t list_serial();

const Packet* packet(uint32_t id);  // render thread: the defined packet, or nullptr
// render thread: OP_PACKET_DEFINE. A definition the same as the packet's current one, word for word, changes nothing:
// the packet's lowered state stays valid (g_epoch and a collected multi-draw batch stay as they are)
void define(const uint32_t* p, uint32_t n);
// render thread: OP_PACKET_DRAW (kind < meshCount) after its register effects were applied: the packet path, or the
// full draw. RANGE: the instance's own index range (the kind's primitive and index type), or null: the kind's mesh
void draw(const uint32_t* regs, const Packet& pk, uint32_t kind, const Mesh* range);
// render thread: is a write of N ALU constants from FIRST (relative to SQ_ALU_CONSTANT0_0: pixel 0..0x3FF, vertex
// 0x400..0x7FF) inside the declared instance register ranges of the packet whose instance was drawn last. Such a
// write keeps that packet's lowered state: its next instance patches every declared range from the registers (the
// material's colours, vertex colours and texture matrices of a layout pane; another draw in between lowers again)
bool instance_regs(uint32_t first, uint32_t n);
void frame_end();  // render thread, at each swap: the periodic counter line
bool batch_on();   // shader-rewrite batches on (packets on, WWHD_NATIVE_DRAW_BATCH not 0)
bool batch_site_on(uint32_t site);  // the site's kinds may be batched (WWHD_NATIVE_DRAW_BATCH_SITES)
// render thread: an instanced batch is collected (gfxvk); flush_batch draws it now
extern bool g_batchPending;
void flush_batch();
// one batch of INSTANCES instances (STREAM: a stream batch; INDIRECT: drawn as one indirect draw)
void count_batch(uint32_t site, uint32_t instances, bool stream, bool indirect);

// fallback / lowering reasons, counted per frame and site
enum Reason : uint32_t {
    kReplayed, kLowered, kVerified,
    kNotVulkan, kStereo, kFramegen, kGrassDistance, kBackdrop, kClouds, kRefused, kBufferChange,
    kBlockSize, kFetchChange, kStrideChange, kRangeChange, kMdi, kBatched,
    kRestored,  // (not a fallback) the lowered state kept by the register journal (registers changed and set back)
    kRegOutside, kReasons
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
