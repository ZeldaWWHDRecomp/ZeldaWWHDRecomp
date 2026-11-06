// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

/** Device-specific input quirks shared by touch visibility and SDL controller discovery. */
public final class ControllerInputPolicy {
    private ControllerInputPolicy() { }

    public static boolean internalKeyboard(String name, int vendor, int product, boolean joystick) {
        // Observed on the Poco M8 Pro: Xiaomi's internal key injector advertises GAMEPAD,
        // but has no joystick. Treating it as a controller hides every touch control.
        return !joystick && vendor == 0x0666 && product == 0x0888 && "uinput-xiaomi".equals(name);
    }
}
