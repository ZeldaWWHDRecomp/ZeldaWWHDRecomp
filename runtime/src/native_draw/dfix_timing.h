// native_draw/dfix_timing.h: per-call cost of make_state and of the support-byte build (WWHD_NATIVE_J3D_DESC_D1TIMING=1,
// measurement only; off: one predicted branch on a static). Lap clock: arm64 cntvct_el0 (else steady_clock).
// "[j3d-dfix] ..." every 300 frames at the first described draw of a frame. Decomp build, fast mode, WWHD_NATIVE_DRAW only.
#pragma once
#include <chrono>
#include <cstdint>

namespace dfixt {  // (inside gfxvk::j3ddescvk: j3d_desc_vk.h includes it there)
inline bool on() {
  static const bool v = ::native_draw::switch_value("WWHD_NATIVE_J3D_DESC_D1TIMING", 0) != 0;
  return v;
}
inline uint64_t ticks() {
#if defined(__aarch64__)
  uint64_t v;
  asm volatile("mrs %0, cntvct_el0" : "=r"(v));
  return v;
#else
  return uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}
inline double ns_per_tick() {
#if defined(__aarch64__)
  static const double v = [] { uint64_t f; asm volatile("mrs %0, cntfrq_el0" : "=r"(f)); return 1e9 / double(f); }();
  return v;
#else
  return 1.0;
#endif
}
enum Slot { kSupport, kMake, kSlots };
inline uint64_t g_ticks[kSlots], g_hits[kSlots];
struct Scope {
  Slot s; uint64_t t0;
  explicit Scope(Slot k) : s(k), t0(on() ? ticks() : 0) {}
  ~Scope() { if (t0) g_ticks[s] += ticks() - t0, ++g_hits[s]; }
};
inline void report(uint64_t frame) {
  static uint64_t last = 0;
  if (!on() || frame < last + 300) return;
  if (last) {
    const double k = ns_per_tick(), f = double(frame - last);
    LOG("[j3d-dfix] support build %.0f ns/call (%.1f/frame), make_state %.0f ns/call (%.2f/frame)",
        g_hits[kSupport] ? g_ticks[kSupport] * k / g_hits[kSupport] : 0.0, g_hits[kSupport] / f,
        g_hits[kMake] ? g_ticks[kMake] * k / g_hits[kMake] : 0.0, g_hits[kMake] / f);
  }
  last = frame;
  for (auto& x : g_ticks) x = 0;
  for (auto& x : g_hits) x = 0;
}
}  // namespace dfixt
