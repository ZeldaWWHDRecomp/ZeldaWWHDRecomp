package org.wwhdrecomp.wwhd;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

/** Queried once per process, off the UI thread, before offering crash reports. */
final class SystemExitReports {
    private static boolean recorded;
    private static File stopMarker(Context context) { return new File(context.getFilesDir(), "expected-game-stop"); }

    // A file rather than SharedPreferences: save tools live in another process.
    static void expectStop(Context context, int pid) {
        try (FileOutputStream out = new FileOutputStream(stopMarker(context))) {
            out.write((pid + "\n" + System.currentTimeMillis()).getBytes(StandardCharsets.US_ASCII));
            out.getFD().sync();
        } catch (java.io.IOException ignored) { }
    }

    static synchronized void record(Context context, File captures) {
        if (recorded) return;
        try {
            ActivityManager manager = context.getSystemService(ActivityManager.class);
            if (manager == null) return;
            List<ApplicationExitInfo> history = manager.getHistoricalProcessExitReasons(context.getPackageName(), 0, 32);
            if (history == null) return;
            ArrayList<SystemExitReport.Exit> exits = new ArrayList<>();
            for (ApplicationExitInfo e : history) {
                // The setup worker, shader preparation and save tools are separate processes.
                exits.add(new SystemExitReport.Exit(e.getTimestamp(), e.getPid(), e.getReason(),
                    e.getStatus(), e.getImportance(), e.getPss(), e.getRss(),
                    context.getPackageName().equals(e.getProcessName())));
            }
            int expectedPid = -1; long expectedAt = 0;
            try {
                List<String> marker = Files.readAllLines(stopMarker(context).toPath(), StandardCharsets.US_ASCII);
                if (marker.size() == 2) { expectedPid = Integer.parseInt(marker.get(0)); expectedAt = Long.parseLong(marker.get(1)); }
            } catch (java.io.IOException | NumberFormatException ignored) { }
            SharedPreferences prefs = context.getSharedPreferences("crash-reports", Context.MODE_PRIVATE);
            long seen = prefs.getLong("systemExitSeen", 0);
            SystemExitReport.Exit exit = SystemExitReport.latest(exits, seen, expectedPid, expectedAt);
            if (exit != null) {
                if (!captures.isDirectory() && !captures.mkdirs()) return;
                File target = new File(captures, "crash-system-" + exit.timestamp + "-" + exit.pid + ".log");
                File pending = new File(captures, target.getName() + ".tmp");
                try (FileOutputStream out = new FileOutputStream(pending)) {
                    out.write(SystemExitReport.text(exit).getBytes(StandardCharsets.UTF_8));
                    out.getFD().sync();
                }
                // Publish only complete reports. Deterministic name also prevents duplicates
                // if the process dies between publication and committing the watermark.
                Files.move(pending.toPath(), target.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                target.setLastModified(exit.timestamp);
            }
            // Retry after I/O errors or a failed preference commit on a later activity/start.
            if (!prefs.edit().putLong("systemExitSeen", SystemExitReport.watermark(exits, seen)).commit()) return;
            recorded = true;
        } catch (RuntimeException | java.io.IOException ignored) {
            // Exit history is best effort on vendor systems; never prevent launching the app.
        }
    }
}
