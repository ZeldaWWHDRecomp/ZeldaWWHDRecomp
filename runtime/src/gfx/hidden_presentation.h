#pragma once
// Hidden runs (WWHD_HIDDEN_WINDOWS=1: test runs, setup's warm and its models step) draw every frame of the game
// into the game's own render targets exactly as in play, but present nothing: the windows are never put on
// screen (desktop) and the app's surface stays unpresented (Android, setup's models step, whose progress
// screen covers the surface). Only the final copy to the screen is left out; no pipeline the game records
// depends on the screen (gfx/vulkan/draw.cpp pipeline_key: shaders, the GX2 render targets' formats and the
// fixed-function state).
//
// WWHD_TEST_PRESENT_HIDDEN=1 (proof runs only): hidden windows still present to their swapchains, so a hidden
// run and a presenting run of the same scripted slice can be compared (tools/installer/model_sweep_keys.py).
// Kept free of SDL and Vulkan so runtime/tools/hidden_presentation_test.cpp can check it.
#include <cstdlib>
#include <cstring>

namespace gfx::hidden {

inline bool env_on(const char* v) { return v && *v && std::strcmp(v, "0") != 0; }

struct Mode {
    bool hidden = false;        // WWHD_HIDDEN_WINDOWS
    bool test_present = false;  // WWHD_TEST_PRESENT_HIDDEN
    // whether a screen of this run may present (shown, restored, back in the foreground)
    bool presents() const { return !hidden || test_present; }
};

inline Mode parse(const char* hidden, const char* testPresent) {
    Mode m;
    m.hidden = env_on(hidden);
    m.test_present = m.hidden && env_on(testPresent);
    return m;
}

inline const Mode& mode() {
    static const Mode m = parse(std::getenv("WWHD_HIDDEN_WINDOWS"), std::getenv("WWHD_TEST_PRESENT_HIDDEN"));
    return m;
}

}  // namespace gfx::hidden
