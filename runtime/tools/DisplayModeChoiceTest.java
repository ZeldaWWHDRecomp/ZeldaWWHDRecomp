package org.wwhdrecomp.wwhd;

public final class DisplayModeChoiceTest {
    private static void check(boolean ok, String what) { if (!ok) throw new AssertionError(what); }
    public static void main(String[] args) {
        // the phone in the R3 checks: one resolution, 120/60/90/40/30/24 Hz
        int[][] phone = {{1, 1200, 2670, 120}, {2, 1200, 2670, 60}, {3, 1200, 2670, 90}, {4, 1200, 2670, 40},
                         {5, 1200, 2670, 30}, {6, 1200, 2670, 24}};
        check(DisplayModeChoice.pick(phone, 1200, 2670, 120) == 1, "120 fps: the 120 Hz mode");
        check(DisplayModeChoice.pick(phone, 1200, 2670, 90) == 3, "90: the lowest rate that reaches it");
        check(DisplayModeChoice.pick(phone, 1200, 2670, 60) == 0, "60 Hz or less: system choice");
        check(DisplayModeChoice.pick(phone, 1200, 2670, 0) == 0, "cleared");
        check(DisplayModeChoice.pick(phone, 1200, 2670, 240) == 0, "no mode reaches 240: system choice");
        check(DisplayModeChoice.pick(phone, 1080, 2400, 120) == 0, "never another resolution");
        // two resolutions at 120 Hz and a 144 Hz mode: same resolution, lowest rate reaching 120
        int[][] multi = {{1, 1440, 3200, 120}, {2, 1080, 2400, 120}, {3, 1080, 2400, 144}, {4, 1080, 2400, 60}};
        check(DisplayModeChoice.pick(multi, 1080, 2400, 120) == 2, "same resolution, 120 not 144");
        check(DisplayModeChoice.pick(multi, 1440, 3200, 144) == 0, "144 at the other resolution only: none");
        check(DisplayModeChoice.pick(new int[][] {{7, 1200, 2670, 119}}, 1200, 2670, 120) == 7, "119.99 Hz rounds");
        check(DisplayModeChoice.pick(null, 1200, 2670, 120) == 0, "no modes");
        System.out.println("display mode choice: ok");
    }
}
