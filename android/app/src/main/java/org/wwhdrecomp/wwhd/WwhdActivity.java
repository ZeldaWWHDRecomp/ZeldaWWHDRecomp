package org.wwhdrecomp.wwhd;

import android.os.Bundle;
import android.content.Intent;
import android.app.AlertDialog;
import java.io.FileOutputStream;
import java.io.InputStream;
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
    @Override protected boolean redirectActivityBeforeNativeStart() {
        // Shader preparation and test activities own separate SDL lifetimes.
        // Only the two concrete gameplay entry points participate in routing.
        if (getClass() != WwhdActivity.class && getClass() != QuestActivity.class) return false;
        Intent selected = QuestActivity.gameIntent(this);
        if (selected.getComponent().getClassName().equals(getClass().getName())) return false;
        // Includes explicit intents and shortcuts created before the theater existed.
        // SDL has not acquired any global state, so finishing this Activity cannot quit it.
        Log.i("wwhd", "redirecting game activity to " + selected.getComponent().getClassName());
        startActivity(selected);
        finish();
        return true;
    }

    public boolean useQuestTheater() { return false; }
    public boolean isQuestDevice() { return QuestActivity.isQuest(this); }
    public static String downloadCatalogue(String url, String destination, long limit) {
        return HttpsDownload.download(url, destination, limit);
    }

    // Public thermal/battery APIs, sampled by the overlay/report via JNI at most once a second.
    public static String performanceThermals() { return PerformanceThermals.read(mSingleton); }

    // ---- code mods (runtime/src/mods/code_mods.cpp, guest_build.h; JNI from game threads)
    /** The code-mod build tools file: "ok:" + its path, or "error:" + a message for the player. */
    public static String guestBuildTools() {
        try { return "ok:" + AndroidGuestTools.prepare(mSingleton.getApplicationContext()); }
        catch (Exception failure) {
            Log.e("wwhd-game", "code-mod tools unavailable", failure);
            return "error:Code-mod tools are unavailable: " + failure.getMessage();
        }
    }

    /** Settings > Mods > Use code mods: the setup builds the game code again with (or without) mod
     *  support, in its own process while the game runs. "" or a message for the player. */
    public static String requestCodeModBuild(boolean enabled) {
        try {
            android.content.Context context = mSingleton.getApplicationContext();
            File job = SetupStore.current(context);
            if (job == null || !new File(job, "host.json").isFile() || !new File(AndroidGame.storage(context), "active.json").isFile())
                return "This game was built on a computer: build it there again with code-mod support";
            AndroidGame.chooseCodeMods(context, enabled);
            codeModRequest = System.currentTimeMillis();
            if (AndroidGame.needsRebuild(context)) SetupService.start(context, null);
            return "";
        } catch (Exception failure) {
            Log.e("wwhd-game", "code-mod rebuild not started", failure);
            return "Setup could not start: " + failure.getMessage();
        }
    }

    private static volatile long codeModRequest;

    /** The setup's rebuild for the code-mod choice, as JSON: state ("building", "ready", "failed"),
     *  stage, done, total, error. */
    public static String codeModBuildStatus() {
        try {
            android.content.Context context = mSingleton.getApplicationContext();
            org.json.JSONObject out = new org.json.JSONObject();
            File job = SetupStore.current(context);
            org.json.JSONObject host = job == null || !new File(job, "host.json").isFile() ? new org.json.JSONObject() :
                SetupStore.read(new File(job, "host.json"));
            // a record from before the request (an earlier run's end) is not this rebuild's
            String state = host.optLong("updated") < codeModRequest ? "" : host.optString("state");
            if (!AndroidGame.needsRebuild(context)) out.put("state", "ready");  // the active build has the chosen support
            else if (state.equals("failed")) out.put("state", "failed").put("error", host.optString("error", "Setup failed; open setup for details"));
            else if (state.equals("paused")) out.put("state", "building").put("stage", "Setup paused: open setup to continue");
            else {
                out.put("state", "building").put("stage", "Setup: " + (state.isEmpty() || state.equals("complete") ? "starting" : state));
                File progress = job == null ? null : new File(job, "state.json");
                org.json.JSONObject event = progress != null && progress.isFile() ? SetupStore.read(progress).optJSONObject("last_event") : null;
                if (event != null && event.optString("stage").equals("compile")) {
                    out.put("stage", "Compiling the game code");
                    out.put("done", event.optInt("compiled") + event.optInt("reused")).put("total", event.optInt("total"));
                } else if (event != null && event.optString("stage").equals("translate")) out.put("stage", "Translating the game code");
            }
            return out.toString();
        } catch (Exception failure) {
            return "{\"state\":\"failed\",\"error\":\"Cannot read setup progress\"}";
        }
    }


    @Override protected void onDestroy() {
        if (phoneRumble != null) phoneRumble.setActive(false);
        super.onDestroy();
    }

    private volatile PhoneRumble phoneRumble;
    public static void phoneRumble(int level) {
        if (mSingleton instanceof WwhdActivity) {
            PhoneRumble output = ((WwhdActivity)mSingleton).phoneRumble;
            if (output != null) output.update(level);
        }
    }

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
        // debug builds: WWHD_* intent extras (benchmark starts over adb), set once SDL's JNI is
        // there but before the game library loads, so its static initializers see them too
        System.loadLibrary("SDL3");
        DebugLaunch.apply(this);
        if (onDeviceGame == null) {
            super.loadLibraries();
        } else {
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
        if (isRedirectedActivity()) return;
        if (hasFocus) applyFullScreen();  // also after a dialog, the file picker or another app had the screen
        if (hasFocus && gamePadDisplay != null) gamePadDisplay.focusChanged();
    }

    // SDL changes the window style when the Full screen setting changes (setSystemUiVisibility, which calls
    // this): follow it right away. Bars shown by something else while full screen: hidden again after a moment.
    @Override public void onSystemUiVisibilityChange(int visibility) {
        super.onSystemUiVisibilityChange(visibility);
        if (isRedirectedActivity()) return;
        final boolean barsShown = (visibility & View.SYSTEM_UI_FLAG_HIDE_NAVIGATION) == 0;
        View decor = getWindow().getDecorView();
        decor.removeCallbacks(reapplyFullScreen);
        decor.postDelayed(reapplyFullScreen, mFullscreenModeActive && barsShown && hasWindowFocus() ? 2000 : 0);
    }

    @Override protected void onResume() {
        super.onResume();
        if (isRedirectedActivity()) return;
        if (phoneRumble != null) phoneRumble.setActive(true);
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
        if (phoneRumble != null) phoneRumble.setActive(false);
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
        if (isRedirectedActivity()) return;
        phoneRumble = new PhoneRumble(this);
        // GamePad screen saved Off: known before onResume acquires a second display (AYN Thor, #222)
        if (!mBrokenLibraries && !isReusingNativeSession())
            GamePadDisplay.seedSavedOff(SavedSecondScreen.off(files));
        // heat guard: fan-cooled handhelds pause 60 fps later than phones (#247)
        if (!mBrokenLibraries) {
            try {
                int heat = HeatGuardDevice.publish(android.os.Build.MANUFACTURER, android.os.Build.BRAND, android.os.Build.MODEL);
                Log.i("wwhd", "heat guard device: " + (heat == HeatGuardDevice.FAN_HANDHELD ? "handheld (fan)" : "phone"));
            } catch (UnsatisfiedLinkError e) {
                // an on-device game library from before device classes: the phone limits stay
                Log.w("wwhd", "heat guard device class not passed: " + e.getMessage());
            }
        }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        applyFullScreen();
        // on-screen controls over the game (the button under the view button shows or hides them)
        if (mLayout != null)
            mLayout.addView(new TouchControls(this), new RelativeLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        if (savedInstanceState == null) CrashReports.offer(this);
    }

    public void shareLog() { runOnUiThread(() -> LogShare.share(this)); }

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

    // Native display_rate.cpp requests the surface cadence and reads back the actual display rate.
    // the display's current refresh rate (Hz; 0: no display)
    public static float displayRefreshRate() {
        Display d = mSingleton != null ? mSingleton.getDisplay() : null;
        return d != null ? d.getRefreshRate() : 0;
    }

    /**
     * With more than 60 fps chosen, also prefer a display mode of the current resolution with at
     * least that rate (some vendor policies ignore the surface request alone); 60 or less clears it
     * so the system chooses again. Native display_rate.cpp calls it only when the chosen rate
     * changes, never per surface, so a window attribute change cannot feed back into a new request.
     */
    public static void preferDisplayMode(final int hz) {
        final SDLActivity a = mSingleton;
        if (a == null) return;
        a.runOnUiThread(() -> {
            android.view.Window w = a.getWindow();
            Display d = a.getDisplay();
            if (w == null || d == null) return;
            Display.Mode current = d.getMode();
            Display.Mode[] supported = d.getSupportedModes();
            int[][] modes = new int[supported.length][];
            for (int i = 0; i < supported.length; i++) {
                Display.Mode m = supported[i];
                modes[i] = new int[] {m.getModeId(), m.getPhysicalWidth(), m.getPhysicalHeight(), Math.round(m.getRefreshRate())};
            }
            int id = DisplayModeChoice.pick(modes, current.getPhysicalWidth(), current.getPhysicalHeight(), hz);
            WindowManager.LayoutParams lp = w.getAttributes();
            if (lp.preferredDisplayModeId == id) return;
            lp.preferredDisplayModeId = id;
            w.setAttributes(lp);
            Log.i("wwhd", id != 0 ? "preferred display mode " + id + " for " + hz + " fps" : "preferred display mode cleared");
        });
    }

}
