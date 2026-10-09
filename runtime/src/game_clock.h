// SPDX-License-Identifier: MPL-2.0
// Anchored clock adapted from GreenNaugahyde's ZeldaWWHDRecompAndroid
// 9551a2508c9eb6f4417ee27116f998237d52addc (gx2_core.cpp).
#pragma once
#include <cstdint>
namespace game_clock {
// Integer nanoseconds preserve the fractional-vsync progress at every rate change.
class Clock {
    int64_t anchor_ = 0, extra_ = 0;
    unsigned rate_ = 1;
public:
    int64_t now(int64_t host) const { return host + extra_ + (host - anchor_) * (rate_ - 1); }
    unsigned rate() const { return rate_; }
    void set_rate(int64_t host, unsigned rate) {
        if (rate < 1 || rate > 4 || rate == rate_) return;
        extra_ = now(host) - host;
        anchor_ = host;
        rate_ = rate;
    }
    int64_t delay(int64_t host, int64_t target) const {
        const int64_t left = target - now(host);
        return left > 0 ? (left + rate_ - 1) / rate_ : 0;
    }
};
struct Sample { int64_t host, guest; unsigned rate; };
Sample sample();
void set_rate(unsigned rate);
unsigned rate();
bool shifted(); // false until the first acceleration: preserve the original clock path
}
