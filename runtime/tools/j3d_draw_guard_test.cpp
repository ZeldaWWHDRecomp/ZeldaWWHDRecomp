// SPDX-License-Identifier: MPL-2.0
// J3D draw buckets that loop on themselves (runtime/src/j3d_draw_guard.h): the packet whose link closes
// the loop is found; cutting it leaves every packet in the list once. The double entry is the one of the
// scene preview hang: an actor drawn twice in one frame enters its packets into the same bucket again.
#include "j3d_draw_guard.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

namespace {
struct Bucket {
    std::map<uint32_t, uint32_t> next;  // packet -> next packet (J3DPacket +0x10)
    uint32_t head = 0;
    // J3DDrawBuffer::entryNonSort / entryImm: the packet goes to the front, without a check
    void entry(uint32_t packet) {
        next[packet] = head;
        head = packet;
    }
    uint32_t link(uint32_t packet) const {
        const auto it = next.find(packet);
        return it == next.end() ? 0 : it->second;
    }
    uint32_t loop_end() const {
        return j3d_draw_guard::loop_end(head, [this](uint32_t p) { return link(p); });
    }
    std::vector<uint32_t> walk(size_t limit) const {
        std::vector<uint32_t> out;
        for (uint32_t p = head; p && out.size() < limit; p = link(p)) out.push_back(p);
        return out;
    }
};

void no_loop() {
    Bucket b;
    assert(b.loop_end() == 0);  // an empty bucket
    for (uint32_t p = 0x10; p <= 0x100; p += 0x10) b.entry(p);
    assert(b.loop_end() == 0);
    assert(b.walk(100).size() == 16);
    // a long list is not a loop
    Bucket l;
    for (uint32_t p = 1; p <= 100000; ++p) l.entry(p * 4);
    assert(l.loop_end() == 0);
}

void self_loop() {
    // the same packet entered twice in a row: it links to itself
    Bucket b;
    b.entry(0x48807AAC);
    b.entry(0x488FAE00);
    b.entry(0x488FAE00);
    assert(b.link(0x488FAE00) == 0x488FAE00);
    const uint32_t last = b.loop_end();
    assert(last == 0x488FAE00);
    b.next[last] = 0;
    assert(b.walk(10) == std::vector<uint32_t>{0x488FAE00});
}

void drawn_twice() {
    // an actor drawn twice in one frame: its 9 tail models (2 packets each) are entered again after the
    // first round, into a bucket that already held another actor's packet
    Bucket b;
    b.entry(0x48807AAC);
    std::vector<uint32_t> packets;
    for (uint32_t m = 0; m < 9; ++m)
        for (uint32_t k = 0; k < 2; ++k) packets.push_back(0x488FAE00 + m * 0x3800 + k * 0xAC);
    for (int round = 0; round < 2; ++round)
        for (uint32_t p : packets) b.entry(p);
    // the walk never reaches the end
    assert(b.walk(1000).size() == 1000);
    const uint32_t last = b.loop_end();
    assert(last != 0);
    b.next[last] = 0;
    const auto drawn = b.walk(1000);
    std::set<uint32_t> once(drawn.begin(), drawn.end());
    assert(once.size() == drawn.size());  // each packet drawn once
    assert(drawn.size() == packets.size());
    assert(b.loop_end() == 0);
    for (uint32_t p : packets) assert(once.count(p));
}

void loop_not_at_head() {
    Bucket b;
    for (uint32_t p = 1; p <= 10; ++p) b.entry(p * 0x100);  // 0xA00 .. 0x100
    b.next[0x100] = 0x500;  // the list's end links back into its middle
    const uint32_t last = b.loop_end();
    assert(last == 0x100);
    b.next[last] = 0;
    assert(b.walk(100).size() == 10);
}

void limit() {
    Bucket b;
    for (uint32_t p = 1; p <= 50; ++p) b.entry(p * 4);
    b.next[4] = 200;  // a loop of all 50
    assert(j3d_draw_guard::loop_end(b.head, [&](uint32_t p) { return b.link(p); }, 10) == 0);  // too long to look at
    assert(j3d_draw_guard::loop_end(b.head, [&](uint32_t p) { return b.link(p); }) == 4);
}
}  // namespace

int main() {
    no_loop();
    self_loop();
    drawn_twice();
    loop_not_at_head();
    limit();
    std::printf("j3d_draw_guard: ok\n");
    return 0;
}
