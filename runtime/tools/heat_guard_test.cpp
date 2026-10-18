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
        assert(l.active && l.pause == 0.90f && l.resume == 0.80f);
        l = limits(kPhone, kRelaxed);
        assert(l.active && l.pause == 0.90f && l.resume == 0.80f);
        l = limits(kFanHandheld, kRelaxed);
        assert(l.active && l.pause == 0.90f && l.resume == 0.80f);
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
    // hysteresis, fan handheld: pause at >= 0.90, resume only below 0.80
    expect(run(kFanHandheld, kNormal, {0.85f, 0.89f, 0.90f, 0.85f, 0.80f, 0.79f, 0.89f, 0.95f}), "--PPP--P",
           "handheld hysteresis");
    // relaxed uses the handheld limits on any device
    expect(run(kPhone, kRelaxed, {0.89f, 0.90f, 0.81f, 0.79f}), "-PP-", "relaxed phone hysteresis");
    expect(run(kFanHandheld, kRelaxed, {0.89f, 0.90f, 0.81f, 0.79f}), "-PP-", "relaxed handheld hysteresis");
    // off: never paused, even at the limit and beyond
    expect(run(kFanHandheld, kOff, {0.95f, 1.0f, 1.2f}), "---", "off on a handheld");

    // a missing reading (no thermal service: NaN) keeps the state
    expect(run(kPhone, kNormal, {NAN, 0.80f, NAN, NAN, 0.50f, NAN}), "-PPP--", "NaN keeps the state");
    // switching to Off while paused resumes at once
    assert(!next_paused(true, 0.95f, limits(kPhone, kOff)));
    // switching from phone to handheld limits while paused at 0.85: stays paused until below 0.80
    assert(next_paused(true, 0.85f, limits(kFanHandheld, kNormal)));
    assert(!next_paused(true, 0.79f, limits(kFanHandheld, kNormal)));

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
