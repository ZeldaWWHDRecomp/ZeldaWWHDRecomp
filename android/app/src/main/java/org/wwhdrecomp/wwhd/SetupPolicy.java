package org.wwhdrecomp.wwhd;

/**
 * Stateful thresholds avoid repeatedly restarting work near a sensor limit.
 *
 * Heat: pause at Android thermal status SEVERE, at a battery temperature of 45.0 °C, or at
 * 42.0 °C while Android also reports MODERATE or higher; resume at LIGHT or lower and at most
 * 41.0 °C for five seconds, with any readable CPU/GPU sensor below 80 °C. A heat pause never ends
 * on its own (#277): once that cool-down rule holds the user may Continue
 * ({@link #continueAfterCooling}), and at any time the user may continue anyway (heat override),
 * which skips heat pauses for the rest of the setup run. Android thermal status CRITICAL or higher
 * always pauses: the phone throttles hard or shuts down at that level whatever setup does.
 *
 * A SEVERE status that no temperature backs up is ignored (see {@link #thermalStatusUnreliable}).
 */
public final class SetupPolicy {
    public static final int THERMAL_LIGHT = 1, THERMAL_MODERATE = 2, THERMAL_SEVERE = 3, THERMAL_CRITICAL = 4;
    public static final long HEAT_CLEAR_MILLIS = 5000;
    private long coolSince = -1, lastSample = -1;
    private boolean hot;
    /** A heat pause that waits for the user's tap (#277); it survives cooling down. */
    private boolean heatWait;
    private boolean low;

    /** Whether setup is held by a heat pause that waits for the user. */
    public boolean heatWaiting() { return heatWait; }

    /** Whether the held heat pause may end with Continue: the cool-down rule holds. */
    public boolean heatCooled() { return heatWait && !hot; }

    /**
     * The user's Continue on a heat pause: ends it once the device has cooled down (true); while
     * still hot it changes nothing (false). A later real heat condition pauses again.
     */
    public boolean continueAfterCooling() {
        if (!heatCooled()) return false;
        heatWait = false;
        return true;
    }

    /**
     * Restores a heat pause recorded before the setup service restarted. The device then has to
     * meet the cool-down rule again before Continue is offered.
     */
    public void restoreHeatWait(boolean waiting) {
        heatWait = waiting;
        if (waiting) { hot = true; coolSince = -1; }
    }

    /** Readable PowerManager thermal status (none ... shutdown), "n/a" when unknown. */
    public static String thermalName(int status) {
        String[] names = {"none", "light", "moderate", "severe", "critical", "emergency", "shutdown"};
        return status >= 0 && status < names.length ? names[status] : "n/a";
    }

    /** "43.1 °C" from BatteryManager tenths; -1 and Integer.MIN_VALUE mean no reading. */
    public static String batteryTemperature(int tenths) {
        if (tenths == -1 || tenths == Integer.MIN_VALUE) return "n/a";
        return String.format(java.util.Locale.ROOT, "%.1f °C", tenths / 10f);
    }

    /*
     * Thermal status trust (issue #254). Some devices report heat that no temperature backs up: an
     * AYANEO Pocket EVO (fan, Snapdragon 8 Gen 2, Android 13) reports SEVERE right after boot at a
     * 30-31 °C battery with its fan far from full speed, and its thermal headroom stays at 1.09-1.38
     * during normal play, so neither signal can confirm the other there. Its CPU sensors read 52-77 °C
     * and its GPU sensors 45-62 °C while the game runs at 60 fps (a Retroid Pocket 6 with the same chip
     * reports NONE at the same readings). So SEVERE is believed only when a real temperature agrees:
     *  - the battery is at least 40.0 °C (setup's own battery limits start at 41-42 °C), or
     *  - the hottest CPU/GPU sensor is at least 90 °C: Snapdragon cores sit in the 70s under gaming
     *    load and only throttle near their 95-105 °C junction limits.
     * Missing readings do not confirm SEVERE either (#277).
     * Otherwise (SEVERE without either temperature confirming it) the status
     * is treated as unknown; setup's battery temperature limits still apply. NONE ... MODERATE are not
     * doubted (they never pause on their own), and CRITICAL or higher is always believed: Android
     * throttles hard or shuts down at that level anyway. The thermal headroom is not used: the EVO
     * forecasts SEVERE (1.0+) while cool. HardwarePropertiesManager (skin sensors) needs a system
     * permission, so a normal app reads the CPU/GPU zones in /sys/class/thermal instead (as the
     * performance report does).
     */
    public static final int THERMAL_TRUST_BATTERY_TENTHS = 400;
    public static final float THERMAL_TRUST_CORE_CELSIUS = 90f;

