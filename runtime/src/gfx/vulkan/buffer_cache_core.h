// Guest buffer cache: the renderer-independent part (Vulkan glue: buffer_cache.cpp; tests:
// runtime/tools/buffer_cache_test.cpp).
//
// Draws read vertex arrays, index arrays and uniform blocks straight from guest memory. Instead of
// copying those bytes into the per-submission upload arena for every draw, the cache keeps one GPU
// copy per guest range and hands out the same region until the range changes. Nothing is hashed or
// compared on the hot path; a range is current while none of its pages has a newer stamp in the page
// write tracker (write_watch.h):
//   - CPU writes (guest code, HLE memcpy, save-state restore, the renderer's own CopySurface writes)
//     fault once on the armed pages and stamp them;
//   - kernel writes into guest memory are bracketed by HostWrite (FSReadFile), which stamps;
//   - explicit guest signals stamp the hint array: DCFlushRange/DCStoreRange (coreinit) and
//     GX2Invalidate of attribute or uniform buffers (on the render thread, in command order);
//   - a save-state load drops everything (invalidate_all, any thread).
// There are no GPU writes into guest memory (no stream-out, render targets stay GPU images).
//
// Upload protocol, the same as the texture checks: arm the range (write-protect, take the stamp), then
// read guest memory. A write that lands before the protection is in the copy; one after it faults and
// carries a newer stamp, so the next lookup uploads again. A lookup returns data as of the last upload
// or newer; the uncached path copies at the same render-thread moment, so both see the same bytes
// unless the game writes the range while the draw is being prepared (a race the console GPU has too).
//
// Old regions are never overwritten: a changed range gets a fresh region and the old one is retired
// through the backing, which frees it after the GPU work that may read it has completed.
//
// Keys: the guest start address plus a kind and a parameter word (converted index data depends on the
// primitive, index type and count). A raw range serves every request with the same start whose size is
// not larger (a longer request uploads the longer range). Ranges with other starts are separate
// entries even when they overlap: each is validated against its own pages.
//
// Ranges written over and over (per-frame uniform blocks, dynamic vertex data) would fault on every
// write and upload on every use. After kChurnLimit uploads caused by writes, each within kChurnFrames
// of the previous one, the entry becomes dynamic: it is no longer armed, lookups return kBypass (the
// caller copies through the upload arena as before), and after a back-off (doubling, up to
// kMaxBackoff frames) it is tried again.
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>

#include "write_watch.h"

namespace gfxvk::bufcache {

// Best-fit allocator over [0, capacity) with coalescing of freed neighbours.
class RangeAllocator {
 public:
  static constexpr uint64_t kFail = ~uint64_t{0};
  explicit RangeAllocator(uint64_t capacity = 0) { reset(capacity); }
  void reset(uint64_t capacity) {
    byOffset_.clear(); bySize_.clear();
    capacity_ = capacity; used_ = 0;
    if (capacity) insert(0, capacity);
  }
  uint64_t allocate(uint64_t size) {
    if (!size) return kFail;
    auto it = bySize_.lower_bound(size);
    if (it == bySize_.end()) return kFail;
    const uint64_t length = it->first, offset = it->second;
    bySize_.erase(it);
    byOffset_.erase(offset);
    if (length > size) insert(offset + size, length - size);
    used_ += size;
    return offset;
  }
  void release(uint64_t offset, uint64_t size) {
    if (!size) return;
    used_ -= size;
    auto next = byOffset_.lower_bound(offset);
    if (next != byOffset_.end() && offset + size == next->first) {
      size += next->second;
      erase_size(next->second, next->first);
      next = byOffset_.erase(next);
    }
    if (next != byOffset_.begin()) {
      auto prev = std::prev(next);
      if (prev->first + prev->second == offset) {
        offset = prev->first;
        size += prev->second;
        erase_size(prev->second, prev->first);
        byOffset_.erase(prev);
      }
    }
    insert(offset, size);
  }
  uint64_t used() const { return used_; }
  uint64_t capacity() const { return capacity_; }
  size_t free_ranges() const { return byOffset_.size(); }

