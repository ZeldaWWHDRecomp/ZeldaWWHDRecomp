package org.wwhdrecomp.wwhd;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Saved touch layouts, one list per style (Classic, Flexible), the built-in Arc layout, and the export
 *  file (JSON, through the Android file picker):
 *  {"format":"wwhd-touch-layouts","version":1,
 *   "current":{"classic":"2;...","flexible":"2;..."},
 *   "layouts":[{"style":"classic","name":"...","layout":"2;..."}, ...]}
 *  Import merges the saved layouts (same style and name: replaced) and returns the file's current
 *  layouts for the caller to apply. Pure Java (its own small JSON reader, no org.json):
 *  tools/android/TouchPresetsTest.java.
 *  Idea from the GreenNaugahyde Android fork (MPL-2.0), LayoutPresets and ControlsView.arcLayout; reimplemented. */
final class TouchPresets {
    static final String CLASSIC = "classic", FLEXIBLE = "flexible", FORMAT = "wwhd-touch-layouts";
    static final int VERSION = 1, MAX_PER_STYLE = 16, MAX_NAME = 40, MAX_FILE = 256 * 1024;

    private final Map<String, LinkedHashMap<String, String>> saved = new LinkedHashMap<>();
    TouchPresets() { saved.put(CLASSIC, new LinkedHashMap<>()); saved.put(FLEXIBLE, new LinkedHashMap<>()); }

