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
}
