// The step history of a converted process's draw state (t60_draw.h): states stamped with the time (30 Hz frames on the
// step clock) of the step that produced them, and the pick of what to draw at the world's time. No guest memory here
// (runtime/tools/t60_test.cpp tests it).
#pragma once
#include <cmath>
#include <deque>
#include <utility>

namespace t60::draw::hist {
constexpr double kEps = 1e-6;
constexpr size_t kKeep = 12;  // steps kept (N = 4: three back, plus a 30 Hz fallback frame's two)

// a state of the newest step `time`: replaces a state of the same step (a process drawn again on a pass without a step
// keeps its step's stamp), appends a newer one; an older stamp (a save state, a reset clock) starts the history over
template <class State>
void stamp(std::deque<std::pair<double, State>>& steps, double time, State s) {
    if (!steps.empty() && std::fabs(steps.back().first - time) < kEps) steps.back().second = std::move(s);
    else {
        if (!steps.empty() && steps.back().first > time) steps.clear();
        steps.emplace_back(time, std::move(s));
    }
    while (steps.size() > kKeep) steps.pop_front();
}

// The draw of a step that is no step of its own (a whole-form actor's picture step) is not kept: the history
// holds that actor's frame ends only, and pick() blends between them. `make` builds the state (only when kept). False:
// nothing kept yet (draw as it is)
template <class State, class Make>
bool record(std::deque<std::pair<double, State>>& steps, double time, bool picture, Make&& make) {
    if (!picture) stamp(steps, time, make());
    return !steps.empty();
}

// what to draw at world time w: the two kept steps around it and the fraction between them (a == b: exactly that step;
// before the first kept step: the first, a new process; after the newest: the newest)
struct Pick { int a = -1, b = -1; float f = 0.0f; };
template <class State>
Pick pick(const std::deque<std::pair<double, State>>& steps, double w) {
    Pick p;
    for (int i = 0; i < (int)steps.size(); i++) {
        if (steps[i].first <= w + kEps) p.a = i;
        if (p.b < 0 && steps[i].first >= w - kEps) p.b = i;
    }
    if (p.a < 0) p.a = p.b;
    if (p.b < 0) p.b = p.a;
    if (p.a >= 0 && p.a != p.b && steps[p.b].first - steps[p.a].first > kEps)
        p.f = (float)((w - steps[p.a].first) / (steps[p.b].first - steps[p.a].first));
    return p;
}
}  // namespace t60::draw::hist
