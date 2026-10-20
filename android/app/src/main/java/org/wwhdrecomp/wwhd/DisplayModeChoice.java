package org.wwhdrecomp.wwhd;

/**
 * The display mode for a chosen frame rate above 60 Hz (WwhdActivity.preferDisplayMode). Pure, so
 * runtime/tools/DisplayModeChoiceTest.java covers it: the mode must keep the current resolution
 * (a preferred mode also fixes the resolution) and reach the rate; the lowest such rate wins, so
 * 120 fps picks 120 Hz, not 144. 40 fps picks a rate that is a whole multiple of 40, so every frame
 * stays on screen equally long: the lowest one above 60 Hz (80, else 120), and 40 Hz only without
 * one (a Xiaomi 14 kept 60 Hz when the app preferred its 40 Hz mode; its 120 Hz mode is granted).
 * 0 leaves the choice to the system (60 fps or 30, or no match).
 */
final class DisplayModeChoice {
    private DisplayModeChoice() {}

    /** modes: {id, width, height, refresh rate (Hz, rounded)} per supported mode. */
    static int pick(int[][] modes, int width, int height, int hz) {
        if (modes == null || hz <= 0 || (hz <= 60 && hz != 40)) return 0;
        int best = 0, bestRate = Integer.MAX_VALUE;
        for (int[] m : modes) {
            if (m.length < 4 || m[1] != width || m[2] != height) continue;
            if (m[3] >= hz - 1 && m[3] < bestRate && (hz > 60 || multipleOf(m[3], hz))) { best = m[0]; bestRate = m[3]; }
        }
        if (hz < 60 && bestRate <= 61) {  // 40 fps: a multiple above 60 Hz first, if there is one
            int above = 0, aboveRate = Integer.MAX_VALUE;
            for (int[] m : modes) {
                if (m.length < 4 || m[1] != width || m[2] != height) continue;
                if (m[3] > 61 && m[3] < aboveRate && multipleOf(m[3], hz)) { above = m[0]; aboveRate = m[3]; }
            }
            if (above != 0) return above;
        }
        return best;
    }

    /** rate (rounded Hz) is a whole multiple of hz: 119 and 120 Hz both count for 40. */
    static boolean multipleOf(int rate, int hz) {
        int r = rate % hz;
        return r <= 1 || r >= hz - 1;
    }
}
