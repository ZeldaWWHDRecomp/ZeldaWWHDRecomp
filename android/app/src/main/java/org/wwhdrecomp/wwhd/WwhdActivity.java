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
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.RelativeLayout;

import org.libsdl.app.SDLActivity;

import java.io.File;

// The game: SDL loads libmain.so and runs its SDL_main (runtime/src/main.cpp).
public class WwhdActivity extends SDLActivity {
    public static String downloadCatalogue(String url, String destination, long limit) {
        return HttpsDownload.download(url, destination, limit);
    }

    // Public thermal/battery APIs, sampled by the overlay/report via JNI at most once a second.
    public static String performanceThermals() { return PerformanceThermals.read(mSingleton); }


    protected GamePadDisplay gamePadDisplay;
    private File onDeviceGame;
    private AndroidGame.Selection selection;
    private static AndroidGame.Selection sessionSelection;
    private static File sessionLibrary;

    @Override protected boolean retainNativeSessionOnRecreation() { return true; }

    @Override public void loadLibraries() {
        if (isReusingNativeSession()) {
            // A setup update must not change the loaded engine or assets mid-session.
            selection = sessionSelection;
            onDeviceGame = sessionLibrary;
            return;
        }
        selection = AndroidGame.selected(this);
        onDeviceGame = selection != null ? selection.library : null;
        if (onDeviceGame == null) {
            super.loadLibraries();
        } else {
            System.loadLibrary("SDL3");
            System.load(onDeviceGame.getAbsolutePath());
            Log.i("wwhd-game", "Loaded on-device game " + onDeviceGame.getAbsolutePath());
            if (selection.game != null) Log.i("wwhd-game", "Selected game assets " + selection.game.getAbsolutePath());
        }
        sessionSelection = selection;
        sessionLibrary = onDeviceGame;
    }

    @Override protected String getMainSharedObject() {
        return onDeviceGame != null ? onDeviceGame.getAbsolutePath() : super.getMainSharedObject();
    }

    protected File shaderUserDirectory() { return AndroidUserDirectories.config(getExternalFilesDir(null)); }

    @Override protected String[] getArguments() {
        java.util.ArrayList<String> args = new java.util.ArrayList<>();
        if (selection != null && selection.game != null) {
            args.add("--game"); args.add(selection.game.getAbsolutePath());
            args.add("--shader-prebuild-library"); args.add(selection.library.getAbsolutePath());
        }
        // Keep devel's external config folder; no implicit WWHD_USER_DIR override.
        return args.toArray(new String[0]);
    }

    // Settings › Display › Full screen on Android: hide the system bars, also the space of the status bar
    // (adapted from GreenNaugahyde's MainActivity.hideSystemBars, MPL-2.0). SDL asks for full screen with the
    // old setSystemUiVisibility flags only, which on Android 11+ leave the navigation bar's space reserved
    // (AYN Odin 2 Portal: status bar space at the top, navigation bar over the bottom of the picture). With
    // the setting on, a swipe from the edge shows the bars for a moment; with it off, they stay visible.
    static void syncSystemBars(Window w) {
        WindowInsetsController c = w != null ? w.getInsetsController() : null;
        if (c == null) return;
        if (mFullscreenModeActive) {
            c.hide(WindowInsets.Type.systemBars());
            c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        } else {
            c.show(WindowInsets.Type.systemBars());
        }
    }

    private void applyFullScreen() {
        Window w = getWindow();
        if (w == null) return;
        w.setDecorFitsSystemWindows(!mFullscreenModeActive);
        if (mFullscreenModeActive) {
            // into a display cutout on the short edges too (SDL's window style sets ALWAYS, which includes them)
            WindowManager.LayoutParams lp = w.getAttributes();
            if (lp.layoutInDisplayCutoutMode != WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES &&
                lp.layoutInDisplayCutoutMode != WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS) {
                lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                w.setAttributes(lp);
            }
        }
        syncSystemBars(w);
        if (gamePadDisplay != null) gamePadDisplay.syncSystemBars();
    }