 private:
  void insert(uint64_t offset, uint64_t size) {
    byOffset_[offset] = size;
    bySize_.emplace(size, offset);
  }
  void erase_size(uint64_t size, uint64_t offset) {
    auto [a, b] = bySize_.equal_range(size);
    for (; a != b; ++a)
      if (a->second == offset) { bySize_.erase(a); return; }
  }
  std::map<uint64_t, uint64_t> byOffset_;
  std::multimap<uint64_t, uint64_t> bySize_;
  uint64_t capacity_ = 0, used_ = 0;
};

// GPU memory of one entry: a range of one of the backing's buffers.
struct Region {
  uint32_t block = UINT32_MAX;
  uint64_t offset = 0, size = 0;
  uint8_t* mapped = nullptr;
  bool valid() const { return block != UINT32_MAX; }
};

struct Backing {
  virtual ~Backing() = default;
  // false: no memory within the budget (the caller falls back to its uncached path)
  virtual bool allocate(uint64_t size, Region& out) = 0;
  // free once the GPU work that may read the region has completed
  virtual void retire(const Region& region) = 0;
};

enum Kind : uint32_t { kRaw = 0, kIndexNative = 1, kIndexConverted = 2 };

struct Key {
  uint32_t addr = 0;
  uint32_t kind = kRaw;
  uint64_t params = 0;  // part of the identity: converted index data depends on these
  uint32_t extra = 0;
  bool operator==(const Key& o) const {
    return addr == o.addr && kind == o.kind && params == o.params && extra == o.extra;
  }
};
struct KeyHash {
  size_t operator()(const Key& k) const {
    uint64_t h = (uint64_t(k.kind) << 32 | k.addr) * 0x9E3779B97F4A7C15ull;
    h ^= (k.params + (uint64_t(k.extra) << 17)) * 0xC2B2AE3D27D4EB4Full;
    return size_t(h ^ (h >> 29));
  }
};

struct Entry {
  uint32_t addr = 0, size = 0;  // guest source range [addr, addr + size) of the current region
  uint32_t outSize = 0;         // bytes in the region
  Region region;
  uint64_t stamp = 0;           // write_watch stamp of the upload (arm)
  uint64_t checkedSeq = 0;      // write_seq() of the last clean check
  uint32_t epoch = 0;
  uint64_t lastUse = 0, uploadFrame = 0, dynamicUntil = 0;
  uint32_t churn = 0, backoffs = 0;
  bool armed = false;           // lookup armed the range for the upload that follows
  uint64_t armStamp = 0, armSeq = 0;
  uint32_t armSize = 0;
  // caller memo of data derived from the region's bytes (index extents); cleared on upload
  uint64_t memoKey = ~uint64_t{0};
  uint32_t memo[3] = {};
  std::vector<uint8_t> shadow;  // CPU copy of the uploaded bytes (verify mode: Cache::keepShadow)
};

struct Stats {
  uint64_t lookups = 0, hits = 0, uploads = 0, uploadBytes = 0;
  uint64_t staleWrites = 0, staleHintOnly = 0, grows = 0, newEntries = 0;
  uint64_t bypassDynamic = 0, bypassNoMemory = 0, becameDynamic = 0;
  uint64_t verifyChecks = 0, verifyMismatches = 0, verifyRaced = 0;
  uint64_t evictions = 0, epochDrops = 0;
};

enum Status { kHit, kMiss, kBypass };

class Cache {
 public:
  // verify mode: keep a CPU copy of every upload, so verify() never reads the mapped GPU memory
  bool keepShadow = false;
  static constexpr uint32_t kChurnLimit = 3, kChurnFrames = 4;
  static constexpr uint64_t kFirstBackoff = 64, kMaxBackoff = 2048;
  static constexpr uint64_t kIdleFrames = 1800;  // evicted after this many frames without use
  static constexpr uint64_t kAlign = 256;        // minUniformBufferOffsetAlignment is at most 256

