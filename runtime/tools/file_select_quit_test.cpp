#include "file_select_quit.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static void view_routing() {
    using namespace file_select_quit;
    // Normal and HiDPI logical canvases have the same normalized hit region.
    for (auto view : {View{1920, 1080, {0, 0, 1920, 1080}}, View{960, 540, {0, 0, 960, 540}}}) {
        assert(view.contains(.41f, .85f));
        assert(view.contains(.59f, .97f));
        assert(!view.contains(.4f, .91f));
        assert(!view.contains(.5f, .99f));
    }
    // A 4:3 window has bars above and below the TV. The old fixed-720
    // hit region missed the visible button and captured clicks in the lower bar.
    const View letterboxed{1280, 960, {0, 120, 1280, 720}};
    assert(letterboxed.contains(.41f, .76f));
    assert(letterboxed.contains(.59f, .85f));
    assert(!letterboxed.contains(.5f, .91f));
    // An ultrawide window must not capture input beside the narrower TV plate.
    const View ultrawide{2560, 1080, {320, 0, 1920, 1080}};
    assert(ultrawide.contains(.45f, .91f));
    assert(!ultrawide.contains(.41f, .91f));
    assert(!ultrawide.contains(.59f, .91f));
    // Android portrait and split-screen TV rectangles can occupy only part
    // of the host window; the lower GamePad/touch area remains outside Quit.
    const View portrait{1080, 1920, {0, 0, 1080, 607.5f}};
    assert(portrait.contains(.5f, .2875f));
    assert(!portrait.contains(.5f, .91f));
    const View split{1920, 1080, {0, 270, 960, 540}};
    assert(split.contains(.25f, .70f));
    assert(!split.contains(.75f, .70f));
    assert((!View{1280, 720, {0, 0, 0, 0}}.contains(.5f, .91f)));
}

static void dialog_routing() {
    using namespace file_select_quit;
    Routing routing;
    // With no Quit press, the real routing policy preserves every other field.
    input::PadState pad{input::kA | input::kB | input::kY, .25f, -.5f, .75f, 1, true, .4f, .6f};
    const auto original = pad;
    auto event = routing.route(pad, true, true, false);
    assert(!event.open && !event.dismiss && event.pressed == 0);
    assert(pad.buttons == original.buttons && pad.lx == original.lx && pad.ly == original.ly && pad.rx == original.rx &&
           pad.ry == original.ry && pad.touch == original.touch && pad.tx == original.tx && pad.ty == original.ty);
    pad = {};
    routing.route(pad, true, true, false);
    pad.buttons = input::kX;
    event = routing.route(pad, true, true, false);
    assert(event.open && pad.buttons == 0);
    pad.buttons = input::kB;
    event = routing.route(pad, true, true, true);
    assert(event.pressed == input::kB && pad.buttons == 0);
    // B that cancelled the dialog must not also go Back in the native menu.
    pad.buttons = input::kB;
    routing.route(pad, true, true, false);
    assert(pad.buttons == 0);
    // A hidden or disabled UI cannot leave that release gate capturing input.
    for (bool shown : {false, true}) {
        pad = original;
        pad.buttons |= input::kX;
        const auto before = pad;
        event = routing.route(pad, false, shown, true);
        assert(event.dismiss && !event.open && event.pressed == 0);
        assert(pad.buttons == before.buttons && pad.lx == before.lx && pad.touch == before.touch);
    }
    // A stale presentation must pass inputs unchanged without dismissing an
    // otherwise eligible confirmation (for example while saving a large PNG).
    pad = original;
    event = routing.route(pad, true, false, true);
    assert(!event.dismiss && !event.open && event.pressed == 0 && pad.buttons == original.buttons);
    pad = original;
    event = routing.route(pad, false, true, true);
    assert(event.dismiss && pad.buttons == original.buttons);
}

int main() {
    view_routing();
    dialog_routing();
    using namespace file_select_quit;
    for (bool setting : {false, true})
        for (bool idle : {false, true}) {
            Routing routing;
            input::PadState pad{
                input::kA | input::kB | input::kX | input::kY | input::kPlus, .25f, -.5f, .75f, 1, true, .4f, .6f};
            const auto original = pad;
            const bool visible = setting && idle;
            assert(routing.filter(pad, visible) == visible);
            auto expected = original;
            if (visible)
                expected.buttons &= ~input::kX;
            assert(pad.buttons == expected.buttons && pad.lx == expected.lx && pad.ly == expected.ly &&
                   pad.rx == expected.rx && pad.ry == expected.ry && pad.touch == expected.touch &&
                   pad.tx == expected.tx && pad.ty == expected.ty);
            pad = original;
            assert(!routing.filter(pad, visible));
            pad = {};
            assert(!routing.filter(pad, visible));
            pad = original;
            assert(routing.filter(pad, visible) == visible);
        }
    // A button held before the UI appears does not activate Quit.
    Routing routing;
    input::PadState pad{input::kX};
    assert(!routing.filter(pad, false));
    assert(pad.buttons == input::kX);
    assert(!routing.filter(pad, true));
    assert(pad.buttons == 0);
    // Turning the option off immediately restores every input, including X.
    pad.buttons = input::kX;
    assert(!routing.filter(pad, false));
    assert(pad.buttons == input::kX);
    puts("file_select_quit_test: passed");
}
