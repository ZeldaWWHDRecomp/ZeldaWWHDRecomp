package org.wwhdrecomp.wwhd;

/**
 * The parts of the game's view the system does not show (issue #349), in the view's pixels. Pure, so the host
 * test (ScreenCoverTest) covers it without Android.
 *
 * Issue #349 (Retroid Pocket Nova, Android 13, 1280x960): the bottom 48 rows of the screen stay black over the
 * game although the view and the swapchain span all 960 rows and the system bars report as hidden. The game's
 * own picture is right there (a 4:3 present dump on the Mac has the rupee digits at rows 911-927, exactly where
 * the device cuts), so only the platform can say which part is not shown. Besides the system bars actually
 * shown (#440), two more reports mean "not shown":
 *  - a display cutout that is a strip along a whole edge (it spans at least 90% of that edge): nothing can be
 *    seen there. Notches and punch holes (a small part of an edge) are left to the picture, as before;
 *  - the window's bounds ending before the view does: the view is laid out past the area the window gets.
 */
public final class ScreenCover {
    private ScreenCover() {}

    /** A cutout covering at least this share of an edge is a strip along it, not a notch. */
    static final float STRIP = 0.9f;

    /**
     * Left, top, right, bottom covered.
     * @param w view width
     * @param h view height
     * @param bars system bars shown over the view (left, top, right, bottom), already in view pixels
     * @param cutouts the display cutout's bounding rects (left, top, right, bottom each) in view coordinates; may be null
     * @param viewOnScreen the view's top-left corner on the screen (x, y)
     * @param window the window's bounds on the screen (left, top, right, bottom); null when unknown
     */
    public static int[] covered(int w, int h, int[] bars, int[][] cutouts, int[] viewOnScreen, int[] window) {
        int[] c = new int[4];
        if (w <= 0 || h <= 0) return c;
        if (bars != null)
            for (int i = 0; i < 4; i++) c[i] = Math.max(c[i], Math.max(0, bars[i]));
        if (cutouts != null)
            for (int[] r : cutouts) {
                if (r == null || r.length < 4 || r[2] <= r[0] || r[3] <= r[1]) continue;
                int rw = Math.min(r[2], w) - Math.max(r[0], 0), rh = Math.min(r[3], h) - Math.max(r[1], 0);
                if (rw <= 0 || rh <= 0) continue;
                boolean across = rw >= STRIP * w, along = rh >= STRIP * h;
                if (across && r[1] <= 0) c[1] = Math.max(c[1], r[3]);          // a strip along the top
                if (across && r[3] >= h) c[3] = Math.max(c[3], h - r[1]);      // ... the bottom
                if (along && r[0] <= 0) c[0] = Math.max(c[0], r[2]);           // ... the left
                if (along && r[2] >= w) c[2] = Math.max(c[2], w - r[0]);       // ... the right
            }
        if (window != null && viewOnScreen != null && window[2] > window[0] && window[3] > window[1]) {
            int x = viewOnScreen[0], y = viewOnScreen[1];
            c[0] = Math.max(c[0], window[0] - x);
            c[1] = Math.max(c[1], window[1] - y);
            c[2] = Math.max(c[2], x + w - window[2]);
            c[3] = Math.max(c[3], y + h - window[3]);
        }
        for (int i = 0; i < 4; i++) c[i] = Math.max(0, Math.min(c[i], i % 2 == 0 ? w : h));
        return c;
    }
}
