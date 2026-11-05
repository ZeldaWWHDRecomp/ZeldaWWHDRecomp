package org.wwhdrecomp.wwhd;

public final class MemoryPressureTest {
    private static void check(boolean ok, String what) { if (!ok) throw new AssertionError(what); }
    public static void main(String[] args) {
        // Android trim levels (ComponentCallbacks2.TRIM_MEMORY_*)
        check(MemoryPressurePolicy.levelFor(0) == MemoryPressurePolicy.NONE, "no level");
        check(MemoryPressurePolicy.levelFor(5) == MemoryPressurePolicy.LIGHT, "RUNNING_MODERATE: light");
        check(MemoryPressurePolicy.levelFor(10) == MemoryPressurePolicy.FULL, "RUNNING_LOW: full");
        check(MemoryPressurePolicy.levelFor(15) == MemoryPressurePolicy.FULL, "RUNNING_CRITICAL: full");
        check(MemoryPressurePolicy.levelFor(20) == MemoryPressurePolicy.LIGHT, "UI_HIDDEN: light");
        check(MemoryPressurePolicy.levelFor(40) == MemoryPressurePolicy.FULL, "BACKGROUND: full");
        check(MemoryPressurePolicy.levelFor(60) == MemoryPressurePolicy.FULL, "MODERATE: full");
        check(MemoryPressurePolicy.levelFor(80) == MemoryPressurePolicy.FULL, "COMPLETE: full");
        check(MemoryPressurePolicy.levelFor(-1) == MemoryPressurePolicy.NONE, "negative: none");
        // free memory
        final long mib = 1L << 20;
        check(MemoryPressurePolicy.low(true, 4096 * mib, 512 * mib), "the system's own flag");
        check(MemoryPressurePolicy.low(false, 700 * mib, 512 * mib), "below threshold + 256 MiB");
        check(!MemoryPressurePolicy.low(false, 768 * mib, 512 * mib), "exactly threshold + 256 MiB: not low");
        check(!MemoryPressurePolicy.low(false, 4096 * mib, 512 * mib), "plenty free");
        check(!MemoryPressurePolicy.low(false, 0, 512 * mib), "no figure (0): not low");
        // spacing of the check's trims
        MemoryPressurePolicy.Spacing s = new MemoryPressurePolicy.Spacing();
        check(s.allow(0), "first trim at time 0");
        check(!s.allow(1000), "1 s later: no");
        check(!s.allow(29999), "just under 30 s: no");
        check(s.allow(30000), "30 s: yes");
        check(!s.allow(30001), "right after: no");
        MemoryPressurePolicy.Spacing negative = new MemoryPressurePolicy.Spacing();
        check(negative.allow(-5000), "any clock value for the first");
        // headroom above the system's line, for save states and dynamic resolution
        check(MemoryPressurePolicy.headroomMb(1024 * mib, 512 * mib) == 512, "512 MiB above the line");
        check(MemoryPressurePolicy.headroomMb(400 * mib, 512 * mib) == 0, "below the line: 0");
        check(MemoryPressurePolicy.headroomMb(0, 512 * mib) == -1, "no figure: unknown");
        check(MemoryPressurePolicy.headroomMb(1024 * mib, -1) == -1, "no threshold: unknown");
        check(MemoryPressurePolicy.headroomMb(Long.MAX_VALUE, 0) == Integer.MAX_VALUE, "huge: clamped");
        check(MemoryPressurePolicy.mb(512 * mib) == 512 && MemoryPressurePolicy.mb(-1) == -1, "mb");
        System.out.println("memory pressure: ok");
    }
}
