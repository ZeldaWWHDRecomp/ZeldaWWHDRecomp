package org.wwhdrecomp.wwhd;

import java.util.HashSet;
import java.util.Set;

/** TouchLayout format 2 (per-control shape, opacity, fill), format 1 migration, the editor's size range,
 *  opacity steps and mirrored grid. */
public final class TouchLayoutTest {
    static void check(boolean ok, String what) { if (!ok) throw new AssertionError(what); }
    static boolean near(float a, float b) { return Math.abs(a - b) < 1e-5f; }

    public static void main(String[] args) {
        Set<String> keys = new HashSet<>();
        for (String k : new String[] {TouchLayout.KEY_A, TouchLayout.KEY_B, TouchLayout.KEY_LEFT_STICK, TouchLayout.KEY_ZR}) keys.add(k);

        // format 2 round trip, with every look
        TouchLayout a = new TouchLayout(keys);
        check(a.put(TouchLayout.KEY_A, new TouchLayout.Placement(.9f, .6f, .06f, TouchLayout.SHAPE_HEXAGON, 60, false)), "put A");
        check(a.put(TouchLayout.KEY_B, .8f, .7f, .05f), "put B");
        check(!a.put("9:9", .5f, .5f, .05f), "unknown key");
        check(!a.put(TouchLayout.KEY_ZR, Float.NaN, .5f, .05f), "NaN");
        String text = a.encode();
        check(text.startsWith("2;"), "format 2: " + text);
        TouchLayout b = new TouchLayout(keys);
        b.decode(text);
        check(b.encode().equals(text), "round trip: " + b.encode());
        TouchLayout.Placement pa = b.get(TouchLayout.KEY_A);
        check(near(pa.x, .9f) && near(pa.y, .6f) && near(pa.radius, .06f), "A place");
        check(pa.shape == TouchLayout.SHAPE_HEXAGON && pa.opacity == 60 && !pa.filled, "A look");
        TouchLayout.Placement pb = b.get(TouchLayout.KEY_B);
        check(pb.shape == TouchLayout.SHAPE_CIRCLE && pb.opacity == 100 && pb.filled, "B default look");
        check(b.get(TouchLayout.KEY_ZR) == null, "untouched control stays default");
        // moving keeps the look
        b.put(TouchLayout.KEY_A, .5f, .5f, .07f);
        check(b.get(TouchLayout.KEY_A).shape == TouchLayout.SHAPE_HEXAGON && b.get(TouchLayout.KEY_A).opacity == 60, "move keeps look");

        // format 1 (saved by older versions) still loads: place and size kept exactly, default look
        TouchLayout old = new TouchLayout(keys);
        old.decode("1;4:0=0.91,0.62,0.11;1:0=0.2,0.7,0.2;");
        TouchLayout.Placement oa = old.get(TouchLayout.KEY_A);
        check(oa != null && near(oa.x, .91f) && near(oa.y, .62f) && near(oa.radius, .11f), "v1 A");
        check(oa.shape == TouchLayout.SHAPE_CIRCLE && oa.opacity == 100 && oa.filled, "v1 default look");
        check(near(old.get(TouchLayout.KEY_LEFT_STICK).radius, .2f), "v1 radius beyond 130 % kept (no silent change)");
        check(old.encode().startsWith("2;4:0=0.91,0.62,0.11,0,100,1;"), "v1 saves as v2: " + old.encode());
        TouchLayout again = new TouchLayout(keys);
        again.decode(old.encode());
        check(again.encode().equals(old.encode()), "v1 -> v2 -> v2 stable");

        // invalid input: other formats, wrong field counts, bad numbers, duplicates, too long
        TouchLayout bad = new TouchLayout(keys);
        bad.decode("3;4:0=0.5,0.5,0.05,0,100,1;");
        check(bad.isEmpty(), "unknown format");
        bad.decode(null);
        check(bad.isEmpty(), "null");
        bad.decode("2;4:0=0.5,0.5,0.05;4:1=x,0.5,0.05,0,100,1;1:0=0.1,0.1,0.1,0,100,1;1:0=0.9,0.9,0.1,0,100,1;");
        check(bad.get(TouchLayout.KEY_A) == null && bad.get(TouchLayout.KEY_B) == null, "v2 needs 6 numbers");
        check(near(bad.get(TouchLayout.KEY_LEFT_STICK).x, .1f), "first duplicate wins");
        bad.decode("1;4:0=0.5,0.5,0.05,0,100,1;");
        check(bad.get(TouchLayout.KEY_A) == null, "v1 needs 3 numbers");
        StringBuilder huge = new StringBuilder("2;");
        while (huge.length() <= TouchLayout.MAX_TEXT) huge.append("4:0=0.5,0.5,0.05,0,100,1;");
        bad.decode(huge.toString());
        check(bad.isEmpty(), "too long");
        // out-of-range values clamp; unknown shapes fall back to the circle
        bad.decode("2;4:0=2,-1,9,77,500,1;4:1=0.5,0.5,0.001,-2,0,0;");
        TouchLayout.Placement c = bad.get(TouchLayout.KEY_A);
        check(c.x == 1 && c.y == 0 && c.radius == TouchLayout.MAX_RADIUS && c.shape == 0 && c.opacity == 100, "clamped A");
        TouchLayout.Placement d = bad.get(TouchLayout.KEY_B);
        check(d.radius == TouchLayout.MIN_RADIUS && d.shape == 0 && d.opacity == TouchLayout.OPACITY_MIN && !d.filled, "clamped B");

        // the Size buttons: 75-130 % of the default, 5 % steps
        float base = .055f;
        check(TouchLayout.sizePercent(TouchLayout.resize(base, base, 1), base) == 105, "+1");
        check(TouchLayout.sizePercent(TouchLayout.resize(base, base, -1), base) == 95, "-1");
        float r = base;
        for (int i = 0; i < 20; i++) r = TouchLayout.resize(r, base, 1);
        check(TouchLayout.sizePercent(r, base) == 130, "max 130 %: " + TouchLayout.sizePercent(r, base));
        for (int i = 0; i < 20; i++) r = TouchLayout.resize(r, base, -1);
        check(TouchLayout.sizePercent(r, base) == 75, "min 75 %");
        check(TouchLayout.sizePercent(TouchLayout.resize(base * 2, base, -1), base) == 130, "an old 200 % comes into range");
        check(TouchLayout.sizePercent(TouchLayout.resize(base * 1.07f, base, 1), base) == 110, "snaps to the 5 % grid");
        // opacity: 100 80 60 40 20 100
        int o = 100;
        int[] want = {80, 60, 40, 20, 100, 80};
        for (int w : want) { o = TouchLayout.nextOpacity(o); check(o == w, "opacity " + o); }
        check(TouchLayout.nextOpacity(55) == 40, "opacity from an odd value");
        // mirrored grid: through the centre, a place and its mirror snap symmetrically
        float W = 2400, cell = 1080 / 14f;
        float left = TouchLayout.snap(300, W / 2, cell), right = TouchLayout.snap(W - 300, W / 2, cell);
        check(near(left + right, W), "mirror: " + left + " + " + right);
        check(TouchLayout.snap(W / 2 + 3, W / 2, cell) == W / 2, "centre line");
        check(TouchLayout.snap(123, W / 2, 0) == 123, "no grid");
        // fit keeps the look
        TouchLayout.Placement f = TouchLayout.fit(new TouchLayout.Placement(1, 1, .1f, TouchLayout.SHAPE_DIAMOND, 40, false), 2000, 1000);
        check(f.shape == TouchLayout.SHAPE_DIAMOND && f.opacity == 40 && !f.filled && f.x < 1 && f.y < 1, "fit");
        System.out.println("TouchLayout: v2 round trip, v1 migration, invalid input, size 75-130 %, opacity, mirrored grid PASS");
    }
}
