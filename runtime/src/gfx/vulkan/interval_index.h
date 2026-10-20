// A static set of half-open intervals [lo, hi) with payloads, built once and queried for the ones that
// overlap a range. Used by the Vulkan texture invalidation (surfaces.cpp invalidate_surface_cache): a
// GX2Invalidate of a texture range used to walk every cached surface (a few hundred to a few thousand,
// about 190 invalidations a frame); with each surface's guest footprint in this index it visits only
// the surfaces whose footprint overlaps the range, and runs the unchanged exact check on those.
//
// Entries are sorted by lo; maxHi[i] is the largest hi among entries 0..i, so a query walks down from
// the last entry starting before the range's end and stops once no earlier entry can reach the range's
// start. Empty intervals (hi <= lo) never overlap anything. Payload order in a query is descending lo.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace gfxvk {

template<class Payload>
class IntervalIndex {
public:
  struct Entry { uint64_t lo = 0, hi = 0; Payload payload{}; };

  void build(std::vector<Entry> entries) {
    entries_ = std::move(entries);
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) { return a.lo < b.lo; });
    maxHi_.resize(entries_.size());
    uint64_t m = 0;
    for (size_t i = 0; i < entries_.size(); ++i) maxHi_[i] = m = std::max(m, entries_[i].hi);
  }
  // calls fn(payload) for every entry overlapping [lo, hi) (hi > lo)
  template<class Fn>
  void query(uint64_t lo, uint64_t hi, Fn&& fn) const {
    if (hi <= lo) return;
    size_t i = size_t(std::lower_bound(entries_.begin(), entries_.end(), hi,
                                       [](const Entry& e, uint64_t v) { return e.lo < v; }) - entries_.begin());
    while (i > 0) {
      --i;
      if (maxHi_[i] <= lo) break;
      if (entries_[i].hi > lo && entries_[i].hi > entries_[i].lo) fn(entries_[i].payload);
    }
  }
  size_t size() const { return entries_.size(); }
  void clear() { entries_.clear(); maxHi_.clear(); }

private:
  std::vector<Entry> entries_;
  std::vector<uint64_t> maxHi_;
};

}  // namespace gfxvk