    private final Runnable reapplyFullScreen = this::applyFullScreen;

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) applyFullScreen();  // also after a dialog, the file picker or another app had the screen
        if (hasFocus && gamePadDisplay != null) gamePadDisplay.focusChanged();
    }

    // SDL changes the window style when the Full screen setting changes (setSystemUiVisibility, which calls
    // this): follow it right away. Bars shown by something else while full screen: hidden again after a moment.
    @Override public void onSystemUiVisibilityChange(int visibility) {
        super.onSystemUiVisibilityChange(visibility);
        final boolean barsShown = (visibility & View.SYSTEM_UI_FLAG_HIDE_NAVIGATION) == 0;
        View decor = getWindow().getDecorView();
        decor.removeCallbacks(reapplyFullScreen);
        decor.postDelayed(reapplyFullScreen, mFullscreenModeActive && barsShown && hasWindowFocus() ? 2000 : 0);
    }

    @Override protected void onResume() {
        super.onResume();
        applyFullScreen();
        // Only start after SDL successfully loaded the native game library.
        if (!mBrokenLibraries) {
            if (gamePadDisplay == null) gamePadDisplay = new GamePadDisplay(this, mLayout, mSurface);
            gamePadDisplay.start();
        }
    }

    public void refreshGamePadDisplay() {
        runOnUiThread(() -> {
            if (gamePadDisplay != null) gamePadDisplay.settingChanged();
        });
    }

    /** Render-thread notification; surface replacement always belongs to the UI thread. */
    public void recoverGamePadSurface(long generation) {
        runOnUiThread(() -> {
            if (gamePadDisplay != null) gamePadDisplay.recoverSurface(generation);
        });
    }

    @Override protected void onPause() {
        if (gamePadDisplay != null) gamePadDisplay.stop();
        super.onPause();
    }
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
        // GamePad screen saved Off: known before onResume acquires a second display (AYN Thor, #222)
        if (!mBrokenLibraries && !isReusingNativeSession())
            GamePadDisplay.seedSavedOff(SavedSecondScreen.off(files));
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        applyFullScreen();
        // on-screen controls over the game (the button under the view button shows or hides them)
        if (mLayout != null)
            mLayout.addView(new TouchControls(this), new RelativeLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        if (files != null && savedInstanceState == null) offerCrashLog(files);
    }

    public void shareLog() { runOnUiThread(() -> LogShare.share(this)); }

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
        syncSystemBars(new AlertDialog.Builder(this).setTitle("Crash report available")
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
            }).show().getWindow());
    }

    private static final int GPU_DRIVER_REQUEST = 4972;
    private static native String installGpuDriver(String path, boolean select);
    // afterFailure: the renderer could not start with the system driver (renderer.cpp), so the
    // installed driver is selected and the app ends; it runs from the next start
    private boolean driverAfterFailure;
    public void chooseGpuDriver(boolean afterFailure) {
        driverAfterFailure = afterFailure;
        runOnUiThread(() -> startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE).setType("*/*"), GPU_DRIVER_REQUEST));
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != GPU_DRIVER_REQUEST) return;
        final boolean afterFailure = driverAfterFailure;
        if (result != RESULT_OK || data == null || data.getData() == null) {
            if (afterFailure) endApp();
            return;
        }
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
                error = installGpuDriver(zip.getAbsolutePath(), afterFailure);
            } catch (Exception e) { error = e.getMessage(); }
            finally { if (zip != null) zip.delete(); }
            final String problem = error;
            final boolean ok = problem == null || problem.isEmpty();
            runOnUiThread(() -> syncSystemBars(new AlertDialog.Builder(this).setTitle(ok ? "Driver installed" : "Driver installation failed")
                .setMessage(!ok ? problem : afterFailure ? "The game uses it from the next start: open the game again."
                                                         : "Select the driver in Graphics, then restart the game.")
                .setCancelable(!afterFailure)
                .setPositiveButton("OK", afterFailure ? (dialog, which) -> endApp() : null).show().getWindow()));
        }, "gpu-driver-install").start();
    }

    private void endApp() {
        finishAndRemoveTask();
        System.exit(0);
    }

    // Settings > Quit Game (native code, after the caches are written): close the game and remove it
    // from the recent apps, as after a driver install. False: no activity to close (native code exits).
    public static boolean quitApp() {
        final SDLActivity a = mSingleton;
        if (!(a instanceof WwhdActivity)) return false;
        a.runOnUiThread(() -> ((WwhdActivity) a).endApp());
        return true;
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
