// Locks on the renderer's shared caches for the draw threads. Ours,
// MPL-2.0 like the rest of the runtime.
//
// A guard locks its area only while another thread may record (second_thread_active()) or with WWHD_VK_LOCK_ALWAYS=1;
// with no draw thread possible this session (draw_thread_state.h) it is one test of a constant. The render thread
// raises the flag before it starts a draw thread's work and lowers it after the join, so a guard skipped on the render
// thread can never overlap a draw thread's.
//
// Areas, in the order a thread may take them (a later one while holding an earlier one, never the reverse):
// buffers (the buffer cache and its entries' memos; a draw holds it from its first lookup to its end) < surfaces
// (the surface maps, uploads, invalidation, the pass caches) < shaders (translation, fetch shaders) < pipelines (the
// pipeline map, pending and declined pipelines) < samplers. Each lock is recursive: an area's entry points call each
// other. The one call the other way, invalidate() -> buffer_cache_guest_invalidate(), runs only with no draw thread
// (a guest invalidate joins them first).
#pragma once
#include <atomic>
#include <mutex>
#include "gfx/vulkan/draw_thread_state.h"

namespace gfxvk::draw_locks {
enum Area { kBuffers, kSurfaces, kShaders, kPipelines, kSamplers, kAreas };
inline std::recursive_mutex g_mutex[kAreas];
inline std::atomic<int> g_threads{0};  // draw threads running now (beside the render thread)
inline bool second_thread_active() { return g_threads.load(std::memory_order_acquire) != 0; }
// render thread: around a draw thread's work
inline void thread_started() { g_threads.fetch_add(1, std::memory_order_acq_rel); }
inline void thread_joined() { g_threads.fetch_sub(1, std::memory_order_acq_rel); }
inline bool engaged() {
  return draw_thread_state::kSplit && (draw_switches::kLockAlways || second_thread_active());
}

class Guard {
 public:
  explicit Guard(Area a) {
    if (engaged()) { m_ = &g_mutex[a]; m_->lock(); }
  }
  ~Guard() { if (m_) m_->unlock(); }
  Guard(const Guard&) = delete;
  Guard& operator=(const Guard&) = delete;
 private:
  std::recursive_mutex* m_ = nullptr;
};
}  // namespace gfxvk::draw_locks
