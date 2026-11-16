// Native J3D material descriptions, stage D1, render side: described draws (WWHD_NATIVE_J3D_DESC=2). Decomp build,
// fast mode, cmake WWHD_NATIVE_DRAW=ON only; nothing here exists in any other build. The records and the command are
// gx2/native_j3d_desc.h (stage D0); in mode 2 the J3D site makes no GX2 call and OP_J3D_DESC_DRAW is the draw.
// Code: native_draw/j3d_desc_draw.cpp (records -> items, the run's register effects, dispatch, counters) and
// native_draw/j3d_desc_vk.h (included by gfx/vulkan/draw.cpp: the Vulkan states and the per-draw binds and draw).
//
// Item: one (Desc version, Env version), resolved once: the net register effect of env image + program, material,
//   state segments (display lists from the define's bytes), as the site's calls leave it. Kept with the Desc version
//   (records are immutable and never freed), found per draw by index (desc id slot), not by a register read.
// State (Vulkan, per item and target pass): made from the item's first full draw (the translated path, captured): the
//   pipeline, descriptor layouts and writes, support bytes + copy list, vertex buffers, index form, dynamic state, the
//   textures, and the ambient words (what the draw reads that neither the item nor the draw's blocks write). A later
//   draw of the item into the same targets is drawn from the state: lookups by index, binds, the draw; no register read
//   (ambient words are checked once per run per ambient class, see below).
// Register effects (what later commands read): a described draw drawn from its state writes no register. Its effect
//   (item + blocks, in that order) is appended to the open run; before any other command that may read or write the
//   register file (and at the end of every command chunk) the run is settled: each register written by the run gets
//   the value of its last write in the run, through apply_regs, once (exact by construction: the run is replayed
//   backwards, first-seen per register wins; WWHD_NATIVE_J3D_DESC_RUNCHECK=1 checks it against the sequential order).
// Ambient words (registers the draw reads but J3D does not set: user decision 1, 2026-11-14, exact: (a)):
//   - not written by any J3D item or block bind seen so far (the "J3D-written" set W): they change only by other
//     commands, which end the run. They are interned as ambient classes (same registers, same values) and each class
//     is compared once per run in which it is used (memcmp); a difference is a fallback to the translated path.
//   - in W (e.g. a texture unit a program declares but this material does not bind, bound by another J3D material):
//     the run is settled before the draw (the register file then holds the run's effects) and they are compared.
//     W only grows; a state re-splits its ambient words when it grew (g_wGen).
// Fallback (the translated path, per draw, counted by reason): settle the run, apply the item's effect and the blocks
//   through apply_regs, render::draw (captured: a new state).
// WWHD_NATIVE_J3D_DESC_VERIFY=N (mode 2: default 0): every Nth draw that would be drawn from its state is drawn the
//   translated way instead and what it resolved is compared with the state (pipeline, programs, targets, raster,
//   descriptor writes, textures, support bytes, vertex layout, index form, the state's stored register words).
// WWHD_NATIVE_J3D_DESC_D1DEBUG=1: a fallback reason histogram every 300 frames.
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include "native_j3d_desc.h"
#include <cstdint>
#include <vector>

