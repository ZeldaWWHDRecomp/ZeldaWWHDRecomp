package org.wwhdrecomp.wwhd;

import android.os.Bundle;
import android.content.Intent;
import android.app.AlertDialog;
import java.io.FileOutputStream;
import java.io.InputStream;
import android.content.ClipData;
import android.net.Uri;
import androidx.core.content.FileProvider;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import android.util.Log;
import android.view.Display;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.RelativeLayout;

import org.libsdl.app.SDLActivity;

import java.io.File;

// The game: SDL loads libmain.so and runs its SDL_main (runtime/src/main.cpp).
public class WwhdActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // The game files go to Android/data/org.wwhdrecomp.wwhd/files/game (code, content, meta):
        // create the folders so they show up for copying over USB.
        File files = getExternalFilesDir(null);
        if (files != null) {
            new File(files, "game").mkdirs();
            new File(files, "save").mkdirs();
            Log.i("wwhd", "game folder: " + new File(files, "game"));
        }
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // on-screen controls over the game (the button under the view button shows or hides them)
        if (mLayout != null)
            mLayout.addView(new TouchControls(this), new RelativeLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        if (files != null && savedInstanceState == null) offerCrashLog(files);
    }

    private void offerCrashLog(File files) {
        File[] reports = new File(files, "captures").listFiles((dir, name) -> name.startsWith("crash-") && name.endsWith(".log"));
        if (reports == null) return;
        File latest = null;
        for (File report : reports)
            if (report.isFile() && (latest == null || report.lastModified() > latest.lastModified())) latest = report;
        if (latest == null) return;
        final File report = latest;
        final String identity = report.getName() + ":" + report.lastModified();
        if (identity.equals(getPreferences(MODE_PRIVATE).getString("offeredCrash", ""))) return;
        getPreferences(MODE_PRIVATE).edit().putString("offeredCrash", identity).apply();
        new AlertDialog.Builder(this).setTitle("Crash report available")
            .setMessage("Share the latest crash report? It contains diagnostic settings and logs.")
            .setNegativeButton("Later", null).setPositiveButton("Share", (dialog, which) -> {
                try {
                    File folder = new File(getCacheDir(), "crash-share");
                    if (!folder.isDirectory() && !folder.mkdirs()) throw new java.io.IOException("Cannot create share folder");
                    File copy = new File(folder, "crash.log");
                    Files.copy(report.toPath(), copy.toPath(), StandardCopyOption.REPLACE_EXISTING);
                    Uri uri = FileProvider.getUriForFile(this, getPackageName() + ".crashlogs", copy);
                    Intent share = new Intent(Intent.ACTION_SEND).setType("text/plain")
                        .putExtra(Intent.EXTRA_STREAM, uri).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    share.setClipData(ClipData.newRawUri("Crash report", uri));
                    startActivity(Intent.createChooser(share, "Share crash report"));
                } catch (Exception e) { Log.w("wwhd", "Cannot share crash report", e); }
            }).show();
    }

    private static final int GPU_DRIVER_REQUEST = 4972;
    private static native String installGpuDriver(String path);
    public void chooseGpuDriver() {
        runOnUiThread(() -> startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE).setType("*/*"), GPU_DRIVER_REQUEST));
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != GPU_DRIVER_REQUEST || result != RESULT_OK || data == null || data.getData() == null) return;
        final android.net.Uri uri = data.getData();
        // Copy the content URI into internal storage; no broad storage permissions needed.
        new Thread(() -> {
            File zip = null;
            String error = "";
            try {
                zip = File.createTempFile("gpu-driver-", ".zip", getCacheDir());
                try (InputStream in = getContentResolver().openInputStream(uri);
                     FileOutputStream out = new FileOutputStream(zip)) {
                    if (in == null) throw new java.io.IOException("Cannot read selected file");
                    byte[] bytes = new byte[65536]; long total = 0; int count;
                    while ((count = in.read(bytes)) != -1) {
                        total += count;
                        if (total > 512L * 1024 * 1024) throw new java.io.IOException("Driver ZIP exceeds 512 MiB");
                        out.write(bytes, 0, count);
                    }
                }
                error = installGpuDriver(zip.getAbsolutePath());
            } catch (Exception e) { error = e.getMessage(); }
            finally { if (zip != null) zip.delete(); }
            final String problem = error;
            runOnUiThread(() -> new AlertDialog.Builder(this).setTitle(problem == null || problem.isEmpty() ? "Driver installed" : "Driver installation failed")
                .setMessage(problem == null || problem.isEmpty() ? "Select the driver in Graphics, then restart the game." : problem)
                .setPositiveButton("OK", null).show());
        }, "gpu-driver-install").start();
    }

    // Frame interpolation at 120/240 fps (runtime/src/platform/display_rate.cpp, called from native
    // code on any thread). Phones with 90/120 Hz screens often run apps at 60 Hz unless the app asks
    // for more, so the game asks for a display mode of the same resolution with at least the
    // frame rate it draws, and reads back the rate the display actually runs at.

    // the display's current refresh rate (Hz; 0: no display)
    public static float displayRefreshRate() {
        Display d = mSingleton != null ? mSingleton.getDisplay() : null;
        return d != null ? d.getRefreshRate() : 0;
    }

    // the highest refresh rate the display offers at its current resolution
    public static float maxRefreshRate() {
        Display d = mSingleton != null ? mSingleton.getDisplay() : null;
        if (d == null) return 0;
        Display.Mode cur = d.getMode();
        float best = d.getRefreshRate();
        for (Display.Mode m : d.getSupportedModes())
            if (m.getPhysicalWidth() == cur.getPhysicalWidth() && m.getPhysicalHeight() == cur.getPhysicalHeight())
                best = Math.max(best, m.getRefreshRate());
        return best;
    }

    // ask for the display mode (same resolution) with the lowest refresh rate of at least hz, or the
    // highest one if none reaches it; hz <= 0 leaves the choice to the system again
    public static void requestRefreshRate(final float hz) {
        final SDLActivity a = mSingleton;
        if (a == null) return;
        a.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Window w = a.getWindow();
                Display d = a.getDisplay();
                if (w == null || d == null) return;
                int id = 0;
                if (hz > 0) {
                    Display.Mode cur = d.getMode(), pick = null, highest = null;
                    for (Display.Mode m : d.getSupportedModes()) {
                        if (m.getPhysicalWidth() != cur.getPhysicalWidth() || m.getPhysicalHeight() != cur.getPhysicalHeight()) continue;
                        if (highest == null || m.getRefreshRate() > highest.getRefreshRate()) highest = m;
                        if (m.getRefreshRate() >= hz - 1 && (pick == null || m.getRefreshRate() < pick.getRefreshRate())) pick = m;
                    }
                    if (pick == null) pick = highest;
                    if (pick != null) id = pick.getModeId();
                }
                WindowManager.LayoutParams lp = w.getAttributes();
                if (lp.preferredDisplayModeId == id) return;
                lp.preferredDisplayModeId = id;
                w.setAttributes(lp);
                Log.i("wwhd", "display mode " + id + " requested for " + hz + " fps");
            }
        });
    }
}
