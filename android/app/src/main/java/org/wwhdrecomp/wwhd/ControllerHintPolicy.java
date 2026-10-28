package org.wwhdrecomp.wwhd;

import java.util.HashSet;
import java.util.Set;

// A game controller was connected. Native code offers the controller setup on a new controller's first
// press (runtime/src/platform/input_sdl.cpp, overlay/controller_setup.h: Set up / Later / Don't ask again,
// remembered in controls.json); that offer replaced the one-time "hold Select to open Settings" toast and
// names the Settings buttons itself. This policy announces each physical controller (its InputDevice
// descriptor) to native code once per app session, and only real game controllers: a keyboard with
// d-pad keys that SDL also lists as a gamepad is never offered the setup.
final class ControllerHintPolicy {
    private final Set<String> announced = new HashSet<>();
    boolean claim(String descriptor, boolean controller) {
        if (!controller || descriptor == null || descriptor.isEmpty()) return false;
        return announced.add(descriptor);
    }
}