    /**
     * Whether SEVERE lacks confirmation from real temperatures. temperatureTenths: battery
     * (-1 or Integer.MIN_VALUE: unknown); hottestCore: hottest CPU/GPU sensor in °C (NaN: unknown).
     */
    public static boolean thermalStatusUnreliable(int thermal, int temperatureTenths, float hottestCore) {
        if (thermal != THERMAL_SEVERE) return false;
        if (temperatureTenths >= THERMAL_TRUST_BATTERY_TENTHS) return false;
        return !(hottestCore >= THERMAL_TRUST_CORE_CELSIUS);  // NaN (unreadable) does not confirm it
    }

    /** The thermal status to act on: the reported one, or -1 (unknown) where it looks unreliable. */
    public static int trustedThermal(int thermal, int temperatureTenths, float hottestCore) {
        return thermalStatusUnreliable(thermal, temperatureTenths, hottestCore) ? -1 : thermal;
    }

    /** "77.2 C", or "n/a" for no readable CPU/GPU sensor. */
    public static String coreText(float hottestCore) {
        return Float.isNaN(hottestCore) ? "n/a" : String.format(java.util.Locale.ROOT, "%.1f C", hottestCore);
    }

    /**
     * The hottest CPU/GPU temperature in °C from root/class/thermal (thermal_zone types containing
     * "cpu" or "gpu", millidegrees), NaN when none is readable. Other zones (battery, skin, "socd",
     * a limits daemon reading 0) are left out.
     */
    public static float hottestCoreSensor(java.io.File root) {
        java.io.File[] zones = new java.io.File(root, "class/thermal").listFiles();
        float hottest = Float.NaN;
        if (zones == null) return hottest;
        for (java.io.File zone : zones) {
            if (!zone.getName().startsWith("thermal_zone")) continue;
            String type = firstLine(new java.io.File(zone, "type"));
            if (type == null || !(type.contains("cpu") || type.contains("gpu"))) continue;
            String temp = firstLine(new java.io.File(zone, "temp"));
            try {
                float celsius = temp == null ? Float.NaN : Long.parseLong(temp.trim()) / 1000f;
                if (celsius >= -40 && celsius <= 200 && !(hottest >= celsius)) hottest = celsius;
            } catch (NumberFormatException ignored) { /* not a reading */ }
        }
        return hottest;
    }

    private static String firstLine(java.io.File file) {
        try (java.io.BufferedReader in = new java.io.BufferedReader(new java.io.FileReader(file))) {
            return in.readLine();
        } catch (java.io.IOException | SecurityException e) {
            return null;
        }
    }

    /**
     * The performance report's "Thermal: ..." value: the status name, or for a status that looks
     * unreliable "severe (reported by the system; battery 30.0 C, CPU/GPU 77.2 C, looks unreliable)".
     */
    public static String reportThermal(int thermal, int temperatureTenths, float hottestCore) {
        if (!thermalStatusUnreliable(thermal, temperatureTenths, hottestCore)) return thermalName(thermal);
        return thermalName(thermal) + " (reported by the system; battery " +
            (temperatureTenths >= 0 ? String.format(java.util.Locale.ROOT, "%.1f C", temperatureTenths / 10f) : "n/a") +
            ", CPU/GPU " + coreText(hottestCore) + ", looks unreliable)";
    }

    public static String readings(int thermal, int temperatureTenths) {
        return readings(thermal, temperatureTenths, Float.NaN);
    }

    public static String readings(int thermal, int temperatureTenths, float hottestCore) {
        String text = "battery " + batteryTemperature(temperatureTenths) + ", thermal status: " + thermalName(thermal);
        if (thermalStatusUnreliable(thermal, temperatureTenths, hottestCore)) text += " (looks unreliable, ignored)";
        return Float.isNaN(hottestCore) ? text : text + ", CPU/GPU " + coreText(hottestCore).replace(" C", " °C");
    }

