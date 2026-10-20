// Render-thread profiler shared by both renderers (render_prof.cpp).
//
// Opt-in diagnostics: every command op is counted; draws and register writes are timed on one
// call in 64 (and the phases inside those sampled draws), rare ops (clears, copies, flushes, swaps)
// on every call. Upload bytes are split by kind; "unique guest bytes" (the union of the guest ranges
// that vertex, index and uniform copies read) is measured on one frame in 16. A classifier counts
// draws whose registers changed only in buffer pointers / ALU constants since the previous draw
// (candidates for a continued-draw fast path).
//
// Every 120 swaps the counters are turned into a text report: logged with WWHD_PROFILE=1,
// WWHD_VK_STATS or WWHD_VK_CPU_ONLY_STATS=1, and kept for the overlay's "Copy performance report"
// button (latest_report()). Disabled by default; WWHD_PROFILE=1 enables it.
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>

namespace rprof {

enum Phase : int {
    // inside sampled draws
    kShader, kIndices, kTargets, kPipeline, kUniforms, kTextures, kDescriptors, kPass, kRecord, kVertex,
    kSubmit, kDrawPhases
};
enum Op : int { kOpRegs, kOpDraw, kOpClear, kOpCopy, kOpScan, kOpInvalidate, kOpFlush, kOpDrawDone, kOpSwap, kOpOther, kOps };
enum Upload : int { kUpOther, kUpVertex, kUpIndex, kUpUbo, kUpUniformVars, kUpTexture, kUploadKinds };
enum Wait : int { kWaitGpu, kWaitAcquire, kWaitPresent, kWaits };
enum Sync : int { kSyncDrawDone, kSyncCopySurface, kSyncFlip, kSyncOther, kSyncs };

bool enabled();
uint64_t now_ns();
uint64_t thread_cpu_ns();  // CPU time of the calling thread (0: not available)

// ---- render thread
extern bool g_draw_sampled;  // the current draw is sampled (phase marks are recorded)
extern uint64_t g_mark;      // time of the previous phase mark
void mark_slow(Phase p);
inline void mark(Phase p) {
    if (g_draw_sampled) mark_slow(p);
}

// op timing around execute_one: begin() returns 0 when this call is not timed
uint64_t op_begin(Op op);
void op_end(Op op, uint64_t started);

// uploads: the kind of the copies made while the scope lives
extern int g_upload_kind;
struct UploadKind {
    int saved;
    explicit UploadKind(Upload k) : saved(g_upload_kind) { g_upload_kind = k; }
    ~UploadKind() { g_upload_kind = saved; }
};
void add_upload(uint64_t bytes);            // in g_upload_kind
void add_upload(Upload kind, uint64_t bytes);
// a copy of guest memory [addr, addr+size) (vertex, index, uniform block): unique-byte tracking
extern bool g_track_unique;
void guest_read_slow(Upload kind, uint32_t addr, uint64_t size);
inline void guest_read(Upload kind, uint32_t addr, uint64_t size) {
    if (g_track_unique) guest_read_slow(kind, addr, size);
}

void add_wait(Wait w, uint64_t ns);         // render thread blocked on the GPU / swapchain
void add_idle(uint64_t ns);                 // render thread waiting for commands

// draw classifier (render thread, gx2 register application)
extern uint32_t g_reg_dirty;  // bit 0: buffer/constant registers changed, bit 1: other registers changed
void note_other_reg(uint32_t reg);          // a non-buffer register changed value
void classify_draw();                       // at each draw: count its class, clear g_reg_dirty
// ALU constants, uniform-block and vertex-buffer words (gx2 looks this up in its register class table)
inline bool fast_class_reg(uint32_t reg) {
    // RegDefines.h: mmSQ_ALU_CONSTANT0_0 0xC000; mmSQ_TEX_RESOURCE_WORD0 0xE000 + 0x7E0 (VS uniform
    // blocks), + 0x250 (PS), + 0xCB0 (GS), + 0x8C0 (vertex attribute buffers), 7 words each
    if (reg >= 0xC000 && reg < 0xC000 + 0x1000) return true;
    for (uint32_t base : {0xE7E0u, 0xE250u, 0xECB0u, 0xE8C0u})
        if (reg >= base && reg < base + 7 * 16) return true;  // includes the vertex stride (pipeline-only)
    return false;
}

// shader translations: called by the Vulkan renderer for each new variant (shaders.cpp)
void shader_variant(bool newProgram, bool onlyUnusedUnits, const char* const* groups, int groupCount);

// once per swap (render thread); hold: drawn in an interpolation hold pass
void frame_end(bool hold);

// ---- game thread
void add_sync(Sync site, uint64_t ns);
// GPU results written back to guest memory (linear surfaces the CPU reads, issue #53): the time the
// GX2DrawDone check spent walking the surfaces (walkNs), and per write-back its bytes and the time to
// read them from the GPU (readNs, including the wait for the GPU)
void add_write_back(uint64_t walkNs, uint32_t surfaces, uint64_t bytes, uint64_t readNs);

// ---- any thread
std::string latest_report();  // the last complete report ("" before the first one)

}  // namespace rprof
