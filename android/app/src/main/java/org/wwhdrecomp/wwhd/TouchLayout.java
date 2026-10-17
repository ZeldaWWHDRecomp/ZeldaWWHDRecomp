package org.wwhdrecomp.wwhd;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

/** User overrides only: untouched controls retain the existing layout exactly. */
final class TouchLayout {
    static final float MIN_RADIUS = .025f, MAX_RADIUS = .22f;
    static final class Placement {
        final float x, y, radius;
        Placement(float x, float y, float radius) { this.x=x; this.y=y; this.radius=radius; }
    }
    private final Set<String> keys;
    private final Map<String, Placement> overrides = new LinkedHashMap<>();
    TouchLayout(Set<String> keys) { this.keys = Set.copyOf(keys); }
    Placement get(String key) { return overrides.get(key); }
    void reset() { overrides.clear(); }
    boolean put(String key, float x, float y, float radius) {
        if (!keys.contains(key) || !Float.isFinite(x) || !Float.isFinite(y) || !Float.isFinite(radius)) return false;
        overrides.put(key, new Placement(clamp(x,0,1),clamp(y,0,1),clamp(radius,MIN_RADIUS,MAX_RADIUS)));
        return true;
    }
    String encode() {
        StringBuilder out = new StringBuilder("1;");
        for (Map.Entry<String, Placement> e : overrides.entrySet()) {
            Placement p=e.getValue();
            out.append(e.getKey()).append('=').append(p.x).append(',').append(p.y).append(',').append(p.radius).append(';');
        }
        return out.toString();
    }
    void decode(String text) {
        reset();
        if (text==null || text.length()>4096 || !text.startsWith("1;")) return;
        for (String entry : text.substring(2).split(";")) {
            String[] pair=entry.split("=",-1);
            if (pair.length!=2 || !keys.contains(pair[0]) || overrides.containsKey(pair[0])) continue;
            String[] values=pair[1].split(",",-1);
            if (values.length!=3) continue;
            try { put(pair[0],Float.parseFloat(values[0]),Float.parseFloat(values[1]),Float.parseFloat(values[2])); }
            catch (NumberFormatException ignored) { /* Invalid saved entries use their defaults. */ }
        }
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
        return new Placement(clamp(p.x,radius/width,1-radius/width),
                clamp(p.y,(top+radius)/height,1-radius/height),normalized);
    }
    private static float clamp(float value,float low,float high) { return Math.max(low,Math.min(high,value)); }

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
