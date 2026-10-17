package org.wwhdrecomp.wwhd;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.util.Log;
import androidx.core.content.FileProvider;
import java.io.File;

/** The same opt-in share flow on the launcher and direct game starts. */
final class CrashReports {
    static void offer(Activity activity) { offer(activity, () -> {}); }
    static void offer(Activity activity, Runnable then) {
        File files = activity.getExternalFilesDir(null);
        if (files == null) { then.run(); return; }
        File captures = new File(files, "captures");
        new Thread(() -> {
            SystemExitReports.record(activity.getApplicationContext(), captures);
            CrashReportFiles latest;
            try { latest = CrashReportFiles.latest(captures); }
            catch (RuntimeException unavailable) { latest = null; }
            final CrashReportFiles report = latest;
            activity.runOnUiThread(() -> {
                if (activity.isFinishing() || activity.isDestroyed()) return;
                if (report == null) { then.run(); return; }
                SharedPreferences prefs = activity.getSharedPreferences("crash-reports", Activity.MODE_PRIVATE);
                String identity = report.identity();
                if (identity.equals(prefs.getString("offeredCrash", ""))) { then.run(); return; }
                prefs.edit().putString("offeredCrash", identity).apply();
                new AlertDialog.Builder(activity).setTitle("Crash report available")
                    .setMessage("Share the latest crash report? It may include Android's exit reason and memory figures alongside native diagnostics.")
                    .setNegativeButton("Later", null).setPositiveButton("Share", (dialog, which) -> {
                        try {
                            File folder = new File(activity.getCacheDir(), "crash-share");
                            if (!folder.isDirectory() && !folder.mkdirs()) throw new java.io.IOException("Cannot create share folder");
                            File copy = new File(folder, "crash.log");
                            report.copyTo(copy);
                            Uri uri = FileProvider.getUriForFile(activity, activity.getPackageName() + ".crashlogs", copy);
                            Intent share = new Intent(Intent.ACTION_SEND).setType("text/plain")
                                .putExtra(Intent.EXTRA_STREAM, uri).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                            share.setClipData(ClipData.newRawUri("Crash report", uri));
                            activity.startActivity(Intent.createChooser(share, "Share crash report"));
                        } catch (Exception e) { Log.w("wwhd", "Cannot share crash report"); }
                    }).setOnDismissListener(dialog -> {
                        if (!activity.isFinishing() && !activity.isDestroyed()) then.run();
                    }).show();
            });
        }, "wwhd-exit-reports").start();
    }
}
