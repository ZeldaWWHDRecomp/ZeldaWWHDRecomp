// Dynamic resolution: the decision logic (no clocks, no renderer), shared by both renderers through
// dynres.cpp and tested on its own (runtime/tools/dynres_test.cpp).
//
// The idea comes from GreenNaugahyde's Android fork (v0.9.5): when the GPU holds the game below its
// frame rate, render at a lower resolution; when it has room again, go back up. The renderers here
// keep the images of recently used scales (no reallocation when the scale returns to one of them), so
// the scale moves through a few fixed levels of the chosen internal resolution rather than in free
// 0.05 steps: 1, 7/8, 3/4, 5/8 and 1/2 give whole-pixel sizes at the game's 1280x720.
//
// Input: one measurement window (about half a second): wall time, frames presented, GPU busy time
// (the union of the command buffers' GPU intervals) and the frame rate the game aims for.
// Busy share = GPU busy time / wall time. GPU-bound = the game misses its frame rate (< 95 %) while
// the GPU is busy more than 90 % of the time: one level down (always one: the renderers keep only the
// images of the levels next to the current one, so a one-level change needs no new images).
// A game held back by the CPU leaves the GPU idle, so the resolution stays.
// Going up: when the share predicted at the next level (pixels grow with the square of the scale)
// stays under 80 % for two windows in a row, and the last step down is at least `hold` ago. The hold
// starts at 2 s and doubles (up to 32 s) when a step up had to be undone within 4 s, so the scale
// does not swing back and forth; 30 s without a step down resets it. The window after a change is
// not judged (its frames were partly at the old scale).
#pragma once
#include <algorithm>
#include <cmath>

namespace dynres {

constexpr float kLevels[] = {1.0f, 0.875f, 0.75f, 0.625f, 0.5f};
constexpr int kLevelCount = int(sizeof kLevels / sizeof kLevels[0]);

// the lowest level allowed by a minimum factor (0 or anything >= 1: level 0 only, i.e. off)
inline int lowest_level(float minFactor) {
    if (!(minFactor > 0.0f) || minFactor >= 1.0f) return 0;
    int lo = 0;
    for (int i = 0; i < kLevelCount; i++)
        if (kLevels[i] >= minFactor - 1e-4f) lo = i;
    return lo;
}

struct Window {
    double seconds = 0;     // wall time of the window
    double busyMs = 0;      // GPU busy time in it (<= 0: no GPU timing, no decision)
    int frames = 0;         // frames presented in it
    double targetFps = 30;  // the frame rate the game aims for
    double budgetMs = 0;    // test switch: GPU budget per frame (> 0 replaces the busy share test)
};

struct Decision {
    int level = 0;
    bool changed = false;
    bool judged = false;  // false: the window was skipped (paused, settling, no timing)
    double share = -1, fps = 0;
};

struct Controller {
    static constexpr double kDownShare = 0.90, kUpShare = 0.80, kMissed = 0.95;
    static constexpr double kHoldBase = 2.0, kHoldMax = 32.0, kUndoneWithin = 4.0, kHoldReset = 30.0;
    static constexpr double kLongWindow = 2.0;  // longer: paused, in the background or loading
    int level = 0, lowest = 0;
    double hold = kHoldBase;
    double sinceDown = 1e9, sinceUp = 1e9;
    int room = 0;
    bool settle = false;

    void reset(int lowestLevel) {
        *this = Controller{};
        lowest = std::clamp(lowestLevel, 0, kLevelCount - 1);
    }
    void set_lowest(int lowestLevel) {
        lowest = std::clamp(lowestLevel, 0, kLevelCount - 1);
        if (level > lowest) level = lowest;
    }
    float factor() const { return kLevels[level]; }
    // a forced level (test schedule): counts as a change (the next window is not judged)
    void force(int l) {
        l = std::clamp(l, 0, kLevelCount - 1);
        if (l != level) settle = true;
        level = l;
    }

    Decision decide(const Window& w) {
        Decision d;
        d.level = level;
        if (w.seconds <= 0) return d;
        sinceDown += w.seconds;
        sinceUp += w.seconds;
        if (sinceDown > kHoldReset) hold = kHoldBase;
        d.fps = w.frames / w.seconds;
        const bool budget = w.budgetMs > 0;
        if (w.frames > 0 && w.busyMs > 0)
            d.share = budget ? (w.busyMs / w.frames) / w.budgetMs : w.busyMs / (w.seconds * 1000.0);
        if (w.seconds > kLongWindow || w.frames < 3 || d.share < 0) { room = 0; return d; }
        if (settle) { settle = false; room = 0; return d; }
        d.judged = true;
        const double target = w.targetFps > 0 ? w.targetFps : 30.0;
        const bool missed = budget || d.fps < target * kMissed;
        const int before = level;
        if (missed && d.share > (budget ? 1.0 : kDownShare)) {
            room = 0;
            if (level < lowest) {
                // one level at a time: the renderers keep the images of the neighbouring levels only
                level = std::min(lowest, level + 1);
                if (sinceUp < kUndoneWithin) hold = std::min(kHoldMax, hold * 2);
                sinceDown = 0;
            }
        } else if (level > 0) {
            const double r = double(kLevels[level - 1]) / kLevels[level];
            const double predicted = d.share * r * r;
            room = predicted < kUpShare ? room + 1 : 0;
            if (room >= 2 && sinceDown >= hold) {
                level--;
                sinceUp = 0;
                room = 0;
            }
        } else {
            room = 0;
        }
        if (level != before) settle = true;
        d.level = level;
        d.changed = level != before;
        return d;
    }
};

// GPU busy time from command buffer intervals that may overlap or come out of order: each interval
// adds only the time no earlier one covered (intervals arrive roughly in submission order).
struct BusyUnion {
    double lastEnd = 0;
    double busyNs = 0;
    void add(double startNs, double endNs) {
        if (!(endNs > startNs)) return;
        if (endNs + 1e9 < lastEnd) lastEnd = 0;  // the clock went back (new device, counter wrap)
        if (startNs < lastEnd) startNs = lastEnd;
        if (endNs > startNs) busyNs += endNs - startNs;
        if (endNs > lastEnd) lastEnd = endNs;
    }
    double take() { double b = busyNs; busyNs = 0; return b; }
};

}  // namespace dynres
