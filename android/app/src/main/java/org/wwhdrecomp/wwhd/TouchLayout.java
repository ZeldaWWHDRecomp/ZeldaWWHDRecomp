package org.wwhdrecomp.wwhd;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

/** User overrides only: untouched controls retain the existing layout exactly.
 *  Format 2 ("2;key=x,y,radius,shape,opacity,filled;...") adds each control's shape, opacity and fill;
 *  format 1 ("1;key=x,y,radius;...", before) still loads, with the default look.
 *  Pure Java (no Android): tools/android/TouchLayoutTest.java checks it.
 *  Per-control look and the mirrored grid: idea from the GreenNaugahyde Android fork (MPL-2.0); reimplemented. */
final class TouchLayout {
    static final float MIN_RADIUS = .025f, MAX_RADIUS = .22f;
    // the editor's Size buttons: 75-130 % of the control's default size, 5 % per press
    static final float SIZE_MIN = .75f, SIZE_MAX = 1.30f, SIZE_STEP = .05f;
    static final int SHAPE_CIRCLE = 0, SHAPE_ROUNDED = 1, SHAPE_SQUARE = 2, SHAPE_DIAMOND = 3, SHAPE_HEXAGON = 4,
            SHAPE_OCTAGON = 5, SHAPES = 6;
    static final int OPACITY_MIN = 20, OPACITY_MAX = 100, OPACITY_STEP = 20, OPACITY_DEFAULT = 100;
    static final int MAX_TEXT = 4096;
    // the controls' keys (TouchControls.key: kind:code)
    static final String KEY_LEFT_STICK = "1:0", KEY_RIGHT_STICK = "1:1",
            KEY_A = "4:0", KEY_B = "4:1", KEY_X = "4:2", KEY_Y = "4:3",
            KEY_UP = "0:11", KEY_DOWN = "0:12", KEY_LEFT = "0:13", KEY_RIGHT = "0:14",
            KEY_ZL = "2:0", KEY_L = "0:9", KEY_R = "0:10", KEY_ZR = "2:1", KEY_MINUS = "0:4", KEY_PLUS = "0:6",
            KEY_LOOK = "0:8";

    static final class Placement {
        final float x, y, radius;
        final int shape, opacity;  // SHAPE_*, percent
        final boolean filled;      // false: outline only
        Placement(float x, float y, float radius) { this(x, y, radius, SHAPE_CIRCLE, OPACITY_DEFAULT, true); }
        Placement(float x, float y, float radius, int shape, int opacity, boolean filled) {
            this.x = x; this.y = y; this.radius = radius; this.shape = shape; this.opacity = opacity; this.filled = filled;
        }
        Placement at(float nx, float ny, float nr) { return new Placement(nx, ny, nr, shape, opacity, filled); }
        Placement look(int nshape, int nopacity, boolean nfilled) { return new Placement(x, y, radius, nshape, nopacity, nfilled); }
    }
    private final Set<String> keys;
    private final Map<String, Placement> overrides = new LinkedHashMap<>();
    TouchLayout(Set<String> keys) { this.keys = Set.copyOf(keys); }
    Placement get(String key) { return overrides.get(key); }
    boolean isEmpty() { return overrides.isEmpty(); }
    void reset() { overrides.clear(); }
    /** Moves or resizes a control, keeping its look. */
    boolean put(String key, float x, float y, float radius) {
        Placement old = overrides.get(key);
        return put(key, old == null ? new Placement(x, y, radius) : old.at(x, y, radius));
    }
    boolean put(String key, Placement p) {
        if (p == null || !keys.contains(key) || !Float.isFinite(p.x) || !Float.isFinite(p.y) || !Float.isFinite(p.radius)) return false;
        overrides.put(key, new Placement(clamp(p.x, 0, 1), clamp(p.y, 0, 1), clamp(p.radius, MIN_RADIUS, MAX_RADIUS),
                p.shape >= 0 && p.shape < SHAPES ? p.shape : SHAPE_CIRCLE,
                Math.max(OPACITY_MIN, Math.min(OPACITY_MAX, p.opacity)), p.filled));
        return true;
    }
    String encode() {
        StringBuilder out = new StringBuilder("2;");
        for (Map.Entry<String, Placement> e : overrides.entrySet()) {
            Placement p = e.getValue();
            out.append(e.getKey()).append('=').append(p.x).append(',').append(p.y).append(',').append(p.radius)
               .append(',').append(p.shape).append(',').append(p.opacity).append(',').append(p.filled ? 1 : 0).append(';');
        }
        return out.toString();
    }
    /** Format 1 or 2; anything else (or too long) is the default layout. Invalid entries use their defaults. */
    void decode(String text) {
        reset();
        if (!valid(text)) return;
        final boolean v2 = text.startsWith("2;");
        for (String entry : text.substring(2).split(";")) {
            String[] pair = entry.split("=", -1);
            if (pair.length != 2 || !keys.contains(pair[0]) || overrides.containsKey(pair[0])) continue;
            String[] values = pair[1].split(",", -1);
            if (values.length != (v2 ? 6 : 3)) continue;
            try {
                float x = Float.parseFloat(values[0]), y = Float.parseFloat(values[1]), r = Float.parseFloat(values[2]);
                if (!v2) { put(pair[0], new Placement(x, y, r)); continue; }
                int shape = Integer.parseInt(values[3]), opacity = Integer.parseInt(values[4]), filled = Integer.parseInt(values[5]);
                put(pair[0], new Placement(x, y, r, shape, opacity, filled != 0));
            } catch (NumberFormatException ignored) { /* Invalid saved entries use their defaults. */ }
        }
    }
    /** A layout text this class reads (format 1 or 2, within the size limit). */
    static boolean valid(String text) {
        return text != null && text.length() <= MAX_TEXT && (text.startsWith("1;") || text.startsWith("2;"));
    }

