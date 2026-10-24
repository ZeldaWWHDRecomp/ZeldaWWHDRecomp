// gx2/submitted_uniforms.h: display-list block index and submission snapshots.
#include "gx2/submitted_uniforms.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace gx2::submitted;

int main() {
  // guest memory stand-in: address a lives at mem[a - kBase]
  constexpr uint32_t kBase = 0x10000000;
  std::vector<uint8_t> mem(0x10000);
  for (size_t i = 0; i < mem.size(); ++i) mem[i] = uint8_t(i * 7);
  auto ptr = [&](uint32_t a) { return mem.data() + (a - kBase); };
  auto readable = [&](uint32_t a, uint32_t n) { return a >= kBase && a + n + Snapshot::kTail <= kBase + mem.size(); };

  // a list's blocks are deduplicated by address (largest size kept); called lists contribute theirs
  DisplayListIndex index;
  index.publish(0x20000000, 0x100, {{kBase + 0x1500, 0x2A0}}, {});  // a material list
  index.publish(0x20001000, 0x800,
                {{kBase + 0x1100, 0x2A0}, {kBase + 0x1100, 0x40}, {kBase + 0x2000, 0x30}, {kBase + 0x1100, 0x2A0}},
                {0x20000000, 0x20000000, 0x20005000 /* never published */});
  std::vector<Range> r;
  assert(index.blocks(0x20001000, 0x800, r));
  assert(r.size() == 3 && r[0].addr == kBase + 0x1100 && r[0].size == 0x2A0 && r[1].addr == kBase + 0x1500 &&
         r[2].addr == kBase + 0x2000);
  r.clear();
  assert(!index.blocks(0x20001000, 0x900, r) && r.empty());  // a call past the recorded end
  assert(!index.blocks(0x20002000, 0x10, r));                 // never recorded
  assert(index.blocks(0x20001000, 0x40, r));                  // a shorter call of the same list

  // the snapshot keeps the bytes of submission time, whatever the guest writes afterwards
  Snapshot s;
  std::vector<Range> ranges = {{kBase + 0x1100, 0x2A0}, {kBase + 0x2000, 0x30}, {0x5000, 0x10} /* unreadable */};
  const size_t copied = s.build(7, ranges, ptr, readable);
  assert(copied == 0x2A0 + 0x30 && s.blocks() == 2 && s.id() == 7);
  std::vector<uint8_t> before(ptr(kBase + 0x1100), ptr(kBase + 0x1100) + 0x2A0);
  std::memset(ptr(kBase + 0x1100), 0xEE, 0x2A0);  // update_ubo rewrites the half meanwhile
  const uint8_t* b = s.find(kBase + 0x1100, 0x2A0);
  assert(b && std::memcmp(b, before.data(), 0x2A0) == 0);
  assert(s.find(kBase + 0x1100, 0x10) == b);           // a smaller binding of the same block
  assert(s.find(kBase + 0x1100, 0x2B0) == nullptr);    // more than was submitted: guest memory
  assert(s.find(kBase + 0x1110, 0x10) == nullptr);     // not a block start
  assert(s.find(0x5000, 0x10) == nullptr);             // unreadable ranges are left to guest memory
  assert(s.block_size(kBase + 0x2000) == 0x30);
  for (int i = 0; i < 3; ++i) assert(s.find(kBase + 0x2000, 0x30));  // cached lookups agree
  // rebuilding a pooled snapshot forgets the old blocks and cache
  std::vector<Range> next = {{kBase + 0x3000, 0x20}};
  s.build(8, next, ptr, readable);
  assert(s.find(kBase + 0x1100, 0) == nullptr && s.find(kBase + 0x2000, 0) == nullptr && s.find(kBase + 0x3000, 0x20));
  std::puts("submitted_uniforms_test: ok");
  return 0;
}
