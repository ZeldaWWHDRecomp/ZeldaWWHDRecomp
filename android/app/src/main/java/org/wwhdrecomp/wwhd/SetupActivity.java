package org.wwhdrecomp.wwhd;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.util.ArrayList;
import java.util.UUID;
import org.json.JSONObject;

/** Progress and controls for a durable phone setup job. */
public class SetupActivity extends Activity {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView status;      // the technical record, under "Details"
    private TextView stage;       // what is happening, in plain words
    private TextView detail;
    private android.widget.ProgressBar bar;
    private Button choose;
    private Button details;
    private Button resume;
    private Button pause;
    private Button play;
    private Button disc;
    private Button common;
    private Button restore;
    private final ArrayList<Button> pickers = new ArrayList<>();
    private boolean phoneSetup;
    private String actionMessage;
    private String pickerJob;
    private static final int FOLDER = 10, ARCHIVE = 11, IMAGE = 12, DISC = 13, COMMON = 14, RESTORE = 15;
    static final String EXTRA_SHOW_SETUP = "org.wwhdrecomp.wwhd.SHOW_SETUP";

    /** A game is ready: an active phone build that needs no rebuild and no setup in progress, or the
     *  PC route's game folder. */
    private boolean readyToPlay() {
        try {
            if (!phoneSetup) {
                File external = getExternalFilesDir(null);
                return external != null && new File(external, "game/code/cking.rpx").isFile();
            }
            if (!new File(AndroidGame.storage(this), "active.json").isFile() || AndroidGame.needsRebuild(this))
                return false;
            File job = SetupStore.current(this);
            File host = job == null ? null : new File(job, "host.json");
            return host == null || !host.isFile() || SetupStore.read(host).optString("state").equals("complete");
        } catch (Exception unreadable) { return false; }
    }

    private boolean shortcutChecked;

