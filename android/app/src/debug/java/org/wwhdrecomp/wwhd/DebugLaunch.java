package org.wwhdrecomp.wwhd;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;

/**
 * Debug builds only (release has a no-op): scripted and benchmark starts over adb. Every string
 * extra named WWHD_* becomes an environment variable of the game before SDL_main runs, e.g.
 *
 *   adb shell am start -S -n <package>/org.wwhdrecomp.wwhd.WwhdActivity \
 *       --es WWHD_EXIT_AT_FRAME 4000 --es WWHD_VK_STUTTER_TRACE {files}/bench/run1/frames.csv
 *
 * "{files}" stands for the app's external files folder (the game's working directory).
 * tools/android/bench_phone.py drives it. Variables stay set for the process: start with -S.
 */
final class DebugLaunch {
    private DebugLaunch() {}

    static void apply(Activity activity) {
        Intent intent = activity.getIntent();
        Bundle extras = intent != null ? intent.getExtras() : null;
        if (extras == null) return;
        File files = activity.getExternalFilesDir(null);
        String root = files != null ? files.getAbsolutePath() : ".";
        for (String key : extras.keySet()) {
            if (!key.startsWith("WWHD_") || !key.matches("[A-Z0-9_]+")) continue;
            Object value = extras.get(key);
            if (!(value instanceof String)) continue;
            String text = ((String) value).replace("{files}", root);
            SDLActivity.nativeSetenv(key, text);
            Log.i("wwhd-bench", "launch env " + key + "=" + text);
        }
    }
}
