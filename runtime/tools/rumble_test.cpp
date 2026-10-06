// Controller rumble (runtime/src/rumble.cpp): GamePad patterns (bits, not bytes; 120 a second; they
// end by themselves), the stop paths (VPADStopMotor, an empty pattern, a save state load), the
// Pro Controller motor and its stale state, and the option.
#include "rumble.h"

#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <thread>

void log_msg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

using rumble::Motor;
constexpr uint64_t kFrame = 1000000 / 60;  // one host update
static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
    const uint64_t t0 = 1000000;
    {
        // nothing asked: still
        Motor m;
        assert(m.level(t0, kFrame) == 0 && !m.active(t0));
    }
    {
        // 16 bits all on: full strength for 16/120 s, then still without a stop
        Motor m;
        const uint8_t on[2] = {0xFF, 0xFF};
        m.gamepad_pattern(on, 16, t0);
        assert(near(m.level(t0, kFrame), 1) && m.active(t0));
        assert(near(m.level(t0 + 10 * Motor::kBitUs, kFrame), 1));
        assert(m.level(t0 + 16 * Motor::kBitUs, kFrame) == 0 && !m.active(t0 + 16 * Motor::kBitUs));
        assert(m.level(t0 + 60 * 1000000ull, kFrame) == 0);
    }
    {
        // the length counts bits: a 4-bit pattern ignores the rest of its byte and what follows it
        // (the former code read `length` bytes, past the pattern, and ran the motor for 500 ms)
        Motor m;
        const uint8_t bits[16] = {0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        m.gamepad_pattern(bits, 4, t0);
        assert(m.level(t0, kFrame) == 0 && m.level(t0 + 2 * Motor::kBitUs, kFrame) == 0);
        assert(!m.active(t0 + 4 * Motor::kBitUs));
        // bit order: the first bit is the lowest of the first byte (as the game builds its patterns)
        const uint8_t first[1] = {0x01};
        m.gamepad_pattern(first, 8, t0);
        assert(m.level(t0, Motor::kBitUs) == 1 && m.level(t0 + Motor::kBitUs, Motor::kBitUs) == 0);
        // every other bit on: half strength over a host frame (two bits)
        const uint8_t half[2] = {0x55, 0x55};
        m.gamepad_pattern(half, 16, t0);
        assert(near(m.level(t0, 2 * Motor::kBitUs), 0.5f));
        // longer than the GamePad takes: 120 bits at most (one second)
        const uint8_t all[32] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        m.gamepad_pattern(all, 255, t0);
        assert(m.active(t0 + 119 * Motor::kBitUs) && !m.active(t0 + 120 * Motor::kBitUs));
    }
    {
        // stops: VPADStopMotor, an empty pattern, no pattern, a new pattern replaces the old one
        Motor m;
        const uint8_t on[15] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        m.gamepad_pattern(on, 120, t0);
        m.gamepad_stop();
        assert(m.level(t0 + kFrame, kFrame) == 0 && !m.active(t0 + kFrame));
        m.gamepad_pattern(on, 120, t0);
        m.gamepad_pattern(on, 0, t0 + kFrame);
        assert(m.level(t0 + kFrame, kFrame) == 0);
        m.gamepad_pattern(on, 120, t0);
        m.gamepad_pattern(nullptr, 120, t0 + kFrame);
        assert(m.level(t0 + kFrame, kFrame) == 0);
        const uint8_t off[15] = {};
        m.gamepad_pattern(on, 120, t0);
        m.gamepad_pattern(off, 120, t0 + kFrame);
        assert(m.level(t0 + 2 * kFrame, kFrame) == 0);
        // a save state load
        m.gamepad_pattern(on, 120, t0);
        m.pro_motor(true, t0);
        m.reset();
        assert(m.level(t0, kFrame) == 0 && !m.active(t0));
    }
    {
        // Pro Controller: on until stopped, while the game keeps sending it (every frame)
        Motor m;
        m.pro_motor(true, t0);
        for (uint64_t t = t0; t < t0 + 3000000; t += 33333) {
            m.pro_motor(true, t);
            assert(near(m.level(t, kFrame), 1));
        }
        m.pro_motor(false, t0 + 3000000);
        assert(m.level(t0 + 3000000, kFrame) == 0);
        // the game's rumble update stopped running (paused) with the motor on: still after a while
        m.pro_motor(true, t0);
        assert(near(m.level(t0 + Motor::kProStaleUs - 1, kFrame), 1));
        assert(m.level(t0 + Motor::kProStaleUs, kFrame) == 0 && !m.active(t0 + Motor::kProStaleUs));
        // the GamePad pattern is still felt beside it
        const uint8_t on[1] = {0xFF};
        m.gamepad_pattern(on, 8, t0 + Motor::kProStaleUs);
        assert(near(m.level(t0 + Motor::kProStaleUs, kFrame), 1));
    }
    {
        // the shared state and the option (real clock)
        assert(rumble::enabled() == !(getenv("WWHD_RUMBLE") && !atoi(getenv("WWHD_RUMBLE"))));
        rumble::set_enabled(true);
        const uint8_t on[15] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        rumble::gamepad_pattern(0, on, 120);
        assert(rumble::host_level(kFrame) > 0);
        rumble::set_enabled(false);  // off: the motors stop with the next update, the request stays
        assert(rumble::host_level(kFrame) == 0);
        rumble::set_enabled(true);
        assert(rumble::host_level(kFrame) > 0);
        rumble::gamepad_stop(1);  // other channels have no motor here
        assert(rumble::host_level(kFrame) > 0);
        rumble::gamepad_stop(0);
        assert(rumble::host_level(kFrame) == 0);
        rumble::pro_motor(0, true);
        assert(rumble::host_level(kFrame) > 0);
        rumble::reset();
        assert(rumble::host_level(kFrame) == 0);
        // a short pattern ends on its own
        rumble::gamepad_pattern(0, on, 4);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        assert(rumble::host_level(kFrame) == 0);
    }
    printf("rumble: all tests passed\n");
    return 0;
}
