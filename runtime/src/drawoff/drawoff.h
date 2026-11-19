// drawoff: the game thread's DrawDone wait off the critical path (decomp build, fast mode only).
//
// Today sead's swap ends every pass with GX2DrawDone, and the port's lazy DrawDone still blocks the game thread until
// the render thread has executed everything queued (gx2_core.cpp render_sync). The render thread runs one display list
// behind the game: the list of pass N executes while pass N+1 builds its own, and pass N+1's DrawDone waits for it.
// So pass N+2's calc (logic + draw-list build) only starts once list N is done, and the render thread idles while it
// runs.
//
// Stage c1 (WWHD_DRAWOFF=1): GX2DrawDone queues a fence and returns; the game thread waits for that fence just before
// the next pass's painter (the sead render, where the agl layer jobs record and the lists are called). The next pass's
// calc then overlaps the render thread's tail. Nothing is drawn differently, the render thread executes the same
// commands in the same order, and a frame is presented no later than today.
//   Full waits stay (DrawDone as today) on passes that draw into a linear render target the CPU reads back (Picto Box:
//   two passes after a GX2SetColorBuffer with a linear-aligned surface), and wherever the port syncs anyway (save
//   states, GX2CopySurface outside lists, the uncapped benchmark's vsync): every later render_sync also covers the
//   pending fence.
//
// Stage c2 (WWHD_DRAWOFF=2): one pass in flight. The game no longer waits for the render thread to catch up with the
// last swap before it starts the next pass: sead's waitForSwap ends once the flips are at most one swap behind (the
// port's GX2GetSwapStatus counts one swap in flight as flipped; the first GX2WaitForVsync after a swap returns at once
// while no more than one is in flight), and GX2DrawDone queues a fence and returns. The explicit cap: the first
// GX2DrawDone of pass N waits for the last fence of pass N-1 (so the render thread is never more than that behind;
// the flip wait gives the same bound). The game then records pass N+1's list while the render thread still executes
// list N (and, at the start of N+1, list N-1's tail): latency up to one pass more, no idle render thread.
//   Hazard (c1 census): the game double-buffers dynamic vertex data per pass, so pass N+1's calc and painter write the
//   half list N-1 reads. c2 copies those ranges when a list is called (the census's classes: buffers bound through
//   GX2SetAttribBuffer while a list is recorded, other than J3D's static model data, and the 3D line, sea and text
//   packet instances) and the renderers read the copy at the original address (vertex_source.h WWHD_VERTEX_PTR), so
//   described draws keep their register state (c1's ring addresses broke them). Binds outside lists get a copy at
//   the bind, kept until the frame's swap.
//   Full waits as c1 (Picto Box linear targets, save states, CopySurface, uncapped vsync).
//
// Switches (read once):
//   WWHD_DRAWOFF=1          c1 (default 0: nothing changes)
//   WWHD_DRAWOFF=2          c2: one pass in flight (Vulkan renderer only; else off). See "Stage c2" below.
//   WWHD_DRAWOFF_CENSUS=1   hazard census (measurement only; meant with WWHD_DRAWOFF=0): the render thread notes the
//                           guest ranges it reads live (not from a submitted-uniform snapshot) per frame with a content
//                           hash; the game thread hashes the read set of the list two passes back at the pass entry
//                           (today's sync point: that list is done) and again where c1 waits (before the painter).
//                           Ranges changed in between were written by the calc c1 overlaps: the hazards. Logged
//                           every WWHD_DRAWOFF_CENSUS_EVERY passes (300) as "[drawoff] census".
//   WWHD_DRAWOFF_TRACE=a:n  event trace of n passes from pass a (game: pass entry, per-frame function, display-list
//                           begin, top-level calls, DrawDone, swap; render: list execution, swap, fences) in the log.
//
// Hooks (all behind #ifdef WWHD_DECOMP_FAST in shared files):
//   interp.cpp hook_0203593C: pass_entry() first; frame_begin()/frame_end() around the per-frame function.
//   gx2_core.cpp: GX2DrawDone -> defer_draw_done(); the painter's first GX2 call on the game thread -> painter_point()
//   (see gx2_core); call_display_list / BeginDisplayList -> trace events; execute OP_CALL/OP_SWAP -> render side.
//   gx2_resources.cpp GX2SetColorBuffer -> linear_target().
//   render_prof.h guest_read and surfaces.cpp upload_surface -> read() (census only).
#pragma once
#include <cstdint>

