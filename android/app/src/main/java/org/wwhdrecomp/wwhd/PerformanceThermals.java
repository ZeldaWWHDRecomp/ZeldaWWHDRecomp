// Battery handling adapted from GreenNaugahyde's PerfStats.java, commit 9551a250 (MPL-2.0).
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, obtain one at https://mozilla.org/MPL/2.0/.
package org.wwhdrecomp.wwhd;

final class PerformanceThermals {
    static String statusName(int status) {
        String[] names = {"none", "light", "moderate", "severe", "critical", "emergency", "shutdown"};
        return status >= 0 && status < names.length ? names[status] : "n/a";
    }
    static String batteryTemperature(int tenths) {
        return tenths == Integer.MIN_VALUE ? "n/a" : String.format(java.util.Locale.ROOT, "%.1f C", tenths / 10f);
    }
    static String read(android.content.Context context) {
        return read(context, android.os.Build.VERSION.SDK_INT);
    }
    static String read(android.content.Context context, int api) {
        if (context == null) return "Thermal: n/a; battery: n/a";
        String thermal = "n/a", battery = "n/a";
        try {
            if (api >= 29) {
                android.os.PowerManager power = (android.os.PowerManager)
                    context.getSystemService(android.content.Context.POWER_SERVICE);
                if (power != null) {
                    thermal = statusName(power.getCurrentThermalStatus());
                }
            }
        } catch (RuntimeException ignored) { /* Optional public telemetry may be unavailable. */ }
        try {
            android.content.Intent b = context.registerReceiver(null,
                new android.content.IntentFilter(android.content.Intent.ACTION_BATTERY_CHANGED));
            if (b != null) {
                int t = b.getIntExtra(android.os.BatteryManager.EXTRA_TEMPERATURE, Integer.MIN_VALUE);
                battery = batteryTemperature(t);
            }
        } catch (RuntimeException ignored) { /* A missing value is n/a, never a made-up zero. */ }
        return "Thermal: " + thermal + "; battery: " + battery;
    }

}
