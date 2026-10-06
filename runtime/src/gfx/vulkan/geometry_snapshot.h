#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace gfxvk {
// Geometry can recur under different materials and vertex bindings. Keep host
// payloads by source, but prove every requested byte is unchanged before reuse.
// GPU slices belong to one device/submission generation; host copies do not.
template<class Slice, class Device>
class GeometrySnapshotCache {
public:
  static constexpr size_t bucketCount = 64, ways = 4;
  static constexpr size_t maxPayloadBytes = 256 * 1024;
  struct Counters {
    uint64_t checks = 0, hits = 0, reusedBytes = 0, evictions = 0;
  } counters;

  template<class Factory>
  Slice get(Device device, uint64_t generation, uint32_t address,
            const void* bytes, size_t size, Factory&& factory) {
    if (!bytes || !size || size > maxPayloadBytes ||
        uint64_t(address) + size > 0x100000000ull)
      return std::forward<Factory>(factory)(bytes, size);
    // Mix low alignment bits as well as the upper address bits.
    uint32_t hash = address ^ (address >> 16);
    hash *= 0x7feb352du;
    auto& bucket = entries_[(hash ^ (hash >> 15)) & (bucketCount - 1)];
    Entry* entry = &bucket[0];
    for (auto& candidate : bucket) {
      if (candidate.used && candidate.address == address) {
        entry = &candidate;
        break;
      }
      if (!candidate.used || (entry->used && candidate.age < entry->age))
        entry = &candidate;
    }
    const bool sameSource = entry->used && entry->address == address;
    bool equal = false;
    if (sameSource && entry->shadow.size() >= size) {
      ++counters.checks;
      equal = std::memcmp(bytes, entry->shadow.data(), size) == 0;
    }
    entry->age = ++age_;
    if (equal && entry->slice.buffer && entry->slice.mapped &&
        entry->device == device && entry->generation == generation &&
        entry->uploadedSize >= size) {
      ++counters.hits;
      counters.reusedBytes += size;
      return entry->slice;
    }
    if (entry->used && !sameSource) ++counters.evictions;
    // Do not publish the old slice if allocation fails or guest bytes change
    // during allocation. The factory uploads the exact captured host payload.
    entry->slice = {};
    entry->used = true;
    entry->address = address;
    if (!equal) {
      entry->shadow.resize(size);
      std::memcpy(entry->shadow.data(), bytes, size);
    }
    auto slice = std::forward<Factory>(factory)(entry->shadow.data(), size);
    if (slice.buffer && slice.mapped && slice.size >= size) {
      entry->slice = slice;
      entry->device = device;
      entry->generation = generation;
      entry->uploadedSize = size;
    }
    return slice;
  }

  void reset() {
    for (auto& bucket : entries_)
      for (auto& entry : bucket) {
        entry.used = false;
        entry.slice = {};
      }
  }

private:
  struct Entry {
    uint32_t address = 0;
    bool used = false;
    uint64_t age = 0, generation = 0;
    Device device{};
    size_t uploadedSize = 0;
    Slice slice{};
    std::vector<uint8_t> shadow;
  };
  std::array<std::array<Entry, ways>, bucketCount> entries_{};
  uint64_t age_ = 0;
};
} // namespace gfxvk
