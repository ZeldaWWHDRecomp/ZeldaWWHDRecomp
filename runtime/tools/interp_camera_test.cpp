// Frame interpolation of the camera: the 10 % distance rule applies only on collision pull-ins and
// event/demo cameras (runtime/src/interp_camera.h, issue #251), and real cuts still cut.
#include "../src/interp_camera.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace interp::camera;

namespace {
// camera looking at the origin from distance r along the -z axis (up +y)
CamState at(float r, float cx = 0, float fovy = 60) {
    CamState s{};
    s.center[0] = cx;
    s.eye[0] = cx;
    s.eye[2] = -r;
    s.up[1] = 1;
    s.fovy = fovy;
    return s;
}
float radius(const CamState& s) { return dist(s.eye, s.center); }
bool same(const CamState& a, const CamState& b) {
    for (int i = 0; i < 3; i++)
        if (a.eye[i] != b.eye[i] || a.center[i] != b.center[i] || a.up[i] != b.up[i]) return false;
    return a.fovy == b.fovy && a.bank == b.bank;
}
}  // namespace

int main() {
    // which steps the rule applies to
    {
        const StepInfo none{}, pulled{true, false}, event{false, true};
        assert(!distance_rule_applies(none, kDistCollision));
        assert(distance_rule_applies(pulled, kDistCollision));
        assert(distance_rule_applies(event, kDistCollision));
        assert(!distance_rule_applies(pulled, kDistOff) && !distance_rule_applies(event, kDistOff));
        assert(distance_rule_applies(none, kDistAlways));  // the old rule (debug switch =2)
    }

    // First person: the subject camera closes the radius from 300 to 20 in a straight line over 7
    // steps (40 units per step: 13 % and more). No collision: every in-between frame is blended, and
    // the radius shrinks on every drawn frame (not once every 4th frame at 120 fps).
    {
        float last = -1;
        int frames = 0;
        for (int k = 0; k < 7; k++) {
            const CamState a = at(300 - 40 * k), b = at(300 - 40 * (k + 1));
            assert(distance_jump(radius(a), radius(b)));  // the old rule cut every one of these steps
            for (float t : {0.25f, 0.5f, 0.75f, 1.0f}) {
                const CamState m = t == 1.0f ? b : blend(a, b, t, distance_rule_applies(StepInfo{}, kDistCollision));
                const float r = radius(m);
                if (t < 1.0f) assert(!same(m, b));
                if (last >= 0) assert(r < last - 1.0f);  // moves on every frame
                if (t < 1.0f) assert(std::fabs(r - ((1 - t) * radius(a) + t * radius(b))) < 1e-3f);
                last = r;
                frames++;
            }
        }
        assert(frames == 28);
        // with the old rule every one of those steps was drawn at its end on all 4 frames
        const CamState a = at(300), b = at(260);
        assert(same(blend(a, b, 0.25f, distance_rule_applies(StepInfo{}, kDistAlways)), b));
    }

    // Leaving first person: the follow camera eases back out from radius 20 (+107 %, +49 %, ...):
    // blended.
    {
        const CamState a = at(20), b = at(41.4f);
        const CamState m = blend(a, b, 0.5f, false);
        assert(std::fabs(radius(m) - 30.7f) < 1e-3f);
    }

    // A wall pulls the eye in from 400 to 150 in one step: drawn exactly (the in-between frames would
    // be inside the wall). The same jump without a reported hit (a smooth but fast zoom) blends.
    {
        const CamState a = at(400), b = at(150);
        const bool rule = distance_rule_applies(StepInfo{true, false}, kDistCollision);
        for (float t : {0.25f, 0.5f, 0.75f}) assert(same(blend(a, b, t, rule), b));
        assert(!same(blend(a, b, 0.5f, false), b));
        // a small change on a collision step (sliding along the wall) still blends
        const CamState c = at(160);
        assert(!same(blend(b, c, 0.5f, rule), c));
    }

    // Event camera cut to a closer shot along the same line of sight while the camera was moving
    // (step 120 after 50: the movement rules alone would not cut it): the distance rule cuts it.
    {
        float last_step = 50;
        const CamState a = at(300), b = at(180);
        assert(!cam_snap(a, b, last_step));
        assert(same(blend(a, b, 0.5f, distance_rule_applies(StepInfo{false, true}, kDistCollision)), b));
    }

    // Real cuts still cut, with or without the distance rule: a warp (> 800), a door (far more than
    // the recent motion), a field-of-view jump, a turn of more than 45 degrees.
    {
        float last_step = 5;
        assert(cam_snap(at(300), at(300, 900), last_step));         // warp
        last_step = 5;
        assert(cam_snap(at(300), at(300, 150), last_step));         // door: 150 after 5
        last_step = 5;
        assert(cam_snap(at(300), at(300, 0, 30), last_step));       // field of view 60 -> 30
        last_step = 5;
        assert(!cam_snap(at(300), at(300, 10), last_step));         // normal follow
        CamState turned = at(300);
        turned.eye[0] = 300;
        turned.eye[2] = 0;  // 90 degrees round the centre
        assert(same(blend(at(300), turned, 0.5f, false), turned));
    }

    // 60 fps (t = 1/2) keeps the original halfway arithmetic: normalised sum of the directions and
    // 0.5 * (la + lb), bit for bit
    {
        CamState a = at(300), b = at(290);
        b.eye[0] = 30;
        const CamState m = blend(a, b, 0.5f, false);
        const float la = radius(a), lb = std::sqrt(30.0f * 30.0f + 290.0f * 290.0f);
        float dir[3], ld = 0;
        for (int i = 0; i < 3; i++) {
            dir[i] = (a.eye[i] - a.center[i]) / la + (b.eye[i] - b.center[i]) / lb;
            ld += dir[i] * dir[i];
        }
        ld = std::sqrt(ld);
        for (int i = 0; i < 3; i++) assert(m.eye[i] == 0.5f * (a.center[i] + b.center[i]) + dir[i] / ld * (0.5f * (la + lb)));
    }

    // letterbox bars: eased heights blend, heights set at once are drawn at once
    {
        assert(trim_blends(2, 0.0f, 22.5f, 90.0f));     // easing in (25 % of 90)
        assert(trim_blends(0, 90.0f, 67.5f, 90.0f));    // easing out
        assert(!trim_blends(4, 90.0f, 0.0f, 90.0f));    // switched off at once
        assert(!trim_blends(3, 0.0f, 90.0f, 90.0f));    // cinema scope set at once
        assert(trim_blends(3, 90.0f, 67.5f, 90.0f));    // size 3 in an event eases out
        assert(!trim_blends(1, 50.0f, 50.0f, 90.0f));
    }
    // camera shake: the base camera blends, the new step's shake is added on top (blending the shaken
    // cameras averaged the alternating offsets away)
    {
        CamState a = at(300), b = at(300);
        Shake ka, kb;
        ka.on = kb.on = true;
        ka.eye[1] = ka.center[1] = 6.0f;    // step S: +6
        kb.eye[1] = kb.center[1] = -5.0f;   // step S+1: -5 (the sign flips every step)
        ka.bank = 100;
        kb.bank = -90;
        const CamState sa = add_shake(a, ka), sb = add_shake(b, kb);
        for (float t : {0.25f, 0.5f, 0.75f}) {
            const CamState m = blend_shaken(sa, ka, sb, kb, t, false);
            assert(std::fabs(m.eye[1] - sb.eye[1]) < 1e-3f && std::fabs(m.center[1] - sb.center[1]) < 1e-3f);
            assert(m.bank == sb.bank);
            const CamState old = blend(sa, sb, t, false);  // (the old way: shrunk towards 0)
            assert(std::fabs(old.eye[1] - b.eye[1]) < std::fabs(sb.eye[1] - b.eye[1]));
        }
        // no shake on either step: exactly the plain blend
        Shake none;
        const CamState p = blend_shaken(a, none, at(280), none, 0.25f, false), q = blend(a, at(280), 0.25f, false);
        for (int i = 0; i < 3; i++) assert(p.eye[i] == q.eye[i] && p.center[i] == q.center[i]);
        // a moving base with a shake: the base blends as without the shake
        CamState c = at(280);
        const CamState m = blend_shaken(sa, ka, add_shake(c, kb), kb, 0.5f, false), base = blend(a, c, 0.5f, false);
        for (int i = 0; i < 3; i++) assert(std::fabs(m.eye[i] - (base.eye[i] + kb.eye[i])) < 1e-3f);
    }
    // camera history slots (issue #299): two cameras drawn on every pass keep their history after
    // more cameras than slots have been seen; stale cameras of earlier stages give way
    {
        struct Slot { uint32_t cam = 0; uint64_t used = 0; int steps = 0; };
        Slot slots[4];
        uint64_t clock = 0;
        // cameras of earlier stages, each drawn for a while and then deleted with its stage
        for (uint32_t cam : {0x1000u, 0x2000u, 0x3000u, 0x4000u, 0x5000u})
            for (int pass = 0; pass < 3; pass++) history_slot(slots, cam, clock).steps++;
        // a stage with two camera processes, both drawn on every pass: no draw starts without history
        for (int pass = 0; pass < 20; pass++)
            for (uint32_t cam : {0xA000u, 0xB000u}) {
                Slot& s = history_slot(slots, cam, clock);
                assert(s.cam == cam);
                assert(s.steps == pass);  // (the old table gave them slot 0 in turn: always 0)
                s.steps++;
            }
        int live = 0;
        for (const Slot& s : slots) live += s.cam == 0xA000u || s.cam == 0xB000u;
        assert(live == 2);
        // a new camera takes an unused slot before one in use
        Slot fresh[3];
        uint64_t c2 = 0;
        history_slot(fresh, 0x1u, c2).steps = 7;
        assert(history_slot(fresh, 0x2u, c2).steps == 0 && history_slot(fresh, 0x1u, c2).steps == 7);
        // as many cameras drawn on every pass as there are slots: none loses its history
        Slot four[4];
        uint64_t c4 = 0;
        for (int pass = 0; pass < 5; pass++)
            for (uint32_t cam = 1; cam <= 4; cam++) assert(history_slot(four, cam, c4).steps++ == pass);
    }
    printf("interp_camera: all tests passed\n");
    return 0;
}
