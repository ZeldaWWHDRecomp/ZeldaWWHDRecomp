// program_hash_cache.h: a program is hashed again exactly when its range may have been written; the
// returned hash always equals a fresh hash of the current bytes at the frame's first lookup.
#include "gfx/vulkan/program_hash_cache.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
// fake write tracking over a small "guest memory": one stamp per 64-byte "page"
struct FakeWatch {
  static inline bool on = true;
  static inline uint64_t clock = 1, seq = 0;
  static inline std::vector<uint64_t> pages = std::vector<uint64_t>(128, 0);
  static inline uint32_t arms = 0;
  static bool active() { return on; }
  static uint64_t arm(uint32_t a, uint32_t n) { ++arms; (void)a; (void)n; return clock++; }
  static bool written_since(uint32_t a, uint32_t n, uint64_t stamp) {
    for (uint32_t p = a / 64; p <= (a + n - 1) / 64; ++p)
      if (pages[p] > stamp) return true;
    return false;
  }
  static uint64_t write_seq() { return seq; }
};
uint8_t memory[8192];
void write(uint32_t a, uint8_t v) {  // a guest write: the "fault" stamps the page
  memory[a] = v;
  FakeWatch::pages[a / 64] = FakeWatch::clock++;
  ++FakeWatch::seq;
}
uint64_t hash_of(uint32_t a, uint32_t n) {
  uint64_t h = 1469598103934665603ull;
  for (uint32_t i = 0; i < n; ++i) h = (h ^ memory[a + i]) * 1099511628211ull;
  return h;
}
}  // namespace

int main() {
  using Cache = gfxvk::vk::ProgramHashCache<FakeWatch>;
  for (int i = 0; i < 8192; ++i) memory[i] = uint8_t(i * 7);
  int hashed = 0;
  auto get = [&](Cache& c, uint32_t a, uint32_t n, uint64_t frame) {
    return c.get(a, n, frame, [&] { ++hashed; return hash_of(a, n); });
  };
  {
    Cache c;
    // first lookup hashes; later frames without writes do not, and agree with a fresh hash
    assert(get(c, 512, 1024, 1) == hash_of(512, 1024) && hashed == 1);
    for (uint64_t f = 2; f < 50; ++f) assert(get(c, 512, 1024, f) == hash_of(512, 1024));
    assert(hashed == 1);
    // a write outside the range: no rehash (the page scan finds nothing)
    write(3000, 1);
    assert(get(c, 512, 1024, 50) == hash_of(512, 1024) && hashed == 1);
    // a write inside: the same frame keeps its result (the old once-per-frame contract) ...
    write(600, 0xAA);
    assert(get(c, 512, 1024, 50) != hash_of(512, 1024) && hashed == 1);
    // ... the next frame sees the new bytes
    assert(get(c, 512, 1024, 51) == hash_of(512, 1024) && hashed == 2);
    assert(get(c, 512, 1024, 52) == hash_of(512, 1024) && hashed == 2);
    // the last byte of the range counts, the byte after it does not
    write(1535, 3);
    assert(get(c, 512, 1024, 53) == hash_of(512, 1024) && hashed == 3);
    write(1536, 3);
    assert(get(c, 512, 1024, 54) == hash_of(512, 1024) && hashed == 3);
    // same address, other size: its own entry
    assert(get(c, 512, 512, 54) == hash_of(512, 512) && hashed == 4);
    // frame ~0 (no frame contract): always hashed
    assert(get(c, 512, 1024, Cache::kNoFrame) == hash_of(512, 1024) && hashed == 5);
    assert(get(c, 512, 1024, Cache::kNoFrame) == hash_of(512, 1024) && hashed == 6);
    // clear (save-state load): hashed again
    c.clear();
    assert(get(c, 512, 1024, 60) == hash_of(512, 1024) && hashed == 7);
    // a small range (a fetch shader) is never watched: hashed once per frame as before
    const uint32_t arms = FakeWatch::arms;
    for (uint64_t f = 61; f < 66; ++f) assert(get(c, 6000, 48, f) == hash_of(6000, 48));
    assert(hashed == 12 && FakeWatch::arms == arms);
  }
  {
    // a range written every frame becomes dynamic: hashed every frame, not armed, still exact
    Cache c;
    hashed = 0;
    const uint32_t armsBefore = FakeWatch::arms;
    for (uint64_t f = 1; f <= 20; ++f) {
      write(2400, uint8_t(f));
      assert(get(c, 2304, 384, f) == hash_of(2304, 384));
    }
    assert(hashed == 20);
    assert(c.stats.becameDynamic >= 1);
    assert(FakeWatch::arms - armsBefore < 20);  // stopped arming once dynamic
    // after the dynamic period it is watched again and unchanged frames skip the hash
    const uint64_t later = 20 + Cache::kDynamicFrames + 1;
    assert(get(c, 2304, 384, later) == hash_of(2304, 384));
    const int h = hashed;
    assert(get(c, 2304, 384, later + 1) == hash_of(2304, 384) && hashed == h);
  }
  {
    // write tracking unavailable or switched off: the old behaviour (once per frame)
    for (int mode = 0; mode < 2; ++mode) {
      Cache c;
      hashed = 0;
      FakeWatch::on = mode != 0;
      c.watchEnabled = mode == 1 ? false : true;
      for (uint64_t f = 1; f <= 5; ++f) {
        assert(get(c, 0, 256, f) == hash_of(0, 256));
        assert(get(c, 0, 256, f) == hash_of(0, 256));
      }
      assert(hashed == 5);
      FakeWatch::on = true;
    }
  }
  {
    // verify mode: a write the tracking missed (here: memory changed without a stamp) is caught
    Cache c;
    c.verify = true;
    assert(get(c, 4096, 256, 1) == hash_of(4096, 256));
    assert(get(c, 4096, 256, 2) == hash_of(4096, 256) && c.stats.verified == 1 && c.stats.mismatches == 0);
    memory[4100] ^= 0xFF;  // no write() call: invisible to the fake tracking
    assert(get(c, 4096, 256, 3) == hash_of(4096, 256) && c.stats.mismatches == 1);
  }
  std::puts("program_hash_cache_test: ok");
  return 0;
}
