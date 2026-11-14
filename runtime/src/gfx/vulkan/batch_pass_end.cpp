// WWHD_VK_BATCH_AT_PASS_END: batch_pass_end.h
#ifdef WWHD_NATIVE_DRAW
#include "batch_pass_end.h"
#include "backend.h"
#include "runtime.h"
#include "gx2/native_draw.h"
#include <cstdlib>

namespace gfxvk::batch_pass_end {
namespace {
uint64_t deferred = 0, made = 0, dropped = 0, generation = 0;
}
bool on() {
  static const bool value = [] {
    const char* e = std::getenv("WWHD_VK_BATCH_AT_PASS_END");
    const bool v = e && *e == '1';
    LOG("[vulkan] draw-batch submissions inside a pass: %s (WWHD_VK_BATCH_AT_PASS_END=%d)", v ? "at the next boundary without a pass" : "at once", int(v));
    return v;
  }();
  return value;
}
bool defer() {
  if (!on() || !R.rendering) return false;
  if (!native_draw::g_batchSubmitPending) ++deferred;
  native_draw::g_batchSubmitPending = true;
  generation = R.submissionGeneration;
  return true;
}
bool take() {
  if (R.rendering) return false;
  native_draw::g_batchSubmitPending = false;
  if (generation != R.submissionGeneration) {
    ++dropped;
    return false;
  }
  ++made;
  return true;
}
void report(double frames, uint64_t submissions) {
  LOG("[vulkan batch at pass end] %s: draw-batch submissions deferred out of a pass %.2f/frame, made at a boundary "
      "%.2f/frame, dropped (another submission first) %.2f/frame; draw-batch submissions %.2f/frame",
      on() ? "on" : "off", deferred / frames, made / frames, dropped / frames, submissions / frames);
  deferred = made = dropped = 0;
}
}  // namespace gfxvk::batch_pass_end
#endif