namespace drawoff {
int mode();  // WWHD_DRAWOFF (0 off)
inline bool on() { return mode() != 0; }
extern bool g_census;  // WWHD_DRAWOFF_CENSUS (set once at start; the render thread's read hooks test it)
extern bool g_trace;   // WWHD_DRAWOFF_TRACE given

// interp (game thread)
void pass_entry();
void frame_begin(bool hold);
void frame_end();

// gx2, game thread
bool defer_draw_done(uint32_t lr);  // after GX2DrawDone's OP_DRAW_DONE: true = do not wait now
void painter_point(const char* why);  // c1 wait point (and census point (b))
void linear_target();               // a linear-aligned colour buffer was set (any thread)
void note_call(uint32_t list, uint32_t size);   // a top-level display-list call (trace)
void note_begin_list(uint32_t lr);  // GX2BeginDisplayList (any thread; trace)
void note_swap();                   // GX2SwapScanBuffers (trace, census)
void draw_done_returned();
void census_dd();                   // GX2DrawDone before its wait (census point (c))
bool c2_notes();                    // c2, or the census noting c2's copies          // GX2DrawDone after its wait (census point (a))
bool in_frame();                    // inside the per-frame function
// vertex copies (any recording thread): the address vertex buffer [addr, addr + size) of a packet instance of site
// CLS is read from; with c1 a copy when it is recorded into a display list (WWHD_DRAWOFF_COPY: 0 never, 1 also
// without c1)
constexpr uint32_t kClassLine = 13, kClassSea = 12, kClassText = 16;  // native_draw kSiteLine, kSiteSea, kSiteText
uint32_t vertex_source(uint32_t cls, uint32_t addr, uint32_t size);
// GX2SetAttribBuffer from guest code at LR: the census notes the caller; a buffer bound while recording is copied
uint32_t attrib_source(uint32_t lr, uint32_t addr, uint32_t size);
bool copies_on();
// ---- c2 (WWHD_DRAWOFF=2)
inline bool c2() { return mode() == 2; }
bool lists_on();  // list_begin / list_end / list_call / nested_call wanted (c1 copies or c2)
// GX2GetSwapStatus / GX2WaitForVsync (game thread): how many swaps the game may be ahead of the executed flips (c2: 1,
// 0 while a full wait is due)
uint32_t swaps_ahead();
// interp's step pacing (game thread): the frames a pass's picture flips after the pass that drew it, beyond the one of
// today (c2: 1, its list is called at the next pass's start; else 0)
int frames_in_flight();
// interp's pass cost under c2 counts the render thread's share (WWHD_DRAWOFF_RENDER_COST, default 1; 0: the flip waits
// leave the pass cost whole and interp decides early in a pass, as before): the waits for a flip that is due but not
// rendered yet stay in the pass's cost, and a pass's in-between decision counts the pass at its recent duration
bool render_cost();
// gx2_core's flips (vsync_pacing.h flip_executed): the frames at the drawn rate a late flip may take beyond one vsync
// and keep the grid (c2 with WWHD_DRAWOFF_GRID_KEEP unset or 1: frames_in_flight(); else 0)
int grid_keep_frames();
// gx2_core's flips: the time a flip counts as executed (c2 with WWHD_DRAWOFF_FLIP_TIME unset or 1): the later of its due
// time and the time the render thread finished its frame, not the game's next GX2 vsync call (under c2 the game does not
// wait for flips, so it notices one up to a whole logic pass late: in-between frames counted 20+ ms late, moved the grid
// and came out bunched with the next logic frame); 0: the game's next call (as before)
bool flip_time();
// interp's paced step start (WWHD_DRAWOFF_SLOT_AHEAD, c2, default 1; 0 = as before): the step slot (ns) to plan the next
// logic pass with. Under c2 a logic frame reaches the render thread a pass later, so after a dropped rest of a step (no
// in-between pass between two logic passes) its flip has not executed when the next logic pass starts: gx2's step slot is
// still that step's own. Counted from it the next step looked a whole step late and started a new schedule (phone, cliff,
// 120 fps: 95 % of ~120 N a minute) or kept the old start (its in-between frames planned a step short). A slot whose
// logic pass would start less than half a step after the last one (`last`) is moved on by whole steps, as gx2's
// next_slot does once that flip executes. `lead`: the start's lead before its slot; `step`: the step length.
int64_t step_slot(int64_t slot, int64_t last, int64_t lead, int64_t step);
// a display-list call recorded into the list this thread records (c2: the called list's vertex ranges join it)
void nested_call(uint32_t list);
// a top-level display-list call (game thread): its vertex snapshot (c2; null: none), handed to the render thread
void* vertex_snapshot(uint32_t list, uint32_t size);
// a GX2SetAttribBuffer outside any list (game thread, c2): a copy of [addr, addr + size), or null
void* vertex_snapshot_now(uint32_t addr, uint32_t size);
// render thread: the vertex snapshot of the call that follows (null: the call ended); a bind's copy outside lists
// (kept until the frame's swap)
void render_vertex_snapshot(void* s);
void render_vertex_overlay(void* s);
const uint8_t* vertex_bytes(uint32_t addr, uint32_t size);  // render thread: the submitted bytes, or null (live memory)
// render thread: the guest address whose submitted bytes (vertex_bytes) P points at, SIZE bytes long; false: not a copy
bool vertex_addr_of(const void* p, uint32_t size, uint32_t& addr);
extern bool g_vertex_on;  // c2 on (set once, before the first frame)
void c2_note(int what);
void add_flip_wait_ns(int64_t ns);  // c2: a GX2WaitForVsync's time (game thread)
int64_t take_flip_wait_ns();        // c2: the vsync waits since the last call (interp's pass cost leaves them out)   // gx2_core counters: 0 a vsync wait skipped, 1 one swap in flight counted as flipped
// GX2BeginDisplayList / GX2EndDisplayList (the recording thread) and every display-list call: a list recorded with
// copies is good for the pass that recorded it only (its ring slice is reused three passes later); a call of it in a
// later pass drains (counted)
void list_begin(uint32_t start);
void list_end(uint32_t size);
void list_call(uint32_t list);

// test aid, WWHD_DRAWOFF_DUMP_BY_SWAP=1 (any WWHD_DRAWOFF; =2: named script frame x 10 + the pass's index in it, so every
// pass of an interpolated step has its own name): frame dumps (WWHD_DUMP_FRAMES) named by the script frame the
// game had at the frame's swap instead of the one live when the render thread reaches it. With the step clock and frame
// interpolation the live step count names a pass by how far the render thread lags (c2 lags a pass more): off and c2
// dumped different passes under one name.
// =3: the game's world clock (mTimer, which the load freezes and the fixed seed's arrival keeps) x 10 + the pass's index:
// arms whose stage load took a different number of steps (host timing) still dump the same world-clock passes.
void note_swap_script(uint64_t swap, uint32_t timer);       // game thread, at GX2SwapScanBuffers (its swap number, mTimer)
uint64_t dump_script_frame(uint64_t frame, uint64_t live);  // render thread: frame's name (live: today's)
// render thread
enum Kind : int { kOther = 0, kVertex = 1, kIndex = 2, kUbo = 3, kTexture = 5, kList = 6, kKinds = 8 };
extern thread_local uint32_t g_tag;  // census: the command being executed (op | packet site << 8)
void read_slow(int kind, uint32_t addr, uint64_t size);
inline void read(int kind, uint32_t addr, uint64_t size) {
    if (g_census) read_slow(kind, addr, size);
}
void render_call(uint32_t list, uint32_t size, bool begin, int depth);  // OP_CALL executed (trace; census: kList)
void render_swap();                 // OP_SWAP executed: the render frame ends
void render_fence(uint64_t id);     // OP_FENCE executed (trace)
}  // namespace drawoff

namespace gx2 {
// gx2_core.cpp (fast mode): a fence the game does not wait for now; the wait for it (DrawDone accounting)
uint64_t drawoff_fence();
void drawoff_wait(uint64_t id);
bool drawoff_fence_done(uint64_t id);
bool submitted_uniform_covers(uint32_t addr, uint32_t size);  // (render thread: inside a block of the active snapshot)
void drawoff_vertex_overlay(void* v);  // c2: a bind's copy outside lists, in order with the commands (game thread)
bool recording_display_list();
}  // namespace gx2
