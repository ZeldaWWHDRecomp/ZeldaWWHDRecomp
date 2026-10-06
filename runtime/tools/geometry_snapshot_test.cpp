#include "gfx/vulkan/geometry_snapshot.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <vector>

struct Slice {
  uintptr_t buffer = 0;
  size_t size = 0;
  void* mapped = nullptr;
};

int main() {
  using Cache = gfxvk::GeometrySnapshotCache<Slice, unsigned>;
  Cache cache;
  std::deque<std::vector<uint8_t>> uploads;
  auto fresh = [&](const void* bytes, size_t size) {
    uploads.emplace_back(std::max(size, size_t(16)), 0);
    if (bytes && size) std::memcpy(uploads.back().data(), bytes, size);
    return Slice{uploads.size(), uploads.back().size(), uploads.back().data()};
  };
  std::array<uint8_t, 64> bytes{};
  for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = uint8_t(i);
  auto get = [&](uint32_t address, size_t size = 64, unsigned device = 1,
                 uint64_t generation = 1) {
    return cache.get(device, generation, address, bytes.data(), size, fresh);
  };
  auto original = get(0x1000);
  assert(get(0x1000).buffer == original.buffer);
  // Nonconsecutive models and different bindings must not discard this source.
  get(0x2000); get(0x3000); get(0x4000);
  assert(get(0x1000).buffer == original.buffer);
  assert(get(0x1000, 16).buffer == original.buffer);
  for (size_t i = 0; i < bytes.size(); ++i) {
    const auto previous = get(0x1000);
    bytes[i] ^= 0x80;
    auto changed = get(0x1000);
    assert(changed.buffer != previous.buffer);
    assert(std::memcmp(changed.mapped, bytes.data(), bytes.size()) == 0);
    // Already queued data stays immutable after guest mutation.
    assert(static_cast<uint8_t*>(previous.mapped)[i] != bytes[i]);
    assert(get(0x1000).buffer == changed.buffer);
  }
  auto full = get(0x1000);
  bytes.back() ^= 1;
  assert(get(0x1000, 16).buffer == full.buffer); // Outside requested prefix.
  auto changed = get(0x1000);
  assert(changed.buffer != full.buffer);
  auto nextGeneration = get(0x1000, 16, 1, 2);
  assert(nextGeneration.buffer != changed.buffer);
  assert(get(0x1000, 16, 1, 2).buffer == nextGeneration.buffer);
  // A short upload in a new generation cannot satisfy a subsequent long draw.
  auto longer = get(0x1000, 64, 1, 2);
  assert(longer.buffer != nextGeneration.buffer);
  assert(std::memcmp(longer.mapped, bytes.data(), bytes.size()) == 0);
  assert(get(0x1000, 64, 2, 2).buffer != longer.buffer);
  cache.reset();
  assert(get(0x1000, 64, 2, 2).buffer != longer.buffer);
  // GPU mappings can be unreadable: hits compare host storage only.
  auto unreadable = [&](const void*, size_t size) { return Slice{9000, size, reinterpret_cast<void*>(1)}; };
  auto inaccessible = cache.get(3, 3, 0x5000, bytes.data(), 64, unreadable);
  assert(cache.get(3, 3, 0x5000, bytes.data(), 64, fresh).buffer == inaccessible.buffer);
  // Failed/throwing allocation cannot publish old bytes or an old GPU slice.
  auto failed = [](const void*, size_t) { return Slice{}; };
  bytes[0] ^= 1;
  assert(!cache.get(3, 3, 0x5000, bytes.data(), 64, failed).buffer);
  auto recovered = cache.get(3, 3, 0x5000, bytes.data(), 64, fresh);
  assert(recovered.buffer != inaccessible.buffer);
  bytes[1] ^= 1;
  try {
    cache.get(3, 3, 0x5000, bytes.data(), 64,
        [](const void*, size_t) -> Slice { throw std::runtime_error("allocation"); });
    assert(false);
  } catch (const std::runtime_error&) {}
  assert(cache.get(3, 3, 0x5000, bytes.data(), 64, fresh).buffer != recovered.buffer);
  // Capture before allocation so the shadow and GPU bytes describe one payload.
  const auto expected = bytes;
  auto mutate = [&](const void* source, size_t size) {
    bytes.fill(99);
    return fresh(source, size);
  };
  auto captured = cache.get(4, 4, 0x6000, bytes.data(), 64, mutate);
  assert(std::memcmp(captured.mapped, expected.data(), 64) == 0);
  assert(cache.get(4, 4, 0x6000, bytes.data(), 64, fresh).buffer != captured.buffer);
  // Overflow, oversized, null and empty requests take fresh-upload fallback.
  assert(get(0xfffffff0u).buffer != get(0xfffffff0u).buffer);
  std::vector<uint8_t> large(Cache::maxPayloadBytes + 1, 7);
  auto a = cache.get(1, 1, 0x7000, large.data(), large.size(), fresh);
  auto b = cache.get(1, 1, 0x7000, large.data(), large.size(), fresh);
  assert(a.buffer != b.buffer);
  assert(cache.get(1, 1, 0x7000, nullptr, 64, fresh).buffer !=
         cache.get(1, 1, 0x7000, nullptr, 64, fresh).buffer);
  assert(get(0x7000, 0).buffer != get(0x7000, 0).buffer);
  // Exercise replacement under more sources than the fixed cache capacity.
  for (uint32_t i = 0; i < 4096; ++i) {
    bytes[0] = uint8_t(i);
    auto uploaded = get(0x10000 + i * 32);
    assert(std::memcmp(uploaded.mapped, bytes.data(), bytes.size()) == 0);
    assert(get(0x10000 + i * 32).buffer == uploaded.buffer);
  }
  assert(cache.counters.evictions > 0);
  std::puts("geometry snapshots: exact bytes, prefixes, mutations, generations, host storage and eviction passed");
}
