package org.wwhdrecomp.wwhd;

import android.content.Context;
import android.os.VibrationAttributes;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.provider.Settings;

/** Short, non-repeating leases: a stalled or killed game cannot leave the phone vibrating. */
final class PhoneRumble {
    private final Context context;
    private final Vibrator vibrator;
    private final VibrationAttributes attributes = new VibrationAttributes.Builder()
            .setUsage(VibrationAttributes.USAGE_MEDIA).build();
    private boolean active, playing;

    PhoneRumble(Context context) {
        this.context = context.getApplicationContext();
        Vibrator found = null;
        try {
            VibratorManager manager = this.context.getSystemService(VibratorManager.class);
            if (manager != null) found = manager.getDefaultVibrator();
            if (found != null && !found.hasVibrator()) found = null;
        } catch (RuntimeException ignored) { /* Missing/restricted service: rumble is optional. */ }
        vibrator = found;
    }

    synchronized void setActive(boolean active) {
        this.active = active;
        if (!active) stop();
    }

    // Called by native input, at most every 100ms for a steady level; stops arrive immediately.
    synchronized void update(int level) {
        if (vibrator == null) return;
        try {
            // Media usage lets Android apply its media-vibration intensity/DND policy. Also
            // honour the public touch-feedback master switch, including on older OEM builds.
            if (!active || level <= 0 || Settings.System.getInt(context.getContentResolver(),
                    Settings.System.HAPTIC_FEEDBACK_ENABLED, 1) == 0) {
                stop();
                return;
            }
            int amplitude = vibrator.hasAmplitudeControl()
                    ? Math.max(1, Math.min(255, Math.round(Math.min(level, 65535) * 255f / 65535f)))
                    : VibrationEffect.DEFAULT_AMPLITUDE;
            vibrator.vibrate(VibrationEffect.createOneShot(150, amplitude), attributes);
            playing = true;
        } catch (RuntimeException ignored) { stop(); }
    }

    private void stop() {
        if (!playing || vibrator == null) return;
        playing = false;
        try { vibrator.cancel(); } catch (RuntimeException ignored) { /* Process lease still expires. */ }
    }
}
