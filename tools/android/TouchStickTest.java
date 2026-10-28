package org.wwhdrecomp.wwhd;

/** TouchStick: fixed and floating centres, the following left stick, and a clean reset when the finger
 *  goes (the virtual pad must never keep a deflection or a moved centre). */
public final class TouchStickTest {
    static void check(boolean ok, String what) { if (!ok) throw new AssertionError(what); }
    static boolean near(float a, float b) { return Math.abs(a - b) < 1e-4f; }

    public static void main(String[] args) {
        TouchStick s = new TouchStick();
        s.home(100, 500, 50);
        check(!s.held() && s.cx == 100 && s.cy == 500 && s.sx == 0 && s.sy == 0, "rest");
        // fixed (Classic): a press off-centre deflects at once, the centre stays
        s.press(125, 500, false);
        check(s.held() && s.cx == 100 && near(s.sx, .5f) && s.sy == 0, "fixed press");
        s.move(100, 400, false);  // past the edge, no follow: clamped to the rim
        check(s.cx == 100 && near(s.sx, 0) && near(s.sy, -1), "clamped");
        s.release();
        check(!s.held() && s.sx == 0 && s.sy == 0 && s.cx == 100 && s.cy == 500, "released");
        // floating (Flexible): the centre comes under the thumb, no deflection at the landing
        s.press(700, 300, true);
        check(s.cx == 700 && s.cy == 300 && s.sx == 0 && s.sy == 0, "floating press");
        s.move(730, 340, false);
        check(near(s.sx, .6f) && near(s.sy, .8f), "floating move");
        // following: the thumb goes far right, the centre comes along, the thumb stays on the rim
        s.move(900, 300, true);
        check(near(s.sx, 1) && near(s.sy, 0) && near(s.cx, 850) && near(s.cy, 300), "follow");
        s.move(860, 300, true);  // a short slide back eases off
        check(near(s.sx, .2f), "after follow: " + s.sx);
        // a layout change while held keeps the centre; release then goes to the new home
        s.home(120, 520, 60);
        check(near(s.cx, 850), "held keeps centre on home()");
        s.release();
        check(s.cx == 120 && s.cy == 520 && s.radius == 60 && s.sx == 0 && s.sy == 0, "reset to the new home");
        // finger loss without a press (cancel twice, release of an idle stick) is harmless
        s.release();
        s.move(0, 0, true);
        check(!s.held() && s.sx == 0 && s.cx == 120, "idle move ignored");
        // a zero radius (before the first layout) or NaN input never makes a NaN deflection
        TouchStick z = new TouchStick();
        z.press(10, 10, true);
        z.move(20, 20, true);
        check(z.sx == 0 && z.sy == 0, "zero radius");
        s.press(120, 520, true);
        s.move(Float.NaN, 0, true);
        check(!Float.isNaN(s.sx) && !Float.isNaN(s.cx), "NaN input");
        System.out.println("TouchStick: fixed, floating, follow, reset on finger loss PASS");
    }
}
