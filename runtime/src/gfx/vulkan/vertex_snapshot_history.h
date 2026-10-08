#pragma once
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace gfxvk {
// Reuse metadata of the vertex snapshot caches (draw.cpp). Each entry keeps a CPU copy of the bytes it
// handed to the GPU, in ordinary (cached) heap memory: reuse checks compare fresh guest bytes with that
// copy, never with slice.mapped. Upload memory is written by the CPU and read by the GPU only; on
// discrete GPUs it is uncached or write-combined, where CPU reads are about 100 times slower than
// cached ones (issue #44: 57 ms/frame of vertex preparation on an RX 6700 XT). The copy is filled first
// and the slice is made from it, so the two hold the same bytes even if the guest writes the range
// meanwhile. runtime/tools/snapshot_cache_test.cpp poisons every mapped byte after the write to prove
// that no reuse decision reads it.
//
// Exception, direct reads (R.uploadReadsDirect, backend.cpp allocate_upload): when the upload arena's
// memory is HOST_CACHED and HOST_COHERENT (Apple silicon/MoltenVK, many UMA drivers), reading it back is
// as fast as reading heap memory, and the extra copy only costs time. An entry remembered with
// direct=true keeps no copy: its slice is made from the fresh bytes and reuse compares slice.mapped,
// which holds exactly those bytes (the slice is immutable until its generation retires). The flag is
// per entry, so the comparison always matches how the entry was made.
template<class Slice>
struct VertexSnapshotHistory {
  struct Entry {
    uint32_t address = 0, size = 0;
    Slice slice{};
    std::vector<uint8_t> bytes;  // the slice's first `size` bytes, on the CPU (empty if direct)
    bool direct = false;         // compare slice.mapped (host-cached upload memory), not bytes
    void clear() { address = size = 0; slice = {}; bytes.clear(); direct = false; }  // keeps the capacity
  };
  Entry last{}, previous{};
  static bool matches(const Entry& entry, uint32_t address, uint32_t size) {
    return entry.slice.buffer && entry.address == address && entry.size == size;
  }
  // fresh: entry.size bytes of guest memory
  static bool equal(const Entry& entry, const void* fresh) {
    return !entry.size ||
           std::memcmp(fresh, entry.direct ? entry.slice.mapped : entry.bytes.data(), entry.size) == 0;
  }
  Entry* secondary(uint32_t address, uint32_t size) {
    return matches(previous, address, size) ? &previous : nullptr;
  }
  void promote() { std::swap(last, previous); }
  void reset() { last.clear(); previous.clear(); }
  // Copies size fresh bytes into the entry, then makes the slice from that copy: make(bytes, size).
  // direct (host-cached upload memory): no copy, the slice is made from fresh and compared later.
  template<class Make>
  Slice remember(uint32_t address, uint32_t size, const void* fresh, bool keepHistory, bool direct,
                 Make&& make) {
    if (!keepHistory) previous.clear();
    // Replace the same key's changed payload rather than retaining versions; a new key moves the
    // current entry to previous (and reuses the old previous entry's storage).
    else if (!matches(last, address, size)) std::swap(previous, last);
    last.address = address;
    last.size = size;
    last.direct = direct;
    if (direct) {
      last.bytes.clear();
      last.slice = std::forward<Make>(make)(fresh, size_t(size));
      return last.slice;
    }
    const auto* source = static_cast<const uint8_t*>(fresh);
    last.bytes.assign(source, source + size);
    last.slice = std::forward<Make>(make)(static_cast<const void*>(last.bytes.data()), size_t(size));
    return last.slice;
  }
};

// Vertex window snapshots (WWHD_VK_VERTEX_COPY_WINDOW): one entry per binding for a reservation of
// which only [begin, begin + length) is copied. The same rule: reuse compares the CPU copy of the
// window, never the mapped slice.
template<class Slice>
struct VertexWindowEntry {
  uint32_t address = 0, reservation = 0, begin = 0, length = 0;
  Slice slice{};
  std::vector<uint8_t> bytes;  // the window's bytes, on the CPU (empty if direct)
  bool direct = false;         // compare the slice's mapped window (host-cached upload memory)
  bool matches(uint32_t a, uint32_t r, uint32_t b, uint32_t l) const {
    return slice.buffer && address == a && reservation == r && begin == b && length == l;
  }
  // fresh: the window's first byte
  bool equal(const void* fresh) const {
    if (!length) return true;
    const void* kept = direct ? static_cast<const void*>(static_cast<const uint8_t*>(slice.mapped) + begin)
                              : static_cast<const void*>(bytes.data());
    return !std::memcmp(fresh, kept, length);
  }
  void clear() { address = reservation = begin = length = 0; slice = {}; bytes.clear(); direct = false; }
  // Keeps a copy of the window's fresh bytes and returns it: the new slice is written from the copy.
  // direct: keeps no copy and returns fresh. Set slice afterwards.
  const uint8_t* remember(uint32_t a, uint32_t r, uint32_t b, uint32_t l, const void* fresh,
                          bool directReads) {
    address = a; reservation = r; begin = b; length = l;
    slice = {};
    direct = directReads;
    const auto* source = static_cast<const uint8_t*>(fresh);
    if (direct) {
      bytes.clear();
      return source;
    }
    bytes.assign(source, source + l);
    return bytes.data();
  }
};
} // namespace gfxvk
