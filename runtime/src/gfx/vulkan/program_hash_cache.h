// Shader program hashes without re-reading unchanged programs (shaders.cpp program_hash).
//
// A shader key starts with a hash of the guest program's bytes (vertex, pixel and fetch shaders).
// The old contract re-hashed every program once per render frame: a few hundred programs of up to
// several KiB, a serial multiply chain over every byte, about a tenth of the draw preparation time
// on an M3 Max. Programs almost never change, so the cache now watches the program's guest pages
// (write_watch.h, the mechanism the texture checks and the Vulkan buffer cache use): a program is
// hashed again only when a CPU write may have reached its range since it was last hashed. Within a
// frame the previous contract stays: the first lookup of a frame decides, later lookups of the same
// frame return that result.
//
// The hash value is the same function of the same bytes as before, so shader keys, pipeline keys
// and the disk caches are unchanged. A range that keeps being written (a program next to data the
// game rewrites every frame) stops being watched for a while and is hashed every frame as before,
// so it costs no repeated write faults.
//
// Watch: arm(addr, size) -> stamp (protects, then the caller hashes), written_since(addr, size,
// stamp), write_seq(), active(). Tests substitute a fake (runtime/tools/program_hash_cache_test.cpp).
#pragma once
#include <array>
#include <cstdint>
#include <unordered_map>

namespace gfxvk::vk {

template<class Watch>
class ProgramHashCache {
public:
  static constexpr uint64_t kNoFrame = ~uint64_t{0};
  // a program seen written again within kChurnFrames of its last hash, kChurnLimit times in a row,
  // is hashed every frame without watching for kDynamicFrames
  static constexpr uint64_t kChurnFrames = 8, kDynamicFrames = 600;
  static constexpr uint32_t kChurnLimit = 4;
  // smaller ranges (fetch shaders are 32-80 bytes, often next to data the game rewrites) are hashed
  // every frame as before: hashing them is cheaper than a write fault or a page scan
  static constexpr uint32_t kMinWatchBytes = 256;
  struct Stats { uint64_t lookups = 0, hashes = 0, watchedHits = 0, frameHits = 0, dynamicHashes = 0, becameDynamic = 0,
                 verified = 0, mismatches = 0,
                 hashedBytes = 0, skippedBytes = 0; } stats;
  bool watchEnabled = true;
  // verify mode (diagnostics): every unchanged-by-watch result is also hashed fresh and compared;
  // a difference is counted and the fresh hash is used
  bool verify = false;

  // hash(bytes...) is called with no arguments when the bytes must be (re)hashed
  template<class Hash>
  uint64_t get(uint32_t address, uint32_t size, uint64_t frame, Hash&& hash) {
    ++stats.lookups;
    // most lookups repeat a recent program: a direct-mapped front table skips the map's hashing
    // and probing (map entries keep their addresses until clear())
    const uint64_t key = (uint64_t(address) << 32) | size;
    auto& slot = recent_[(address >> 6 ^ address >> 16 ^ size) & (recent_.size() - 1)];
    if (!slot.entry || slot.key != key) slot = {key, &entries_[key]};
    auto& e = *slot.entry;
    if (frame != kNoFrame && e.frame == frame) { ++stats.frameHits; return e.hash; }
    const bool watch = watchEnabled && frame != kNoFrame && size >= kMinWatchBytes && Watch::active();
    if (watch && e.watched) {
      const uint64_t seq = Watch::write_seq();
      if (seq == e.checkedSeq || !Watch::written_since(address, size, e.stamp)) {
        e.checkedSeq = seq;
        e.frame = frame;
        ++stats.watchedHits;
        stats.skippedBytes += size;
        if (verify) {
          ++stats.verified;
          const uint64_t fresh = hash();
          if (fresh != e.hash) { ++stats.mismatches; e.hash = fresh; }
        }
        return e.hash;
      }
      // written: churn bookkeeping, then hash again below
      e.churn = frame - e.hashFrame <= kChurnFrames ? e.churn + 1 : 1;
      if (e.churn >= kChurnLimit) {
        e.churn = 0;
        e.dynamicUntil = frame + kDynamicFrames;
        ++stats.becameDynamic;
      }
    }
    e.watched = false;
    if (watch && frame >= e.dynamicUntil) {
      // arm first, then read the bytes (write_watch.h): a write racing the hash is stamped later
      e.checkedSeq = Watch::write_seq();
      e.stamp = Watch::arm(address, size);
      e.watched = true;
    } else if (watch) {
      ++stats.dynamicHashes;
    }
    e.hash = hash();
    e.frame = frame;
    e.hashFrame = frame;
    ++stats.hashes;
    stats.hashedBytes += size;
    return e.hash;
  }
  void clear() { recent_ = {}; entries_.clear(); }
  size_t size() const { return entries_.size(); }

private:
  struct Entry {
    uint64_t hash = 0, frame = kNoFrame, hashFrame = 0, stamp = 0, checkedSeq = 0, dynamicUntil = 0;
    uint32_t churn = 0;
    bool watched = false;
  };
  std::unordered_map<uint64_t, Entry> entries_;
  struct Recent { uint64_t key = 0; Entry* entry = nullptr; };
  std::array<Recent, 1024> recent_{};
};

}  // namespace gfxvk::vk
