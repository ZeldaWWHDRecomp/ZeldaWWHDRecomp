package org.wwhdrecomp.wwhd;

import android.content.Context;
import android.content.Intent;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.RelativeLayout;
import android.widget.TextView;
import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Iterator;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * One attempt of setup's models step (ModelsStep, tools/android/android_models.py): the game in its own process
 * (:modelprep), started by the setup screen with the step's options, drawing the sweep hidden
 * (WWHD_HIDDEN_WINDOWS: the game's own render targets exactly as in play, nothing presented). The player sees
 * only setup's progress screen over it: the phase or stage, the bar, the time left and Cancel. The game ends the
 * process itself (an orderly quit that writes the shader caches, or a model that stops it); the setup screen
 * underneath then starts the next attempt.
 */
public final class ModelPrepareActivity extends WwhdActivity {
    private static final String TAG = "wwhd-models";
    private final Handler handler = new Handler(Looper.getMainLooper());
    private File job;
    private String attempt;
    private JSONObject request;
    private boolean quitting;
    private boolean stopping;
    private TextView stage, detail;
    private ProgressBar bar;
    private Button cancel;

    static Intent intent(Context context, File job, String attempt) {
        return new Intent(context, ModelPrepareActivity.class).putExtra("job", job.getAbsolutePath())
            .putExtra("attempt", attempt).addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION);
    }

    /** Not the requested attempt (an old start, a second tap): this activity starts nothing. */
    @Override protected boolean redirectActivityBeforeNativeStart() {
        try {
            String path = getIntent().getStringExtra("job");
            attempt = getIntent().getStringExtra("attempt");
            job = path == null ? null : new File(path).getCanonicalFile();
            if (job == null || !ModelsStep.validId(attempt)) throw new IllegalStateException("no attempt");
            request = SetupStore.read(new File(job, ModelsStep.ATTEMPT));
            if (!attempt.equals(request.optString("id"))) throw new IllegalStateException("attempt no longer requested");
            JSONObject status = new File(job, ModelsStep.STATUS).isFile() ? SetupStore.read(new File(job, ModelsStep.STATUS)) : null;
            if (status != null && attempt.equals(status.optString("id"))) throw new IllegalStateException("attempt started already");
            for (String key : new String[] {"cwd", "stdout"})
                if (!ModelsStep.inside(new File(request.getString(key)).getCanonicalPath(), job.getPath()))
                    throw new IllegalStateException("attempt files outside the setup job");
            writeStatus("running");
            Log.i(TAG, "attempt " + attempt + " starting");
            return false;
        } catch (Exception stale) {
            Log.w(TAG, "not starting: " + stale.getMessage());
            finish();
            overridePendingTransition(0, 0);
            return true;
        }
    }

    private void writeStatus(String state) {
        try {
            SetupStore.write(new File(job, ModelsStep.STATUS), new JSONObject().put("schema", 1).put("id", attempt)
                .put("pid", android.os.Process.myPid()).put("state", state));
        } catch (Exception failure) { Log.w(TAG, "cannot write the attempt's status", failure); }
    }

    /** The step's options for the game, set before its library loads (WwhdActivity.loadLibraries). */
    @Override protected void applyLaunchEnvironment() {
        try {
            JSONObject env = request.getJSONObject("env");
            for (Iterator<String> keys = env.keys(); keys.hasNext();) {
                String key = keys.next();
                if (ModelsStep.gameOption(key)) org.libsdl.app.SDLActivity.nativeSetenv(key, env.getString(key));
            }
            // its output (WWHD-PROGRESS lines) for the step's Python; its own working folder (log, crash reports)
            org.libsdl.app.SDLActivity.nativeSetenv("WWHD_STDOUT_FILE", request.getString("stdout"));
            org.libsdl.app.SDLActivity.nativeSetenv("WWHD_CWD", request.getString("cwd"));
        } catch (Exception failure) {
            throw new IllegalStateException("models step options unreadable", failure);
        }
    }

    @Override protected String[] getArguments() {
        ArrayList<String> args = new ArrayList<>(Arrays.asList(super.getArguments()));
        JSONArray requested = request.optJSONArray("args");
        for (int i = 0; requested != null && i + 1 < requested.length(); i++) {
            String name = requested.optString(i);
            // the attempt's empty save folder (the title screen); the game folder the step read its lists from
            // (also when this app runs its built-in game library: the attempt's working folder is not the app's)
            if (name.equals("--save") || (name.equals("--game") && !args.contains("--game"))) {
                args.add(name);
                args.add(requested.optString(++i));
            }
        }
        return args.toArray(new String[0]);
    }

    @Override protected boolean usesSecondScreen() { return false; }
    @Override protected boolean offersCrashReports() { return false; }  // a model that stops the game is expected here
    @Override protected boolean retainNativeSessionOnRecreation() { return false; }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (isRedirectedActivity() || mLayout == null) return;
        mLayout.addView(progressScreen(), new RelativeLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        handler.post(poll);
    }

    /** Setup's progress screen (as SetupActivity shows the step), opaque over the game's surface. */
    private View progressScreen() {
        Context theme = new android.view.ContextThemeWrapper(this, android.R.style.Theme_DeviceDefault_NoActionBar);
        android.util.TypedValue background = new android.util.TypedValue();
        theme.getTheme().resolveAttribute(android.R.attr.colorBackground, background, true);
        android.util.DisplayMetrics metrics = getResources().getDisplayMetrics();
        int padding = (int)(24 * metrics.density), gap = (int)(12 * metrics.density);
        LinearLayout layout = new LinearLayout(theme);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(padding, padding, padding, padding);
        layout.setBackgroundColor(background.type >= android.util.TypedValue.TYPE_FIRST_COLOR_INT &&
            background.type <= android.util.TypedValue.TYPE_LAST_COLOR_INT ? (background.data | 0xFF000000) : 0xFF000000);
        layout.setClickable(true);  // the game underneath gets no touches
        TextView title = new TextView(theme);
        title.setText(R.string.app_name);
        title.setTextSize(28);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setTextColor(0xFFFFFFFF);
        layout.addView(title);
        stage = new TextView(theme);
        stage.setTextSize(20);
        stage.setTextColor(0xFFE8EEF2);
        stage.setPadding(0, gap, 0, 0);
        stage.setText(R.string.setup_models_running);
        layout.addView(stage);
        bar = new ProgressBar(theme, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        bar.setIndeterminate(true);
        bar.setPadding(0, gap, 0, 0);
        layout.addView(bar);
        detail = new TextView(theme);
        detail.setTextSize(16);
        detail.setPadding(0, gap / 2, 0, gap);
        layout.addView(detail);
        cancel = new Button(theme);
        cancel.setText(R.string.setup_cancel);
        cancel.setOnClickListener(view -> cancelStep());
        layout.addView(cancel);
        TextView note = new TextView(theme);
        note.setText(R.string.setup_models_hidden);
        note.setTextSize(14);
        note.setPadding(0, gap, 0, 0);
        layout.addView(note);
        android.widget.ScrollView scroll = new android.widget.ScrollView(theme);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(0xFF000000 | background.data);
        scroll.addView(layout);
        return scroll;
    }

    /** Cancel (and Back): the step's Python stops this attempt in order and keeps what was prepared. */
    private void cancelStep() {
        if (stopping) return;
        stopping = true;
        cancel.setEnabled(false);
        stage.setText(R.string.setup_models_stopping);
        new Thread(() -> {
            try { SetupStore.write(new File(job, ModelsStep.CANCEL), new JSONObject().put("schema", 1)); }
            catch (Exception failure) { Log.e(TAG, "cannot record Cancel", failure); }
        }, "wwhd-models-cancel").start();
        // the setup process asks for the orderly quit (it must know the stop was asked for, not a crash); should
        // it not, the game quits after a while anyway
        handler.postDelayed(() -> quit("no stop request after Cancel"), 15000);
    }

    @Override public void onBackPressed() {
        if (isRedirectedActivity()) { super.onBackPressed(); return; }
        cancelStep();
    }

    /** The game quits as from Settings › Quit Game (it writes the shader caches, then ends this process). */
    private void quit(String why) {
        if (quitting) return;
        quitting = true;
        Log.i(TAG, "attempt " + attempt + ": quitting (" + why + ")");
        org.libsdl.app.SDLActivity.nativeSendQuit();
        handler.postDelayed(() -> {
            Log.w(TAG, "attempt " + attempt + ": no orderly quit; ending the process");
            android.os.Process.killProcess(android.os.Process.myPid());
        }, 25000);
    }

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            try {
                File stop = new File(job, ModelsStep.STOP);
                if (stop.isFile() && attempt.equals(SetupStore.read(stop).optString("id"))) quit("stop requested");
                File progress = new File(job, ModelsStep.PROGRESS);
                if (progress.isFile() && !stopping) {
                    JSONObject value = SetupStore.read(progress);
                    String[] text = texts(ModelPrepareActivity.this, value);
                    if (!text[0].contentEquals(detail.getText())) detail.setText(text[0]);
                    int position = ModelsStep.bar(value.optDouble("fraction", Double.NaN));
                    if (position < 0) bar.setIndeterminate(true);
                    else { bar.setIndeterminate(false); bar.setProgress(position); }
                }
            } catch (Exception notYet) { /* the step writes these files as it goes */ }
            handler.postDelayed(this, 250);
        }
    };

    /** The detail line under "Preparing shaders: models": the phase or stage and the time left. */
    static String[] texts(Context context, JSONObject progress) {
        ModelsStep.Phase phase = ModelsStep.phase(progress.optString("phase", null));
        String place = progress.optString("place", "");
        String what;
        switch (phase) {
            case OBJECTS: what = context.getString(R.string.setup_models_phase_objects); break;
            case ROOMS: what = context.getString(R.string.setup_models_phase_rooms); break;
            case ENVIRONMENT: what = context.getString(R.string.setup_models_phase_environment); break;
            case ACTORS: what = context.getString(R.string.setup_models_phase_actors); break;
            case PLANTS: what = context.getString(R.string.setup_models_phase_plants); break;
            case PARTICLES: what = context.getString(R.string.setup_models_phase_particles); break;
            case VISITS: what = place.isEmpty() ? context.getString(R.string.setup_models_phase_visits) :
                context.getString(R.string.setup_models_phase_visit, place); break;
            case FILE_SELECT: what = context.getString(R.string.setup_models_phase_file_select); break;
            default: what = context.getString(R.string.setup_models_phase_starting);
        }
        long minutes = progress.isNull("eta_seconds") ? -1 : ModelsStep.minutesLeft(progress.optLong("eta_seconds", -1));
        String left = minutes < 0 ? context.getString(R.string.setup_models_estimating) : minutes == 0 ?
            context.getString(R.string.setup_minute_left) : context.getString(R.string.setup_minutes_left, minutes);
        return new String[] {what + left};
    }

    @Override protected void onStop() {
        super.onStop();
        // the player left the progress screen (Home, another app, screen off): the step pauses until setup is
        // on the screen again; the setup process stops this attempt in order
        if (!isRedirectedActivity() && !quitting && !stopping && !isChangingConfigurations()) {
            Log.i(TAG, "attempt " + attempt + ": progress screen left");
            writeStatus("left");
        }
    }

    @Override protected void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        super.onDestroy();
    }
}
