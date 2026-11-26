#pragma once
#include "input_map.h"
namespace overlay {
// Records the controller input the user presses next (Controls bindings, the controller setup's jump
// button, Shortcuts holds, the Mac Controls window).
//
// Each input is measured against its own resting value, not against 0: the first poll after reset()
// takes every input's value as its rest. An input counts as pressed only when it rises clearly above
// its rest (kRise, and above kPress as before for a normal controller). An input already held at the
// start rests high until it is let go: a drop of kRelease or more lowers its rest to the new value, so
// the button that started the capture counts only on its next press. Nothing waits for every input to
// be released at once, so a trigger resting half-way, a drifting stick or an extra axis that never
// reads 0 neither blocks the capture nor gets recorded. A controller whose inputs all rest at 0 records
// what it did before: the first input above kPress.
class BindingCapture {
public:
    static constexpr float kPress = 0.6f;    // pressed (as before): above this
    static constexpr float kDown = 0.5f;     // held (Shortcuts holds): above this
    static constexpr float kRise = 0.5f;     // ... and at least this far above the input's rest
    static constexpr float kRelease = 0.25f; // a drop this large lowers the rest (let go; not stick noise)

    void reset() { armed_ = false; }
    // the first input pressed in index order this poll (input_map::Pad), -1: none
    int poll(const float* values) {
        if (!armed_) {
            for (int p = 0; p < input_map::kPadCount; ++p) rest_[p] = values[p];
            armed_ = true;
            return -1;
        }
        int pressed = -1;
        for (int p = 1; p < input_map::kPadCount; ++p) {
            if (values[p] <= rest_[p] - kRelease) rest_[p] = values[p];
            if (pressed < 0 && values[p] > kPress && rises(values, p)) pressed = p;
        }
        return pressed;
    }
    // input p is held clearly above its rest (> kDown); before the first poll: above kDown
    bool down(const float* values, int p) const {
        return p > 0 && p < input_map::kPadCount && values[p] > kDown && (!armed_ || rises(values, p));
    }
    float rest(int p) const { return armed_ ? rest_[p] : 0.f; }

private:
    bool rises(const float* values, int p) const { return values[p] - rest_[p] >= kRise - 1e-4f; }
    bool armed_ = false;
    float rest_[input_map::kPadCount] = {};
};
}  // namespace overlay
