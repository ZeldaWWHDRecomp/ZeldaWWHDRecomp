// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

public final class InternalInputDevicesTest {
    public static void main(String[] args) {
        assert InternalInputDevices.internalKeyboard("uinput-xiaomi", 1638, 2184, false);
        assert !InternalInputDevices.internalKeyboard("uinput-xiaomi", 1638, 2184, true);
        assert !InternalInputDevices.internalKeyboard("Xbox Wireless Controller", 1118, 2835, false);
        assert !InternalInputDevices.internalKeyboard("uinput-xiaomi", 1234, 2184, false);
        assert !InternalInputDevices.internalKeyboard("uinput-xiaomi", 1638, 1234, false);
        assert !InternalInputDevices.internalKeyboard(null, 1638, 2184, false);
        System.out.println("Poco internal key injector excluded; physical controller identities retained PASS");
    }
}
