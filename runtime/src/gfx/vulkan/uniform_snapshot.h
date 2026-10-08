#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace gfxvk {
// CPU-only metadata for the last immutable snapshot in each VS/PS logical
// uniform slot (16 guest blocks and one support block per stage).
// Slice provides buffer, mapped and size; Device supports equality. The factory
// owns allocation, alignment and zero padding exactly as the normal snapshot
// path does. Returned bytes must remain immutable until the generation retires.
//
// Each entry keeps a CPU copy of its bytes (ordinary cached heap memory) and reuse
// compares against that copy, never against slice.mapped: upload memory is
// uncached or write-combined on discrete GPUs, where CPU reads are about 100 times
// slower (issue #44). The copy is taken first and the factory writes the slice
// from it, so both hold the same bytes even if the guest writes meanwhile.
// runtime/tools/snapshot_cache_test.cpp poisons mapped bytes to prove it.
//
// Exception, directReads (R.uploadReadsDirect, backend.cpp allocate_upload): when the
// upload memory is HOST_CACHED and HOST_COHERENT (Apple silicon/MoltenVK, many UMA
// drivers), reads are as fast as heap reads; the entry then keeps no copy, the
// factory gets the caller's bytes and reuse compares slice.mapped. The mode is
// stored per entry, so a comparison always matches how its entry was made.
template<class Slice, class Device>
class UniformSnapshotCache {
public:
  static constexpr size_t slotCount = 34;
  struct Counters {
    uint64_t lookups = 0, checks = 0, comparisons = 0, hits = 0;
    // Avoided allocation bytes, including the snapshot's minimum-size padding.
    uint64_t reusedBytes = 0;
  } counters;

  // Caller must validate device uniform range limits before lookup. The factory
  // receives the source to copy (the entry's CPU copy of the fresh bytes, the
  // fresh bytes themselves if directReads, or nullptr) and the requested length,
  // including nullptr/zero-size.
  template<class Factory>
  Slice get(Device device, uint64_t generation, size_t slot,
            const void* bytes, size_t size, bool directReads, Factory&& factory) {
    if (!initialized_ || device_ != device || generation_ != generation) {
      for (auto& entry : entries_) entry.clear();  // keeps the copies' capacity
      device_ = device;
      generation_ = generation;
      initialized_ = true;
    }
    ++counters.lookups;
    if (slot >= slotCount)
      return std::forward<Factory>(factory)(bytes, size);
    auto& last = entries_[slot];
    if (last.valid && last.size == size) {
      ++counters.checks;
      bool equal;
      if (!size) equal = true;
      else if (!bytes) equal = last.knownZero;
      else {
        ++counters.comparisons;
        // a zero-filled entry (made without source bytes) keeps no copy: its bytes are all zero
        equal = last.knownZero ? all_zero(bytes, size)
              : std::memcmp(bytes, last.direct ? last.slice.mapped : last.bytes.data(), size) == 0;
      }
      if (equal) {
        ++counters.hits;
        counters.reusedBytes += last.slice.size;
        return last.slice;
      }
    }
    const void* source = nullptr;
    if (directReads) {
      last.bytes.clear();
      source = bytes;
    } else if (bytes && size) {
      const auto* b = static_cast<const uint8_t*>(bytes);
      last.bytes.assign(b, b + size);
      source = last.bytes.data();
    }
    auto slice = std::forward<Factory>(factory)(source, size);
    // Failed/empty factories never publish an entry. Allocation padding is not
    // compared as guest memory.
    if (slice.buffer && slice.mapped && slice.size == (size < 16 ? 16 : size)) {
      last.slice = slice;
      last.size = size;
      last.valid = true;
      last.knownZero = !bytes || !size;
      last.direct = directReads;
    } else {
      last.clear();
    }
    return slice;
  }

  void reset() {
    for (auto& entry : entries_) entry.clear();
    initialized_ = false;
  }

private:
  static bool all_zero(const void* bytes, size_t size) {
    const auto* b = static_cast<const uint8_t*>(bytes);
    for (size_t i = 0; i < size; ++i)
      if (b[i]) return false;
    return true;
  }
  struct Entry {
    Slice slice{};
    size_t size = 0;
    bool valid = false, knownZero = false;
    bool direct = false;  // compare slice.mapped (host-cached upload memory), not bytes
    std::vector<uint8_t> bytes;  // the slice's first `size` bytes, on the CPU (empty if knownZero or direct)
    void clear() { slice = {}; size = 0; valid = knownZero = direct = false; bytes.clear(); }
  };
  std::array<Entry, slotCount> entries_{};
  Device device_{};
  uint64_t generation_ = 0;
  bool initialized_ = false;
};
} // namespace gfxvk
