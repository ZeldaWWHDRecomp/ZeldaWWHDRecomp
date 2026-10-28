package org.wwhdrecomp.wwhd;

/** One on-screen stick: where it rests (the layout's place), where its centre is while a thumb holds it,
 *  and its deflection. Fixed sticks (Classic) keep their centre; floating sticks (Flexible) centre under
 *  the thumb where it lands. A following stick (the left one, an option) comes along when the thumb
 *  slides past its edge. Every way a finger ends (up, cancel, focus loss, hiding, a style change) goes
 *  through release(), which puts the centre back and the deflection to zero.
 *  Pure Java: tools/android/TouchStickTest.java.
 *  Idea from the GreenNaugahyde Android fork (MPL-2.0), ControlsView's floating sticks; reimplemented. */
final class TouchStick {
    float homeX, homeY, radius;  // the resting place (pixels)
    float cx, cy;                // the centre now
    float sx, sy;                // deflection -1..1 (y down)
    private boolean held;

    boolean held() { return held; }

    /** The layout placed the stick (or the screen changed): a held stick keeps its centre until released. */
    void home(float x, float y, float r) {
        homeX = x; homeY = y; radius = r;
        if (!held) { cx = x; cy = y; }
    }
    /** A thumb lands at (x, y): floating, the centre comes under it; fixed, the stick deflects towards it. */
    void press(float x, float y, boolean floating) {
        held = true;
        cx = floating ? x : homeX;
        cy = floating ? y : homeY;
        sx = sy = 0;
        move(x, y, false);
    }
    /** The thumb moved; follow: the centre comes along when the thumb is past the edge. */
    void move(float x, float y, boolean follow) {
        if (!held || !(radius > 0)) return;
        float dx = (x - cx) / radius, dy = (y - cy) / radius, len = (float) Math.sqrt(dx * dx + dy * dy);
        if (!Float.isFinite(len)) return;
        if (len > 1f) {
            dx /= len; dy /= len;
            if (follow) {  // the thumb stays on the edge: a short slide back eases off
                cx = x - dx * radius;
                cy = y - dy * radius;
            }
        }
        sx = dx; sy = dy;
    }
    /** The finger is gone (or every input is released): back to rest, no deflection. */
    void release() {
        held = false;
        sx = sy = 0;
        cx = homeX; cy = homeY;
    }
}
