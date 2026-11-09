package org.wwhdrecomp.wwhd;

/**
 * Issue #349: whether the game's surface needs to be made again because its view grew after the surface was made.
 *
 * On a Retroid Pocket Nova (Android 13, 1280x960) the game's surface is made while the navigation bar still takes the
 * bottom 48 rows (view 1280x912); full screen then grows the view to 1280x960 and the surface's buffer and the
 * swapchain follow (surfaceChanged, a new swapchain at 1280x960), yet the screen keeps showing only the rows the
 * surface was made with: the bottom 48 stay black. A swapchain and Vulkan surface made after the view grew do not
 * change that (the reporter's log: both are made only after the view is 1280x960). What does is a new surface from
 * the system, as after the app was in the background. So when the view grows past the size its surface was made
 * at, the surface is made again once the size settles (WwhdActivity.renewGrownSurface); a smaller view needs none.
 *
 * Pure logic (no Android classes), driven by the SurfaceHolder callbacks; SurfaceRegrowTest covers it.
 */
final class SurfaceRegrow {
    /** How long the size must stay unchanged before the surface is made again (a window resized step by step). */
    static final long SETTLE_MS = 300;
    /** How many settle times a due renewal waits for the game's first picture before it is dropped (two minutes). */
    static final int MAX_WAITS = 400;

    private int madeWidth, madeHeight;  // the current surface's first size (its first surfaceChanged), 0 before
    private int width, height;          // its latest size
    private boolean due;

    /** surfaceCreated: a new surface; its first surfaceChanged gives the size it was made at. */
    void created() {
        madeWidth = madeHeight = width = height = 0;
        due = false;
    }

    /** surfaceChanged: true when the surface grew past the size it was made at (a new one is due). */
    boolean changed(int newWidth, int newHeight) {
        if (newWidth <= 0 || newHeight <= 0) return due;
        width = newWidth;
        height = newHeight;
        if (madeWidth <= 0 || madeHeight <= 0) {
            madeWidth = newWidth;
            madeHeight = newHeight;
            due = false;
        } else {
            due = newWidth > madeWidth || newHeight > madeHeight;
        }
        return due;
    }

    /** surfaceDestroyed: no surface, nothing due (the next one is made at the view's size then). */
    void destroyed() {
        madeWidth = madeHeight = width = height = 0;
        due = false;
    }

    /** A new surface is due (checked again when the settle time is over). */
    boolean due() { return due; }

    /** The new surface is being asked for: not again until a surface outgrows its size once more. */
    void renewing() { due = false; }

    /** For the log: "made at WxH, now WxH". */
    String describe() {
        return "made at " + madeWidth + "x" + madeHeight + ", now " + width + "x" + height;
    }
}
