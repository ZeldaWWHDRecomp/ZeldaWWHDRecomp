package org.wwhdrecomp.wwhd;

import java.util.Map;

/** The context buttons as native code decides them (controls plan P3): TouchControls polls
 *  nativeContextButtons into an int[LEN] and this class reads it. The layout is
 *  runtime/src/mods/context_touch.h (the source of truth); the rules are context_buttons.h. Java only
 *  draws what the pack says (which controls show, with the game's word or item icon, which to draw held,
 *  when to play the golden rings) and routes fingers as before; items by tap, aim-only buttons, the auto
 *  put-away and the ZL latch are done natively on the pad input.
 *  Pure Java (no Android): tools/android/ContextButtonsTest.java.
 *  Idea from the GreenNaugahyde Android fork (MPL-2.0), ControlsView's context buttons; reimplemented. */
final class ContextPack {
    static final int VERSION = 1, LEN = 40;
    // header flags (raw[1])
    static final int ENABLED = 1, ACTIVE = 1 << 1, TAP_CONTINUES = 1 << 2, AIM_LATCHED = 1 << 3,
            ZL_LATCHED = 1 << 4, CLUSTER_HIDDEN = 1 << 5,
            // requested from native code (P3-REQUESTS.md): the HUD names the item buttons A1 A2 A3
            ITEM_NAMES_A = 1 << 6,
            // Link in the button pictures (controls plan P8, link_poses.h): an action with a pose picture
            // (ContextAssets.poseFile) shows Link in that pose above its word
            LINK_POSES = 1 << 7;
    // per-button state
    static final int SHOWN = 1, KIND_SHIFT = 1, KIND_MASK = 3 << 1, HELD = 1 << 3;
    static final int KIND_PLAIN = 0, KIND_ACTION = 1, KIND_ITEM = 2, KIND_FAST_FORWARD = 3;
    // ctxbtn::Button
    static final int A = 0, B = 1, X = 2, Y = 3, R = 4, ZR = 5, ZL = 6, PLUS = 7, R3 = 8, UP = 9, LEFT_RIGHT = 10,
            COUNT = 11;
    /** buttonFor: a control the context rules leave alone (−, the sticks, Jump / Moon, the show/hide button) */
    static final int UNCHANGED = -1;
    /** buttonFor: a control that is hidden while the rules apply (L, D-pad down) */
    static final int HIDDEN = -2;
    private static final int FIRST = 4;

    private final int[] raw = new int[LEN];
    private boolean valid;

    /** The pack as nativeContextButtons returned it (version: its return value). An unknown version or a
     *  short array is no pack: every control shows as without context buttons. True when anything changed. */
    boolean set(int[] v, int version) {
        final boolean ok = v != null && v.length >= LEN && version == VERSION && v[0] == VERSION;
        boolean changed = ok != valid;
        valid = ok;
        for (int i = 0; i < LEN; i++) {
            final int n = ok ? v[i] : 0;
            if (raw[i] != n) { raw[i] = n; changed = true; }
        }
        return changed;
    }
    void clear() { set(null, 0); }

    boolean valid() { return valid; }
    int flags() { return raw[1]; }
    boolean enabled() { return valid && (raw[1] & ENABLED) != 0; }
    /** the rules apply now: only the controls that do something show */
    boolean on() { return enabled() && (raw[1] & ACTIVE) != 0; }
    int assetsGeneration() { return raw[2]; }
    boolean itemNamesA() { return valid && (raw[1] & ITEM_NAMES_A) != 0; }
    boolean linkPoses() { return valid && (raw[1] & LINK_POSES) != 0; }

    private static boolean inRange(int b) { return b >= 0 && b < COUNT; }
    int state(int b) { return inRange(b) ? raw[FIRST + 3 * b] : 0; }
    boolean shown(int b) { return (state(b) & SHOWN) != 0; }
    int kind(int b) { return (state(b) & KIND_MASK) >> KIND_SHIFT; }
    int label(int b) { return inRange(b) ? raw[FIRST + 3 * b + 1] : 0; }
    int ring(int b) { return inRange(b) ? raw[FIRST + 3 * b + 2] : 0; }

