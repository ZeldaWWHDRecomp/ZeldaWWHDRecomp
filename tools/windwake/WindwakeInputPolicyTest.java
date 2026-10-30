// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

public final class WindwakeInputPolicyTest {
    public static void main(String[] args) {
        assert WindwakeInputPolicy.internalKeyboard("uinput-xiaomi", 1638, 2184, false);
        assert !WindwakeInputPolicy.internalKeyboard("uinput-xiaomi", 1638, 2184, true);
        assert !WindwakeInputPolicy.internalKeyboard("Xbox Wireless Controller", 1118, 2835, false);
        assert !WindwakeInputPolicy.internalKeyboard("uinput-xiaomi", 1234, 2184, false);
        assert !WindwakeInputPolicy.internalKeyboard("uinput-xiaomi", 1638, 1234, false);
        assert !WindwakeInputPolicy.internalKeyboard(null, 1638, 2184, false);
        System.out.println("Poco internal key injector excluded; physical controller identities retained PASS");
    }
}
