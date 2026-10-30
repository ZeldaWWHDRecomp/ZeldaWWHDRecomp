// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

import java.util.Map;

public final class WindwakeTouchProfileTest {
    private static void near(float value, float expected) { assert Math.abs(value - expected) < .001f : value; }
    public static void main(String[] args) {
        WindwakeTouchProfile mobile = new WindwakeTouchProfile(Map.of());
        assert !mobile.fixedMovement && mobile.aimPercent == 70;
        near(mobile.swipeDelta(100, false, 200), 100);
        near(mobile.swipeDelta(100, true, 100), 70);
        near(mobile.swipeDelta(100, true, 200) * 2, 70); // native camera multiplier cancels
        near(mobile.swipeDelta(100, true, 50) * .5f, 70);
        near(mobile.swipeDelta(Float.NaN, true, 100), 0);
        assert WindwakeTouchProfile.percent("NaN", 70) == 70;
        assert WindwakeTouchProfile.percent("Infinity", 70) == 70;
        assert WindwakeTouchProfile.percent("-9", 70) == 25;
        assert WindwakeTouchProfile.percent("99", 70) == 300;
        assert mobile.freeRoute(0, true) == 0; // game UI still owns its touches
        assert mobile.freeRoute(4, true) == 4; // choices/dialogues still own theirs
        assert mobile.freeRoute(1, false) == 1;
        assert mobile.freeRoute(1, true) == 3; // either free half aims
        WindwakeTouchProfile fixed = new WindwakeTouchProfile(Map.of("windwakeFixedMove", "1", "windwakeAimSpeed", "1.50"));
        assert fixed.freeRoute(1, false) == 5;
        assert fixed.freeRoute(1, true) == 3;
        near(fixed.swipeDelta(100, true, 100), 150);
        assert !WindwakeTouchProfile.allowPinch(200, 2000, 2772, false); // run + look
        assert !WindwakeTouchProfile.allowPinch(1900, 2200, 2772, true); // precise aiming
        assert WindwakeTouchProfile.allowPinch(1900, 2200, 2772, false);
        ContextPack context = new ContextPack();
        int[] pack = new int[ContextPack.LEN]; pack[0] = ContextPack.VERSION;
        pack[1] = ContextPack.ENABLED | ContextPack.ACTIVE;
        pack[4 + 3 * ContextPack.X] = ContextPack.SHOWN | ContextPack.KIND_ITEM << ContextPack.KIND_SHIFT;
        pack[5 + 3 * ContextPack.X] = 0x20; // telescope, without the item-hold latch
        context.set(pack, ContextPack.VERSION);
        assert context.telescope() && context.gestureAim();
        WindwakeScopeState scope = new WindwakeScopeState();
        scope.update(context); assert scope.active() && scope.itemButton() == ContextPack.X;
        int[] prompt = new int[ContextPack.LEN]; prompt[0] = ContextPack.VERSION;
        prompt[1] = ContextPack.ENABLED | ContextPack.ACTIVE;
        prompt[4 + 3 * ContextPack.A] = ContextPack.SHOWN | ContextPack.KIND_ACTION << ContextPack.KIND_SHIFT;
        context.set(prompt, ContextPack.VERSION); scope.update(context);
        assert scope.active(); // observed telescope -> empty A:00 message transition
        WindwakeScopeState ordinary = new WindwakeScopeState(); ordinary.update(context); assert !ordinary.active();
        prompt[1] |= ContextPack.TAP_CONTINUES; context.set(prompt, ContextPack.VERSION); scope.update(context);
        assert !scope.active(); // a real dialogue immediately takes the touch back
        pack[4 + 3 * ContextPack.PLUS] = ContextPack.SHOWN;
        context.set(pack, ContextPack.VERSION);
        assert !context.gestureAim() && !context.telescope(); // equipped in ordinary play
        pack[1] |= ContextPack.AIM_LATCHED; context.set(pack, ContextPack.VERSION);
        assert context.gestureAim(); // latched bow
        context.clear(); assert !context.gestureAim();
        for (String key : new String[] {TouchLayout.KEY_A, TouchLayout.KEY_B, TouchLayout.KEY_X, TouchLayout.KEY_Y,
                TouchLayout.KEY_PLUS, TouchLayout.KEY_MINUS, TouchLayout.KEY_LOOK, "10:0"}) {
            TouchLayout.Placement p = WindwakeTouchProfile.home(key);
            assert p != null && p.x > 0 && p.x < 1 && p.y > 0 && p.y < 1;
        }
        System.out.println("Phone gestures: independent aim speed, UI ownership, run/look multitouch, layout and input validation PASS");
    }
}
