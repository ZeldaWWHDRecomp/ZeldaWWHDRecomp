#include "mods/camera_mouse.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <initializer_list>

int main() {
    using namespace mods::camera_mouse;
    for (unsigned proc : {0x86, 0x87, 0x88, 0x89, 0x8E, 0x8F, 0x90, 0x91}) assert(!aiming_item(proc));
    for (unsigned proc : {0x76, 0x7D, 0x80, 0x81, 0x83, 0x84, 0x8A, 0x8B, 0x8C, 0x8D, 0x94, 0x95}) assert(aiming_item(proc));
    assert(cannon_aim(0x8E,true) && !cannon_aim(0x8E,false));
    for(unsigned proc : {0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8F,0x90,0x91}) assert(!cannon_aim(proc,true));
    assert(!aiming_item(0) && !aiming_item(0xFFFFFFFF));
    // Convert back with each style's live yaw multiplier: equal points mean equal degrees.
    for (double per : {4.0, 6.0, 8.0, 12.0}) {
        for (float points : {-20.f, -5.f, 0.f, 5.f, 20.f}) {
            assert(std::fabs(yaw_speed(points, 0.15f, per) * 0.92f * per + points * 0.15f) < 0.00001);
        }
    }
    assert(yaw_speed(5, 0.15f, 0) == yaw_speed(5, 0.15f, 8));
    assert(yaw_speed(5, 0.15f, std::numeric_limits<double>::quiet_NaN()) == yaw_speed(5, 0.15f, 8));
    assert(fallback_stick(0) == 0);
    assert(std::fabs(fallback_stick(5) - 0.4f) < 0.00001f);
    assert(std::fabs(fallback_stick(1) - 0.08f) < 0.00001f);
    assert(fallback_stick(-5) == -fallback_stick(5));
    assert(fallback_stick(100) == 1 && fallback_stick(-100) == -1);
    // No retained rate after a stop, and an immediate proportional reversal.
    const float movement[] = {5, 10, 0, -5, 0};
    const float rates[] = {0.4f, 0.8f, 0, -0.4f, 0};
    for (int i = 0; i < 5; ++i) assert(std::fabs(fallback_stick(movement[i]) - rates[i]) < 0.00001f);
    // First person (issue #558): subject_step against the game's own arithmetic (CalcSubjectAngle and the camera,
    // single precision, truncation to s16 units), for the subject styles' parameters (cking.rpx: SS01 R3 look, SX01
    // bow / hookshot / grappling hook, SY01 boomerang, SX02, SN15 telescope / Picto Box zoomed in: k_eff < k).
    struct Game {
        long yaw = 0;      // units turned right so far (Link's facing)
        int v = 0;         // pitch, units (down +)
        float m388 = 0;
        bool rebuild;      // the camera rebuilds m388 from Link's body angle in whole units (Link turned himself)
        void run(float x, float y, float k, float yaw_max, float pitch_max, bool swim) {
            float m384 = -(x * k);
            m384 = std::fmax(-1.f, std::fmin(1.f, m384));
            yaw -= (long)(int16_t)(int32_t)((float)(yaw_max * m384) * 182.04445f);
            const float yg = swim ? -y : y;
            float m = std::fmax(-1.f, std::fmin(1.f, yg * k + m388));
            v = (int16_t)(int32_t)((float)(pitch_max * m) * 182.04445f);
            m388 = rebuild ? (float)((float)(v * (360.0 / 65536)) / pitch_max) : m;
        }
    };
    struct Style { double k, yaw, pitch; };
    for (Style st : {Style{.04, 90, 70}, Style{.04, 90, 60}, Style{.04, 90, 50}, Style{.04 * (1 - .75 * .6), 90, 70}}) {
        for (bool rebuild : {true, false}) {
            for (bool swim : {false, true}) {
                // slow (one point a step at the lowest sensitivity), normal, a fast flick beyond 90 degrees a step
                for (double per_step : {.08, .011, .15 * 7, -.3 * 40, .15 * 1000}) {
                    Game g{0, 0, 0, rebuild};
                    double rest_yaw = 0, rest_pitch = 0, want_right = 0, want_down = 0;
                    const int steps = 120;
                    for (int i = 0; i < steps; i++) {
                        const double right = per_step, down = (i % 3 == 0 ? -1 : 0.5) * std::fmin(std::fabs(per_step), 1.0) / 2;
                        SubjectStep r = subject_step(right, down, rest_yaw, rest_pitch, st.k, st.yaw, st.pitch, g.m388, swim);
                        rest_yaw = r.rest_yaw, rest_pitch = r.rest_pitch;
                        assert(std::fabs(r.rest_pitch) <= 0.5 + 1e-9);
                        g.run(r.x, r.y, (float)st.k, (float)st.yaw, (float)st.pitch, swim);
                        want_right += right, want_down += swim ? -down : down;
                        // the view stands on the unit nearest to the mouse's way so far, within the pitch range
                        const double carried = std::fabs(rest_yaw);
                        assert(std::fabs(g.yaw + rest_yaw - want_right * kUnitsPerDegree) < 1e-3 * (1 + carried));
                        if (std::fabs(per_step) < 90) assert(std::fabs(rest_yaw) <= 0.5 + 1e-9);
                        if (std::fabs(want_down) * kUnitsPerDegree < st.pitch * kUnitsPerDegree - 1)
                            if (!(std::fabs(g.v - want_down * kUnitsPerDegree) <= 0.5 + 1e-6)) { fprintf(stderr, "k %g pitch %g rebuild %d swim %d per %g i %d v %d want %.3f m388 %.6f\n", st.k, st.pitch, rebuild, swim, per_step, i, g.v, want_down * kUnitsPerDegree, g.m388); assert(false); }
                    }
                    if (std::fabs(per_step) >= 90) {  // a flick faster than the game turns: the rest comes next
                        for (int i = 0; i < 4000 && std::fabs(rest_yaw) > 0.5; i++) {
                            SubjectStep r = subject_step(0, 0, rest_yaw, 0, st.k, st.yaw, st.pitch, g.m388, swim);
                            rest_yaw = r.rest_yaw;
                            g.run(r.x, 0, (float)st.k, (float)st.yaw, (float)st.pitch, swim);
                        }
                        if (!(std::fabs(g.yaw - want_right * kUnitsPerDegree) <= 0.5 + 1e-6)) { fprintf(stderr, "k %g rebuild %d swim %d per %g yaw %ld want %.3f rest %.3f\n", st.k, rebuild, swim, per_step, g.yaw, want_right * kUnitsPerDegree, rest_yaw); assert(false); }
                    }
                }
            }
        }
    }
    // not a subject style (or no data): no input at all, never a division by zero
    for (Style st : {Style{0, 90, 70}, Style{.04, 0, 70}, Style{.04, 90, 0}, Style{-.04, 90, 70},
                     Style{std::numeric_limits<double>::quiet_NaN(), 90, 70}}) {
        SubjectStep r = subject_step(5, 5, 0, 0, st.k, st.yaw, st.pitch, 0, false);
        assert(r.x == 0 && r.y == 0 && r.rest_yaw == 0 && r.rest_pitch == 0);
    }
    assert(landing(0) == 0 && landing(3) == 3.5 && landing(-3) == -3.5);
    assert(kMouseViewEase == 1.0);
    puts("camera_mouse: aiming procedures, yaw speed, fallback stick, first-person mouse steps PASS");
}
