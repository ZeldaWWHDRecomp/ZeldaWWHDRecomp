#pragma once
#include <cstdint>

// Which camera_draw of a blended logic pass gets an exact pre-run (interp.cpp exact_prerun).
// More than one camera process can be drawn on a pass (the sea: two). At 30 fps the culls after a camera's
// draw see that camera's view, so every camera of the pass needs its own exact culling view, not only the
// first one. Pure logic, so it can be tested without the game.
namespace interp::prerun_cams {
enum class Kind { None, First, More };
struct Cams {
    static constexpr int kMax = 8;
    uint32_t cam[kMax] = {};
    int n = 0;
    // first_done: the pass's first camera already had its pre-run. all_cams: further cameras get one too.
    Kind take(bool first_done, uint32_t c, bool all_cams) {
        if (!first_done) {
            cam[0] = c;
            n = 1;
            return Kind::First;
        }
        if (!all_cams) return Kind::None;
        for (int i = 0; i < n; i++)
            if (cam[i] == c) return Kind::None;  // (a camera drawn twice on a pass keeps its first pre-run)
        if (n >= kMax) return Kind::None;
        cam[n++] = c;
        return Kind::More;
    }
};
}  // namespace interp::prerun_cams
