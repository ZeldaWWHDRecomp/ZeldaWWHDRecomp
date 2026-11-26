// SPDX-License-Identifier: MPL-2.0
// The sound listener with frame interpolation (runtime/src/interp_listener.h, issue #533): simulates
// the camera draws of a step and the sound engine's step between them, at 60 fps and at 40 fps, right
// after a stage change (the listener still holds the loading screen's placeholder camera). The engine
// must hear the step's exact camera (as the original, where camera_draw runs just before its step),
// never the placeholder and never a halfway camera, and the listener must not change within a step
// at 60 fps (no hop between the halfway and the exact camera).
#include <cassert>
#include <cstdio>

#include "../src/interp_listener.h"

namespace {
struct Draw { bool blended, prerun; float eye; };
constexpr float kPlaceholder = 1e7f;

// one step: the logic pass's draws, the engine's step, then the record pass's draw; returns what the
// engine heard and checks the listener after the record pass
// (record: nullptr when the logic pass is itself the record pass)
float step(const Draw* logic, int n, const Draw* record, bool record_blended, float& listener, bool no_hop) {
    for (int i = 0; i < n; i++)
        if (interp::listener_takes_camera(logic[i].blended, logic[i].prerun, record_blended)) listener = logic[i].eye;
    const float heard = listener;
    if (record && interp::listener_takes_camera(record->blended, record->prerun, record_blended)) listener = record->eye;
    if (no_hop) assert(listener == heard);
    return heard;
}
}  // namespace

int main() {
    // 60 fps, first step of the new stage: exact camera 100, halfway camera 50
    {
        float listener = kPlaceholder;
        const Draw logic[] = {{true, true, 100.0f}, {true, false, 50.0f}};
        const Draw record{false, false, 100.0f};
        assert(step(logic, 2, &record, false, listener, true) == 100.0f);
        // next step: exact 110, halfway 105
        const Draw logic2[] = {{true, true, 110.0f}, {true, false, 105.0f}};
        const Draw record2{false, false, 110.0f};
        assert(step(logic2, 2, &record2, false, listener, true) == 110.0f);
    }
    // a draw with the halfway camera alone never reaches the listener
    assert(!interp::listener_takes_camera(true, false, false));
    // interpolation off, or the first draw of a camera (nothing to blend): every draw
    assert(interp::listener_takes_camera(false, false, false));
    // 40 fps step with a blended record pass (the logic pass is the record pass): the pre-run's exact
    // camera, then the blended record camera, which the engine hears (one listener update a step as
    // the frames, as before)
    {
        float listener = kPlaceholder;
        const Draw logic[] = {{true, true, 100.0f}, {true, false, 75.0f}};
        assert(step(logic, 2, nullptr, true, listener, false) == 75.0f);
        assert(listener != kPlaceholder);
    }
    // 40 fps step whose record pass is exact: the engine hears the exact camera
    {
        float listener = 75.0f;
        const Draw logic[] = {{true, true, 120.0f}, {true, false, 90.0f}};
        const Draw record{false, false, 120.0f};
        assert(step(logic, 2, &record, false, listener, true) == 120.0f);
    }
    std::puts("interp_listener_test: ok");
    return 0;
}