    /** The editor's Size buttons: `steps` presses (+ larger, - smaller) from `radius`, as 75-130 % of the
     *  control's default radius `base`, on a 5 % grid. A size outside the range (an older layout's) comes
     *  into it with the first press. */
    static float resize(float radius, float base, int steps) {
        if (!(base > 0)) return radius;
        float size = Math.round(radius / base / SIZE_STEP) * SIZE_STEP + steps * SIZE_STEP;
        size = clamp(size, SIZE_MIN, SIZE_MAX);
        return clamp(size * base, MIN_RADIUS, MAX_RADIUS);
    }
    /** The size as the editor shows it (percent of the default). */
    static int sizePercent(float radius, float base) { return base > 0 ? Math.round(radius / base * 100) : 100; }
    /** The editor's opacity button: 100, 80, 60, 40, 20, then 100 again. */
    static int nextOpacity(int opacity) {
        int next = (Math.round(opacity / (float) OPACITY_STEP) - 1) * OPACITY_STEP;
        return next < OPACITY_MIN ? OPACITY_MAX : next;
    }
    /** The mirrored grid: lines every `cell` through `origin` (the screen's centre), so a place on the
     *  left has its mirror image on the right. */
    static float snap(float v, float origin, float cell) {
        if (!(cell > 0)) return v;
        return origin + Math.round((v - origin) / cell) * cell;
    }

    static Placement fit(Placement p, int width, int height) {
        return fit(p,width,height,0);
    }
    static Placement fit(Placement p, int width, int height, int topInset) {
        if (width<=0 || height<=0) return p;
        int top=Math.max(0,Math.min(height-1,topInset));
        float shortSide=Math.min(width,height);
        // The editor toolbar may leave less room than a control's saved size.
        // Fit its preview below the toolbar; persistent radii still use put's bounds.
        float normalized=Math.min(clamp(p.radius,MIN_RADIUS,MAX_RADIUS),(height-top)/(2f*shortSide));
        float radius=normalized*shortSide;
        return p.at(clamp(p.x,radius/width,1-radius/width),
                clamp(p.y,(top+radius)/height,1-radius/height),normalized);
    }
    static float clamp(float value,float low,float high) { return Math.max(low,Math.min(high,value)); }

    /** Every edit boundary releases pad/forwarded touches, including a still-held gesture. */
    static final class EditSession {
        private boolean editing, discardGesture;
        private int pointers;
        boolean editing() { return editing; }
        boolean discarding() { return discardGesture; }
        void pointers(int count) { pointers=Math.max(0,count); if (pointers==0) discardGesture=false; }
        void setEditing(boolean on,Runnable releaseInputs) {
            if (editing==on) return;
            editing=on;
            discardGesture=pointers>0;
            releaseInputs.run();
        }
    }
}
