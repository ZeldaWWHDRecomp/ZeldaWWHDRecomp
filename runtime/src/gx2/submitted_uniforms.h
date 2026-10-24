// Uniform blocks as the guest submitted them to the GPU.
//
// On the Wii U the GPU reads a uniform block from memory when it executes the draw, and it starts
// executing a frame's display lists as soon as GX2 submits them (GX2Flush, which WWHD calls right
// after its display-list calls). Our render thread executes the same commands much later: it walks
// the frame's display lists over most of the frame, while the game already writes the next frame's
// uniforms. WWHD records some draws of frame N before its "update_ubo" job switches the per-frame
// scene block (0x2A0 bytes, double-buffered 0x400 apart) to the other half, so those draws bind the
// half that the job rewrites during frame N+1, while the render thread executes them. Which values
// such a draw got depended on thread timing (issue: non-identical repeat pictures of the #178 cliff
// and Forbidden Woods; WWHD_VK_UNIFORM_VERIFY mismatches at guest 3B5B1100..3B5B16FF).
//
// The fix gives every draw of a called display list the bytes its uniform blocks held when the list
// was submitted, as the console's GPU (which runs the list right away) reads them:
// - recording a display list (GX2BeginDisplayList .. GX2EndDisplayList) notes the uniform blocks it
//   binds and the display lists it calls; at its end the list's blocks, the called lists' blocks
//   included, are published by its start address (DisplayListIndex);
// - a display-list call from the command stream (not one recorded into a list) copies those blocks
//   into a Snapshot (one copy per call, a few hundred KB per frame in total), which travels with the
//   call through the render queue;
// - while the render thread executes the call, the renderers read the blocks from the snapshot
//   (gx2::submitted_uniform_block); everything else still reads guest memory.
// Draws issued directly (outside display lists) keep reading guest memory when they execute.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gx2::submitted {

struct Range {
  uint32_t addr, size;
};

// sorts by address and keeps one entry per address (the largest size)
inline void dedupe(std::vector<Range>& r) {
  std::sort(r.begin(), r.end(), [](const Range& a, const Range& b) {
    return a.addr != b.addr ? a.addr < b.addr : a.size > b.size;
  });
  r.erase(std::unique(r.begin(), r.end(), [](const Range& a, const Range& b) { return a.addr == b.addr; }), r.end());
}

// What each recorded display list binds (any thread; one mutex, a few dozen publications per frame).
class DisplayListIndex {
public:
  // At GX2EndDisplayList: the blocks bound while recording [start, start + size) and the lists it
  // called. Called lists contribute what they bind (as published now; a list never published
  // contributes nothing, and lists are followed this way to any depth they were published with).
  void publish(uint32_t start, uint32_t size, std::vector<Range> blocks, std::vector<uint32_t> calls) {
    std::sort(calls.begin(), calls.end());
    calls.erase(std::unique(calls.begin(), calls.end()), calls.end());
    std::lock_guard<std::mutex> lk(mutex_);
    for (uint32_t call : calls) {
      if (call == start) continue;
      auto it = lists_.find(call);
      if (it != lists_.end()) blocks.insert(blocks.end(), it->second.blocks.begin(), it->second.blocks.end());
    }
    dedupe(blocks);
    auto& e = lists_[start];
    e.size = size;
    e.blocks = std::move(blocks);
  }
  // The blocks of the list starting at `start`; false if none was published there or a call goes
  // past its recorded end (then the call reads guest memory as before).
  bool blocks(uint32_t start, uint32_t size, std::vector<Range>& out) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = lists_.find(start);
    if (it == lists_.end() || size > it->second.size) return false;
    out.insert(out.end(), it->second.blocks.begin(), it->second.blocks.end());
    return true;
  }
  void clear() {
    std::lock_guard<std::mutex> lk(mutex_);
    lists_.clear();
  }
  size_t size() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return lists_.size();
  }

private:
  struct List {
    uint32_t size = 0;
    std::vector<Range> blocks;
  };
  mutable std::mutex mutex_;
  std::unordered_map<uint32_t, List> lists_;
};

// The submitted bytes of a call's uniform blocks. Built on the submitting thread, then read only by
// the render thread (find's small cache is the render thread's).
class Snapshot {
public:
  static constexpr uint32_t kMaxBlock = 0x10000;  // as the renderers clamp a block's size
  static constexpr uint32_t kTail = 256;          // readable slack (Metal uploads round up to 256 bytes)

  // Copies each range's bytes (rounded up to 16) through `ptr(addr)`; `readable(addr, size)` says
  // which ranges may be read at all (others are left to guest memory). Returns the bytes copied.
  template <class Ptr, class Readable>
  size_t build(uint64_t id, std::vector<Range>& ranges, Ptr&& ptr, Readable&& readable) {
    id_ = id;
    dedupe(ranges);
    addrs_.clear();
    entries_.clear();
    size_t total = 0;
    for (const Range& r : ranges) {
      const uint32_t size = std::min(r.size, kMaxBlock);
      if (!size || !readable(r.addr, size)) continue;
      total += (size + 15) & ~15u;
    }
    bytes_.resize(total + kTail);
    size_t at = 0;
    for (const Range& r : ranges) {
      const uint32_t size = std::min(r.size, kMaxBlock);
      if (!size || !readable(r.addr, size)) continue;
      const uint32_t copied = (size + 15) & ~15u;
      std::memcpy(bytes_.data() + at, ptr(r.addr), copied);
      addrs_.push_back(r.addr);
      entries_.push_back({uint32_t(at), size});
      at += copied;
    }
    std::memset(bytes_.data() + at, 0, kTail);
    for (auto& c : cache_) c = {0, 0};
    return at;
  }
  uint64_t id() const { return id_; }
  size_t blocks() const { return addrs_.size(); }
  // The submitted bytes of the block at addr when the snapshot holds at least `size` of them
  // (`size` 0: any), else nullptr.
  const uint8_t* find(uint32_t addr, uint32_t size) const {
    // blocks are 0x100..0x400 aligned: a multiplicative hash spreads them over the slots
    // ((addr >> 4) & 63 put every 0x400-aligned block into one slot)
    auto& c = cache_[(addr * 2654435761u) >> (32 - kCacheBits)];
    uint32_t index;
    if (c.addr == addr && c.index) index = c.index - 1;
    else {
      auto it = std::lower_bound(addrs_.begin(), addrs_.end(), addr);
      if (it == addrs_.end() || *it != addr) return nullptr;
      index = uint32_t(it - addrs_.begin());
      c = {addr, index + 1};
    }
    const Entry& e = entries_[index];
    return size <= e.size ? bytes_.data() + e.offset : nullptr;
  }
  // bytes the snapshot holds for the block at addr (0: not in the snapshot)
  uint32_t block_size(uint32_t addr) const {
    auto it = std::lower_bound(addrs_.begin(), addrs_.end(), addr);
    return it != addrs_.end() && *it == addr ? entries_[it - addrs_.begin()].size : 0;
  }

private:
  struct Entry {
    uint32_t offset, size;
  };
  struct Slot {
    uint32_t addr, index;  // index + 1 (0: empty)
  };
  static constexpr unsigned kCacheBits = 8;
  static constexpr size_t kCache = size_t(1) << kCacheBits;
  uint64_t id_ = 0;
  std::vector<uint32_t> addrs_;  // sorted
  std::vector<Entry> entries_;
  std::vector<uint8_t> bytes_;
  mutable Slot cache_[kCache] = {};
};

}  // namespace gx2::submitted
