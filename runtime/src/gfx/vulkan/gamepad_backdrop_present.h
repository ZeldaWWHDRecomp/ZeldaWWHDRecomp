#pragma once
#include <cstddef>
namespace gfxvk {
// The Off-TV plan retains an empty TV quad before the actual GamePad quad.
// Search beyond the first quad ONLY for that full-output mode: an ordinary TV
// picture with a GamePad PiP must never acquire a full-window backdrop.
template<class Quads, class Image>
std::size_t gamepad_backdrop_quad(const Quads& quads, Image* drc, bool drcOnly) {
    if (!drc || quads.empty()) return quads.size();
    if (!drcOnly && quads.front().image != drc) return quads.size();
    for (std::size_t i = 0; i < quads.size(); ++i) {
        const auto& q = quads[i];
        if (q.image == drc && !q.solid && q.box.w > 0 && q.box.h > 0) return i;
        if (!drcOnly) break;
    }
    return quads.size();
}
// Bars are drawn after every quad before the GamePad picture (the Off-TV
// plan's 0.6-alpha PiP frame, whose border reaches into the bars) and before
// the picture itself: the same order as Metal's compose_tv (frame, bars,
// picture). Bars first would leave the frame as a dark seam over them.
template<class Quads>
void insert_backdrop_bars(Quads& quads, std::size_t picture, const Quads& bars) {
    if (picture > quads.size()) picture = quads.size();
    quads.insert(quads.begin() + std::ptrdiff_t(picture), bars.begin(), bars.end());
}
}