    public static String description(String reason) {
        if ("manual".equals(reason)) return "Paused by you";
        if ("heat".equals(reason)) return "Waiting for the Android device to cool down";
        if ("critical".equals(reason)) return "Paused because Android reports the device as critically hot";
        if ("battery".equals(reason)) return "Waiting for charging or at least 30% battery";
        if ("battery_unknown".equals(reason)) return "Waiting for battery information";
        // setup's models step (ModelsStep): the game draws only with the setup screen in front
        if ("screen".equals(reason)) return "Preparing model shaders goes on when setup is on the screen again";
        return "Paused at a safe checkpoint";
    }

    /** The pause text with the current sensor readings for heat pauses. */
    public static String description(String reason, int thermal, int temperatureTenths) {
        return description(reason, thermal, temperatureTenths, Float.NaN);
    }

    public static String description(String reason, int thermal, int temperatureTenths, float hottestCore) {
        return description(reason, thermal, temperatureTenths, hottestCore, false);
    }

    /** As above; cooled: a heat pause whose device has cooled down and now waits for Continue. */
    public static String description(String reason, int thermal, int temperatureTenths, float hottestCore,
                                     boolean cooled) {
        if ("heat".equals(reason))
            return (cooled ? "The Android device has cooled down" : description(reason)) +
                " (" + readings(thermal, temperatureTenths, hottestCore) + ")";
        if ("critical".equals(reason))
            return description(reason) + " (" + readings(thermal, temperatureTenths, hottestCore) + "). " +
                "Setup waits until it cools down. Continue anyway does not apply here: " +
                "at this level the Android device throttles hard or shuts down regardless.";
        return description(reason);
    }

    /** Whether "Continue anyway" can skip this pause reason. */
    public static boolean overridable(String reason) {
        return "heat".equals(reason) || "battery".equals(reason) || "battery_unknown".equals(reason);
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
        return update(thermal, temperatureTenths, batteryPercent, charging, manual, false, false);
    }

    /**
     * The pause reason, or null to run. heatOverride and batteryOverride are the user's
     * "Continue anyway" for this setup run; they never skip a manual pause or CRITICAL status.
     * A heat pause stays until {@link #continueAfterCooling} or heatOverride ends it.
     */
    public String update(int thermal, int temperatureTenths, int batteryPercent,
                         boolean charging, boolean manual, boolean heatOverride, boolean batteryOverride) {
        return update(thermal, temperatureTenths, Float.NaN, batteryPercent, charging, manual,
            heatOverride, batteryOverride, System.nanoTime() / 1000000L);
    }

    /** Raw sensor snapshot and monotonic milliseconds; also used by deterministic replay tests. */
    public String update(int reported, int temperatureTenths, float hottestCore, int batteryPercent,
                         boolean charging, boolean manual, boolean heatOverride, boolean batteryOverride,
                         long now) {
        int thermal = trustedThermal(reported, temperatureTenths, hottestCore);
        boolean known = temperatureTenths >= 0;
        if (thermal >= THERMAL_SEVERE || (known && temperatureTenths >= 450) ||
            (known && temperatureTenths >= 420 && thermal >= THERMAL_MODERATE)) {
            hot = true;
            coolSince = -1;
        } else if (hot) {
            // At least one real temperature is required; every readable temperature must be cool.
            // A missing/delayed sample breaks the run.
            boolean cool = thermal <= THERMAL_LIGHT && (known || !Float.isNaN(hottestCore)) &&
                (!known || temperatureTenths <= 410) &&
                !(hottestCore >= 80f);
            if (!cool || now < lastSample || (lastSample >= 0 && now - lastSample > 2000)) coolSince = -1;
            if (cool) {
                if (coolSince < 0) coolSince = now;
                if (now - coolSince >= HEAT_CLEAR_MILLIS) { hot = false; coolSince = -1; }
            }
        }
        lastSample = now;
        // A heat pause holds until the user taps (#277); Continue anyway drops it for this run.
        if (heatOverride) heatWait = false;
        else if (hot) heatWait = true;
        if (charging || batteryPercent >= 30) low = false;
        else if (batteryPercent >= 0 && batteryPercent <= 20) low = true;
        if (manual) return "manual";
        if (thermal >= THERMAL_CRITICAL) return "critical";
        if (heatWait) return "heat";
        if (batteryOverride) return null;
        if (low) return "battery";
        if (!charging && batteryPercent < 0) return "battery_unknown";
        return null;
    }
}
