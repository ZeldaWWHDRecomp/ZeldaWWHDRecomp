package org.wwhdrecomp.wwhd;

import java.util.Arrays;

// javac -d "$OUT" android/app/src/main/java/org/wwhdrecomp/wwhd/ScreenCover.java \
//   android/app/src/test/java/org/wwhdrecomp/wwhd/ScreenCoverTest.java && java -ea -cp "$OUT" org.wwhdrecomp.wwhd.ScreenCoverTest
public final class ScreenCoverTest {
    static void expect(int[] got, int l, int t, int r, int b) {
        if (!Arrays.equals(got, new int[] {l, t, r, b}))
            throw new AssertionError("expected " + l + "," + t + "," + r + "," + b + " got " + Arrays.toString(got));
    }
    /** -1,-1,-1,-1: null expected */
    static void expectBox(int[] got, int l, int t, int r, int b) {
        int[] want = l == -1 && t == -1 && r == -1 && b == -1 ? null : new int[] {l, t, r, b};
        if (!Arrays.equals(got, want))
            throw new AssertionError("expected " + Arrays.toString(want) + " got " + Arrays.toString(got));
    }
    public static void main(String[] args) {
        final int[] none = {0, 0, 0, 0}, origin = {0, 0}, screen = {0, 0, 1280, 960};
        // nothing reported: nothing covered (the layout as before)
        expect(ScreenCover.covered(1280, 960, none, null, origin, screen), 0, 0, 0, 0);
        expect(ScreenCover.covered(1280, 960, null, new int[0][], origin, null), 0, 0, 0, 0);
        // a shown navigation bar (#440's case) still counts
        expect(ScreenCover.covered(1280, 960, new int[] {0, 0, 0, 48}, null, origin, screen), 0, 0, 0, 48);
        // a cutout strip along the bottom edge (the whole width, 48 rows): covered
        expect(ScreenCover.covered(1280, 960, none, new int[][] {{0, 912, 1280, 960}}, origin, screen), 0, 0, 0, 48);
        // ... along the top, left and right edges
        expect(ScreenCover.covered(1280, 960, none, new int[][] {{0, 0, 1280, 30}}, origin, screen), 0, 30, 0, 0);
        expect(ScreenCover.covered(2400, 1080, none, new int[][] {{0, 0, 90, 1080}, {2380, 0, 2400, 1080}},
                origin, new int[] {0, 0, 2400, 1080}), 90, 0, 20, 0);
        // a notch or a punch hole (a small part of an edge) stays the picture's, as before (#225)
        expect(ScreenCover.covered(2400, 1080, none, new int[][] {{0, 440, 90, 640}}, origin,
                new int[] {0, 0, 2400, 1080}), 0, 0, 0, 0);
        expect(ScreenCover.covered(1280, 960, none, new int[][] {{600, 0, 680, 40}}, origin, screen), 0, 0, 0, 0);
        // a cutout not touching an edge, or empty: ignored
        expect(ScreenCover.covered(1280, 960, none, new int[][] {{0, 400, 1280, 450}, {5, 5, 5, 5}, null}, origin, screen),
                0, 0, 0, 0);
        // the window ends 48 rows before the view does: those rows are not shown
        expect(ScreenCover.covered(1280, 960, none, null, origin, new int[] {0, 0, 1280, 912}), 0, 0, 0, 48);
        // a view placed inside a larger window (side by side, a margin): nothing clipped
        expect(ScreenCover.covered(1000, 700, none, null, new int[] {100, 50}, screen), 0, 0, 0, 0);
        // a view moved past the window's left edge
        expect(ScreenCover.covered(1280, 960, none, null, new int[] {-40, 0}, screen), 40, 0, 0, 0);
        // the sources together: the largest per edge
        expect(ScreenCover.covered(1280, 960, new int[] {0, 24, 0, 30}, new int[][] {{0, 912, 1280, 960}}, origin, screen),
                0, 24, 0, 48);
        // a size not known yet: nothing
        expect(ScreenCover.covered(0, 0, new int[] {0, 0, 0, 48}, null, origin, screen), 0, 0, 0, 0);
        // the window on the display (FLAG_LAYOUT_NO_LIMITS may lay it out past the display's edge)
        expectBox(ScreenCover.onDisplay(screen, screen), 0, 0, 1280, 960);
        expectBox(ScreenCover.onDisplay(new int[] {0, 0, 1280, 1008}, screen), 0, 0, 1280, 960);
        expectBox(ScreenCover.onDisplay(new int[] {0, -48, 1280, 960}, screen), 0, 0, 1280, 960);
        expectBox(ScreenCover.onDisplay(new int[] {100, 50, 900, 650}, screen), 100, 50, 900, 650);  // a smaller window
        expectBox(ScreenCover.onDisplay(null, screen), 0, 0, 1280, 960);
        expectBox(ScreenCover.onDisplay(new int[] {0, 0, 1280, 912}, null), 0, 0, 1280, 912);
        expectBox(ScreenCover.onDisplay(new int[] {0, 0, 0, 0}, new int[] {0, 0, 0, 0}), -1, -1, -1, -1);
        expectBox(ScreenCover.onDisplay(new int[] {2000, 0, 2400, 960}, screen), 2000, 0, 2400, 960);  // no overlap
        // ... and so a view 48 rows taller than the display (laid out past it): those rows are not shown
        expect(ScreenCover.covered(1280, 1008, none, null, origin, ScreenCover.onDisplay(new int[] {0, 0, 1280, 1008}, screen)),
                0, 0, 0, 48);
        System.out.println("Screen cover: shown bars, cutout strips (not notches), window bounds, window on the display PASS");
    }
}
