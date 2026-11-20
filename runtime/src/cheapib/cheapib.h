// cheapib: cheap in-between frames (decomp build, fast mode only; cheapib/DESIGN.md). With interpolation at 120 fps a
// 30 Hz logic step draws 1 logic pass and 3 hold passes; today every hold pass rebuilds the whole frame (every
// process's draw, agl's layer jobs recording new display lists, the render thread executing them). cheapib makes
// in-between frames cheaper on both threads.
//
// Switch WWHD_CHEAPIB (default 0: nothing changes; read once):
//   8  S1, test-only ceiling: hold passes skip fpcDw_Handler (every process's draw) and set agl's replay flag (manager
//      flag 0x200 of the HD RenderQueueManager): agl's update skips the layer jobs and the painter calls the logic
//      pass's submitted display lists again, recording nothing. No patches: in-betweens repeat the logic frame's
//      picture, and the record pass draws nothing new (next-step model blends and shadow-cascade cameras go stale).
//   9  S2, = S1 plus: the replayed painter's commands do not reach the render thread (skip + re-present: the render
//      CPU floor). Each painter call is checked against the logic pass's (address, size, content hash); a mismatch
//      sends the rest of the window normally (counted).
//   1  stage 1 (cheapib/DESIGN.md §6): as S1, plus the moving models' uniform blocks patched into the logic pass's
//      ring slots (interp's blend and the UBO update 027F5018; cheapib_models.h). Camera block and dynamic sites
//      (sea, particles, sway, cloth, trails, lines) not yet: frozen at the logic pass.
// Counters: logged every 300 logic steps ("[cheapib] ...").
//
// Hooks (all behind #ifdef WWHD_DECOMP_FAST in shared files):
//   interp.cpp hook_0203593C: pass_entry() first, frame_begin()/frame_end() around the per-frame function;
//   decomp f_pc_manager.cpp fpcM_Management: PORT_CHEAPIB_SKIP_DRAW() (port_hooks.h) -> wwhd_cheapib_skip_draw();
//   decomp agl/RenderQueue.cpp drawView: PORT_CHEAPIB_REPLAY() -> wwhd_cheapib_replay() (update, which runs in the painter
//   before it, clears the manager's 0x200);
//   gx2_core.cpp: call_display_list -> drop_call(), enqueue/enqueue_regs -> drop_op(), recording -> note_recorded().
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cheapib {
int mode();  // WWHD_CHEAPIB (0 off)
inline bool on() { return mode() != 0; }

// interp's pass entry (hook_0203593C), before anything else: the previous pass's painter window ends here
void pass_entry();
// around the per-frame function f_0203593C_orig: HOLD (an in-between pass), INTERP (interpolation on)
void frame_begin(bool hold, bool interp);
void frame_end(bool hold, bool interp);

// stage 1 (WWHD_CHEAPIB=1): hold passes replay the logic pass's lists, with the moving parts patched (cheapib_models.h)
bool stage1();
// inside the per-frame function of a replayed hold pass
bool replaying();
// WWHD_CHEAPIB_VERIFY=STEP:N (stage 1, Mac): those steps' in-betweens drawn in full, the patch compared (ib_census.h)
bool verify_on();
int verify_kind();           // the current pass of a verified step: 0 logic, 1..3 in-between; -1 none
bool verifying();            // inside the per-frame function of a verified in-between
void set_verify_close(void (*fn)());  // ib_census.h: closes the painter window at the next pass entry
// ib_census.h: around the patch of a verified in-between (cheapib_models.h replay): the patched view
void set_verify_patch(void (*begin)(), void (*end)());
void verify_patch(bool begin);
// debug WWHD_CHEAPIB_DUMP=STEP:N (render thread, at each swap): the file the TV image of this swap goes to, or empty
std::string swap_dump_label();

// fpcM_Management: nonzero skips fpcDw_Handler this pass (a replayed hold pass)
bool skip_draw();
// interp's step plan (paced): true plans every in-between (WWHD_CHEAPIB=8/9 with WWHD_CHEAPIB_PLAN_ALL=1, test-only: the
// pacing's in-between estimate is the step's first hold pass, which waits for the logic frame's list)
bool plan_all();
// agl's drawView (RenderQueueManager::drawView): true calls the submitted commands again (the painter of a replayed pass)
bool painter_replay();

// gx2 (game thread, top level, outside recording): true drops the command (the S2 painter window)
bool drop_call(uint32_t list, uint32_t size);
bool drop_op(uint32_t op);
// gx2: bytes recorded into display lists (any thread)
void note_recorded(uint32_t bytes);

// the verify's block sizes (WWHD_CHEAPIB_VERIFY): gx2's GX2SetVertexShader / GX2SetPixelShader hand over the shader
// object; declared_block_size gives the size its uniform block LOCATION declares (GX2UniformBlock), found by the
// program's start register (SQ_PGM_START_VS/PS: the program address >> 8). 0: the shader or the block is unknown.
void note_shader(uint32_t shader, bool pixel);
uint32_t declared_block_size(uint32_t programReg, bool pixel, uint32_t location);

// b1 (WWHD_CHEAPIB_B1=1 with WWHD_CHEAPIB=1, the record worker on; cheapib_b1.h): the render side of a replayed
// in-between re-emits the logic pass's recorded commands, call by call, with the patched uniform blocks uploaded again
bool b1_on();
// gx2 (render thread): a top-level display-list call; true: replayed (not executed). b1_call_end after an executed one
bool b1_call_begin(uint32_t list, uint32_t size);
void b1_call_end();
// a logic pass's call is being captured (render thread): pod_commands.cpp hands every command it appends, draw.cpp's
// snapshot() every upload slice it fills
extern bool g_b1_capturing;
void b1_capture(const void* stream, uint32_t opcode, const void* payload, size_t bytes);
void b1_note_upload(uint64_t buffer, uint64_t offset, uint64_t sliceSize, const void* data, size_t size);
// draw.cpp packet_flush: a batch's per-instance entries (16 bytes each, the vertex stream bound at BUFFER/OFFSET), each
// read from a uniform block (SOURCES: block address, offset in the block, per entry); stage 2 refreshes them on replays
void b1_note_instances(uint64_t buffer, uint64_t offset, uint64_t sliceSize, const uint32_t* sources, size_t entries);
// stage 2, CPU-written vertex sites (cheapib_s2_vertex.cpp, WWHD_CHEAPIB_S2 bit 64): the guest ranges of the sites' vertex
// buffers (vtx_site: any thread; true when [ADDR, ADDR + SIZE) overlaps one); a replayed pass's vertex bytes reach guest
// memory on the render thread, in order before its calls (gx2 OP_CHEAPIB_VTX -> vtx_apply)
bool vtx_site(uint32_t addr, uint32_t size);
void vtx_apply(void* writes);
// draw.cpp / range_batch.h, while a call is captured: a batch copied SIZE bytes of the site range at ADDR into CONTAINER
// at OFF (its upload becomes a vertex record whose segments a replay rebuilds from guest memory)
void b1_vtx_src(const std::vector<uint8_t>* container, size_t off, uint32_t addr, uint32_t size);
#define CHEAPIB_VTX_SRC(container, off, addr, size) \
    if (::cheapib::g_b1_capturing) ::cheapib::b1_vtx_src(container, off, addr, size);
// host-packed uploads (support uniforms: shaders.cpp pack_uniforms_into, draw.cpp packet_support_bytes): b1_src_arm(BASE)
// starts BASE's source list, b1_src(BASE, DST, SRC) notes that the 16 bytes at BASE + DST were copied from SRC (a pointer
// into the active snapshot's blocks); the next upload of BASE records its slice with that list, so a replay rebuilds those
// entries from this pass's blocks (the rest stays the logic pass's bytes). Only while g_b1_capturing (CHEAPIB_B1_SRC*)
void b1_src_arm(const void* base, int who = 0);  // (WHO: the packer, for WWHD_CHEAPIB_B1_CHECK's log)
void b1_src(const void* base, uint32_t dst, const void* src);
#define CHEAPIB_B1_SRC_ARM(base, who) if (::cheapib::g_b1_capturing) ::cheapib::b1_src_arm(base, who);
#define CHEAPIB_B1_SRC(base, dst, src) if (::cheapib::g_b1_capturing) ::cheapib::b1_src(base, dst, src);
// register sources of host-packed uploads: b1_src_reg(BASE, DST, REG, WORDS) notes that the WORDS words at BASE + DST were
// copied from the register file at REG (an ALU constant). An entry whose registers the call itself did not write before
// (inherited from the stream before the call: the camera rows a top-level GX2SetVertexUniformReg set) is rebuilt on a
// replay from the register file as it is then; the call's own writes are the same on every pass (kept as logged)
constexpr uint32_t kB1Alu0 = 0xC000, kB1AluWords = 0x1000;  // (mmSQ_ALU_CONSTANT0_0; gx2_core.cpp checks it)
void b1_src_reg(const void* base, uint32_t dst, uint32_t reg, uint32_t words);
#define CHEAPIB_B1_REG(base, dst, reg, words) if (::cheapib::g_b1_capturing) ::cheapib::b1_src_reg(base, dst, reg, words);
// draw.cpp: a packet's lowering draw keeps its packed support bytes (FROM) as its capture's supBytes (TO): their source
// list goes with them (dropped when the lowering is not captured), and each instance's copy (BASE, packet_support_bytes)
// starts with it, so a replay rebuilds the entries of the packet's other blocks and inherited registers too
void b1_src_keep(const void* from, const void* to);
void b1_src_inherit(const void* key, const void* base);
// gx2_core.cpp: ALU constants the executed call writes (FIRST..+N, register indices; all of them on a context load)
void b1_alu_written(uint32_t first, uint32_t n);
// (debug count) a captured call read a uniform block's bytes from guest memory at render time (not in its snapshot):
// PATH 0 instance entries, 1 packet blocks, 2 pack_uniforms_into, 3 packet support, 4 J3D description support
void b1_guest_read(int path, uint32_t addr, uint32_t size);
#define CHEAPIB_B1_GUEST(path, addr, size) if (::cheapib::g_b1_capturing) ::cheapib::b1_guest_read(path, addr, size);
#define CHEAPIB_B1_ALU_WRITTEN(first, n) if (::cheapib::g_b1_capturing) ::cheapib::b1_alu_written(first, n);
// backend.cpp peek_retire: true while SLOT (a logic pass's, held for its in-betweens) must stay as it is (not cleaned)
bool b1_hold(const void* slot);
// backend.cpp retire_submission, after SLOT's fence: true keeps it as it is (held). A held slot's release (its
// activation, ACTIVATING, or a later retirement once a newer logic pass holds) first retires the submissions whose
// replays bound its descriptor sets and upload blocks; a retired slot submitted after a held one gives its deferred
// objects to that held slot (replayed commands may still use them)
bool b1_retire(void* slot, bool activating);
// backend.cpp next_submission_slot (WWHD_CHEAPIB_B1_SKIP=1): true passes SLOT over (the current logic pass's held slot,
// its in-betweens still to replay), so the ring need not outlast a step
bool b1_skip(const void* slot);
// backend.cpp transition_image (render thread, while a logic pass's call is captured): the surface's layout and use
// before the call changes them
void b1_note_image(void* surface);
// backend.h mark_gpu_written / mark_draw_target_written while a call is captured: the surfaces it wrote (a replay marks
// them written again, in order: linear targets are written back at GX2DrawDone, caches key on writeSeq)
void b1_note_written(void* surface);

// b2 (WWHD_CHEAPIB_B2=1 with b1; cheapib_b2.h): a replayed segment that starts and ends outside a rendering instance
// is recorded once per step into a secondary command buffer (by the emitter) and executed again on each in-between,
// after its uniform slices were refreshed in place (staging copy + barriers)
// record_mt.cpp emitter_started: the emitter thread (its CPU time in the WWHD_CHEAPIB_STATS lines)
void note_emitter_thread();
extern bool g_b2_shadow;  // pod_commands.cpp: every appended command goes to b2_shadow (the state at segment edges)
void b2_shadow(const void* stream, uint32_t opcode, const void* payload, size_t bytes);
// record_mt.cpp replay_extension (the emitter): Ext::CheapibB2Exec / CheapibB2Free
void b2_ext(uint32_t code, const std::byte* bytes, size_t size, bool execute);
// pod_commands.cpp append/append_scalars, first: while a rendering instance is withheld (cheapib_b2.h), an append
// outside the replay emits the withheld part first
extern bool g_b2_guard;
void b2_guard(const void* stream, uint32_t opcode);

// debug (hang hunt, part 5; any mode, fast build, WWHD_CHEAPIB_QNOTE=1): the J3D pipeline queue's worker queues noted at pass points (WHERE:
// 0 pass entry, 1 frame begin, 2 frame end, 3 before the patch's drain, 4 after it); the watchdog prints the last notes
// and the queues' current state (gx2_core.cpp watchdog_thread)
void qnote(int where);
void watchdog_dump();

}  // namespace cheapib
