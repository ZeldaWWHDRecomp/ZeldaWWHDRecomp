// SPDX-License-Identifier: MPL-2.0
// J3D draw buckets that loop on themselves (runtime/src/j3d_draw_guard.cpp).
//
// A J3DDrawBuffer keeps one singly linked packet list per bucket (packet +0x10: the next packet).
// The entry functions put a packet at the front of its bucket without checking whether it is in a
// list already, so a packet entered twice before the buffer's frameInit closes a loop: the second
// entry links it to the packets that already follow it. drawHead, drawTail and frameInit then walk
// that list forever. This finds such a loop and the packet whose link closes it.
#pragma once

#include <cstdint>

namespace j3d_draw_guard {

// The packet whose next link closes a loop in the list from `head` (cutting that link leaves every
// packet in the list once), or 0 when the list ends. `next(p)` reads a packet's next link; 0 ends the
// list. Brent's cycle detection: no memory, at most a few passes over the list. `limit` bounds the
// steps (a list that long is reported as not looping).
template <class Next>
uint32_t loop_end(uint32_t head, Next next, uint32_t limit = 1u << 20) {
    if (!head) return 0;
    // find the loop's length
    uint32_t power = 1, length = 1, tortoise = head, hare = next(head), steps = 1;
    while (hare && hare != tortoise) {
        if (++steps > limit) return 0;
        if (power == length) {
            tortoise = hare;
            power *= 2;
            length = 0;
        }
        hare = next(hare);
        ++length;
    }
    if (!hare) return 0;
    // the loop's first packet: one walker `length` packets ahead of the other, they meet there
    uint32_t first = head, ahead = head;
    for (uint32_t i = 0; i < length; ++i) ahead = next(ahead);
    while (first != ahead) {
        first = next(first);
        ahead = next(ahead);
    }
    // the last packet of the loop links back to its first
    uint32_t last = first;
    while (next(last) != first) last = next(last);
    return last;
}

}  // namespace j3d_draw_guard
