// Multi-threaded command recording, stage 1: the record worker ("Vulkan record", record_worker.cpp) as the one emitter
// of the renderer (translated draws and, in the decomp build fast mode, native draw packets). Every build with
// cmake WWHD_VK_SMOOTH (cmake/VkSmooth.cmake, on by default); every item works only with the record worker
// (WWHD_VK_RECORD_WORKER=1) and is off unless its bit is set (smooth_defaults.h: 127 on phones and the Steam Deck):
//   WWHD_VK_RECORD_MT=bits (or "all"):
//     1  the slot's command pool is reset on the emitter: a reused slot's activation puts a reset command into the
//        POD stream instead of resetting on the render thread (the slot's fence was waited for, as before)
//     2  publish at pass cuts: after vkCmdEndRendering the pending stream is published once it holds >= 8 KiB
//     4  recycled POD buffers: the emitter hands replayed byte vectors back; the producer reuses one per publish
//     8  native worker presentation by default on the allowlisted drivers (worker_present_policy.h);
//        WWHD_R5_NATIVE_PRESENT=0/1 still wins
//     16 descriptor tokens (record_tokens.h; also WWHD_VK_RECORD_DESCRIPTORS=1)
//     32 pipeline futures (pipeline_future.h; also WWHD_VK_RECORD_PIPELINE_FUTURES=1)
//     64 no draw-batch submissions while the worker records (WWHD_VK_DRAW_BATCH's split after 2048 draws is skipped):
//        with the split a frame has one submission more than the ring's slots minus one, and activating the next
//        slot waits for the GPU to finish the previous frame's use of it (phone: 12-15 ms per frame whenever a frame
//        crosses 2048 draws). The worker already overlaps the recording the split was meant to start early.
//   WWHD_VK_RECORD_SPIN_US=n (default 30, 0 = the old blocking waits): the emitter's idle wait and the producer's
//     wait for an emitted serial spin (with yield) up to n microseconds before they block on the condition variable
//   WWHD_VK_RECORD_CHECK=1: every vkCmdBindDescriptorSets checks that no descriptor token reaches the driver and that
//     a token is translated on the render thread only while the emitter is drained (record_tokens.h)
// Counter line "[vulkan record mt]" every stats period (report).
#pragma once
#ifdef WWHD_VK_SMOOTH
#include "loader.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gfxvk {
class PodCommands;
}
namespace gfxvk::record_mt {
enum Bits : unsigned { kPoolReset = 1, kPassPublish = 2, kRecycle = 4, kNativePresent = 8, kTokens = 16, kFutures = 32,
                     kNoBatchSubmit = 64 };
unsigned bits();  // the switch, resolved and logged once
// bit B on and the record worker running (render thread)
bool on(unsigned bit);
unsigned spin_us();
bool check();
// the record worker's thread marks itself (record_tokens' check)
void emitter_started();
bool on_emitter();

// --- bit 1: command-pool reset on the emitter (backend.cpp hooks) ---
// cleanup_submission: true when the reset of COMMANDS is left to the slot's next activation (bit 1, worker running)
bool defer_command_pool_reset(const void* slot, VkCommandPool commands);
// activate_submission (render thread): slot SLOT (ring index INDEX) becomes the recording slot. Puts the deferred
// command-pool reset and the emitter's per-slot descriptor reset (record_tokens) into the POD stream, or does them
// here when no stream records (the producer presentation scope drained the emitter first)
void activated(const void* slot, uint32_t index, VkDevice device);

// --- bit 2 ---
// end_encoder, right after the pass ended: publish the pending stream at >= 8 KiB
void pass_ended();

// --- bit 4 (record_worker.cpp) ---
// the next publish's byte vector (empty, with the capacity of a replayed one when one is free)
std::vector<std::byte> recycled_bytes();
// the emitter, after replaying BYTES
void recycle(std::vector<std::byte>&& bytes);

// --- bit 64 ---
// draw.cpp draw_batch_submit: true when the draw-batch submission is skipped (bit 64, worker running; counted)
bool skip_draw_batch();

// --- bit 8 ---
// WWHD_R5_NATIVE_PRESENT unset: whether the allowlisted drivers present on the worker by default
bool native_present_default();

// --- POD extension commands (pod_commands.cpp opcode 31) ---
enum class Ext : uint32_t { SlotReset = 1, DefineSet = 2, BindFuture = 3 };
// append one extension command (sub-opcode + payload) to STREAM; false: rejected (the caller drains and does the
// work inline)
bool append(PodCommands& stream, Ext op, std::span<const std::byte> payload);
// the replay of opcode 31 (pod_commands.cpp)
void replay_extension(const std::byte* bytes, size_t size, bool execute);

// counters ([vulkan record mt]); relaxed atomics (the emitter counts too)
struct Counters {
  std::atomic<uint64_t> cmdResetsQueued{0}, cmdResetsInline{0}, passPublishes{0}, recycled{0}, waitSpins{0},
      waitSpinHits{0}, idleSpins{0}, idleSpinHits{0}, drains{0}, encoderFallbacks{0}, batchSkips{0};
};
Counters& counters();
void report(double frames);
}  // namespace gfxvk::record_mt
#endif
