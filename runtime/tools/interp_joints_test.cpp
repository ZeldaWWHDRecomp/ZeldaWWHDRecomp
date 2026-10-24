// Frame interpolation of joint matrices: joints hidden and not updated on the record pass are drawn at
// the current step, everything else keeps blending (runtime/src/interp_joints.h, issue #255).
#include "../src/interp_joints.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace interp::joints;

namespace {
// guest words of a 3x4 matrix: identity rotation, translation (x, y, z)
std::vector<uint32_t> at(float x, float y, float z) {
    const float m[12] = {1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z};
    std::vector<uint32_t> w(12);
    for (int k = 0; k < 12; k++) w[k] = f_word(m[k]);
    return w;
}
std::vector<uint32_t> cat(std::initializer_list<std::vector<uint32_t>> ms) {
    std::vector<uint32_t> w;
    for (const auto& m : ms) w.insert(w.end(), m.begin(), m.end());
    return w;
}
void lerp(const float* a, const float* b, float* o, float t) {
    for (int k = 0; k < 12; k++) o[k] = a[k] + (b[k] - a[k]) * t;
}
float tx(const std::vector<uint32_t>& w, int joint) { return word_f(w[12 * joint + 3]); }
bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
}  // namespace

int main() {
    // stale(): only a hidden joint whose matrix was not written since the record pass before
    {
        auto a = at(1, 2, 3), b = at(1, 2, 3.5f);
        assert(stale(true, a.data(), a.data()));       // hidden, frozen: stale (a pose not in use)
        assert(stale(true, a.data(), nullptr));        // hidden, first record: nothing to blend from
        assert(!stale(true, b.data(), a.data()));      // hidden but animated (a skinning bone): blends
        assert(!stale(false, a.data(), a.data()));     // drawn and still: blends (a still prop)
        assert(!stale(false, a.data(), nullptr));
    }

    // Link's hands model: joint 0 the root, joint 1 the fist last used at x = 0, joint 2 the open hand
    // in use at x = 100. The fist comes back at Link's hand (x = 110): drawn there on every in-between
    // frame instead of flying in from x = 0.
    {
        auto prev = cat({at(0, 0, 0), at(0, 0, 0), at(100, 0, 0)});
        auto cur = cat({at(10, 0, 0), at(110, 0, 0), at(110, 0, 0)});
        const uint8_t flags[3] = {0, 1, 0};
        for (float t : {0.25f, 0.5f, 0.75f}) {
            std::vector<uint32_t> out(cur.size());
            Result r = blend(prev.data(), cur.data(), flags, 3, 400.0f, t, out.data(), lerp);
            assert(!r.cut && r.shown == 1);
            assert(near(tx(out, 0), 10 * t));          // the root blends
            assert(near(tx(out, 1), 110));             // the returning fist: current step
            assert(near(tx(out, 2), 100 + 10 * t));    // the hand in use blends
        }
        // without the flags (the old behaviour) the fist is drawn between the two places
        std::vector<uint32_t> out(cur.size());
        Result r = blend(prev.data(), cur.data(), nullptr, 3, 400.0f, 0.25f, out.data(), lerp);
        assert(!r.cut && r.shown == 0 && near(tx(out, 1), 27.5f));
    }

    // a stale joint far from its old place does not cut the whole model (it used to: the hand
    // joints start out as zero matrices at the world origin)
    {
        auto prev = cat({at(5000, 0, 0), at(0, 0, 0)});
        auto cur = cat({at(5010, 0, 0), at(5010, 0, 0)});
        const uint8_t flags[2] = {0, 1};
        std::vector<uint32_t> out(cur.size());
        Result r = blend(prev.data(), cur.data(), flags, 2, 400.0f, 0.5f, out.data(), lerp);
        assert(!r.cut && r.shown == 1 && near(tx(out, 0), 5005) && near(tx(out, 1), 5010));
        // a drawn joint that teleports still cuts
        const uint8_t none[2] = {0, 0};
        r = blend(prev.data(), cur.data(), none, 2, 400.0f, 0.5f, out.data(), lerp);
        assert(r.cut);
    }

    // continuously drawn models are unchanged: every joint blends, nothing is counted as shown, and a
    // stale joint that did not move is not counted either
    {
        auto prev = cat({at(0, 0, 0), at(1, 1, 1), at(7, 7, 7)});
        auto cur = cat({at(4, 0, 0), at(1, 1, 5), at(7, 7, 7)});
        const uint8_t flags[3] = {0, 0, 1};
        std::vector<uint32_t> out(cur.size()), ref(cur.size());
        Result r = blend(prev.data(), cur.data(), flags, 3, 400.0f, 0.5f, out.data(), lerp);
        Result r0 = blend(prev.data(), cur.data(), nullptr, 3, 400.0f, 0.5f, ref.data(), lerp);
        assert(!r.cut && r.shown == 0 && !r0.cut && out == ref);
        assert(near(tx(out, 0), 2) && near(word_f(out[12 + 11]), 3));
    }

    // Link's hands model after the Triforce cutscene (issue #255, second report): the root joint is
    // never written and holds 0xFFFFFFFF words (NaN) from the heap; the pose joints in use (fist at
    // joint 2, shield grip at joint 3 here) move with the arms. The root did not move: it takes no
    // part in the cut, and the hands blend with the arms on every in-between frame.
    {
        const std::vector<uint32_t> garbage = {0xFFFFFFFFu, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x345CDD47u, 0x04000000u, 0, 0,
                                               0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0xFFFFFFFFu};  // as the guest held it
        auto prev = cat({garbage, at(0, 0, 0), at(-20.6f, -2057.3f, 3651.1f), at(23.9f, -2058.3f, 3674.9f)});
        auto cur = cat({garbage, at(0, 0, 0), at(-10.5f, -2043.2f, 3617.3f), at(21.3f, -2052.8f, 3662.0f)});
        assert(std::isnan(word_f(garbage[3])));
        const uint8_t flags[4] = {0, 1, 0, 0};  // root: a bone (no shapes), joint 1: a hidden pose
        for (float t : {0.25f, 0.5f, 0.75f}) {
            std::vector<uint32_t> out(cur.size());
            Result r = blend(prev.data(), cur.data(), flags, 4, 400.0f, t, out.data(), lerp);
            assert(!r.cut && r.shown == 0);
            assert(std::equal(out.begin(), out.begin() + 12, garbage.begin()));  // the root word for word
            assert(std::fabs(tx(out, 2) - (-20.6f + 10.1f * t)) < 0.01f);           // the fist blends
            assert(std::fabs(word_f(out[12 * 3 + 11]) - (3674.9f - 12.9f * t)) < 0.01f);  // the grip blends
        }
        // a joint that moves to or from a NaN still cuts
        std::vector<uint32_t> out(cur.size());
        auto moved = cur;
        moved[3] = f_word(5);
        Result r = blend(prev.data(), moved.data(), flags, 4, 400.0f, 0.5f, out.data(), lerp);
        assert(r.cut && r.cut_joint == 0);
        // and a moving joint that teleports is the one named
        auto jump = cur;
        jump[12 * 2 + 3] = f_word(5000);
        r = blend(prev.data(), jump.data(), flags, 4, 400.0f, 0.5f, out.data(), lerp);
        assert(r.cut && r.cut_joint == 2);
    }

    // a joint that did not move is copied word for word (no blend arithmetic on it)
    {
        auto prev = cat({at(1.5f, 2, 3), at(0, 0, 0)});
        auto cur = cat({at(1.5f, 2, 3), at(8, 0, 0)});
        std::vector<uint32_t> out(cur.size());
        int calls = 0;
        auto counting = [&](const float* a, const float* b, float* o, float t) { calls++; lerp(a, b, o, t); };
        Result r = blend(prev.data(), cur.data(), nullptr, 2, 400.0f, 0.5f, out.data(), counting);
        assert(!r.cut && r.shown == 0 && calls == 1);
        assert(std::equal(out.begin(), out.begin() + 12, cur.begin()) && near(tx(out, 1), 4));
    }

    puts("interp_joints_test: ok");
    return 0;
}
