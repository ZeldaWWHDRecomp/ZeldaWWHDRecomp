// interval_index.h: a query returns exactly the entries overlapping the range (brute force comparison
// over random interval sets, including empty, nested, touching and whole-space intervals).
#include "gfx/vulkan/interval_index.h"

#include <cassert>
#include <cstdio>
#include <random>
#include <set>
#include <vector>

int main() {
  using Index = gfxvk::IntervalIndex<int>;
  std::mt19937_64 rng(12345);
  for (int round = 0; round < 400; ++round) {
    const int n = int(rng() % 200);
    const uint64_t space = round % 3 == 0 ? 64 : round % 3 == 1 ? 4096 : (uint64_t(1) << 33);
    std::vector<Index::Entry> entries;
    for (int i = 0; i < n; ++i) {
      uint64_t lo = rng() % space, len = rng() % 5 == 0 ? 0 : rng() % (space / 4 + 1);
      if (rng() % 50 == 0) { lo = 0; len = ~uint64_t{0}; }  // whole space (a footprint that failed)
      entries.push_back({lo, len == ~uint64_t{0} ? len : lo + len, i});
    }
    Index index;
    index.build(entries);
    assert(index.size() == entries.size());
    for (int q = 0; q < 200; ++q) {
      const uint64_t a = rng() % (space + 8), b = a + rng() % (space / 2 + 2);
      std::set<int> want, got;
      for (const auto& e : entries)
        if (e.hi > e.lo && e.lo < b && a < e.hi && b > a) want.insert(e.payload);
      index.query(a, b, [&](int p) { assert(got.insert(p).second); });
      assert(want == got);
    }
  }
  Index empty;
  empty.build({});
  empty.query(0, 100, [](int) { assert(false); });
  std::puts("interval_index_test: ok");
  return 0;
}
