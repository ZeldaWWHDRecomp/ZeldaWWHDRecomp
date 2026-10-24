#pragma once
#include "overlay.h"

namespace overlay {
// Issue #267: with controller input in the background on, a controller press while no game window has
// the focus plays the game but never opens Settings there (the menu would take the controller from a
// game the player watches on another screen, with no keyboard focus to close it by key). Closing an
// open menu and the other controller shortcuts work as with the focus. With the option off nothing
// changes (the host reads no controllers in the background then).
inline bool controller_may_open_settings(bool window_focused, bool background_controllers) {
    return window_focused || !background_controllers;
}
// Driven by the input pump, not rendered frames. Times are monotonic seconds.
class SettingsShortcut {
    OpenReason reason_ = OpenReason::View;
    double since_ = -1;
    bool latched_ = false, home_ = false;
public:
    OpenReason reason() const { return reason_; }
    enum Action { None, Open, Close };
    Action poll(double now, bool select, bool home, bool open) {
        Action action = None;
        if (home && !home_) { action = open ? Close : Open; reason_ = OpenReason::Home; }
        home_ = home;
        if (!select) { since_ = -1; latched_ = false; }
        else if (!latched_) {
            if (open) { action = Close; latched_ = true; reason_ = OpenReason::View; }
            else {
                if (since_ < 0) since_ = now;
                if (now - since_ >= 0.5) { action = Open; latched_ = true; reason_ = OpenReason::View; }
            }
        }
        return action;
    }
};
}  // namespace overlay
