// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

public final class WindwakeStartupTouchTest {
    public static void main(String[] args) {
        ContextPack pack = new ContextPack();
        int[] raw = new int[ContextPack.LEN]; raw[0] = ContextPack.VERSION;
        pack.set(raw, ContextPack.VERSION);
        assert pack.startupScene() == ContextPack.STARTUP_UNKNOWN && !pack.startupTouch(); // older SDK
        for (int scene = ContextPack.STARTUP_BOOT; scene <= ContextPack.STARTUP_GAME; scene++) {
            raw[3] = ContextPack.STARTUP_MAGIC | scene;
            pack.set(raw, ContextPack.VERSION);
            assert pack.startupScene() == scene;
            assert pack.startupTouch() == (scene != ContextPack.STARTUP_GAME);
        }
        raw[1] = ContextPack.ENABLED; // an in-game menu also has ACTIVE=false
        raw[3] = ContextPack.STARTUP_MAGIC | ContextPack.STARTUP_GAME;
        pack.set(raw, ContextPack.VERSION);
        assert !pack.on() && !pack.startupTouch(); // its gameplay menu pad remains available
        raw[3] = 0x57570202; pack.set(raw, ContextPack.VERSION);
        assert !pack.startupTouch(); // unknown extension, no false startup mode
        WindwakeStartupTouch tap = new WindwakeStartupTouch();
        tap.down(2, 100, 100); assert tap.up(ContextPack.STARTUP_TITLE, 2, 103, 100, 8);
        tap.down(2, 100, 100); tap.move(2, 130, 100, 8);
        assert !tap.up(ContextPack.STARTUP_TITLE, 2, 100, 100, 8); // no drag-to-confirm
        tap.down(2, 100, 100); assert !tap.up(ContextPack.STARTUP_FILES, 2, 100, 100, 8);
        tap.down(2, 100, 100); tap.cancel(); assert !tap.up(ContextPack.STARTUP_TITLE, 2, 100, 100, 8);
        tap.down(2, 100, 100); assert !tap.up(ContextPack.STARTUP_TITLE, 3, 100, 100, 8);
        tap.down(2, Float.NaN, 100); assert !tap.up(ContextPack.STARTUP_TITLE, 2, 100, 100, 8);
        System.out.println("Startup touch: exact native phase, gameplay-menu isolation and cancelled gestures PASS");
    }
}
