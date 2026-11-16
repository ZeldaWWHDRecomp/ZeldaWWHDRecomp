// Native J3D material descriptions (the port's native renderer, stage D0). Decomp build, fast mode, cmake
// WWHD_NATIVE_DRAW=ON only; nothing here exists in any other build. Code: native_draw/j3d_desc.cpp (game-side entry
// points, tables, render-side verify) and gfx/vulkan/draw.cpp (one call: what a described draw resolved).
// Game side: decomp JSystem/J3DGraph/J3DNativeDesc.cpp (one hook in j3d::ShapePacket::submitShape, after the
// program choice), port_native_draw.h wwhd_j3ddesc_*.
//
// A J3D shape draw is described by three published records and one command:
//   Desc  per (material binding, pass, program record, view class): the static part of the draw, made from the game
//         objects the site's bind calls read. Validity key = exactly those objects' words (program record, frame
//         record, shader + its display list, draw state, binding state parts, the vertex streams and the material
//         sampler slots, see J3DNativeDesc.h). The register image is made at define by running the site's own bind
//         callees (same code, same inputs) into a capture that is never emitted, in three segments in site order:
//         program (shader mode, stage programs or the program's display-list call), material (textures/samplers of
//         the material units, vertex streams/fetch shader) and state (render state of passes 1/2; pass 0's fixed
//         state + alpha state).
//   Env   per (draw input, frame record, pass, view), re-made once per game frame and whenever its key changes: the
//         pass's frame textures (units 8-14, RenderTextureBindings::bindExtra) and, in pass 0, the view's projection
//         render state (applyShapeProjection). Units 8-14's bindings and the projection belong to the pass, not to
//         the material: pass-0 cascade slices share one Desc.
//   Mesh  the index range the shape draws (agl::gx2util::IndexedMesh::draw: primitive, count, index type, index
//         address, base vertex), published once.
//   OP_J3D_DESC_DRAW (gx2_cmd.h 0x50): {desc id, desc version, env id, env version, mesh id, flags, block count,
//         blocks {stage << 16 | location, address, size}}. The blocks are the draw's uniform blocks in the site's
//         order (view, model matrices, shape, material, extra), computed game-side from the objects exactly as the
//         bind calls compute them. Their contents stay where the game wrote them.
// Records are not commands: J3D draws are recorded into display lists that execute later (possibly again), so every
// version is published, immutable, into tables shared with the render thread before the first draw naming it is
// recorded; the render thread finds it by (id, version).
//
// Register effect of a described draw (what the site's calls leave in the register file): env image, then the desc
// segments (program, material, state), then the blocks, net per register. Registers the draw reads but J3D never sets
// (ambient: texture words of units a program declares but J3D never binds, A0E1/A0E2, A187, ...) are not in it: they
// are taken as a per-pass snapshot (user decision 1, 2026-11-14): if one changes mid-pass, that draw falls back to the
// translated path.
//
// Switch WWHD_NATIVE_J3D_DESC (with packets on; fast build default 2; 0 = off: nothing described, nothing emitted):
//   1  describe and verify (D0): the site keeps every GX2 call and its draw; OP_J3D_DESC_DRAW goes before the site's
//      binds and the render thread checks it at the site's draw (the next OP_DRAW_INDEXED).
//   2  (D1, the default) described draws: the site makes no GX2 call; the render thread binds and draws from the
//      records.
// WWHD_NATIVE_J3D_DESC_VERIFY=N (mode 1; default 1): every Nth described draw is checked (0: none, counters only).
// WWHD_NATIVE_J3D_DESC_TIMING=1: game-thread cost of the description (key compare, whole hook) in the counter line.
// Counters: one "[j3d-desc]" line per 300 frames (inventory, defines, verify results, ambient, timing).
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include <cstdint>
#include <vector>

