package org.wwhdrecomp.wwhd;

/** Stateful thresholds avoid repeatedly restarting work near a sensor limit. */
public final class SetupPolicy {
    private boolean hot;
    private boolean low;

    public static String description(String reason) {
        if ("manual".equals(reason)) return "Paused by you";
        if ("heat".equals(reason)) return "Waiting for the phone to cool down";
        if ("battery".equals(reason)) return "Waiting for charging or at least 30% battery";
        if ("battery_unknown".equals(reason)) return "Waiting for battery information";
        return "Paused at a safe checkpoint";
    }

    /**
     * Compiler processes to run at once: about two cores each (the game keeps the phone busy
     * otherwise too), about 512 MB of free memory each (a compiler peaked near 260 MB on a test
     * phone) with 1 GB kept free, one on low-memory phones or when the phone is already warm.
     */
    public static int compileJobs(int cores, long availableBytes, boolean lowRam, int thermal) {
        if (lowRam || thermal >= 2) return 1;
        long byMemory = (availableBytes - (1L << 30)) / (512L << 20);
        return (int) Math.max(1, Math.min(4, Math.min(cores / 2, byMemory)));
    }

    /** "Compiling: 30/80 · about 12 min left" from a compile progress event, or null. */
    public static String compileProgress(String state, int compiled, int reused, int total, long etaSeconds) {
        if (total <= 0) return null;
        if ("complete".equals(state)) return "Compiling: done (" + total + "/" + total + ")";
        String text = "Compiling: " + Math.min(total, compiled + reused) + "/" + total;
        if (etaSeconds >= 0) {
            long minutes = (etaSeconds + 59) / 60;
            text += minutes <= 1 ? " · about a minute left" : " · about " + minutes + " min left";
        }
        return text;
    }

    public String update(int thermal, int temperatureTenths, int batteryPercent,
                         boolean charging, boolean manual) {
        if (thermal >= 3 || temperatureTenths >= 420) hot = true;
        else if (thermal <= 1 && (temperatureTenths < 0 || temperatureTenths <= 390)) hot = false;
        if (charging || batteryPercent >= 30) low = false;
        else if (batteryPercent >= 0 && batteryPercent <= 20) low = true;
        if (manual) return "manual";
        if (hot) return "heat";
        if (low) return "battery";
        if (!charging && batteryPercent < 0) return "battery_unknown";
        return null;
    }
}
