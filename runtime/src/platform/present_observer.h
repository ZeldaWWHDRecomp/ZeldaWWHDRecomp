#pragma once
#include <cstdint>
#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
#ifdef __OBJC__
@class CAMetalLayer;
@class NSWindow;
#endif
namespace present_observer {
bool enabled();
uint64_t native_now_ns();
void fail(const char *reason);
bool begin_drawable_identity(uint64_t epoch, uint32_t id, uint64_t frame);
void end_drawable_identity();
void scenario_origin(uint64_t render_frame, uint64_t logic_step);
void google_present(uint64_t epoch, uint32_t id, uint64_t frame);
void google_history(uint64_t epoch, uint32_t id, uint64_t frame,
                    uint64_t desired, uint64_t actual, uint64_t earliest,
                    uint64_t margin);
void render_swap(uint64_t swap, uint64_t frame);
void scheduler_due(uint64_t swap, int64_t steady_due_ns, uint64_t due_tick,
                   int64_t shift_before, int64_t shift_after, uint32_t granule,
                   bool shifted_clock, bool uncapped);
void presentation_topology(uint64_t epoch, uint32_t images,
                           uint32_t acquire_slots, uint32_t submission_slots,
                           uint32_t present_mode, bool async);
void flush();
#ifdef __OBJC__
CAMetalLayer *make_layer(NSWindow *window);
#endif
} // namespace present_observer
#else
namespace present_observer {
inline bool enabled() { return false; }
inline uint64_t native_now_ns() { return 0; }
inline void fail(const char *) {}
inline bool begin_drawable_identity(uint64_t, uint32_t, uint64_t) { return false; }
inline void end_drawable_identity() {}
inline void scenario_origin(uint64_t, uint64_t) {}
inline void google_present(uint64_t, uint32_t, uint64_t) {}
inline void google_history(uint64_t, uint32_t, uint64_t, uint64_t, uint64_t,
                           uint64_t, uint64_t) {}
inline void render_swap(uint64_t, uint64_t) {}
inline void scheduler_due(uint64_t, int64_t, uint64_t, int64_t, int64_t,
                          uint32_t, bool, bool) {}
inline void presentation_topology(uint64_t, uint32_t, uint32_t, uint32_t,
                                  uint32_t, bool) {}
inline void flush() {}
} // namespace present_observer
#endif

namespace present_observer {
// Observer-only scope: never changes acquisition, recording or presentation.
class DrawableIdentityScope {
  bool owned_;
public:
  DrawableIdentityScope(uint64_t epoch, uint32_t id, uint64_t frame)
      : owned_(begin_drawable_identity(epoch, id, frame)) {}
  ~DrawableIdentityScope() { if (owned_) end_drawable_identity(); }
  DrawableIdentityScope(const DrawableIdentityScope&) = delete;
  DrawableIdentityScope& operator=(const DrawableIdentityScope&) = delete;
};
} // namespace present_observer