    /** Once per completed build: the home-screen shortcut with the game's own icon. */
    private void offerShortcut() {
        if (shortcutChecked) return;
        shortcutChecked = true;
        new Thread(() -> {
            AndroidGame.Selection selection = AndroidGame.selected(this);  // (hashes the build: off the UI thread)
            if (selection == null || selection.game == null) return;
            android.content.SharedPreferences prefs = getSharedPreferences("setup", MODE_PRIVATE);
            String generation = selection.library.getName();
            if (generation.equals(prefs.getString("shortcut_offered", ""))) return;
            prefs.edit().putString("shortcut_offered", generation).apply();
            runOnUiThread(() -> GameShortcut.offer(this, selection.game));
        }, "wwhd-game-shortcut").start();
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (state != null) pickerJob = state.getString("picker_job");
        try (java.io.InputStream python = getAssets().open("python-build.json");
             java.io.InputStream compiler = getAssets().open("toolchain-apk.json");
             java.io.InputStream sdk = getAssets().open("runtime-sdk.json")) { phoneSetup = true; }
        catch (Exception unavailable) { phoneSetup = false; }
        // the app icon starts the game once there is one to play; the launcher's "Game setup"
        // shortcut (and an update that needs a rebuild, or a setup still running) opens this screen
        if (state == null && Intent.ACTION_MAIN.equals(getIntent().getAction()) &&
                !getIntent().getBooleanExtra(EXTRA_SHOW_SETUP, false) && readyToPlay()) {
            startActivity(new Intent(this, WwhdActivity.class));
            finish();
            return;
        }
        android.util.DisplayMetrics metrics = getResources().getDisplayMetrics();
        int padding = (int)(24 * metrics.density), gap = (int)(12 * metrics.density);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(padding, padding, padding, padding);
        TextView title = new TextView(this);
        title.setText(R.string.app_name);
        title.setTextSize(28);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        title.setTextColor(0xFFFFFFFF);
        layout.addView(title);
        stage = new TextView(this);
        stage.setTextSize(20);
        stage.setTextColor(0xFFE8EEF2);
        stage.setPadding(0, gap, 0, 0);
        layout.addView(stage);
        bar = new android.widget.ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        bar.setPadding(0, gap, 0, 0);
        layout.addView(bar);
        detail = new TextView(this);
        detail.setTextSize(16);
        detail.setPadding(0, gap / 2, 0, gap);
        layout.addView(detail);
        // one button for the three kinds of dump; the activity results are the same as before
        choose = button(layout, getString(R.string.setup_choose), this::chooseKind);
        pickers.add(choose);
        restore = button(layout, getString(R.string.setup_restore), () -> pick(RESTORE));
        disc = button(layout, getString(R.string.setup_disc_key), () -> pick(DISC));
        common = button(layout, getString(R.string.setup_common_key), () -> pick(COMMON));
        resume = button(layout, getString(R.string.setup_start), () -> control(SetupService.RESUME));
        pause = button(layout, getString(R.string.setup_pause), () -> control(SetupService.PAUSE));
        play = button(layout, getString(R.string.setup_play), () -> startActivity(new Intent(this, WwhdActivity.class)));
        play.setTextSize(20);
        play.setTextColor(0xFFFFFFFF);
        play.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFF0D5C8A));
        details = new Button(this, null, android.R.attr.borderlessButtonStyle);
        details.setText(R.string.setup_details_show);
        layout.addView(details);
        details.setOnClickListener(view -> {
            boolean show = status.getVisibility() != android.view.View.VISIBLE;
            status.setVisibility(show ? android.view.View.VISIBLE : android.view.View.GONE);
            details.setText(show ? R.string.setup_details_hide : R.string.setup_details_show);
        });
        status = new TextView(this);
        status.setTextSize(14);
        status.setVisibility(android.view.View.GONE);
        layout.addView(status);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(layout);
        setContentView(scroll);
    }

    /** "Choose your game": a folder, a Cemu .wua archive or a .wud/.wux disc image. */
    private void chooseKind() {
        new android.app.AlertDialog.Builder(this).setTitle(R.string.setup_choose)
            .setItems(new CharSequence[] {getString(R.string.setup_kind_folder), getString(R.string.setup_kind_wua),
                getString(R.string.setup_kind_image)}, (dialog, which) -> pick(which == 0 ? FOLDER : which == 1 ? ARCHIVE : IMAGE))
            .setNegativeButton(android.R.string.cancel, null).show();
    }

    private static void show(android.view.View view, boolean visible) {
        int value = visible ? android.view.View.VISIBLE : android.view.View.GONE;
        if (view.getVisibility() != value) view.setVisibility(value);
    }

    /** The plain-words line, the detail line and the bar (0..1000, -1 moving, -2 hidden). */
    private void phase(String stageText, String detailText, int progress) {
        setTextIfChanged(stage, stageText);
        setTextIfChanged(detail, detailText == null ? "" : detailText);
        show(detail, detailText != null);
        show(bar, progress > -2);
        if (progress == -1) bar.setIndeterminate(true);
        else if (progress >= 0) { bar.setIndeterminate(false); bar.setProgress(progress); }
    }

    private boolean replaceable(File job) throws Exception {
        if (job == null) return true;
        File host = new File(job, "host.json");
        if (!host.isFile()) return true;
        String state = SetupStore.read(host).optString("state");
        return state.equals("selected") || state.equals("failed") || state.equals("complete") ||
            (state.equals("paused") && new File(job, "manual-pause.json").isFile());
    }

    private void pick(int request) {
        try {
            actionMessage = null;
            File job = SetupStore.current(this);
            if (!replaceable(job)) throw new Exception("Pause setup and wait for its checkpoint before changing input");
            pickerJob = job == null ? null : job.getName();
            boolean folder = request == FOLDER;
            if (request == RESTORE) {
                if (job == null || new File(job, "job.json").exists()) throw new Exception("This job already uses a private input copy");
                folder = SetupStore.read(new File(job, "source.json")).getString("kind").equals("folder");
            }
            Intent intent = new Intent(folder ? Intent.ACTION_OPEN_DOCUMENT_TREE : Intent.ACTION_OPEN_DOCUMENT);
            if (!folder) intent.addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, request);
        } catch (Exception failure) { actionMessage = "Cannot choose input: " + failure.getMessage(); }
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        try {
            actionMessage = null;
            File current = SetupStore.current(this);
            if (!replaceable(current)) throw new Exception("Pause setup before changing input");
            if ((request == RESTORE || request == DISC || request == COMMON) &&
                (current == null || !current.getName().equals(pickerJob)))
                throw new Exception("The selected job changed. Open its picker again.");
            Uri uri = data.getData();
            if (request == RESTORE) {
                if (new File(current, "job.json").exists()) throw new Exception("The input has already been copied privately");
                JSONObject source = SetupStore.read(new File(current, "source.json"));
                if (!uri.toString().equals(source.getString("uri")))
                    throw new Exception("Choose the same previously selected file or folder to restore access. Use the dump-selection buttons for a different input.");
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                actionMessage = "Access restored. Tap Start / resume to retry using completed private copies.";
            } else if (request == DISC || request == COMMON) {
                if (current == null) throw new Exception("Choose a disc image first");
                JSONObject source = SetupStore.read(new File(current, "source.json"));
                if (!source.getString("kind").equals("image")) throw new Exception("Keys are only needed for disc images");
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                SetupImport.selectKey(current, request == DISC ? "disc" : "common", uri);
                if (new File(current, "job.json").isFile())
                    actionMessage = "Replacement key selected. Tap Start / resume to copy it privately and retry. Your dump copy is retained.";
            } else if (request == FOLDER || request == ARCHIVE || request == IMAGE) {
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                String id = UUID.randomUUID().toString().replace("-", "");
                File job = SetupStore.job(this, id);
                SetupStore.write(new File(job, "source.json"), new JSONObject().put("schema", 1)
                    .put("kind", request == FOLDER ? "folder" : request == ARCHIVE ? "archive" : "image").put("uri", uri.toString()));
                SetupStore.write(new File(job, "manual-pause.json"), new JSONObject().put("schema", 1));
                SetupStore.write(new File(job, "host.json"), new JSONObject().put("schema", 1).put("state", "selected"));
                SetupStore.select(this, id);
            }
        } catch (Exception failure) { actionMessage = "Cannot keep selected input: " + failure.getMessage(); }
        finally { pickerJob = null; }
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        state.putString("picker_job", pickerJob);
        super.onSaveInstanceState(state);
    }

    private Button button(LinearLayout layout, String label, Runnable action) {
        Button button = new Button(this);
        button.setText(label);
        button.setOnClickListener(view -> action.run());
        layout.addView(button);
        return button;
    }

    private void control(String action) {
        try {
            actionMessage = null;
            if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
                requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 1);
            SetupService.start(this, action);
        } catch (Exception failure) { actionMessage = "Cannot start setup: " + failure.getMessage(); }
    }

    private static void setTextIfChanged(TextView view, String text) {
        // Replacing status then appending feedback on every poll continually
        // emits accessibility changes, even when nothing has changed.
        if (!text.contentEquals(view.getText())) view.setText(text);
    }

    private boolean keyReady(File job, JSONObject source, String key, boolean correcting, java.util.Set<String> grants) throws Exception {
        if (source == null) return false;
        if (correcting) {
            File pending = SetupImport.keyReplacement(job, key);
            if (pending.isFile()) return grants.contains(SetupStore.read(pending).getString("uri"));
            return new File(job, "input/" + key + ".key").isFile();
        }
        return grants.contains(source.optString(key + "_uri"));
    }

    private static final String CHECKPOINTS = "Setup keeps a private copy of your input. Leave room for that copy, extracted files, compiled output and at least 1 GiB working reserve. Keys are selected as files and kept private.\n\nYou can close this screen while setup runs. Import resumes from verified complete files; an interrupted file copy or extraction restarts. Python and compiler resource preparation pause during unpacking and restart the incomplete stage. Checksum validation and translation respond to pause requests; an interrupted translation restarts. Compilation and linking also respond to pause requests. An interrupted command restarts; verified objects are kept. Saves stay in their current folder.\n\nThe optional PC route still uses the game folder copied over USB.";

    /** The top of the screen: one plain sentence, a detail line and the bar. */
    private void plainPhase(File job, JSONObject host, String state, boolean needsKeys, boolean canStart) throws Exception {
        if (!phoneSetup) { phase(getString(play.isEnabled() ? R.string.setup_ready : R.string.setup_pc_route), null, -2); return; }
        if (job == null) {
            phase(getString(play.isEnabled() ? R.string.setup_ready : R.string.setup_welcome), getString(R.string.setup_welcome_detail), -2);
            return;
        }
        switch (state) {
            case "selected":
                if (needsKeys && !canStart) phase(getString(R.string.setup_keys), getString(R.string.setup_keys_detail), -2);
                else phase(getString(R.string.setup_selected), getString(R.string.setup_selected_detail), -2);
                return;
            case "importing": {
                File imported = new File(job, "import-progress.json");
                if (imported.isFile()) {
                    JSONObject progress = SetupStore.read(imported);
                    int files = Math.max(1, progress.getInt("files"));
                    phase(getString(R.string.setup_copying), getString(R.string.setup_copying_detail,
                        progress.getInt("complete"), progress.getInt("files")), (int)(1000L * progress.getInt("complete") / files));
                } else phase(getString(R.string.setup_copying), null, -1);
                return;
            }
            case "preparing": phase(getString(R.string.setup_preparing), getString(R.string.setup_leave_detail), -1); return;
            case "running": {
                File record = new File(job, "state.json");
                JSONObject event = record.isFile() ? SetupStore.read(record).optJSONObject("last_event") : null;
                String step = event == null ? "" : event.optString("stage");
                if (step.equals("compile") && event.optInt("total") > 0) {
                    int total = event.optInt("total"), done = Math.min(total, event.optInt("compiled") + event.optInt("reused"));
                    long eta = event.optLong("eta_seconds", -1);
                    String left = eta < 0 ? "" : eta < 90 ? getString(R.string.setup_minute_left) :
                        getString(R.string.setup_minutes_left, (eta + 59) / 60);
                    phase(getString(R.string.setup_compiling), getString(R.string.setup_compiling_detail, done, total) + left,
                        (int)(1000L * done / total));
                } else if (step.equals("translate")) phase(getString(R.string.setup_translating), getString(R.string.setup_leave_detail), -1);
                else if (step.equals("link") || step.equals("activate")) phase(getString(R.string.setup_finishing), null, -1);
                else phase(getString(R.string.setup_extracting), getString(R.string.setup_leave_detail), -1);
                return;
            }
            case "paused": {
                String reason = host == null ? "" : host.optString("reason");
                int text = reason.equals("heat") ? R.string.setup_paused_heat :
                    reason.equals("battery") || reason.equals("battery_unknown") ? R.string.setup_paused_battery : R.string.setup_paused;
                phase(getString(text), getString(reason.equals("manual") || reason.isEmpty() ?
                    R.string.setup_paused_detail : R.string.setup_paused_auto_detail), -2);
                return;
            }
            case "failed": phase(getString(R.string.setup_failed), getString(R.string.setup_failed_detail), -2); return;
            case "complete": phase(getString(R.string.setup_ready), getString(R.string.setup_ready_detail), 1000); return;
            default: phase(getString(R.string.setup_working), null, -1);
        }
    }

    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            try {
                File job = SetupStore.current(SetupActivity.this);
                JSONObject source = job != null && new File(job, "source.json").isFile() ? SetupStore.read(new File(job, "source.json")) : null;
                boolean importing = phoneSetup && source != null && !new File(job, "job.json").isFile();
                JSONObject hostState = job != null && new File(job, "host.json").isFile() ? SetupStore.read(new File(job, "host.json")) : null;
                boolean correctingKeys = !importing && phoneSetup && source != null && source.optString("kind").equals("image") &&
                    hostState != null && (hostState.optString("state").equals("failed") ||
                    (hostState.optString("state").equals("paused") && new File(job, "manual-pause.json").isFile()));
                boolean needsKeys = (importing && source.optString("kind").equals("image")) || correctingKeys;
                java.util.HashSet<String> grants = new java.util.HashSet<>();
                if (importing || correctingKeys) for (android.content.UriPermission permission : getContentResolver().getPersistedUriPermissions())
                    if (permission.isReadPermission()) grants.add(permission.getUri().toString());
                boolean discReady = keyReady(job, source, "disc", correctingKeys, grants);
                boolean commonReady = keyReady(job, source, "common", correctingKeys, grants);
                restore.setVisibility(importing ? android.view.View.VISIBLE : android.view.View.GONE);
                restore.setEnabled(importing && replaceable(job));
                disc.setVisibility(needsKeys ? android.view.View.VISIBLE : android.view.View.GONE);
                common.setVisibility(needsKeys ? android.view.View.VISIBLE : android.view.View.GONE);
                setTextIfChanged(disc, getString(discReady ? R.string.setup_disc_key_ready : source != null && source.has("disc_uri") ? R.string.setup_disc_key_again : R.string.setup_disc_key));
                setTextIfChanged(common, getString(commonReady ? R.string.setup_common_key_ready : source != null && source.has("common_uri") ? R.string.setup_common_key_again : R.string.setup_common_key));
                disc.setEnabled(needsKeys && replaceable(job));
                common.setEnabled(needsKeys && replaceable(job));
                resume.setEnabled(phoneSetup && job != null && (!needsKeys || (discReady && commonReady)));
                pause.setEnabled(phoneSetup && job != null);
                for (Button picker : pickers) picker.setEnabled(phoneSetup && replaceable(job));
                File external = getExternalFilesDir(null);
                play.setEnabled(new File(AndroidGame.storage(SetupActivity.this), "active.json").isFile() ||
                    (external != null && new File(external, "game/code/cking.rpx").isFile()));
                String hostName = hostState == null ? (job == null ? "" : "selected") : hostState.optString("state");
                boolean working = hostName.equals("importing") || hostName.equals("preparing") || hostName.equals("running");
                show(play, play.isEnabled() && !working);
                show(choose, phoneSetup && !working);
                setTextIfChanged(choose, getString(job == null ? R.string.setup_choose : R.string.setup_choose_other));
                show(pause, phoneSetup && working);
                show(resume, phoneSetup && job != null && !working && !hostName.equals("complete"));
                setTextIfChanged(resume, getString(hostName.equals("selected") ? R.string.setup_start : R.string.setup_continue));
                plainPhase(job, hostState, hostName, needsKeys, resume.isEnabled());
                String text;
                if (!phoneSetup) text = "This APK supports the PC build route. Phone setup needs an APK containing Python and the compiler.";
                else if (job == null) text = "Choose your game dump to set up on this phone.";
                else {
                    File host = new File(job, "host.json");
                    JSONObject value = host.isFile() ? SetupStore.read(host) : new JSONObject().put("state", "ready to start");
                    text = "Setup: " + value.getString("state");
                    if (value.optString("state").equals("complete")) offerShortcut();
                    if (value.has("reason")) text += "\n" + SetupPolicy.description(value.getString("reason"));
                    if (value.has("error")) text += "\n" + value.getString("error");
                    if (importing && !grants.contains(source.optString("uri")))
                        text += "\nDump access is missing. Restore access to the same selected dump if another file copy is needed. Completed private copies are retained.";
                    if (needsKeys) text += "\n" + (resume.isEnabled() ? correctingKeys ?
                        "Private dump retained. Change either key if needed, then tap Start / resume to retry." :
                        "Both key files selected. Tap Start." : "Select both key files before starting.");
                    if (value.optString("state").equals("importing")) {
                        File imported = new File(job, "import-progress.json");
                        if (imported.isFile()) {
                            JSONObject progress = SetupStore.read(imported);
                            text += "\nCopied " + progress.getLong("copied") / (1024 * 1024) + " MiB; " + progress.getInt("complete") + "/" + progress.getInt("files") + " files";
                        }
                    }
                    File progress = new File(job, "state.json");
                    if (value.optString("state").equals("running") && progress.isFile()) {
                        JSONObject event = SetupStore.read(progress).optJSONObject("last_event");
                        String compiling = event == null || !event.optString("stage").equals("compile") ? null :
                            SetupPolicy.compileProgress(event.optString("state"), event.optInt("compiled"), event.optInt("reused"),
                                event.optInt("total"), event.optLong("eta_seconds", -1));
                        if (compiling != null) text += "\n" + compiling;
                        else if (event != null) text += "\n" + event.optString("stage", "setup") + ": " + event.optString("state");
                    }
                }
                if (actionMessage != null) text += "\n" + actionMessage;
                setTextIfChanged(status, text + "\n\n" + CHECKPOINTS);
            } catch (Exception failure) { setTextIfChanged(status, "Cannot read setup progress: " + failure.getMessage()); }
            handler.postDelayed(this, 500);
        }
    };

    @Override protected void onResume() {
        super.onResume();
        refresh.run();
        new Thread(() -> {
            try {
                File job = SetupStore.current(this);
                if (job == null || new File(job, "manual-pause.json").exists()) return;
                File host = new File(job, "host.json");
                if (!host.isFile()) return;
                String state = SetupStore.read(host).optString("state");
                boolean changed = state.equals("complete") && AndroidGame.needsRebuild(this);
                if (SetupRecoveryPolicy.shouldStart(state, false, true, changed))
                    runOnUiThread(() -> {
                        try { SetupService.start(this, null); }
                        catch (Exception failure) { status.setText("Tap Resume to continue setup: " + failure.getMessage()); }
                    });
            } catch (Exception ignored) { /* refresh shows a readable record error */ }
        }, "wwhd-setup-check").start();
    }
    @Override protected void onPause() { handler.removeCallbacks(refresh); super.onPause(); }
}