  explicit Cache(Backing& backing) : backing_(backing) {}
  ~Cache() { clear(); }

  void set_frame(uint64_t frame) { frame_ = frame; }
  uint64_t frame() const { return frame_; }
  // any thread (save-state load): every entry is re-uploaded on its next use
  void invalidate_all() { epoch_.fetch_add(1, std::memory_order_acq_rel); }

  // Finds the entry for key covering size source bytes. kHit: e->region holds current bytes. kMiss: the
  // range is armed; read guest memory now (not earlier) and call upload(). kBypass: dynamic range, use
  // the uncached path. e stays valid until the next end_frame().
  Status lookup(const Key& key, uint32_t size, Entry*& e) {
    ++stats.lookups;
    const uint32_t epoch = epoch_.load(std::memory_order_acquire);
    auto [it, inserted] = map_.try_emplace(key);
    e = &it->second;
    Entry& x = *e;
    x.lastUse = frame_;
    if (inserted) {
      ++stats.newEntries;
      x.addr = key.addr;
    } else if (x.region.valid()) {
      if (x.epoch != epoch) {
        ++stats.epochDrops;
        drop_region(x);
      } else if (size <= x.size) {
        if (current(x)) { ++stats.hits; return kHit; }
        ++stats.staleWrites;
        const bool hintOnly = !wwatch::written_since(x.addr, x.size, x.stamp);
        stats.staleHintOnly += hintOnly;
        if (note_write(x) && onDynamic) onDynamic(key, x, hintOnly);
        drop_region(x);
      } else {
        ++stats.grows;  // a longer request: not a write
        drop_region(x);
      }
    }
    if (frame_ < x.dynamicUntil) {
      ++stats.bypassDynamic;
      x.armed = false;
      return kBypass;
    }
    // arm first, then the caller reads guest memory (see the header comment)
    x.armSeq = wwatch::write_seq();
    x.armStamp = wwatch::arm(key.addr, size);
    x.armSize = size;
    x.armed = true;
    return kMiss;
  }

  // After kMiss: copies outSize bytes into a new region. false: no memory (use the uncached path).
  bool upload(Entry& e, const void* bytes, uint32_t outSize) {
    if (!e.armed) return false;
    e.armed = false;
    Region region;
    if (!outSize || !backing_.allocate(round(outSize), region)) {
      ++stats.bypassNoMemory;
      pressure_ = true;
      return false;
    }
    std::memcpy(region.mapped, bytes, outSize);
    if (keepShadow) {
      const auto* b = static_cast<const uint8_t*>(bytes);
      e.shadow.assign(b, b + outSize);
    } else {
      e.shadow.clear();
    }
    e.region = region;
    e.size = e.armSize;
    e.outSize = outSize;
    e.stamp = e.armStamp;
    e.checkedSeq = e.armSeq;
    e.epoch = epoch_.load(std::memory_order_acquire);
    e.uploadFrame = frame_;
    e.memoKey = ~uint64_t{0};
    resident_ += region.size;
    ++stats.uploads;
    stats.uploadBytes += outSize;
    return true;
  }

  // Verify mode: compares the region's first n bytes with freshly computed ones. A difference with a
  // newer stamp on the range is a write racing this check (counted, not a mismatch). Diagnostics only
  // (WWHD_VK_BUFFER_CACHE_VERIFY=1). It compares against the CPU copy of the upload (keepShadow): the
  // mapped GPU memory is uncached or write-combined on discrete GPUs, and reading it made verify mode
  // run at ~1 fps on an RX 6700 XT (issue #91). Without a copy it falls back to the mapped bytes.
  bool verify(Entry& e, const void* expected, uint32_t n, uint32_t* firstDiff = nullptr) {
    ++stats.verifyChecks;
    n = std::min(n, e.outSize);
    const auto* a = static_cast<const uint8_t*>(expected);
    const uint8_t* have = e.shadow.size() >= n ? e.shadow.data() : e.region.mapped;
    if (!std::memcmp(a, have, n)) return true;
    if (wwatch::changed_since(e.addr, e.size, e.stamp)) { ++stats.verifyRaced; return true; }
    ++stats.verifyMismatches;
    if (firstDiff) {
      uint32_t i = 0;
      while (i < n && a[i] == have[i]) ++i;
      *firstDiff = i;
    }
    return false;
  }

