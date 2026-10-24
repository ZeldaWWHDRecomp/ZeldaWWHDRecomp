// SPDX-License-Identifier: MPL-2.0
// Safety net: a J3D packet is never entered into a draw bucket twice, and a bucket whose packet list
// loops on itself anyway is cut before the game walks it (j3d_draw_guard.h). J3DDrawBuffer::drawHead
// (027F10CC), drawTail (027F1174) and frameInit (027F093C) walk every bucket until a packet's next
// link is 0; the entries (entryZSort 027F0C14, entryNonSort 027F0DA8, entryImm 027F0E04) put a packet
// at the front of its bucket without looking whether it is in one already. A packet entered twice before frameInit (an actor
// drawn twice in one frame) closes a loop, and the walk never ends: the game thread draws the same
// packets until its display list is full and keeps going (2026-10-21: the editor's scene preview hang;
// story_demo.cpp creates its actors in Link's layer now, which removed that cause). Here a second
// entry of a packet that is in a bucket already is left out (the packet is drawn once), and a loop
// that is there all the same has the link that closes it set to 0, so the frame finishes. Lists
// without a loop are left as they are; both cases are said in the log (rate-limited).
#include "j3d_draw_guard.h"

#include <atomic>
#include <cstdint>

#include "runtime.h"

extern "C" void f_027F10CC_orig(Cpu* c);  // J3DDrawBuffer::drawHead(this, context)
extern "C" void f_027F1174_orig(Cpu* c);  // J3DDrawBuffer::drawTail(this, context)
extern "C" void f_027F093C_orig(Cpu* c);  // J3DDrawBuffer::frameInit(this)
extern "C" void f_027F0C14_orig(Cpu* c);  // J3DDrawBuffer::entryZSort(this, packet)
extern "C" void f_027F0DA8_orig(Cpu* c);  // J3DDrawBuffer::entryNonSort(this, packet)
extern "C" void f_027F0E04_orig(Cpu* c);  // J3DDrawBuffer::entryImm(this, packet, bucket)

namespace {
// J3DDrawBuffer: +0 bucket count, +4 the buckets (one packet pointer each); J3DPacket +0x10: next
constexpr uint32_t kBucketCount = 0x0, kBuckets = 0x4, kNext = 0x10;
constexpr uint32_t kEntrySlot = 0x94;  // J3DPacket: the bucket it is in (0: none; frameInit and remove clear it)
constexpr uint32_t kMaxBuckets = 0x10000;  // more is not a draw buffer: left alone
std::atomic<unsigned> g_cuts{0}, g_skips{0};

bool guest_word(uint32_t address) { return address >= mem::kMem2Start && address <= mem::kMem2End - 4 && !(address & 3); }

void cut_loops(uint32_t buffer, const char* walk) {
    if (!guest_word(buffer) || !guest_word(buffer + kBuckets)) return;
    const uint32_t count = ld32(buffer + kBucketCount), buckets = ld32(buffer + kBuckets);
    if (!count || count > kMaxBuckets || !guest_word(buckets) || !guest_word(buckets + (count - 1) * 4)) return;
    // a link outside guest memory ends the walk here (the game's own walk would fault on it anyway)
    auto next = [](uint32_t packet) { return guest_word(packet + kNext) ? ld32(packet + kNext) : 0u; };
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t head = ld32(buckets + i * 4);
        const uint32_t last = j3d_draw_guard::loop_end(head, next);
        if (!last) continue;
        const uint32_t first = ld32(last + kNext);
        st32(last + kNext, 0);
        const unsigned n = ++g_cuts;
        if (n <= 4 || (n & (n - 1)) == 0)
            LOG("[j3d] draw buffer %08X bucket %u looped on itself (packet %08X entered twice; the loop closes at %08X): "
                "cut before %s so the frame goes on (%u so far)",
                buffer, i, first, last, walk, n);
    }
}
// A packet that is in a bucket already (entered earlier this frame and not yet reset by frameInit): a
// second entry would link it to the packets after it and close a loop, or cut the packets after its
// first place out of the list (they keep their bucket and the next entry of one of them asserts). It is
// queued for drawing already, so the second entry is left out. Entries of a packet in no bucket are the
// game's own.
bool entered_already(uint32_t packet, const char* entry) {
    if (!guest_word(packet + kEntrySlot)) return false;
    const uint32_t slot = ld32(packet + kEntrySlot);
    if (!slot) return false;
    const unsigned n = ++g_skips;
    if (n <= 4 || (n & (n - 1)) == 0)
        LOG("[j3d] packet %08X (in bucket %08X) entered again by %s before its buffer was reset: left out, it is "
            "drawn once (%u so far)",
            packet, slot, entry, n);
    return true;
}
}  // namespace

extern "C" void hook_027F0C14(Cpu* c) {
    if (entered_already(c->r[4], "entryZSort")) { c->r[3] = 1; return; }
    f_027F0C14_orig(c);
}
extern "C" void hook_027F0DA8(Cpu* c) {
    if (entered_already(c->r[4], "entryNonSort")) { c->r[3] = 1; return; }
    f_027F0DA8_orig(c);
}
extern "C" void hook_027F0E04(Cpu* c) {
    if (entered_already(c->r[4], "entryImm")) { c->r[3] = 1; return; }
    f_027F0E04_orig(c);
}

extern "C" void hook_027F10CC(Cpu* c) {
    cut_loops(c->r[3], "drawHead");
    f_027F10CC_orig(c);
}
extern "C" void hook_027F1174(Cpu* c) {
    cut_loops(c->r[3], "drawTail");
    f_027F1174_orig(c);
}
extern "C" void hook_027F093C(Cpu* c) {
    cut_loops(c->r[3], "frameInit");
    f_027F093C_orig(c);
}
