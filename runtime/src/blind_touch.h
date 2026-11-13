// SPDX-License-Identifier: MPL-2.0
// A controller's touchpad (DualShock 4, DualSense) as the GamePad's touch screen, used without looking at it: the
// whole pad is the whole GamePad picture. A finger on the pad shows where it is (a highlight on the GamePad picture);
// clicking the pad down touches the GamePad there, until the click lets go (a tap, or a drag while held). While no
// GamePad picture is in sight, a finger or a click only brings the picture up (picture-in-picture,
// gfx::display_touchpad); it then stays while the pad is in use and for kTouchpadShowSeconds after.
#pragma once

namespace input {

constexpr double kTouchpadShowSeconds = 10;

struct BlindTouch {
    bool clicked = false;    // the pad is clicked down
    bool touching = false;   // that click touches the GamePad (else it only brought the picture up)
    double shown_until = 0;  // the picture this brought up stays until then

    struct Result {
        bool touch = false;      // touch the GamePad now
        bool highlight = false;  // show the finger on the GamePad picture
    };
    // One read: a finger on the pad, the pad clicked, `visible` a GamePad picture in sight without this (the PiP or
    // GamePad mode, a GamePad window).
    Result step(bool finger, bool click, bool visible, double now) {
        const bool shown = now < shown_until, showing = visible || shown;
        if (click && !clicked) touching = showing;
        if (!click) touching = false;
        clicked = click;
        if ((finger || click) && (shown || !showing)) shown_until = now + kTouchpadShowSeconds;
        return {touching, (finger || click) && showing};
    }
};

}  // namespace input