    /** The context button a control is (its TouchControls key, TouchLayout.KEY_*), UNCHANGED or HIDDEN. */
    static int buttonFor(String key) {
        if (key == null) return UNCHANGED;
        switch (key) {
            case TouchLayout.KEY_A: return A;
            case TouchLayout.KEY_B: return B;
            case TouchLayout.KEY_X: return X;
            case TouchLayout.KEY_Y: return Y;
            case TouchLayout.KEY_R: return R;
            case TouchLayout.KEY_ZR: return ZR;
            case TouchLayout.KEY_ZL: return ZL;
            case TouchLayout.KEY_PLUS: return PLUS;
            case TouchLayout.KEY_UP: return UP;
            case TouchLayout.KEY_LEFT: case TouchLayout.KEY_RIGHT: return LEFT_RIGHT;
            case TouchLayout.KEY_L: case TouchLayout.KEY_DOWN: return HIDDEN;
            default: return UNCHANGED;
        }
    }

    /** Whether the control shows now (without context buttons, or while they do not apply: every one). */
    boolean visible(String key) {
        if (!on()) return true;
        final int b = buttonFor(key);
        return b == UNCHANGED || (b != HIDDEN && shown(b));
    }
    /** Natively held (a latched item, the ZL latch): drawn pressed although no finger is on it. */
    boolean held(String key) {
        if (!enabled()) return false;
        final int b = buttonFor(key);
        if (b == ZL && (raw[1] & ZL_LATCHED) != 0) return true;
        return b >= 0 && (state(b) & HELD) != 0;
    }
    /** The look the control has now: KIND_* (plain: its letter, as without context buttons). */
    int kindFor(String key) {
        final int b = buttonFor(key);
        return on() && b >= 0 && shown(b) ? kind(b) : KIND_PLAIN;
    }
    /** kindFor's label: the action code or item number. */
    int labelFor(String key) {
        final int b = buttonFor(key);
        return b >= 0 ? label(b) : 0;
    }

    /** The word an action button shows: the game's (words: actions.txt), else built-in English, else null
     *  (then the button's letter). */
    static String word(int code, Map<Integer, String> words) {
        if (words != null) {
            final String w = words.get(code);
            if (w != null && !w.isEmpty()) return w;
        }
        return english(code);
    }

    /** Built-in English words for the actions (dActionStatus_e, play_context.h), used until the game's own
     *  words are exported or when the export failed. null: no word (the letter shows). */
    static String english(int code) {
        switch (code) {
            case 0x01: return "Look";
            case 0x02: return "Speak";
            case 0x03: return "Charts";
            case 0x04: return "Lift";
            case 0x05: return "Climb";
            case 0x06: case 0x16: return "Let Go";
            case 0x07: return "Return";
            case 0x08: return "Put Away";
            case 0x09: return "Drop";
            case 0x0A: return "Check";
            case 0x0B: return "Open";
            case 0x0C: return "Attack";
            case 0x0E: return "Throw";
            case 0x0F: return "Crouch";
            case 0x10: return "Sidle";
            case 0x11: return "Grab";
            case 0x12: return "Jump";
            case 0x13: return "Stop";
            case 0x17: return "Choose";
            case 0x19: return "Next";
            case 0x1B: return "Pick Up";
            case 0x1C: return "Get In";
            case 0x1D: return "Get Out";
            case 0x20: return "Snap";
            case 0x21: return "Info";
            case 0x22: return "Mode";
            case 0x23: return "Fly";
            case 0x24: return "Call";
            case 0x25: return "Bid";
            case 0x26: return "Read";
            case 0x27: return "Cancel";
            case 0x2C: return "Cruise";
            case 0x2F: return "Swing";
            case 0x30: return "Sea Chart";
            case 0x35: return "Sword";
            case 0x36: return "Defend";
            case 0x37: return "Swing";
            case 0x48: return "Album";
            case 0x49: return "Aim";
            default: return null;
        }
    }

    /** The button's own name as drawn: X / Y / R are A1 / A2 / A3 when the HUD names them so. */
    static String letter(int b, String fallback, boolean itemNamesA) {
        if (itemNamesA) {
            if (b == X) return "A1";
            if (b == Y) return "A2";
            if (b == R) return "A3";
        }
        return fallback;
    }

    /** The text size that fits `text` (its width at size 1: `unitWidth`) into a button of radius r: as large
     *  as a letter would be (0.9 r) but at most `maxWidth` wide, and never below `minSize`. */
    static float fitTextSize(float unitWidth, float r, float maxWidth, float minSize) {
        float size = r * 0.9f;
        if (unitWidth > 0 && unitWidth * size > maxWidth) size = maxWidth / unitWidth;
        return Math.max(minSize, size);
    }
}
