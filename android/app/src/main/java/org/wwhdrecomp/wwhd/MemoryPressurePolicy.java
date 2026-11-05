package org.wwhdrecomp.wwhd;

/**
 * The decisions of MemoryPressure.java, pure so runtime/tools/MemoryPressureTest.java covers them without Android.
 * Trim levels are the values of ComponentCallbacks2.TRIM_MEMORY_*; the results are memory_trim::Level
 * (runtime/src/platform/memory_trim.h).
 */
final class MemoryPressurePolicy {
    private MemoryPressurePolicy() {}

    static final int NONE = 0;
    static final int LIGHT = 1;
    static final int FULL = 2;

    // ComponentCallbacks2.TRIM_MEMORY_* (several deprecated in Android 14, which stopped sending them to apps)
    static final int RUNNING_MODERATE = 5;
    static final int RUNNING_LOW = 10;
    static final int UI_HIDDEN = 20;
    static final int BACKGROUND = 40;

    /** Free memory counts as low below the system's own threshold plus this margin. */
    static final long MARGIN_BYTES = 256L << 20;
    /** At most one trim from the free-memory check in this time (a trim takes effect at the next frame). */
    static final long CHECK_TRIM_INTERVAL_MS = 30000;

    /** The trim for an Android trim level. */
    static int levelFor(int trimLevel) {
        // in the background, on the list of cached apps: the higher the level, the sooner the app is ended
        if (trimLevel >= BACKGROUND) return FULL;
        // the app's window was hidden (Home, another app): it may be ended while it is away
        if (trimLevel >= UI_HIDDEN) return LIGHT;
        // running while the system is low (Android 13 and older still send these)
        if (trimLevel >= RUNNING_LOW) return FULL;
        if (trimLevel >= RUNNING_MODERATE) return LIGHT;
        return NONE;
    }

    /** ActivityManager.MemoryInfo's lowMemory, availMem and threshold: is memory low? */
    static boolean low(boolean systemLow, long availBytes, long thresholdBytes) {
        return systemLow || (availBytes > 0 && availBytes < thresholdBytes + MARGIN_BYTES);
    }

    /** Memory above the system's low-memory line in MB (0 at or below it), -1 when the system gave no figure. */
    static int headroomMb(long availBytes, long thresholdBytes) {
        if (availBytes <= 0 || thresholdBytes < 0) return -1;
        return (int) Math.min(Integer.MAX_VALUE, Math.max(0L, availBytes - thresholdBytes) >> 20);
    }

    /** Bytes as MB, -1 for no figure. */
    static int mb(long bytes) { return bytes < 0 ? -1 : (int) Math.min(Integer.MAX_VALUE, bytes >> 20); }

    /** Spacing of the free-memory check's trims (times in ms of any monotonic clock). */
    static final class Spacing {
        private long last;
        private boolean any;
        boolean allow(long now) {
            if (any && now - last < CHECK_TRIM_INTERVAL_MS) return false;
            any = true;
            last = now;
            return true;
        }
    }
}
