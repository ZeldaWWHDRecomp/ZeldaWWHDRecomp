// The heat guard's threshold policy (platform/heat_guard.h): device class x setting x a sequence of
// thermal headroom readings -> 60 fps paused or not, with the hysteresis between pause and resume.
#include "platform/heat_guard.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>

using namespace heat_guard;

namespace {
// The paused state after each reading, as a string of 'P' (paused) and '-' (60 fps).
std::string run(DeviceClass device, Setting setting, std::initializer_list<float> headrooms) {
    const Limits l = limits(device, setting);
    bool paused = false;
    std::string out;
    for (float h : headrooms) {
        paused = next_paused(paused, h, l);
        out += paused ? 'P' : '-';
    }
    return out;
}
void expect(const std::string& got, const char* want, const char* what) {
    if (got != want) {
        std::fprintf(stderr, "%s: got %s, want %s\n", what, got.c_str(), want);
        assert(false);
    }
}
}  // namespace

int main() {
    // limits per device class and setting
    {
        Limits l = limits(kPhone, kNormal);
        assert(l.active && l.pause == 0.72f && l.resume == 0.60f);
        l = limits(kFanHandheld, kNormal);
        assert(l.active && l.pause == 1.00f && l.resume == 0.90f);
        l = limits(kPhone, kRelaxed);
        assert(l.active && l.pause == 0.90f && l.resume == 0.80f);
        l = limits(kFanHandheld, kRelaxed);
        assert(l.active && l.pause == 1.10f && l.resume == 1.00f);
        assert(!limits(kPhone, kOff).active && !limits(kFanHandheld, kOff).active);
    }
    // the AYANEO Pocket S2 Pro trace from issue #247: 0.57 -> 0.67 -> 0.73, then it stays warm
    const auto s2 = {0.57f, 0.67f, 0.73f, 0.74f, 0.70f, 0.65f, 0.62f};
    expect(run(kPhone, kNormal, s2), "--PPPPP", "phone limits pause at 0.73 and never resume above 0.60");
    expect(run(kFanHandheld, kNormal, s2), "-------", "fan handheld keeps 60 fps at 0.73");
    expect(run(kPhone, kRelaxed, s2), "-------", "relaxed phone keeps 60 fps at 0.73");
    expect(run(kPhone, kOff, s2), "-------", "off never pauses");

    // hysteresis, phone: pause at >= 0.72, resume only below 0.60 (0.60 itself stays paused)
    expect(run(kPhone, kNormal, {0.71f, 0.72f, 0.65f, 0.60f, 0.59f, 0.71f, 0.72f}), "-PPP--P", "phone hysteresis");
    // hysteresis, fan handheld: pause at >= 1.00, resume only below 0.90
    expect(run(kFanHandheld, kNormal, {0.95f, 0.99f, 1.00f, 0.95f, 0.90f, 0.89f, 0.99f, 1.05f}), "--PPP--P",
           "handheld hysteresis");
    // relaxed: one step above the device's normal limits
    expect(run(kPhone, kRelaxed, {0.89f, 0.90f, 0.81f, 0.79f}), "-PP-", "relaxed phone hysteresis");
    expect(run(kFanHandheld, kRelaxed, {1.09f, 1.10f, 1.01f, 0.99f}), "-PP-", "relaxed handheld hysteresis");
    // the AYANEO EVO and KONKR Pocket FIT Elite traces from #247 (0.90-0.96 at about 70 C): 60 fps stays on
    expect(run(kFanHandheld, kNormal, {0.90f, 0.92f, 0.94f, 0.96f, 0.93f, 0.89f}), "------", "handheld keeps 60 fps at 0.90-0.96");
    // off: never paused, even at the limit and beyond
    expect(run(kFanHandheld, kOff, {0.95f, 1.0f, 1.2f}), "---", "off on a handheld");

    // a missing reading (no thermal service: NaN) keeps the state
    expect(run(kPhone, kNormal, {NAN, 0.80f, NAN, NAN, 0.50f, NAN}), "-PPP--", "NaN keeps the state");
    // switching to Off while paused resumes at once
    assert(!next_paused(true, 0.95f, limits(kPhone, kOff)));
    // switching from phone to handheld limits while paused at 0.95: stays paused until below 0.90
    assert(next_paused(true, 0.95f, limits(kFanHandheld, kNormal)));
    assert(!next_paused(true, 0.89f, limits(kFanHandheld, kNormal)));

    // temperature check on fan handhelds (#254). AYANEO Pocket EVO, v0.4.0 and older logs: headroom
    // 1.09 -> 1.38 in normal 60 fps play at a 30-31 C battery, hottest CPU sensor 60-77 C: 60 fps stays on
    {
        const Limits l = limits(kFanHandheld, kNormal);
        const Temperatures evo{30.5f, 77.2f};
        bool paused = false;
        for (float h : {1.09f, 1.11f, 1.20f, 1.30f, 1.33f, 1.38f}) {
            paused = next_paused(paused, h, l, kFanHandheld, evo);
            assert(!paused);
        }
        // battery unreadable (power_supply blocked), CPU sensors cool: still not paused
        assert(!next_paused(false, 1.38f, l, kFanHandheld, Temperatures{NAN, 60.1f}));
        // real heat: a hot CPU or a hot battery confirms the headroom
        assert(next_paused(false, 1.05f, l, kFanHandheld, Temperatures{31.0f, 92.0f}));
        assert(next_paused(false, 1.05f, l, kFanHandheld, Temperatures{44.0f, 70.0f}));
        assert(next_paused(false, 1.00f, l, kFanHandheld, Temperatures{40.0f, NAN}));
        assert(next_paused(false, 1.00f, l, kFanHandheld, Temperatures{NAN, 90.0f}));
        assert(!next_paused(false, 1.00f, l, kFanHandheld, Temperatures{39.9f, 89.9f}));
        // the headroom still has to reach the limit: hot sensors alone never pause
        assert(!next_paused(false, 0.95f, l, kFanHandheld, Temperatures{45.0f, 95.0f}));
        // no readable temperature: cannot confirm a new handheld pause
        assert(!next_paused(false, 1.00f, l, kFanHandheld, Temperatures{}));
        // instantaneous release candidate: warm (85 C) keeps the pause, cool permits release,
        // a headroom below 0.90 ends it whatever the temperatures
        assert(next_paused(true, 1.20f, l, kFanHandheld, Temperatures{33.0f, 85.0f}));
        assert(next_paused(true, 1.20f, l, kFanHandheld, Temperatures{38.0f, 70.0f}));
        assert(!next_paused(true, 1.20f, l, kFanHandheld, Temperatures{33.0f, 79.9f}));
        assert(!next_paused(true, 0.89f, l, kFanHandheld, Temperatures{45.0f, 95.0f}));
        assert(next_paused(true, 1.20f, l, kFanHandheld, Temperatures{}));
        // Off never pauses
        assert(!next_paused(true, 1.5f, limits(kFanHandheld, kOff), kFanHandheld, Temperatures{45.0f, 95.0f}));
        // phones keep the headroom rule whatever the sensors read
        const Limits p = limits(kPhone, kNormal);
        assert(next_paused(false, 0.72f, p, kPhone, Temperatures{30.0f, 60.0f}));
        assert(next_paused(true, 0.65f, p, kPhone, Temperatures{30.0f, 60.0f}));
        assert(!next_paused(false, 0.71f, p, kPhone, Temperatures{45.0f, 95.0f}));
        assert(!checks_temperatures(kPhone) && checks_temperatures(kFanHandheld));
    }

    // #277 replay and timed hysteresis exercise the state used by perf_hint.cpp.
    {
        const auto l = limits(kFanHandheld, kNormal);
        State state;
        int64_t now = 0;
        for (float h : {0.99f, 1.01f, 1.38f, 0.89f, 1.09f}) {
            assert(!state.update(h, l, kFanHandheld, {30.5f, 77.2f}, now));
            now += 1000;
            assert(!state.update(h, l, kFanHandheld, {}, now));
            now += 1000;
        }
        assert(state.update(1.1f, l, kFanHandheld, {40.f, 92.f}, 10000));
        assert(state.update(0.89f, l, kFanHandheld, {}, 11000));
        assert(state.update(1.1f, l, kFanHandheld, {39.f, 85.f}, 12000));
        assert(state.update(1.1f, l, kFanHandheld, {31.f, 70.f}, 13000));
        assert(state.update(NAN, l, kFanHandheld, {}, 14000));
        for (int64_t t = 15000; t < 20000; t += 1000)
            assert(state.update(1.1f, l, kFanHandheld, {31.f, 70.f}, t));
        assert(state.update(1.1f, l, kFanHandheld, {31.f, 70.f}, 19999));
        assert(!state.update(1.1f, l, kFanHandheld, {31.f, 70.f}, 20000));
        assert(state.update(1.1f, l, kFanHandheld, {40.f, 92.f}, 21000));
        assert(!state.update(1.1f, limits(kFanHandheld, kOff), kFanHandheld, {}, 21001));
        // Phones also need sustained headroom below their existing resume limit.
        State phone;
        const auto p = limits(kPhone, kNormal);
        assert(phone.update(.8f, p, kPhone, {}, 0));
        assert(phone.update(.5f, p, kPhone, {}, 1000));
        assert(phone.update(.5f, p, kPhone, {}, 10000)); // sampling gap
        for (int64_t t = 11000; t < 15000; t += 1000)
            assert(phone.update(.5f, p, kPhone, {}, t));
        assert(!phone.update(.5f, p, kPhone, {}, 15000));
    }

    // the setting's names (saved value and WWHD_HEAT_GUARD)
    assert(parse(nullptr) == kNormal && parse("normal") == kNormal && parse("0") == kNormal);
    assert(parse("relaxed") == kRelaxed && parse("1") == kRelaxed);
    assert(parse("off") == kOff && parse("2") == kOff);
    assert(parse("garbage") == kNormal && parse("") == kNormal);  // invalid: the safe default
    for (Setting s : {kNormal, kRelaxed, kOff}) assert(parse(name(s)) == s);
    assert(to_device(1) == kFanHandheld && to_device(0) == kPhone && to_device(7) == kPhone);
    assert(to_setting(2) == kOff && to_setting(-1) == kNormal);
    std::puts("heat guard policy: ok");
    return 0;
}