  // Once per frame: evicts idle entries (and, over budget or after an allocation failure, the least
  // recently used ones down to 3/4 of the budget). Invalidates Entry pointers.
  void end_frame(uint64_t budgetBytes) {
    const bool over = resident_ > budgetBytes || pressure_;
    if (!over && frame_ - lastSweep_ < 64) return;
    lastSweep_ = frame_;
    pressure_ = false;
    for (auto it = map_.begin(); it != map_.end();) {
      Entry& x = it->second;
      if (x.lastUse + kIdleFrames < frame_ && frame_ >= x.dynamicUntil) {
        if (x.region.valid()) ++stats.evictions;
        drop_region(x);
        it = map_.erase(it);
      } else ++it;
    }
    const uint64_t target = budgetBytes / 4 * 3;
    if (resident_ <= target && !over) return;
    std::vector<std::pair<uint64_t, Entry*>> lru;
    for (auto& [k, x] : map_)
      if (x.region.valid() && x.lastUse < frame_) lru.push_back({x.lastUse, &x});
    std::sort(lru.begin(), lru.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& [use, x] : lru) {
      if (resident_ <= target) break;
      ++stats.evictions;
      drop_region(*x);
    }
  }

  void clear() {
    for (auto& [k, x] : map_) drop_region(x);
    map_.clear();
  }

  uint64_t resident_bytes() const { return resident_; }
  size_t entries() const { return map_.size(); }
  Stats stats;
  // diagnostics: called when an entry becomes dynamic (hintOnly: its last invalidation was a hint, not
  // a write fault)
  void (*onDynamic)(const Key& key, const Entry& entry, bool hintOnly) = nullptr;

 private:
  static uint64_t round(uint64_t n) { return (std::max<uint64_t>(n, 16) + kAlign - 1) / kAlign * kAlign; }
  bool current(Entry& e) {
    const uint64_t seq = wwatch::write_seq();
    if (seq == e.checkedSeq) return true;  // nothing stamped anywhere since the last clean check
    if (wwatch::changed_since(e.addr, e.size, e.stamp)) return false;
    e.checkedSeq = seq;
    return true;
  }
  // returns true when the entry just became dynamic
  bool note_write(Entry& e) {
    const uint64_t age = frame_ - e.uploadFrame;
    if (age > kMaxBackoff) e.backoffs = 0;  // was stable for long: start the back-off over
    e.churn = age <= kChurnFrames ? e.churn + 1 : 1;
    if (e.churn < kChurnLimit) return false;
    e.churn = 0;
    e.dynamicUntil = frame_ + std::min<uint64_t>(kFirstBackoff << std::min<uint32_t>(e.backoffs, 16), kMaxBackoff);
    ++e.backoffs;
    ++stats.becameDynamic;
    return true;
  }
  void drop_region(Entry& e) {
    if (!e.region.valid()) return;
    backing_.retire(e.region);
    resident_ -= e.region.size;
    e.region = {};
    std::vector<uint8_t>().swap(e.shadow);
    e.memoKey = ~uint64_t{0};
  }

  Backing& backing_;
  std::unordered_map<Key, Entry, KeyHash> map_;
  std::atomic<uint32_t> epoch_{0};
  uint64_t frame_ = 0, lastSweep_ = 0, resident_ = 0;
  bool pressure_ = false;
};

}  // namespace gfxvk::bufcache
