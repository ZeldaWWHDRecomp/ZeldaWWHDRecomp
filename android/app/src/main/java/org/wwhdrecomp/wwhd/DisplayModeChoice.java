package org.wwhdrecomp.wwhd;

/**
 * The display mode for a chosen frame rate above 60 Hz (WwhdActivity.preferDisplayMode). Pure, so
 * runtime/tools/DisplayModeChoiceTest.java covers it: the mode must keep the current resolution
 * (a preferred mode also fixes the resolution) and reach the rate; the lowest such rate wins, so
 * 120 fps picks 120 Hz, not 144. 0 leaves the choice to the system (60 Hz or less, or no match).
 */
final class DisplayModeChoice {
    private DisplayModeChoice() {}

    /** modes: {id, width, height, refresh rate (Hz, rounded)} per supported mode. */
    static int pick(int[][] modes, int width, int height, int hz) {
        if (hz <= 60 || modes == null) return 0;
        int best = 0, bestRate = Integer.MAX_VALUE;
        for (int[] m : modes) {
            if (m.length < 4 || m[1] != width || m[2] != height) continue;
            if (m[3] >= hz - 1 && m[3] < bestRate) { best = m[0]; bestRate = m[3]; }
        }
        return best;
    }
}