    static boolean style(String s) { return CLASSIC.equals(s) || FLEXIBLE.equals(s); }
    static String cleanName(String name) {
        if (name == null) return "";
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < name.length() && b.length() < MAX_NAME; i++) {
            char c = name.charAt(i);
            if (!Character.isISOControl(c)) b.append(c);
        }
        return b.toString().trim();
    }

    List<String> names(String style) {
        Map<String, String> m = saved.get(style);
        return m == null ? Collections.emptyList() : new ArrayList<>(m.keySet());
    }
    String get(String style, String name) {
        Map<String, String> m = saved.get(style);
        return m == null ? null : m.get(name);
    }
    /** Saves (or replaces) a layout; false when the style, name or text is not valid or the list is full. */
    boolean save(String style, String name, String layout) {
        Map<String, String> m = saved.get(style);
        name = cleanName(name);
        if (m == null || name.isEmpty() || !TouchLayout.valid(layout)) return false;
        if (!m.containsKey(name) && m.size() >= MAX_PER_STYLE) return false;
        m.put(name, layout);
        return true;
    }
    boolean remove(String style, String name) {
        Map<String, String> m = saved.get(style);
        return m != null && m.remove(name) != null;
    }

    // ---- the saved list (SharedPreferences) and the export file
    /** The export file, or the saved list when current is null. current: style -> layout text. */
    String toJson(Map<String, String> current) {
        StringBuilder b = new StringBuilder("{\"format\":");
        quote(b, FORMAT).append(",\"version\":").append(VERSION);
        if (current != null) {
            b.append(",\"current\":{");
            boolean first = true;
            for (Map.Entry<String, String> e : current.entrySet()) {
                if (!style(e.getKey()) || !TouchLayout.valid(e.getValue())) continue;
                if (!first) b.append(',');
                first = false;
                quote(b, e.getKey()).append(':');
                quote(b, e.getValue());
            }
            b.append('}');
        }
        b.append(",\"layouts\":[");
        boolean first = true;
        for (Map.Entry<String, LinkedHashMap<String, String>> s : saved.entrySet())
            for (Map.Entry<String, String> e : s.getValue().entrySet()) {
                if (!first) b.append(',');
                first = false;
                b.append("{\"style\":");
                quote(b, s.getKey()).append(",\"name\":");
                quote(b, e.getKey()).append(",\"layout\":");
                quote(b, e.getValue()).append('}');
            }
        return b.append("]}").toString();
    }
    /** Reads a saved list or an export file into this one (merging); fills `current` (style -> layout)
     *  with the file's current layouts when given. Returns how many layouts were taken, or -1 when the
     *  text is not such a file (nothing changes then). */
    int fromJson(String text, Map<String, String> current) {
        if (text == null || text.length() > MAX_FILE) return -1;
        Object root;
        try { root = new Reader(text).document(); } catch (IllegalArgumentException e) { return -1; }
        if (!(root instanceof Map)) return -1;
        Map<?, ?> m = (Map<?, ?>) root;
        if (!FORMAT.equals(m.get("format")) || !(m.get("version") instanceof Double)
                || ((Double) m.get("version")).intValue() > VERSION) return -1;
        int taken = 0;
        if (m.get("layouts") instanceof List)
            for (Object o : (List<?>) m.get("layouts")) {
                if (!(o instanceof Map)) continue;
                Map<?, ?> e = (Map<?, ?>) o;
                if (e.get("style") instanceof String && e.get("name") instanceof String && e.get("layout") instanceof String
                        && save((String) e.get("style"), (String) e.get("name"), (String) e.get("layout"))) taken++;
            }
        if (current != null && m.get("current") instanceof Map)
            for (Map.Entry<?, ?> e : ((Map<?, ?>) m.get("current")).entrySet())
                if (e.getKey() instanceof String && style((String) e.getKey()) && e.getValue() instanceof String
                        && TouchLayout.valid((String) e.getValue())) current.put((String) e.getKey(), (String) e.getValue());
        return taken;
    }

    // ---- the built-in Arc layout
    /** The Arc layout for a w x h view (pixels): A, B, X, Y, R and ZR on two quarter arcs around the
     *  bottom right corner, where the right thumb rests; ZL on the left edge for the other hand; the
     *  d-pad at the top, + and - at the bottom centre; the sticks where Classic needs them (Flexible
     *  ignores their place). Radii: each control's default (`base`, fraction of the short side). In
     *  pixels first, so the arcs stay round on any screen shape. */
    static String arc(int w, int h, Map<String, Float> base) {
        if (w <= 0 || h <= 0) return "2;";
        final float s = Math.min(w, h), u = s / 7f, m = u * 0.6f;
        final float cx = w - m, cy = h - m, r1 = u * 2.9f, r2 = r1 + u * 1.1f;
        StringBuilder b = new StringBuilder("2;");
        put(b, KEYS_INNER[0], polar(cx, cy, r1, 90), w, h, base);
        put(b, KEYS_INNER[1], polar(cx, cy, r1, 112.5f), w, h, base);
        put(b, KEYS_INNER[2], polar(cx, cy, r1, 135), w, h, base);
        put(b, KEYS_INNER[3], polar(cx, cy, r1, 157.5f), w, h, base);
        put(b, KEYS_OUTER[0], polar(cx, cy, r2, 90), w, h, base);
        put(b, KEYS_OUTER[1], polar(cx, cy, r2, 112.5f), w, h, base);
        put(b, KEYS_OUTER[2], polar(cx, cy, r2, 135), w, h, base);
        put(b, TouchLayout.KEY_ZL, new float[] {u * 0.75f, h * 0.5f}, w, h, base);
        // the d-pad right of the top left corner (the view button and the controller button are there)
        final float px = u * 3.2f, py = u * 1.25f, e = u * 0.55f;
        put(b, TouchLayout.KEY_UP, new float[] {px, py - e}, w, h, base);
        put(b, TouchLayout.KEY_DOWN, new float[] {px, py + e}, w, h, base);
        put(b, TouchLayout.KEY_LEFT, new float[] {px - e, py}, w, h, base);
        put(b, TouchLayout.KEY_RIGHT, new float[] {px + e, py}, w, h, base);
        put(b, TouchLayout.KEY_MINUS, new float[] {w * 0.5f - u * 0.8f, h - u * 0.45f}, w, h, base);
        put(b, TouchLayout.KEY_PLUS, new float[] {w * 0.5f + u * 0.8f, h - u * 0.45f}, w, h, base);
        put(b, TouchLayout.KEY_LEFT_STICK, new float[] {u * 2.1f, h - u * 2.1f}, w, h, base);
        put(b, TouchLayout.KEY_RIGHT_STICK, new float[] {cx - r2 - u * 1.6f, h - u * 1.7f}, w, h, base);
        return b.toString();
    }
    // inner arc from the top to the left: R, Y, A, B; outer: ZR, X, L
    static final String[] KEYS_INNER = {TouchLayout.KEY_R, TouchLayout.KEY_Y, TouchLayout.KEY_A, TouchLayout.KEY_B};
    static final String[] KEYS_OUTER = {TouchLayout.KEY_ZR, TouchLayout.KEY_X, TouchLayout.KEY_L};
    private static float[] polar(float cx, float cy, float r, float degrees) {
        double a = Math.toRadians(degrees);
        return new float[] {cx + r * (float) Math.cos(a), cy - r * (float) Math.sin(a)};
    }
    private static void put(StringBuilder b, String key, float[] xy, int w, int h, Map<String, Float> base) {
        Float r = base == null ? null : base.get(key);
        b.append(key).append('=').append(xy[0] / w).append(',').append(xy[1] / h).append(',')
         .append(r == null ? 0.055f : r).append(',').append(TouchLayout.SHAPE_CIRCLE).append(',')
         .append(TouchLayout.OPACITY_DEFAULT).append(",1;");
    }

    // ---- JSON
    private static StringBuilder quote(StringBuilder b, String s) {
        b.append('"');
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '"' || c == '\\') b.append('\\').append(c);
            else if (c < 0x20) b.append(String.format(java.util.Locale.ROOT, "\\u%04x", (int) c));
            else b.append(c);
        }
        return b.append('"');
    }
    /** A small strict JSON reader: objects (Map), arrays (List), strings, numbers (Double), booleans, null. */
    static final class Reader {
        private final String s;
        private int i, depth;
        Reader(String s) { this.s = s; }
        Object document() {
            Object v = value();
            space();
            if (i != s.length()) throw new IllegalArgumentException("trailing text");
            return v;
        }
        private void space() { while (i < s.length() && " \t\r\n".indexOf(s.charAt(i)) >= 0) i++; }
        private char peek() { if (i >= s.length()) throw new IllegalArgumentException("end"); return s.charAt(i); }
        private void expect(char c) { if (peek() != c) throw new IllegalArgumentException("expected " + c); i++; }
        private Object value() {
            space();
            char c = peek();
            if (c == '{' || c == '[') {
                if (++depth > 16) throw new IllegalArgumentException("too deep");
                Object v = c == '{' ? object() : array();
                depth--;
                return v;
            }
            if (c == '"') return string();
            if (s.startsWith("true", i)) { i += 4; return Boolean.TRUE; }
            if (s.startsWith("false", i)) { i += 5; return Boolean.FALSE; }
            if (s.startsWith("null", i)) { i += 4; return null; }
            int start = i;
            while (i < s.length() && "+-0123456789.eE".indexOf(s.charAt(i)) >= 0) i++;
            try { return Double.parseDouble(s.substring(start, i)); }
            catch (NumberFormatException e) { throw new IllegalArgumentException("bad value"); }
        }
        private Map<String, Object> object() {
            Map<String, Object> m = new LinkedHashMap<>();
            expect('{');
            space();
            if (peek() == '}') { i++; return m; }
            while (true) {
                space();
                String k = string();
                space();
                expect(':');
                m.put(k, value());
                space();
                if (peek() == ',') { i++; continue; }
                expect('}');
                return m;
            }
        }
        private List<Object> array() {
            List<Object> l = new ArrayList<>();
            expect('[');
            space();
            if (peek() == ']') { i++; return l; }
            while (true) {
                l.add(value());
                space();
                if (peek() == ',') { i++; continue; }
                expect(']');
                return l;
            }
        }
        private String string() {
            expect('"');
            StringBuilder b = new StringBuilder();
            while (true) {
                char c = peek();
                i++;
                if (c == '"') return b.toString();
                if (c < 0x20) throw new IllegalArgumentException("control character");
                if (c != '\\') { b.append(c); continue; }
                char e = peek();
                i++;
                switch (e) {
                    case '"': case '\\': case '/': b.append(e); break;
                    case 'b': b.append('\b'); break;
                    case 'f': b.append('\f'); break;
                    case 'n': b.append('\n'); break;
                    case 'r': b.append('\r'); break;
                    case 't': b.append('\t'); break;
                    case 'u':
                        if (i + 4 > s.length()) throw new IllegalArgumentException("bad escape");
                        try { b.append((char) Integer.parseInt(s.substring(i, i + 4), 16)); }
                        catch (NumberFormatException x) { throw new IllegalArgumentException("bad escape"); }
                        i += 4;
                        break;
                    default: throw new IllegalArgumentException("bad escape");
                }
            }
        }
    }
}
