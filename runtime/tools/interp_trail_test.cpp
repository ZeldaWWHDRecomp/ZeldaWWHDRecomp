// Frame interpolation of swing trails: the head follows the blade on every drawn frame, the oldest
// point stays put, the trail shrinks smoothly after the swing (runtime/src/interp_trail.h).
#include "../src/interp_trail.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace interp::trail;

namespace {
// a sword swinging along x: sample s of the swing is at x = s (tip y = 10, base y = 0); a history
// with its newest sample `newest` and `count` segments
Frame swing(int newest, int count) {
    Frame f{};
    f.lanes = 2;
    f.count = count;
    for (int i = 0; i < kPoints; i++) {
        const float x = (float)(newest - i);
        f.p[0][i][0] = x; f.p[0][i][1] = 10; f.p[0][i][2] = 0;
        f.p[1][i][0] = x; f.p[1][i][1] = 0;  f.p[1][i][2] = 0;
    }
    return f;
}
bool close_to(float a, float b) { return std::fabs(a - b) < 1e-4f; }
}  // namespace

int main() {
    // 10 new samples per step (the sword's sub-frames), from 20 to 30 segments
    {
        const Frame a = swing(100, 20), b = swing(110, 30);
        assert(new_samples(a, b) == 10);
        float last_head = 100;
        for (float t : {0.25f, 0.5f, 0.75f}) {
            Frame m{};
            assert(at(a, b, t, m));
            const float head = m.p[0][0][0];
            assert(close_to(head, 100 + 10 * t));         // the blade's position at S+t
            assert(head > last_head);                  // moves on every drawn frame
            last_head = head;
            const float tail = m.p[0][m.count + 1][0];  // the oldest drawn point: 79 on both steps
            assert(std::fabs(tail - 79) <= 0.5f);
            assert(m.p[1][0][1] == 0 && m.p[0][0][1] == 10);  // lanes kept apart
        }
    }
    // a new swing: the trail grows from the blade at S
    {
        Frame a = swing(0, 0);
        const Frame b = swing(8, 8);
        Frame m{};
        assert(at(a, b, 0.5f, m));
        assert(close_to(m.p[0][0][0], 4) && m.count == 4);
    }
    // after the swing: no new samples, the count shrinks by 10 a step: blended length, same head
    {
        const Frame a = swing(100, 30), b = swing(100, 20);
        assert(new_samples(a, b) == 0);
        Frame m{};
        assert(at(a, b, 0.25f, m));
        assert(close_to(m.p[0][0][0], 100));
        assert(m.count == 28 || m.count == 27);
    }
    // the history was reset (a new swing elsewhere) with a trail drawn before: drawn as it is
    {
        const Frame a = swing(100, 20);
        Frame b = swing(500, 5);
        Frame m{};
        assert(new_samples(a, b) == -1);
        assert(!at(a, b, 0.5f, m));
    }
    // nothing changed: drawn as it is
    {
        const Frame a = swing(100, 20);
        Frame m{};
        assert(!at(a, a, 0.5f, m));
    }
    // a full history (58 segments) keeps every drawn point inside the 60 points
    {
        const Frame a = swing(100, 58), b = swing(110, 58);
        Frame m{};
        assert(at(a, b, 0.25f, m));
        assert(m.count + 1 + 7.5f <= kPoints - 1 + 0.5f);
    }
    printf("interp_trail: all tests passed\n");
    return 0;
}
