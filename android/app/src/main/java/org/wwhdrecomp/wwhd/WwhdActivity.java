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
        if (selected.getComponent().getClassName().equals(getClass().getName())) {
            try { SaveSession.enterGame(this); }
            catch (java.io.IOException failure) {
                android.widget.Toast.makeText(this, failure.getMessage(), android.widget.Toast.LENGTH_LONG).show();
                finish();
                return true;
            }
            return false;
        }
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

    /** Settings > Mods > More > Install from a file (runtime/src/platform/android_documents.cpp, game
     *  thread): the picked content:// document copied to the app's cache. "ok:" + path or "error:" + message. */
    public static String copyPickedDocument(String uri, long limit) {
        return PickedDocuments.copy(mSingleton.getApplicationContext(), uri, limit);
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
                return "This game was built on a computer: build it there again " + (enabled ? "with" : "without") + " code-mod support";
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


    // ---- Settings > Randomizer (runtime/src/mods/randomizer_job.cpp; JNI from game threads): randomized games made
    // or imported by the phone's setup while the game runs, the same jobs as the setup screen's Randomized game...
    /** "" when this game can make randomized games, else why not (for the player). */
    public static String randomizerUnavailable() {
        try {
            android.content.Context context = mSingleton.getApplicationContext();
            if (!AndroidRandomizer.available(context)) return "This app has no randomizer.";
            File job = SetupStore.current(context);
            if (job == null || !new File(job, "host.json").isFile() || !new File(AndroidGame.storage(context), "active.json").isFile())
                return "This game was built on a computer: make randomized games there (setup's Randomized games).";
            AndroidGame.Selection current = AndroidGame.selected(context);
            if (current == null || current.game == null) return "Set up the game first.";
            return "";
        } catch (Exception failure) {
            return "The randomizer is unavailable: " + failure.getMessage();
        }
    }

    private static final int RANDOMIZER_SEED_REQUEST = 4973;

    /** {import, preset, seed, permalink}: a new game (its settings, then a setup job) or the folder picker for a seed
     *  from the official randomizer. "" when it started (randomizerStatus follows it), else a message. */
    public static String requestRandomizer(String text) {
        try {
            org.json.JSONObject request = new org.json.JSONObject(text);
            WwhdActivity activity = mSingleton instanceof WwhdActivity ? (WwhdActivity)mSingleton : null;
            android.content.Context context = mSingleton.getApplicationContext();
            if (!AndroidRandomizer.replaceable(SetupStore.current(context)))
                return "Setup is running: wait for it, or pause it in the setup screen.";
            if (request.optBoolean("import")) {
                if (activity == null) return "The folder picker is unavailable here.";
                AndroidRandomizer.picking();
                activity.runOnUiThread(() -> {
                    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                    for (java.util.Map.Entry<String, Object> extra : SetupPicker.extras(true).entrySet()) {
                        if (extra.getValue() instanceof Boolean) intent.putExtra(extra.getKey(), (Boolean)extra.getValue());
                        else intent.putExtra(extra.getKey(), android.net.Uri.parse((String)extra.getValue()));
                    }
                    intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                    try { activity.startActivityForResult(intent, RANDOMIZER_SEED_REQUEST); }
                    catch (Exception failure) { AndroidRandomizer.requested(null, "No folder picker: " + failure.getMessage(), true); }
                });
                return "";
            }
            String preset = request.optString("preset", "standard"), seed = request.optString("seed"), permalink = request.optString("permalink");
            // (the settings commands and the job: off the game thread, which draws the page meanwhile)
            AndroidRandomizer.starting();  // (until the job exists)
            new Thread(() -> {
                try {
                    String id = AndroidRandomizer.makeJob(context, preset, seed.trim(), permalink.trim());
                    AndroidRandomizer.requested(id, "", false);
                    SetupService.start(context, SetupService.RESUME);
                } catch (Exception failure) {
                    Log.e("wwhd-game", "randomized game not started", failure);
                    AndroidRandomizer.requested(null, AndroidRandomizer.message(failure), false);
                }
            }, "wwhd-rando-make").start();
            return "";
        } catch (Exception failure) {
            Log.e("wwhd-game", "randomizer request refused", failure);
            return "Setup could not start: " + failure.getMessage();
        }
    }

    /** The page's request as JSON (AndroidRandomizer.status, RandomizerProgress): state, stage, done, total, error,
     *  seed, hash, imported. */
    public static String randomizerStatus() { return AndroidRandomizer.status(mSingleton.getApplicationContext()); }

    private void randomizerSeedChosen(int result, Intent data) {
        if (result != RESULT_OK || data == null || data.getData() == null) {
            AndroidRandomizer.requested(null, "", true);  // closed without a folder
            return;
        }
        final android.net.Uri uri = data.getData();
        final android.content.Context context = getApplicationContext();
        new Thread(() -> {
            try {
                String id = AndroidRandomizer.importJob(context, uri);
                AndroidRandomizer.requested(id, "", true);
                SetupService.start(context, SetupService.RESUME);
            } catch (Exception failure) {
                Log.e("wwhd-game", "seed import not started", failure);
                AndroidRandomizer.requested(null, AndroidRandomizer.message(failure), true);
            }
        }, "wwhd-rando-import").start();
    }

    @Override protected void onDestroy() {
        if (phoneRumble != null) phoneRumble.setActive(false);
        super.onDestroy();
    }

    private volatile PhoneRumble phoneRumble;
    private MemoryPressure memoryPressure;  // UI thread (lifecycle callbacks)
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
        if (getClass() == WwhdActivity.class || getClass() == QuestActivity.class) {
            try { SaveSession.enterGame(this); }
            catch (java.io.IOException failure) { throw new IllegalStateException(failure.getMessage(), failure); }
        }
        if (isReusingNativeSession()) {
            // A setup update must not change the loaded engine or assets mid-session.
            selection = sessionSelection;
            onDeviceGame = sessionLibrary;
            return;
        }
        selection = AndroidGame.selected(this);
        onDeviceGame = selection != null ? selection.library : null;
        if (getClass() == WwhdActivity.class || getClass() == QuestActivity.class) gameStarted = true;
        // debug builds: WWHD_* intent extras (benchmark starts over adb), set once SDL's JNI is
        // there but before the game library loads, so its static initializers see them too
        System.loadLibrary("SDL3");
        DebugLaunch.apply(this);
        applyLaunchEnvironment();
        if (onDeviceGame == null) {
            super.loadLibraries();
        } else {
            System.load(onDeviceGame.getAbsolutePath());
            Log.i("wwhd-game", "Loaded on-device game " + onDeviceGame.getAbsolutePath());
            if (selection.game != null) Log.i("wwhd-game", "Selected game assets " + selection.game.getAbsolutePath());
        }
        if (getClass() == WwhdActivity.class || getClass() == QuestActivity.class) {
            boolean deferred;
            try { deferred = supportsSaveProfilePreparation(); }
            catch (UnsatisfiedLinkError oldLibrary) { deferred = false; }
            if (!deferred) {
                // Older on-device native libraries cannot declare save_profile;
                // keep their ordinary vanilla import recovery working.
                try { SaveSession.prepareLegacyGame(); }
                catch (java.io.IOException failure) { throw new IllegalStateException(failure.getMessage(), failure); }
            }
        }
        sessionSelection = selection;
        sessionLibrary = onDeviceGame;
    }

    private static native boolean supportsSaveProfilePreparation();
    /** The parts of the game's view the system does not show (ScreenCover), in its pixels, and its size (issue #349). */
    private static native void publishObscured(int left, int top, int right, int bottom, int viewWidth, int viewHeight);
    /** One line for the game's log: what the system reports about the screen around the game's view (issue #349). */
    private static native void publishScreenReport(String report);
    private boolean obscuredUnsupported;  // an on-device game library from before publishObscured
    private boolean screenReportUnsupported;  // ... from before publishScreenReport
    private String lastScreenReport;
    private int surfaceBufferWidth, surfaceBufferHeight;  // the game surface's buffer (surfaceChanged), 0 before

    // Issue #349: on a Retroid Pocket Nova (Android 13, 1280x960) the bottom 48 rows of the screen stay black over
    // the game; with Match window or 4:3 the picture fills the screen and the rupee counter sits in those rows.
    // The game draws them (the same picture on the Mac shows the counter at rows 911-927), so the part the system
    // does not show goes to the TV layout, which places the pictures and Match window's shape in the rest
    // (ScreenCover): the system bars actually shown over the view (getInsets: hidden and swipe-revealed transient
    // bars count as none), a display cutout along a whole edge, and the window's bounds ending before the view.
    // Everything the system reports about it goes to the game's log once per change, for devices that still
    // cover part of the game without reporting it.
    private void reportObscured() {
        if (obscuredUnsupported || mSurface == null) return;
        WindowInsets insets = mSurface.getRootWindowInsets();
        int w = mSurface.getWidth(), h = mSurface.getHeight();
        if (insets == null || w <= 0 || h <= 0) return;
        android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
        int[] at = new int[2], onScreen = new int[2];
        mSurface.getLocationInWindow(at);
        mSurface.getLocationOnScreen(onScreen);
        View root = mSurface.getRootView();
        int[] shown = {Math.max(0, bars.left - at[0]), Math.max(0, bars.top - at[1]),
                Math.max(0, bars.right - (root.getWidth() - at[0] - w)),
                Math.max(0, bars.bottom - (root.getHeight() - at[1] - h))};
        // the cutout's rects come in window coordinates: moved into the view's
        android.view.DisplayCutout cutout = insets.getDisplayCutout();
        java.util.List<android.graphics.Rect> rects = cutout != null ? cutout.getBoundingRects() : java.util.Collections.emptyList();
        int[][] cut = new int[rects.size()][];
        for (int i = 0; i < cut.length; i++) {
            android.graphics.Rect r = rects.get(i);
            cut[i] = new int[] {r.left - at[0], r.top - at[1], r.right - at[0], r.bottom - at[1]};
        }
        android.graphics.Rect bounds = null, display = null;
        try { bounds = getWindowManager().getCurrentWindowMetrics().getBounds(); } catch (RuntimeException ignored) { }
        try { display = getWindowManager().getMaximumWindowMetrics().getBounds(); } catch (RuntimeException ignored) { }
        // the window's part of the display: with FLAG_LAYOUT_NO_LIMITS (Full screen) it may reach past the edge
        int[] window = ScreenCover.onDisplay(bounds != null ? new int[] {bounds.left, bounds.top, bounds.right, bounds.bottom} : null,
                display != null ? new int[] {display.left, display.top, display.right, display.bottom} : null);
        int[] c = ScreenCover.covered(w, h, shown, cut, onScreen, window);
        reportScreen(insets, w, h, onScreen, root, rects, bounds, c);
        try {
            publishObscured(c[0], c[1], c[2], c[3], w, h);
        } catch (UnsatisfiedLinkError e) {
            obscuredUnsupported = true;
            Log.w("wwhd", "system bar area not passed: " + e.getMessage());
        }
    }
    private static String box(android.graphics.Insets i) { return i.left + "," + i.top + "," + i.right + "," + i.bottom; }
    private void reportScreen(WindowInsets insets, int w, int h, int[] onScreen, View root,
            java.util.List<android.graphics.Rect> cutouts, android.graphics.Rect window, int[] covered) {
        StringBuilder s = new StringBuilder();
        s.append("view ").append(w).append('x').append(h).append(" at ").append(onScreen[0]).append(',').append(onScreen[1])
         .append(", root ").append(root.getWidth()).append('x').append(root.getHeight())
         .append(", window ").append(window != null ? window.toShortString() : "?");
        try {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            s.append(" (cutout mode ").append(lp.layoutInDisplayCutoutMode).append(')');
            Display d = getWindow().getDecorView().getDisplay();
            if (d != null) {
                Display.Mode m = d.getMode();
                s.append(", display mode ").append(m.getPhysicalWidth()).append('x').append(m.getPhysicalHeight());
                android.graphics.Rect max = getWindowManager().getMaximumWindowMetrics().getBounds();
                s.append(", largest window ").append(max.toShortString());
            }
        } catch (RuntimeException ignored) { }
        s.append("; bars shown ").append(box(insets.getInsets(WindowInsets.Type.systemBars())))
         .append(", bars when shown ").append(box(insets.getInsetsIgnoringVisibility(WindowInsets.Type.systemBars())))
         .append(", tappable ").append(box(insets.getInsets(WindowInsets.Type.tappableElement())))
         .append(", gestures ").append(box(insets.getInsets(WindowInsets.Type.mandatorySystemGestures())))
         .append(", cutout ").append(box(insets.getInsets(WindowInsets.Type.displayCutout())));
        for (android.graphics.Rect r : cutouts) s.append(' ').append(r.toShortString());
        android.view.DisplayCutout cut = insets.getDisplayCutout();
        if (cut != null) s.append(", waterfall ").append(box(cut.getWaterfallInsets()));
        // What the game really gets (#349: does the surface reach rows 912-959 of the Nova's screen?): the surface
        // view's rectangle on the screen and its buffer's size (the swapchain's), the part of the screen the system
        // says the window can be seen in, the stable insets (the bars' space whether shown or not), and the
        // full-screen state with the flags it set and the system's navigation mode (0 three buttons, 1 two
        // buttons, 2 gestures; not a public setting, -1 when unknown).
        s.append("; surface on screen [").append(onScreen[0]).append(',').append(onScreen[1]).append("][")
         .append(onScreen[0] + w).append(',').append(onScreen[1] + h).append("], buffer ")
         .append(surfaceBufferWidth).append('x').append(surfaceBufferHeight);
        android.graphics.Rect visible = new android.graphics.Rect();
        mSurface.getWindowVisibleDisplayFrame(visible);
        s.append(", visible frame ").append(visible.toShortString())
         .append(", stable ").append(insets.getStableInsetLeft()).append(',').append(insets.getStableInsetTop()).append(',')
         .append(insets.getStableInsetRight()).append(',').append(insets.getStableInsetBottom());
        Window win = getWindow();
        s.append(", full screen ").append(mFullscreenModeActive ? "on" : "off")
         .append(" (ui flags 0x").append(Integer.toHexString(win.getDecorView().getSystemUiVisibility()))
         .append(", window flags 0x").append(Integer.toHexString(win.getAttributes().flags)).append(')');
        int navigationMode = -1;
        try { navigationMode = android.provider.Settings.Secure.getInt(getContentResolver(), "navigation_mode", -1); }
        catch (RuntimeException ignored) { }
        s.append(", navigation mode ").append(navigationMode);
        s.append("; left out ").append(covered[0]).append(',').append(covered[1]).append(',').append(covered[2])
         .append(',').append(covered[3]);
        String line = s.toString();
        if (line.equals(lastScreenReport)) return;
        lastScreenReport = line;
        Log.i("wwhd", "screen: " + line);
        if (screenReportUnsupported) return;
        try {
            publishScreenReport(line);
        } catch (UnsatisfiedLinkError e) {
            screenReportUnsupported = true;
        }
    }

    /** Native main calls this only after freezing the trusted save-profile plan. */
    public static String prepareSelectedGameSave(String directory) {
        try {
            SaveSession.prepareSelectedGameSave(directory);
            return "ok";
        } catch (java.io.IOException failure) {
            return "error:" + failure.getMessage();
        }
    }

    private static volatile boolean gameStarted;
    /** The game itself (not setup's preparation activities, which have processes of their own) runs in this
     *  process: setup's models step must not write the shader caches meanwhile. */
    public static boolean gameLoaded() { return gameStarted; }

    /** Options for this start of the game, set before its library loads (setup's models step). */
    protected void applyLaunchEnvironment() { }
    /** The GamePad picture on a second display (setup's models step: nothing is shown). */
    protected boolean usesSecondScreen() { return true; }
    /** The share offer for the last crash report when the game starts. */
    protected boolean offersCrashReports() { return true; }

    @Override protected String getMainSharedObject() {
        return onDeviceGame != null ? onDeviceGame.getAbsolutePath() : super.getMainSharedObject();
    }

    protected File shaderUserDirectory() { return AndroidUserDirectories.config(getExternalFilesDir(null)); }

    @Override protected String[] getArguments() {
        java.util.ArrayList<String> args = new java.util.ArrayList<>();
        if (selection != null && selection.game != null) {
            args.add("--game"); args.add(selection.game.getAbsolutePath());
            args.add("--shader-prebuild-library"); args.add(selection.library.getAbsolutePath());
            // a randomized game (the in-app randomizer): its changed files over the game
            if (selection.randomizerFiles != null) { args.add("--randomizer-files"); args.add(selection.randomizerFiles.getAbsolutePath()); }
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

    // SDL's full-screen window style (SDLActivity COMMAND_CHANGE_WINDOW_STYLE and its rehideSystemUi): the same
    // flags, so neither side sees a change the other made and starts hiding the bars again
    private static final int IMMERSIVE_UI_FLAGS = View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.INVISIBLE;
    // the window as it was before Full screen's additions below (put back when the setting is turned off)
    private boolean fullScreenWindowStyled;
    private int windowNavigationBarColor;
    private boolean windowNavigationBarContrast;

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
            // Issue #349: on a Retroid Pocket Nova (Android 13, likely three-button navigation) the navigation bar
            // reports as hidden and the view spans all 960 rows, yet the bottom 48 (the bar's height) stay black.
            // Some handheld ROMs keep the bar's space unless the window asks for it the ways emulators do:
            if (!fullScreenWindowStyled) {
                windowNavigationBarColor = w.getNavigationBarColor();
                windowNavigationBarContrast = w.isNavigationBarContrastEnforced();
                fullScreenWindowStyled = true;
            }
            // the window may be laid out over the whole display, the navigation bar's space included
            w.addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
            // nothing drawn for the bar's space: no bar colour and no contrast scrim (API 29+) over the game
            w.setNavigationBarColor(android.graphics.Color.TRANSPARENT);
            w.setNavigationBarContrastEnforced(false);
            // the old full-screen flags besides the WindowInsetsController (syncSystemBars): systems that still go
            // by them hide and give up the bars only then. Only while one of them is missing (the system clears
            // them when it shows the bars; onSystemUiVisibilityChange follows a change, so no set-and-follow loop)
            View decor = w.getDecorView();
            if ((decor.getSystemUiVisibility() & IMMERSIVE_UI_FLAGS) != IMMERSIVE_UI_FLAGS) decor.setSystemUiVisibility(IMMERSIVE_UI_FLAGS);
        } else if (fullScreenWindowStyled) {
            // Full screen turned off: the window as before (SDL already set its own flags for a window with bars)
            fullScreenWindowStyled = false;
            w.clearFlags(WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
            w.setNavigationBarColor(windowNavigationBarColor);
            w.setNavigationBarContrastEnforced(windowNavigationBarContrast);
        }
        syncSystemBars(w);
        if (gamePadDisplay != null) gamePadDisplay.syncSystemBars();
        // the game's log: what the screen looks like with this applied (once per change, reportScreen)
        if (mSurface != null && !mBrokenLibraries) w.getDecorView().post(this::reportObscured);
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
        if (memoryPressure != null) memoryPressure.start();
        applyFullScreen();
        // Only start after SDL successfully loaded the native game library.
        if (!mBrokenLibraries && usesSecondScreen()) {
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
        if (memoryPressure != null) memoryPressure.stop();
        if (gamePadDisplay != null) gamePadDisplay.stop();
        super.onPause();
    }
    @Override public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        if (memoryPressure != null) memoryPressure.onTrimMemory(level);
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
            // Native startup selects the save root before SaveSession prepares it under the lease.
            Log.i("wwhd", "game folder: " + new File(files, "game"));
        }
        super.onCreate(savedInstanceState);
        if (isRedirectedActivity()) return;
        phoneRumble = new PhoneRumble(this);
        // low memory: the renderer frees what it can make again (onTrimMemory below, and a free-memory check)
        if (!mBrokenLibraries) memoryPressure = new MemoryPressure(this);
        // GamePad screen saved Off: known before onResume acquires a second display (AYN Thor, #222)
        if (!mBrokenLibraries && !isReusingNativeSession() && usesSecondScreen())
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
        if (!mBrokenLibraries && mSurface != null && mLayout != null) {
            // the bars over the game: after each insets change (bars shown/hidden) and size change
            mLayout.setOnApplyWindowInsetsListener((v, insets) -> {
                v.post(this::reportObscured);
                return v.onApplyWindowInsets(insets);
            });
            mSurface.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> reportObscured());
            // ... and its buffer's size (the swapchain's) in the game's log after each change (#349)
            mSurface.getHolder().addCallback(new android.view.SurfaceHolder.Callback() {
                @Override public void surfaceCreated(android.view.SurfaceHolder holder) { }
                @Override public void surfaceChanged(android.view.SurfaceHolder holder, int format, int width, int height) {
                    surfaceBufferWidth = width;
                    surfaceBufferHeight = height;
                    if (mSurface != null) mSurface.post(WwhdActivity.this::reportObscured);
                }
                @Override public void surfaceDestroyed(android.view.SurfaceHolder holder) { }
            });
        }
        // on-screen controls over the game (the button under the view button shows or hides them)
        if (mLayout != null)
            mLayout.addView(new TouchControls(this), new RelativeLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        if (savedInstanceState == null && offersCrashReports()) CrashReports.offer(this);
    }

    public void shareLog() { runOnUiThread(() -> LogShare.share(this)); }
    /** Settings > Bug reports > Share log, with "Include my save files" (off by default; names made "Link") */
    public void shareLogWithSaves(boolean saves) { runOnUiThread(() -> LogShare.share(this, saves)); }

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
    TouchControls touchControls;  // set when it is attached (its layouts' export and import files)
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == TouchControls.EXPORT_REQUEST || request == TouchControls.IMPORT_REQUEST) {
            if (touchControls != null) touchControls.layoutFile(request, result == RESULT_OK && data != null ? data.getData() : null);
            return;
        }
        if (request == RANDOMIZER_SEED_REQUEST) { randomizerSeedChosen(result, data); return; }
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
