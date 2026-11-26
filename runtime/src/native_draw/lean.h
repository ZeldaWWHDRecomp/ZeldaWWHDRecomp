// native_draw/lean.h: thinner lowering bookkeeping for every packet family (on by default since its phone pairs, 2026-11-13:
// render CPU faster or neutral at the cliff, lava room and sea; WWHD_NATIVE_DRAW_LEAN=0 turns it off).
// Included by gfx/vulkan/draw.cpp inside namespace gfxvk before packet_draw (it uses PacketCapture).
// - The lowering's PacketCapture (about 10 KB: per stage the descriptor writes and infos) is the thread's heap scratch,
//   reset in place at each lowering and swapped into the lowered kind (a pointer swap) instead of a stack object in
//   packet_draw's frame (which every instance, replays included, paid for) and a copy into the kind. The scratch is
//   used with the switch off too (packet_draw's frame stays small either way); off, it is copied into the kind as before.
// - The once-per-reason refusal logs build their string keys only when the kind's reason changes.
// Decomp build, fast mode, WWHD_NATIVE_DRAW=ON only.
#pragma once
inline bool packet_lean_on() {
  static const bool on = [] {
    const char* e = std::getenv("WWHD_NATIVE_DRAW_LEAN");
    const bool v = !e || *e != '0';
    LOG("[native-draw] lean lowering bookkeeping %s (WWHD_NATIVE_DRAW_LEAN=%d)", v ? "on" : "off", v ? 1 : 0);
    return v;
  }();
  return on;
}
// the lowering's capture (render thread): reset in place, a fresh PacketCapture
inline thread_local std::unique_ptr<PacketCapture> g_captureScratch;
inline PacketCapture& packet_capture_scratch() {
  if (!g_captureScratch) g_captureScratch = std::make_unique<PacketCapture>();
  else {
    g_captureScratch->~PacketCapture();
    new (g_captureScratch.get()) PacketCapture();
  }
  return *g_captureScratch;
}
