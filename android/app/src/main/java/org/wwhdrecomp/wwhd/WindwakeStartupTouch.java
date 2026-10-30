// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

/** One title tap. File selection and the name keyboard keep the real GamePad touch events. */
final class WindwakeStartupTouch {
    private int pointer = -1;
    private float downX, downY;
    private boolean tap;
    boolean tracking() { return pointer >= 0; }
    void down(int id, float x, float y) { pointer = id; downX = x; downY = y; tap = Float.isFinite(x) && Float.isFinite(y); }
    void move(int id, float x, float y, float slop) {
        if (id != pointer || !Float.isFinite(x) || !Float.isFinite(y) || !Float.isFinite(slop) || slop < 0 ||
                (x-downX)*(x-downX) + (y-downY)*(y-downY) > slop*slop) tap = false;
    }
    boolean up(int scene, int id, float x, float y, float slop) {
        move(id, x, y, slop);
        boolean confirm = tracking() && tap && scene == ContextPack.STARTUP_TITLE;
        cancel(); return confirm;
    }
    void cancel() { pointer = -1; tap = false; }
}