namespace native_draw::j3ddesc::d1 {

using Apply = void (*)(uint32_t first, const uint32_t* v, uint32_t n);

bool on();  // mode() == 2 and the render thread runs (WWHD_NO_RENDER_THREAD: the switch has no effect)

struct Item {
    const Desc* desc = nullptr;
    const Env* env = nullptr;
    // the net register effect (env image, program, material, state; each register once, its last value), sorted by
    // register, and the same as runs of consecutive registers {first, count} over VALUES
    std::vector<uint32_t> regs, values, runs;
    // the GX2Invalidate calls in the site's binds {flags, address, size} (OP_INVALIDATE: no register effect; executed
    // for every draw of the item, before it, as the site's calls make them)
    std::vector<uint32_t> invalidates;
    std::vector<uint32_t> programInvalidates;  // those of the program segment: only when the site binds the program
    const char* refused = nullptr;  // not a plain register effect: drawn the translated way, commands executed
    void* render = nullptr;         // the renderer's states (j3d_desc_vk.h)
    uint64_t settled = 0;           // the settle that took its effect last (once per settle)
    // its writes of watched registers (V: J3D-written words some state reads as ambient), made again when V grew
    std::vector<uint32_t> vregs, vvals;
    uint64_t vGen = 0;
    bool value(uint32_t reg, uint32_t& v) const;
};
// a uniform block bind of the draw: its first register (stage base + location * 7), address, size in bytes
struct Block { uint32_t reg = 0, addr = 0, size = 0; };
inline void block_words(const Block& b, uint32_t w[7]) {  // (as gx2_resources.cpp's uniform block bind writes it)
    w[0] = b.addr, w[1] = b.size - 1, w[2] = 0, w[3] = 1, w[4] = 0, w[5] = 0, w[6] = 0xC0000000u;
}
struct In {
    uint32_t* regs = nullptr;  // the register file (g_regs): read only by the translated path and a volatile check
    Item* item = nullptr;
    const Mesh* mesh = nullptr;
    uint32_t blockCount = 0;
    Block blocks[kMaxBlocks];
    const uint32_t* raw = nullptr;  // the command's blocks as sent ({stage << 16 | location, address, size}; D2's depth)
    uint32_t rawCount = 0;
};

// the J3D-written set W (render thread)
extern uint64_t g_wGen;   // moves when W grows
bool written(uint32_t reg);
void add_written(const uint32_t* regs, size_t n);
void add_written_block(uint32_t first);  // a block bind's 7 registers

// the run (render thread)
extern uint64_t g_runSerial;  // moves at every command that ends a run (and at a reset)
extern uint32_t g_runCount;   // draws in the open run
void settle_run();             // the run's effect into the register file (no-op without one)
void push_run(const In& in);   // a draw drawn from its state: its effect joins the run
// the value REG has with the run applied (the run's last write of it, else the register file), without settling;
// false: the run is long and holds none in its last entries (the caller settles and reads the file)
bool effective(uint32_t reg, uint32_t& value);
// V: registers looked up with effective() (a state's J3D-written ambient words); the run keeps their last values
void add_watched(const uint32_t* regs, size_t n);
void apply_effects(const In& in);  // the translated path: run settled, then the item's effect and the blocks applied
// gx2_core.cpp: before every command but OP_NOP, OP_J3D_DESC_DRAW and commands that neither read nor write registers
inline void foreign() {
    ++g_runSerial;
    if (g_runCount) settle_run();
}

// counters (render thread)
enum Count : uint32_t {
    cDraws, cReplayed, cFull, cVerified, cMismatch, cItems, cItemsRefused, cStates, cStatesRefused, cRuns, cSettles,
    cSettleWords, cClassChecks, cClassWords, cVolatileSettles, cLost, cGated,
    // fallback reasons
    fNoState, fNoPass, fRefusedState, fItemRefused, fSurfaces, fBinds, fAmbient, fVolatile, fTexture, fIndex,
    fOffset,
    // (WWHD_NATIVE_J3D_DESC_D1TIMING=1) render-thread ns: described draws (whole), settles
    tDrawNs, tSettleNs, cInvalidates, cLiveVertex, cVolatileWords, tInvalidateNs, pChecks, pTextures, pIndices, pSets, pRecord, pAfter, tVkNs,
    // (WWHD_NATIVE_J3D_DESC_PASSBEGIN=1) described draws that began their pass; refusals: cloud settings, no targets, no
    // state for the targets, a pass subset draw() could take
    cPassBegun, cPassBeginClouds, cPassBeginLookup, cPassBeginNoMatch, cPassBeginSubset,
    // (D1TIMING) us: the translated fallbacks (full draw), their state making and replacement
    tFallbackFullNs, tFallbackMakeNs,
    // (MSMEMO) ambient lists taken from the memo, classes kept; (SUPPORT2) support slices built, reused
    cMemoHits, cMemoClassKept, cSupBuilt, cSupReused, cCounts
};
void count(Count c, uint64_t n = 1);
void note(const char* why);  // (WWHD_NATIVE_J3D_DESC_D1DEBUG=1) a fallback reason
void note_site(const char* why, const Item& it);  // (_D1DEBUG=2) the same per desc id, pass and env id
bool debug_on();
uint32_t verify_every();

// gx2_core.cpp
void draw(uint32_t* regs, Apply apply, const uint32_t* p, uint32_t n);  // OP_J3D_DESC_DRAW, mode 2
void frame_end();  // render thread, at each swap: the counter line every 300 frames
void reset();      // render thread: shaders, pipelines or samplers reset (states hold them)

}  // namespace native_draw::j3ddesc::d1

#ifdef WWHD_HAS_VULKAN
namespace gfxvk {
// the described draw on the Vulkan renderer: drawn from its state, or the translated way (j3d_desc_vk.h)
void j3ddesc_draw(const native_draw::j3ddesc::d1::In& in);
void j3ddesc_clear();
}
#endif
#endif
