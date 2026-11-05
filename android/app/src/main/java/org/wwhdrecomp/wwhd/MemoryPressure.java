package org.wwhdrecomp.wwhd;

import android.app.ActivityManager;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;

/**
 * Low memory: asks the renderer to free what it can make again (runtime/src/platform/memory_trim.h), so the
 * system is less likely to end the game. Two signals: onTrimMemory (WwhdActivity), and, while the game is in the
 * foreground, a look at the system's free memory every two seconds, because Android 14 and newer no longer send
 * the RUNNING_* trim levels to apps. The decisions are in MemoryPressurePolicy.java.
 */
final class MemoryPressure {
    /** The source passed to native code for a trim from the free-memory check (not an Android level). */
    static final int SOURCE_CHECK = -1;
    static final long CHECK_INTERVAL_MS = 2000;

    private static native void nativeTrim(int level, int source);
    private static native void nativeHeadroom(int headroomMb, int thresholdMb);

    private final Context context;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final MemoryPressurePolicy.Spacing spacing = new MemoryPressurePolicy.Spacing();
    private final ActivityManager.MemoryInfo info = new ActivityManager.MemoryInfo();
    private boolean checking;
    private boolean available = true;  // false once the game library turned out not to have nativeTrim

    MemoryPressure(Context context) { this.context = context.getApplicationContext(); }

    private final Runnable check = new Runnable() {
        @Override public void run() {
            if (!checking) return;
            ActivityManager am = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
            if (am != null) {
                am.getMemoryInfo(info);
                // save states check it before they start, dynamic resolution follows it (memory_trim.h)
                sendHeadroom(MemoryPressurePolicy.headroomMb(info.availMem, info.threshold), MemoryPressurePolicy.mb(info.threshold));
                if (MemoryPressurePolicy.low(info.lowMemory, info.availMem, info.threshold) && spacing.allow(SystemClock.elapsedRealtime())) {
                    Log.i("wwhd", "memory low: " + (info.availMem >> 20) + " MB free (system threshold "
                            + (info.threshold >> 20) + " MB): freeing caches");
                    send(MemoryPressurePolicy.FULL, SOURCE_CHECK);
                }
            }
            handler.postDelayed(this, CHECK_INTERVAL_MS);
        }
    };

    /** onResume: check while the game is in the foreground. */
    void start() {
        if (checking) return;
        checking = true;
        handler.post(check);  // the first figure at once (a save may come before the first two seconds)
    }

    /** onPause. */
    void stop() {
        checking = false;
        handler.removeCallbacks(check);
    }

    /** onTrimMemory. */
    void onTrimMemory(int trimLevel) {
        int level = MemoryPressurePolicy.levelFor(trimLevel);
        if (level == MemoryPressurePolicy.NONE) return;
        Log.i("wwhd", "onTrimMemory(" + trimLevel + "): " + (level == MemoryPressurePolicy.FULL ? "full" : "light") + " trim");
        send(level, trimLevel);
    }

    private boolean headroomAvailable = true;  // false once the game library turned out not to have nativeHeadroom

    private void sendHeadroom(int headroomMb, int thresholdMb) {
        if (!headroomAvailable) return;
        try {
            nativeHeadroom(headroomMb, thresholdMb);
        } catch (UnsatisfiedLinkError e) {
            headroomAvailable = false;
            Log.w("wwhd", "free memory not passed: " + e.getMessage());
        }
    }

    private void send(int level, int source) {
        if (!available) return;
        try {
            nativeTrim(level, source);
        } catch (UnsatisfiedLinkError e) {
            // an on-device game library from before this signal
            available = false;
            Log.w("wwhd", "memory trim not passed: " + e.getMessage());
        }
    }
}
