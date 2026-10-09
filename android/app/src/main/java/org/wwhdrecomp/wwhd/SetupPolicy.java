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
