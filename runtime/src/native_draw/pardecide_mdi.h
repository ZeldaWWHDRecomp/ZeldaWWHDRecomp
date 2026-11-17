// pardecide single-thread win (decomp build fast mode): WWHD_NATIVE_MDI_DIRECT=1 (default off). A stream multi-draw
// batch (draw.cpp mdi_append / packet_mdi_flush) copies each instance's vertex stream from guest memory straight into
// the flush's upload slice, instead of into the batch's CPU vector (zero-filled by resize) and from there into the
// slice. Included by draw.cpp before packet_draw.
//
// Exactness: the same slice (allocate_upload of the same size and alignment as snapshot(): max(total, 16), 4), the
// same bytes (each instance's guest bytes at the same offset, the stride padding and the tail zero). The guest bytes are
// read at the flush instead of at the append: a batch is flushed before every command but packet draws, packet
// definitions, register writes and pass descriptions (gx2_core.cpp execute_one), so no fence, DrawDone, swap,
// readback or copy runs in between, and the render thread writes no guest memory in between.
#pragma once
#include <vector>

namespace pdmdi {
inline bool on() {
  static const bool v = [] {
    const char* e = std::getenv("WWHD_NATIVE_MDI_DIRECT");
    return e && *e == '1';
  }();
  return v;
}
struct Src {
  uint32_t addr, size, at, padded;
};
inline std::vector<Src> g_src;
inline size_t g_size = 0;
inline void clear() {
  g_src.clear();
  g_size = 0;
}
// mdi_append: the instance's place in the batch stream (as B.bytes.size() was)
inline size_t append(uint32_t addr, uint32_t size, uint32_t padded) {
  const size_t at = g_size;
  g_src.push_back({addr, size, uint32_t(at), padded});
  g_size += padded;
  return at;
}
// packet_mdi_flush: the stream's slice (snapshot(B.bytes) made direct)
inline UploadSlice upload() {
  auto slice = allocate_upload(std::max<size_t>(g_size, 16), 4);
  uint8_t* out = static_cast<uint8_t*>(slice.mapped);
  for (const Src& s : g_src) {
    std::memcpy(out + s.at, mem::ptr(s.addr), s.size);
    if (s.padded > s.size) std::memset(out + s.at + s.size, 0, s.padded - s.size);
  }
  if (slice.size > g_size) std::memset(out + g_size, 0, slice.size - g_size);
  return slice;
}
}  // namespace pdmdi
