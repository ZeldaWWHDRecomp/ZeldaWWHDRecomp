package org.wwhdrecomp.wwhd;

/** The "new action" golden rings on a context button (controls plan P3): native code counts each time a
 *  button gains a new action (the pack's golden-ring counter); every change while the button shows plays
 *  the animation once: two gold rings spread out from the button and fade, the second a little later,
 *  and the button pops slightly larger at the start. The first counter seen is only a baseline (no
 *  rings when the controls appear or the game starts).
 *  Pure Java (no Android): tools/android/ContextButtonsTest.java.
 *  Idea from the GreenNaugahyde Android fork (MPL-2.0), ControlsView's highlight of A; reimplemented. */
final class GoldenRings {
    static final long DURATION_MS = 800, RING_MS = 560, STAGGER_MS = DURATION_MS - RING_MS;
    static final int RINGS = 2;
    static final float SPREAD = 0.7f, POP = 0.12f, POP_PART = 0.3f;
    static final int GOLD = 0xFFE9C255;

    private final int[] last;
    private final boolean[] known;
    private final long[] start;

    GoldenRings(int buttons) {
        last = new int[buttons];
        known = new boolean[buttons];
        start = new long[buttons];
        for (int i = 0; i < buttons; i++) start[i] = Long.MIN_VALUE;
    }

    /** No pack (or an unknown one): the next counters seen are baselines again; nothing plays. */
    void forget() {
        for (int i = 0; i < last.length; i++) { known[i] = false; start[i] = Long.MIN_VALUE; }
    }

    /** The pack's counter for button b; shown: the button shows now (the rules apply and it does something).
     *  True when the animation starts. A change while hidden only moves the baseline. */
    boolean note(int b, int counter, boolean shown, long now) {
        if (b < 0 || b >= last.length) return false;
        if (!known[b]) { known[b] = true; last[b] = counter; return false; }
        if (counter == last[b]) return false;
        last[b] = counter;
        if (!shown) return false;
        start[b] = now;
        return true;
    }

    /** Stop button b's animation (it was hidden). */
    void stop(int b) { if (b >= 0 && b < start.length) start[b] = Long.MIN_VALUE; }

    /** 0..1 through button b's animation, or -1 when it is not playing. */
    float progress(int b, long now) {
        if (b < 0 || b >= start.length || start[b] == Long.MIN_VALUE) return -1;
        final long t = now - start[b];
        if (t < 0 || t >= DURATION_MS) return -1;
        return t / (float) DURATION_MS;
    }

    /** Any animation playing (the view redraws every frame while one does). */
    boolean playing(long now) {
        for (int b = 0; b < start.length; b++) if (progress(b, now) >= 0) return true;
        return false;
    }

    /** Ring k's own progress 0..1 at animation progress f, or -1 when it is not drawn. */
    static float ringT(int k, float f) {
        if (f < 0 || k < 0 || k >= RINGS) return -1;
        final float t = (f * DURATION_MS - k * STAGGER_MS) / RING_MS;
        return t < 0 || t >= 1 ? -1 : t;
    }
    /** A ring's radius in button radii. */
    static float radius(float t) { return 1f + SPREAD * t; }
    /** A ring's alpha 0..255: bright at first, fading out quickly. */
    static int alpha(float t) { return t < 0 ? 0 : Math.round(230 * (1 - t) * (1 - t)); }
    /** A ring's stroke width in button radii: thinner as it spreads. */
    static float stroke(float t) { return 0.15f * (1 - 0.6f * t); }
    /** The button's size during the animation: a short pop to 1 + POP and back. */
    static float pop(float f) {
        return f < 0 || f >= POP_PART ? 1f : 1f + POP * (float) Math.sin(Math.PI * f / POP_PART);
    }
}