namespace gfxvk::vk { struct Shader; }
struct LatteFetchShader;
namespace native_draw::j3ddesc {

int mode();  // WWHD_NATIVE_J3D_DESC (0 unless packets are on)

// the description key's fixed words (decomp J3DNativeDesc.h kKeyFixedWords; layout there), then its variable part
constexpr uint32_t kKeyFixed = 41;
// capture segments (wwhd_j3ddesc_segment); the order of the register effect is env, program, material, state
enum Segment : uint32_t { kSegNone = 0, kSegProgram = 1, kSegMaterial = 2, kSegState = 3, kSegEnv = 4, kSegs = 5 };

// why a J3D draw (or the attempt at one) is not described: the inventory (wwhd_j3ddesc_skip, J3DNativeDesc.h has the
// same values). 0 = described.
enum Reason : uint32_t {
    kDescribed = 0,
    kPassMasked,      // submitShape returned at the pass mask (no draw: counted, not a J3D draw)
    kNoResource,      // no shape resource (no draw)
    kNamedFrame,      // the "wii_pipeline" fallback program path (model without direct programs)
    kNoProgram,       // the program record is the null record (index out of range)
    kListBinds,       // the program's display list binds blocks or textures, or is not readable
    kForeign,         // a define capture held a command that is not a register write or a list call
    kPortFeature,     // a port feature acts per draw (stereo, generated 60 fps, GamePad backdrop): the site draws
    kTableFull,       // ids exhausted
    kPrepareOnly,     // ShapePacket::prepareShape (the ship sight): the site's calls stay (not hooked)
    kNotRecording,    // the draw is not being recorded into a display list (submitted-uniform snapshots differ)
    kReasons
};

// ---------------------------------------------------------------- records (immutable once published)
struct Desc {
    uint32_t id = 0, version = 0;
    uint32_t pass = 0, binding = 0, record = 0, frame = 0, shader = 0, viewClass = 0;
    std::vector<uint32_t> key;                // the validity words at define (J3DNativeDesc.h DescKey + variable part)
    std::vector<uint32_t> segment[kSegState + 1];  // [kSegProgram..kSegState]: commands as gx2::emit encodes them
    std::vector<uint8_t> lists;               // the bytes of the display lists the program segment calls, at define
    const Desc* prev = nullptr;               // the version before
};
struct Env {
    uint32_t id = 0, version = 0;
    uint32_t pass = 0, view = 0, input = 0, frame = 0, projection = 0;
    std::vector<uint32_t> key;    // the env key words at capture (J3DNativeDesc.h EnvKey)
    std::vector<uint32_t> image;  // commands (frame textures; pass 0: projection render state)
    uint64_t gameFrame = 0;       // the game frame it was captured in
    const Env* prev = nullptr;
};
struct Mesh {
    uint32_t prim = 0, count = 0, indexType = 0, indexAddr = 0, baseVertex = 0;
};
// OP_J3D_DESC_DRAW payload
constexpr uint32_t kDrawHeaderWords = 7;  // desc id, desc version, env id, env version, mesh id, flags, block count
constexpr uint32_t kBlockWords = 3;       // stage << 16 | location (stage 0 vertex, 1 pixel, 2 geometry), address, size
constexpr uint32_t kMaxBlocks = 16;
enum DrawFlag : uint32_t {
    kFlagRecording = 1,  // emitted while a display list is recorded
    kFlagDefined = 2,    // this draw (re)defined its desc
    kFlagEnvMade = 4,    // this draw (re)captured its env
    kFlagProgramBind = 8,  // the site binds the program in this draw (shader != the render context's argument:
                           // setProgramShaderMode and the program list run, with the segment's OP_INVALIDATE)
    // bits 8-15: index + 1 of the draw's first block of the pass's extra (scene) block, 0: none. Its ring index is
    // flipped by another thread while J3D draws are recorded; the site's read and the description's can see either
    // slot (the game's own race, lead ruling 2026-11-14)
    kFlagExtraShift = 8, kFlagExtraMask = 0xFF00,
};
const Desc* find_desc(uint32_t id, uint32_t version);  // render thread (any thread after publication)
const Env* find_env(uint32_t id, uint32_t version);
const Mesh* find_mesh(uint32_t id);

// ---------------------------------------------------------------- port internals (gx2_core.cpp, draw.cpp)
// game threads: a define segment is open; gx2::emit hands every command to capture(), which consumes it (true)
extern thread_local bool t_capturing;
bool capture(uint32_t op, const uint32_t* p, uint32_t n);
// the same for gx2::set_regs, which writes its command in place (recording buffer or render queue), not through emit
bool capture_regs(uint32_t first, const uint32_t* values, uint32_t count);
// render thread
void draw(const uint32_t* regs, const uint32_t* p, uint32_t n);  // OP_J3D_DESC_DRAW (verify mode: held pending)
extern bool g_pending;                                           // a described draw waits for the site's draw
// the site's OP_DRAW_INDEXED (before render::draw): the verify of the pending description against the register file
void site_draw(const uint32_t* regs, const uint32_t* p, uint32_t n);
// what the full draw resolved (gfx/vulkan/draw.cpp, after its resolution; only while g_resolving)
extern bool g_resolving;
struct Resolved {
    uint64_t vsKey = 0, psKey = 0, fsKey = 0;
    const void* pipeline = nullptr;
    const void* colors[8] = {};
    const void* depth = nullptr;
    uint32_t textureCount[2] = {};
    struct Texture {
        uint32_t unit = 0;
        const void* surface = nullptr;
        uint64_t view = 0, sampler = 0;
    } textures[2][18];
    std::vector<uint32_t> reads;  // the registers the resolution read (read set: ambient detection)
};
void resolved(const uint32_t* regs, const Resolved& r);
// the registers a full draw with these programs reads (shader/fetch/target/output/dynamic groups, stencil masks, the
// programs' texture, sampler, uniform-block and uniform-register words, the fetch shader's attribute buffers)
void read_regs(const gfxvk::vk::Shader* vs, const gfxvk::vk::Shader* ps, const LatteFetchShader* fs,
               std::vector<uint32_t>& out);
void game_swap();  // game thread, GX2SwapScanBuffers: the game frame (content checks, env re-capture)
void command(uint32_t op);  // render thread: any other command between a described draw and the site's draw
// WWHD_NATIVE_J3D_DESC_UNITS=COUNT[:N] (debug, both modes): for the J3D draws with mesh index count COUNT, which
// command last wrote pixel texture units 0-7 (gx2_core.cpp OP_SET_REGS / every other command call in)
extern bool g_trace;
void trace_write(uint32_t first, uint32_t count);
void trace_op(uint32_t op, const uint32_t* p, uint32_t n);
void frame_end();           // render thread, at each swap: the counter line
void reset();               // render thread: a save-state load or shader reset

}  // namespace native_draw::j3ddesc
#endif
