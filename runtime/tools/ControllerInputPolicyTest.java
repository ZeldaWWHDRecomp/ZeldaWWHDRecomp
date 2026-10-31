// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

public final class ControllerInputPolicyTest {
    public static void main(String[] args) {
        assert ControllerInputPolicy.internalKeyboard("uinput-xiaomi", 1638, 2184, false);
        assert !ControllerInputPolicy.internalKeyboard("uinput-xiaomi", 1638, 2184, true);
        assert !ControllerInputPolicy.internalKeyboard("Xbox Wireless Controller", 1118, 2835, false);
        assert !ControllerInputPolicy.internalKeyboard("uinput-xiaomi", 1234, 2184, false);
        assert !ControllerInputPolicy.internalKeyboard("uinput-xiaomi", 1638, 1234, false);
        assert !ControllerInputPolicy.internalKeyboard(null, 1638, 2184, false);
        System.out.println("Poco internal key injector excluded; physical controller identities retained PASS");
    }
}
