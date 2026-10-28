package org.wwhdrecomp.wwhd;

import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

/** TouchPresets: saved layouts per style, the export/import file (round trip, merge, bad files), v1
 *  layouts inside a file, and the Arc layout. */
public final class TouchPresetsTest {
    static void check(boolean ok, String what) { if (!ok) throw new AssertionError(what); }

    public static void main(String[] args) {
        TouchPresets p = new TouchPresets();
        check(p.save(TouchPresets.CLASSIC, "Mine", "2;4:0=0.5,0.5,0.05,1,80,0;"), "save");
        check(p.save(TouchPresets.FLEXIBLE, "Thumbs \"wide\" \\ é\n", "1;4:0=0.4,0.4,0.05;"), "save v1 text, odd name");
        check(!p.save("sideways", "x", "2;"), "unknown style");
        check(!p.save(TouchPresets.CLASSIC, "  ", "2;"), "empty name");
        check(!p.save(TouchPresets.CLASSIC, "bad", "nonsense"), "invalid layout");
        check(p.names(TouchPresets.FLEXIBLE).get(0).equals("Thumbs \"wide\" \\ é"), "control characters dropped, trimmed");
        for (int i = 0; i < 40; i++) p.save(TouchPresets.CLASSIC, "n" + i, "2;");
        check(p.names(TouchPresets.CLASSIC).size() == TouchPresets.MAX_PER_STYLE, "limit");
        check(p.save(TouchPresets.CLASSIC, "Mine", "2;4:1=0.5,0.5,0.05,0,100,1;"), "replace at the limit");
        check(p.get(TouchPresets.CLASSIC, "Mine").startsWith("2;4:1"), "replaced");
        check(p.remove(TouchPresets.CLASSIC, "n3") && !p.remove(TouchPresets.CLASSIC, "n3"), "remove");

        // the export file round trip, with the current layouts
        Map<String, String> current = new LinkedHashMap<>();
        current.put(TouchPresets.CLASSIC, "2;4:0=0.9,0.6,0.06,4,60,0;");
        current.put(TouchPresets.FLEXIBLE, "1;4:0=0.8,0.6,0.06;");
        String file = p.toJson(current);
        TouchPresets q = new TouchPresets();
        Map<String, String> got = new HashMap<>();
        int n = q.fromJson(file, got);
        check(n == p.names(TouchPresets.CLASSIC).size() + p.names(TouchPresets.FLEXIBLE).size(), "all taken: " + n);
        check(q.names(TouchPresets.CLASSIC).equals(p.names(TouchPresets.CLASSIC)), "order kept");
        check(q.toJson(current).equals(file), "round trip");
        check(got.equals(current), "current layouts");
        // the v1 text survives and decodes
        Set<String> keys = new HashSet<>();
        keys.add(TouchLayout.KEY_A);
        TouchLayout l = new TouchLayout(keys);
        l.decode(got.get(TouchPresets.FLEXIBLE));
        check(l.get(TouchLayout.KEY_A) != null && l.get(TouchLayout.KEY_A).x == .8f, "v1 inside a file");
        // the saved list (no current) reads back too
        TouchPresets r = new TouchPresets();
        check(r.fromJson(p.toJson(null), null) > 0 && r.names(TouchPresets.FLEXIBLE).size() == 1, "saved list");

        // merging: same style and name replaced, others added
        TouchPresets m = new TouchPresets();
        m.save(TouchPresets.CLASSIC, "Mine", "2;");
        m.save(TouchPresets.CLASSIC, "Only here", "2;");
        m.fromJson(file, null);
        check(m.get(TouchPresets.CLASSIC, "Mine").startsWith("2;4:1") && m.get(TouchPresets.CLASSIC, "Only here") != null, "merge");

        // bad files change nothing
        TouchPresets e = new TouchPresets();
        e.save(TouchPresets.CLASSIC, "Keep", "2;");
        String[] bad = {null, "", "[]", "{}", "{\"format\":\"other\",\"version\":1}", "{\"format\":\"wwhd-touch-layouts\",\"version\":9}",
                "{\"format\":\"wwhd-touch-layouts\",\"version\":1,\"layouts\":[", "{\"format\":\"wwhd-touch-layouts\",\"version\":1} x",
                "{\"format\":\"wwhd-touch-layouts\",\"version\":1,\"layouts\":[{\"style\":\"classic\",\"name\":\"a\\q\",\"layout\":\"2;\"}]}",
                "[[[[[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]]]"};
        for (String b : bad) check(e.fromJson(b, new HashMap<>()) == -1, "bad file: " + b);
        StringBuilder big = new StringBuilder("{\"format\":\"wwhd-touch-layouts\",\"version\":1,\"x\":\"");
        while (big.length() < TouchPresets.MAX_FILE + 10) big.append("aaaaaaaaaa");
        check(e.fromJson(big.append("\"}").toString(), null) == -1, "too large");
        check(e.names(TouchPresets.CLASSIC).size() == 1, "unchanged after bad files");
        // entries that are not valid are skipped, the rest taken
        String mixed = "{\"format\":\"wwhd-touch-layouts\",\"version\":1,\"current\":{\"classic\":\"junk\",\"tilted\":\"2;\"},\"layouts\":["
                + "{\"style\":\"classic\",\"name\":\"ok\",\"layout\":\"2;\"},{\"style\":\"classic\",\"name\":\"bad\",\"layout\":\"x\"},"
                + "{\"style\":5,\"name\":\"n\",\"layout\":\"2;\"},7,{\"style\":\"flexible\",\"name\":\"\\u0041b\",\"layout\":\"2;\"}]}";
        Map<String, String> cur = new HashMap<>();
        check(e.fromJson(mixed, cur) == 2 && cur.isEmpty() && e.get(TouchPresets.FLEXIBLE, "Ab") != null, "mixed file");

        // the Arc layout: valid, on screen, round on wide and narrow screens, A/B/X/Y/R/ZR near the corner
        Map<String, Float> base = new HashMap<>();
        base.put(TouchLayout.KEY_A, .055f);
        for (int[] wh : new int[][] {{2400, 1080}, {1920, 1200}, {2800, 1260}}) {
            int w = wh[0], h = wh[1];
            String arc = TouchPresets.arc(w, h, base);
            check(TouchLayout.valid(arc), "arc valid");
            Set<String> all = new HashSet<>();
            for (String k : new String[] {TouchLayout.KEY_LEFT_STICK, TouchLayout.KEY_RIGHT_STICK, TouchLayout.KEY_A, TouchLayout.KEY_B,
                    TouchLayout.KEY_X, TouchLayout.KEY_Y, TouchLayout.KEY_UP, TouchLayout.KEY_DOWN, TouchLayout.KEY_LEFT, TouchLayout.KEY_RIGHT,
                    TouchLayout.KEY_ZL, TouchLayout.KEY_L, TouchLayout.KEY_R, TouchLayout.KEY_ZR, TouchLayout.KEY_MINUS, TouchLayout.KEY_PLUS}) all.add(k);
            TouchLayout t = new TouchLayout(all);
            t.decode(arc);
            for (String k : all) {
                TouchLayout.Placement pl = t.get(k);
                check(pl != null && pl.x > 0 && pl.x < 1 && pl.y > 0 && pl.y < 1, "arc places " + k);
            }
            check(Math.abs(t.get(TouchLayout.KEY_A).radius - .055f) < 1e-6, "default radius from base");
            float s = Math.min(w, h), cx = w - s / 7f * .6f, cy = h - s / 7f * .6f;
            double rA = Math.hypot(t.get(TouchLayout.KEY_A).x * w - cx, t.get(TouchLayout.KEY_A).y * h - cy);
            double rB = Math.hypot(t.get(TouchLayout.KEY_B).x * w - cx, t.get(TouchLayout.KEY_B).y * h - cy);
            double rX = Math.hypot(t.get(TouchLayout.KEY_X).x * w - cx, t.get(TouchLayout.KEY_X).y * h - cy);
            check(Math.abs(rA - rB) < 1 && rX > rA + s / 7f, "two round arcs");
            check(t.get(TouchLayout.KEY_A).x > .5f && t.get(TouchLayout.KEY_ZL).x < .2f, "sides");
        }
        check(TouchPresets.arc(0, 100, base).equals("2;"), "no size");
        System.out.println("TouchPresets: saved layouts, export/import round trip and merge, bad files, v1 inside, Arc PASS");
    }
}
