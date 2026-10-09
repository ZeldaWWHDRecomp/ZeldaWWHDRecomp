package org.wwhdrecomp.wwhd;

/** Window-coordinate hinge to content-coordinate panes; independent of Android for host tests. */
public final class FoldLayout {
    private FoldLayout() {}

    /** Returns left/top/width/height for the TV pane, followed by the GamePad pane, or null. */
    public static int[] panes(int width, int height, int windowX, int windowY,
            int left, int top, int right, int bottom, boolean vertical) {
        if (width <= 0 || height <= 0 || right < left || bottom < top) return null;
        // Use long arithmetic before subtracting potentially stale window coordinates.
        long l = (long) left - windowX, r = (long) right - windowX;
        long t = (long) top - windowY, b = (long) bottom - windowY;
        if (vertical) {
            if (t > 0 || b < height || l <= 0 || r >= width) return null;
            return new int[] {0, 0, (int) l, height, (int) r, 0, width - (int) r, height};
        }
        if (l > 0 || r < width || t <= 0 || b >= height) return null;
        return new int[] {0, 0, width, (int) t, 0, (int) b, width, height - (int) b};
    }
}
