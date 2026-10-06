// Controller rumble: what the game asks of its controllers' motors, and what the host's motors
// should do about it.
//
// The game drives two kinds of motor (runtime/src/hle):
//   GamePad (VPADControlMotor / VPADStopMotor): the game sends a pattern of up to 120 bits, the
//     first in the lowest bit of the first byte; the GamePad plays one bit every 1/120 s (the game
//     waits nbits * 8333 us before it sends the next one) and then stops by itself. An empty
//     pattern or VPADStopMotor stops it at once. A new pattern replaces the one playing.
//   Pro Controller (WPADControlMotor): on or off until the game says otherwise. The game's rumble
//     update sends the state every frame while an effect runs, so a state that has not been sent
//     for kProStaleUs means that update no longer runs (game paused or stalled): it counts as off.
// A host controller has one motor with one strength; level() is the share of "on" bits over the
// next host frame (a GamePad pattern with every other bit set rumbles at half strength), or full
// strength while the Pro Controller motor is on.
//
// The option (settings overlay > Controls > Rumble, saved as "rumble"; WWHD_RUMBLE=0 starts with
// it off) only silences the host motors: the game's requests are still followed, so turning it on
// again picks up an effect that is still running.
#pragma once
#include <cstdint>

namespace rumble {

// The requested motor state, with explicit times (microseconds on any monotonic clock): the logic
// on its own, for the tests (runtime/tools/rumble_test.cpp). Not thread-safe; the functions below are.
class Motor {
public:
    static constexpr uint32_t kBitUs = 8333;           // GamePad: one pattern bit, 1/120 s
    static constexpr uint32_t kMaxBits = 120;          // GamePad: longest pattern (15 bytes)
    static constexpr uint64_t kProStaleUs = 500000;    // Pro Controller: no refresh for this long = off

    void gamepad_pattern(const uint8_t* bits, uint32_t nbits, uint64_t now);  // copies the pattern
    void gamepad_stop();
    void pro_motor(bool on, uint64_t now);
    void reset();                                      // everything still (a save state was loaded)
    // 0..1: what the motor does from `now` for `window` microseconds (the host's update interval)
    float level(uint64_t now, uint64_t window) const;
    bool active(uint64_t now) const;                   // some motor still runs or will run

private:
    uint8_t pattern_[kMaxBits / 8] = {};
    uint32_t nbits_ = 0;
    uint64_t start_ = 0;
    bool pro_on_ = false;
    uint64_t pro_time_ = 0;
};

// ---- the game's requests (guest threads, HLE). Only channel 0 has a motor here (one GamePad,
// one Pro Controller); other channels are ignored.
void gamepad_pattern(uint32_t chan, const uint8_t* bits, uint32_t nbits);
void gamepad_stop(uint32_t chan);
void pro_motor(uint32_t chan, bool on);
void reset();                    // save state loaded: the game's earlier requests no longer apply

// ---- the option (any thread)
bool enabled();
void set_enabled(bool on);       // off stops the host motors with the host's next update
bool env_override();             // WWHD_RUMBLE is set: it decides the start value, not the saved one

// ---- host (main thread, once per update): the motor strength now, 0..1; 0 while the option is off
float host_level(uint64_t window_us);

bool log_enabled();              // WWHD_LOG_RUMBLE=1: log requests and motor changes

}  // namespace rumble
