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
    private TextView updateStatus;
    private android.widget.ProgressBar bar;
    private Button choose;
    private Button details;
    private Button resume;
    private Button pause;
    private Button continueAnyway;
    private String continueAction;
    private Button play;
    private Button addIcon;
    private Button disc;
    private Button common;
    private Button restore;
    private android.widget.CheckBox codeMods;
    private TextView codeModsDetail;
    private android.widget.CheckBox models;
    private TextView modelsDetail;
    private Button cancelModels;
    private long screenResumed;
    private final ArrayList<Button> pickers = new ArrayList<>();
    private boolean phoneSetup;
    private boolean launchingGame;
    private String actionMessage;
    private TextView keysHint;
    private TextView folderHint;
    /** A chosen folder is being checked for the game (off the UI thread). */
    private volatile boolean checkingFolder;
    private String pickerJob;
    private String shaderWarmJob;
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

    /** Once per completed build: the home-screen game icon with the game's own picture. Android
     *  refuses the request unless the screen is in front, so it waits for focus and counts as made
     *  only once accepted. */
    private void offerShortcut() {
        if (shortcutChecked || !hasWindowFocus()) return;
        shortcutChecked = true;
        if (!GameShortcut.supported(this)) return;
        if (!phoneSetup) {  // the PC route: its game folder, once
            File external = getExternalFilesDir(null);
            File game = external == null ? null : new File(external, "game");
            android.content.SharedPreferences prefs = getSharedPreferences("setup", MODE_PRIVATE);
            if (game == null || !new File(game, "meta/iconTex.tga").isFile() || prefs.getBoolean("pc_icon_offered", false)) return;
            if (GameShortcut.offer(this, game)) prefs.edit().putBoolean("pc_icon_offered", true).apply();
            return;
        }
        new Thread(() -> {
            AndroidGame.Selection selection = AndroidGame.selected(this);  // (hashes the build: off the UI thread)
            if (selection == null || selection.game == null) return;
            android.content.SharedPreferences prefs = getSharedPreferences("setup", MODE_PRIVATE);
            String generation = selection.library.getName();
            if (generation.equals(prefs.getString("shortcut_offered", ""))) return;
            runOnUiThread(() -> {
                if (GameShortcut.offer(this, selection.game)) prefs.edit().putString("shortcut_offered", generation).apply();
            });
        }, "wwhd-game-shortcut").start();
    }

    /** The setup screen's button: the game icon again (refused or removed), or where it is. */
    private void addIcon() {
        new Thread(() -> {
            File game = GameWidget.activeGame(this);
            boolean widget = android.appwidget.AppWidgetManager.getInstance(this)
                .getAppWidgetIds(new android.content.ComponentName(this, GameWidget.class)).length > 0;
            boolean shortcut = !widget && GameShortcut.pinned(this);  // launchers that can't pin widgets
            runOnUiThread(() -> {
                if (game == null) android.widget.Toast.makeText(this, R.string.game_icon_none, android.widget.Toast.LENGTH_SHORT).show();
                else if (widget) android.widget.Toast.makeText(this, R.string.game_icon_present, android.widget.Toast.LENGTH_LONG).show();
                else if (shortcut) android.widget.Toast.makeText(this, R.string.game_icon_present_shortcut, android.widget.Toast.LENGTH_LONG).show();
                else if (!GameShortcut.offer(this, game))
                    android.widget.Toast.makeText(this, R.string.game_icon_unsupported, android.widget.Toast.LENGTH_LONG).show();
            });
        }, "wwhd-game-icon").start();
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (state != null) pickerJob = state.getString("picker_job");
        try (java.io.InputStream python = getAssets().open("python-build.json");
             java.io.InputStream compiler = getAssets().open("toolchain-apk.json");
             java.io.InputStream sdk = getAssets().open("runtime-sdk.json")) { phoneSetup = true; }
        catch (Exception unavailable) { phoneSetup = false; }
        if (phoneSetup && state == null) cleanJobs();  // failed or abandoned imports left by an earlier run
        // the app icon starts the game once there is one to play; the launcher's "Game setup"
        // shortcut (and an update that needs a rebuild, or a setup still running) opens this screen
        if ((state != null && state.getBoolean("launching_game", false)) ||
                (state == null && Intent.ACTION_MAIN.equals(getIntent().getAction()) &&
                !getIntent().getBooleanExtra(EXTRA_SHOW_SETUP, false) && readyToPlay())) {
            launchingGame = true;
            CrashReports.offer(this, () -> {
                startActivity(QuestActivity.gameIntent(this));
                finish();
            });
            return;
        }
        CrashReports.offer(this);
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
        updateStatus = new TextView(this);
        updateStatus.setText(SetupPresentation.UPDATING);
        updateStatus.setTextSize(16);
        updateStatus.setVisibility(android.view.View.GONE);
        layout.addView(updateStatus);
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
        // Play first: once a game is ready it is the main action
        play = button(layout, getString(R.string.setup_play), () -> startActivity(QuestActivity.gameIntent(this)));
        button(layout, "Share log", () -> LogShare.share(this));
        play.setTextSize(20);
        play.setTextColor(0xFFFFFFFF);
        play.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFF0D5C8A));
        // the home-screen game icon again, for players who declined or removed it
        addIcon = button(layout, getString(R.string.game_icon_add), this::addIcon);
        // one button for the three kinds of dump; the activity results are the same as before
        choose = button(layout, getString(R.string.setup_choose), this::chooseKind);
        pickers.add(choose);
        folderHint = new TextView(this);
        folderHint.setText(R.string.setup_folder_hint);
        folderHint.setPadding(0, 0, 0, gap);
        layout.addView(folderHint);
        restore = button(layout, getString(R.string.setup_restore), () -> pick(RESTORE));
        keysHint = new TextView(this);
        keysHint.setText(R.string.setup_keys_detail);
        layout.addView(keysHint);
        disc = button(layout, getString(R.string.setup_disc_key), () -> pick(DISC));
        common = button(layout, getString(R.string.setup_common_key), () -> pick(COMMON));
        resume = button(layout, getString(R.string.setup_start), () -> control(SetupService.RESUME));
        pause = button(layout, getString(R.string.setup_pause), () -> control(SetupService.PAUSE));
        continueAnyway = button(layout, getString(R.string.setup_continue_anyway), () -> { if (continueAction != null) control(continueAction); });
        continueAnyway.setVisibility(android.view.View.GONE);
        // setup's models step: Cancel stops it and keeps what was prepared
        cancelModels = button(layout, getString(R.string.setup_cancel), this::cancelModels);
        cancelModels.setVisibility(android.view.View.GONE);
        // the desktop setup's "code mods" choice: the game code with mod support for guest mods
        codeMods = new android.widget.CheckBox(this);
        codeMods.setText(R.string.setup_code_mods);
        codeMods.setTextSize(16);
        codeMods.setChecked(AndroidGame.codeModsChosen(this));
        codeMods.setOnCheckedChangeListener((view, checked) -> chooseCodeMods(checked));
        layout.addView(codeMods);
        codeModsDetail = new TextView(this);
        codeModsDetail.setText(R.string.setup_code_mods_detail);
        codeModsDetail.setPadding(0, 0, 0, gap);
        layout.addView(codeModsDetail);
        // the desktop setup's optional "Preparing shaders: models" step
        models = new android.widget.CheckBox(this);
        models.setText(R.string.setup_models);
        models.setTextSize(16);
        models.setChecked(AndroidGame.modelsChosen(this));
        models.setOnCheckedChangeListener((view, checked) -> chooseModels(checked));
        layout.addView(models);
        modelsDetail = new TextView(this);
        modelsDetail.setText(R.string.setup_models_detail);
        modelsDetail.setPadding(0, 0, 0, gap);
        layout.addView(modelsDetail);
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

    /** The Code mods switch: remembered for the next build; a finished setup builds again now (the
     *  current build stays playable until the new one is ready). */
    private void chooseCodeMods(boolean enabled) {
        new Thread(() -> {
            try {
                AndroidGame.chooseCodeMods(this, enabled);
                File job = SetupStore.current(this);
                File host = job == null ? null : new File(job, "host.json");
                if (host != null && host.isFile() && SetupStore.read(host).optString("state").equals("complete") &&
                        AndroidGame.needsRebuild(this))
                    runOnUiThread(() -> {
                        try { SetupService.start(this, null); }
                        catch (Exception failure) { status.setText("Tap Resume to continue setup: " + failure.getMessage()); }
                    });
            } catch (Exception failure) {
                runOnUiThread(() -> status.setText("Cannot save the code-mod choice: " + failure.getMessage()));
            }
        }, "wwhd-code-mods").start();
    }

    /** The models step switch: remembered for the next setup; ticked on a ready game, the step runs now. */
    private void chooseModels(boolean enabled) {
        new Thread(() -> {
            try {
                AndroidGame.chooseModels(this, enabled);
                File job = SetupStore.current(this);
                File host = job == null ? null : new File(job, "host.json");
                if (enabled && !WindwakeSession.inUse(this) && host != null && host.isFile() &&
                        SetupStore.read(host).optString("state").equals("complete") &&
                        new File(AndroidGame.storage(this), "active.json").isFile() && !AndroidGame.needsRebuild(this))
                    runOnUiThread(() -> control(SetupService.RUN_MODELS));
            } catch (Exception failure) {
                runOnUiThread(() -> status.setText("Cannot save the models choice: " + failure.getMessage()));
            }
        }, "wwhd-models-choice").start();
    }

    /** Cancel of the models step: it stops in order and keeps what was prepared. */
    private void cancelModels() {
        cancelModels.setEnabled(false);
        new Thread(() -> {
            try {
                File job = SetupStore.current(this);
                if (job != null) SetupStore.write(new File(job, ModelsStep.CANCEL), new JSONObject().put("schema", 1));
            } catch (Exception failure) {
                runOnUiThread(() -> status.setText("Cannot cancel: " + failure.getMessage()));
            }
        }, "wwhd-models-cancel").start();
    }

    /** The models step's attempts: each requested one is started once, over this screen, without animation. */
    private void routeModels(File job, String hostName) throws Exception {
        boolean step = hostName.equals(ModelsStep.STATE);
        JSONObject host = new File(job, "host.json").isFile() ? SetupStore.read(new File(job, "host.json")) : null;
        boolean screenPause = hostName.equals("paused") && host != null && ModelsStep.SCREEN_REASON.equals(host.optString("reason"));
        // the game draws in landscape, as in play: this screen turns with it so the step shows one screen
        int orientation = step || screenPause ? android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE :
            android.content.pm.ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED;
        if (getRequestedOrientation() != orientation) setRequestedOrientation(orientation);
        // the step draws only with the screen on: between two attempts this screen keeps it on too
        if (step) getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // setup is on the screen again: the step goes on (not while the game itself runs in this app, in the
        // background: two games must not write the shader caches)
        if (screenPause && !WindwakeSession.inUse(this) && android.os.SystemClock.elapsedRealtime() - screenResumed > 5000) {
            screenResumed = android.os.SystemClock.elapsedRealtime();
            new Thread(() -> {
                try {
                    SetupStore.remove(new File(job, ModelsStep.SCREEN));
                    runOnUiThread(() -> control(null));
                } catch (Exception failure) { android.util.Log.w("wwhd-setup", "models step not resumed", failure); }
            }, "wwhd-models-resume").start();
        }
        if (!step) return;
        File requestFile = new File(job, ModelsStep.ATTEMPT), launchedFile = new File(job, ModelsStep.LAUNCHED);
        String requested = requestFile.isFile() ? SetupStore.read(requestFile).optString("id") : null;
        String launched = launchedFile.isFile() ? SetupStore.read(launchedFile).optString("id") : null;
        if (!ModelsStep.launch(requested, launched) || !hasWindowFocus()) return;
        SetupStore.write(launchedFile, new JSONObject().put("schema", 1).put("id", requested));
        startActivity(ModelPrepareActivity.intent(this, job, requested));
        overridePendingTransition(0, 0);
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
            if (checkingFolder) throw new Exception("Still checking the chosen folder");
            pickerJob = job == null ? null : job.getName();
            boolean folder = request == FOLDER;
            if (request == RESTORE) {
                if (job == null || new File(job, "job.json").exists()) throw new Exception("This job already uses a private input copy");
                folder = SetupStore.read(new File(job, "source.json")).getString("kind").equals("folder");
            }
            Intent intent = new Intent(folder ? Intent.ACTION_OPEN_DOCUMENT_TREE : Intent.ACTION_OPEN_DOCUMENT);
            if (!folder) intent.addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            // start in Download (Android refuses Download itself as a folder, not a folder inside it) and
            // ask for the internal storage among the picker's places
            for (java.util.Map.Entry<String, Object> extra : SetupPicker.extras(folder).entrySet()) {
                if (extra.getValue() instanceof Boolean) intent.putExtra(extra.getKey(), (Boolean)extra.getValue());
                else intent.putExtra(extra.getKey(), Uri.parse((String)extra.getValue()));
            }
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
                JSONObject source = SetupImport.keySource(current);
                if (!source.getString("kind").equals("image")) throw new Exception("Keys are only needed for disc images");
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                SetupImport.selectKey(this, current, request == DISC ? "disc" : "common", uri);
                if (new File(current, "job.json").isFile())
                    actionMessage = "Replacement key file selected. Tap Start / resume to read it and retry. Your dump copy is retained.";
            } else if (request == FOLDER) checkFolder(uri);
            else if (request == ARCHIVE || request == IMAGE) {
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                newJob(this, request == ARCHIVE ? "archive" : "image", uri, null);
                cleanJobs();
            }
        } catch (Exception failure) { actionMessage = "Cannot keep selected input: " + failure.getMessage(); }
        finally { pickerJob = null; }
    }

    /** A new setup job for the chosen game becomes the selected one. */
    private static void newJob(android.content.Context context, String kind, Uri uri, String document) throws Exception {
        synchronized (SetupJobCleanup.CREATION) {
            String id = UUID.randomUUID().toString().replace("-", "");
            File job = SetupStore.job(context, id);
            JSONObject source = new JSONObject().put("schema", 1).put("kind", kind).put("uri", uri.toString());
            if (document != null) source.put("document", document);
            SetupStore.write(new File(job, "source.json"), source);
            SetupStore.write(new File(job, "manual-pause.json"), new JSONObject().put("schema", 1));
            SetupStore.write(new File(job, "host.json"), new JSONObject().put("schema", 1).put("state", "selected"));
            SetupStore.select(context, id);
        }
    }

    /** Removes the private copies of games no longer chosen (never the selected game, the installed build
     *  or a job the setup service holds), off the UI thread. */
    private void cleanJobs() {
        android.content.Context context = getApplicationContext();
        new Thread(() -> {
            SetupJobCleanup.Report report = SetupJobCleanup.clean(AndroidGame.storage(context));
            if (!report.removed.isEmpty() || !report.failed.isEmpty() || report.skipped != null)
                android.util.Log.i("wwhd-setup", report.summary());
        }, "wwhd-setup-cleanup").start();
    }

    /** Checks the chosen folder for the game before anything is copied; a wrong folder is refused with a
     *  message that says which folder to choose, and nothing is kept of it. */
    private void checkFolder(Uri uri) {
        checkingFolder = true;
        android.content.Context context = getApplicationContext();
        new Thread(() -> {
            String message = null, refusal = null;
            try {
                SetupFolderCheck.Result found = SetupImport.checkFolder(context,
                    uri, android.provider.DocumentsContract.getTreeDocumentId(uri));
                android.util.Log.i("wwhd-setup", "chosen folder: " + found.kind + " " + found.path);
                if (!found.usable()) refusal = folderMessage(found);
                else {
                    if (!replaceable(SetupStore.current(context))) throw new Exception("Pause setup before changing input");
                    context.getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    newJob(context, "folder", uri, found.kind == SetupFolderCheck.Kind.SUBFOLDER ? found.document : null);
                    if (found.kind == SetupFolderCheck.Kind.SUBFOLDER) message = folderMessage(found);
                    SetupJobCleanup.Report report = SetupJobCleanup.clean(AndroidGame.storage(context));
                    android.util.Log.i("wwhd-setup", report.summary());
                }
            } catch (Exception failure) {
                refusal = getString(R.string.setup_folder_unreadable, String.valueOf(failure.getMessage()));
            }
            final String chosen = message, refused = refusal;
            runOnUiThread(() -> {
                checkingFolder = false;
                actionMessage = refused != null ? refused : chosen;
                if (refused == null && chosen != null)  // the game was found in a folder inside the chosen one
                    android.widget.Toast.makeText(this, chosen, android.widget.Toast.LENGTH_LONG).show();
                if (refused != null && !isFinishing() && !isDestroyed())
                    new android.app.AlertDialog.Builder(this).setTitle(R.string.setup_folder_wrong)
                        .setMessage(refused).setPositiveButton(R.string.setup_folder_again, (dialog, which) -> pick(FOLDER))
                        .setNegativeButton(android.R.string.cancel, null).show();
            });
        }, "wwhd-setup-folder-check").start();
    }

    private String folderMessage(SetupFolderCheck.Result found) {
        switch (found.kind) {
            case SUBFOLDER: return getString(R.string.setup_folder_subfolder, found.path);
            case INSIDE_GAME: return getString(R.string.setup_folder_inside, found.path);
            case NOT_THIS_GAME: return found.path.isEmpty() ? getString(R.string.setup_folder_not_game_here) :
                getString(R.string.setup_folder_not_game, found.path);
            case SEVERAL: return getString(R.string.setup_folder_several, found.path);
            default: return getString(R.string.setup_folder_none);
        }
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        state.putString("picker_job", pickerJob);
        state.putBoolean("launching_game", launchingGame);
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
        if (SetupKeyAccess.failed(job, key)) return false;
        if (correcting) {
            File pending = SetupImport.keyReplacement(job, key);
            if (pending.isFile()) return grants.contains(SetupStore.read(pending).getString("uri"));
            File configuration = new File(job, "job.json");
            if (configuration.isFile() && SetupKeyAccess.legacyFile(job, SetupStore.read(configuration).getJSONObject("input"), key) != null) return true;
            return grants.contains(source.optString(key + "_uri"));
        }
        return grants.contains(source.optString(key + "_uri"));
    }

    private static final String CHECKPOINTS = "Setup keeps a private copy of your input. Leave room for that copy, extracted files, compiled output and at least 1 GiB working reserve. Selected key files are read only. Earlier private key copies are retained until setup completes. Choosing a different game deletes the private copy of the earlier choice; the game you can already play is kept.\n\nYou can close this screen while setup runs. Import resumes from verified complete files; an interrupted file copy or extraction restarts. Python and compiler resource preparation pause during unpacking and restart the incomplete stage. Checksum validation and translation respond to pause requests; an interrupted translation restarts. Compilation and linking also respond to pause requests. An interrupted command restarts; verified objects are kept. Saves stay in their current folder.\n\nThe optional PC route still uses the game folder copied over USB.";

    /** The top of the screen: one plain sentence, a detail line and the bar. */
    private void plainPhase(File job, JSONObject host, String state, boolean needsKeys, boolean canStart) throws Exception {
        if (checkingFolder) { phase(getString(R.string.setup_folder_checking), null, -1); return; }
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
                } else if (step.equals("shaders")) phase(SetupPresentation.READING_SHADERS, getString(R.string.setup_leave_detail),
                    event.has("fraction") ? (int)(1000 * event.optDouble("fraction")) : -1);
                else if (step.equals("translate")) phase(getString(R.string.setup_translating), getString(R.string.setup_leave_detail), -1);
                else if (step.equals("link") || step.equals("activate")) phase(getString(R.string.setup_finishing), null, -1);
                else phase(getString(R.string.setup_extracting), getString(R.string.setup_leave_detail), -1);
                return;
            }
            case "paused": {
                String reason = host == null ? "" : host.optString("reason");
                if (reason.equals(ModelsStep.SCREEN_REASON)) {
                    phase(getString(R.string.setup_paused_screen), getString(WindwakeSession.inUse(this) ?
                        R.string.setup_models_game_open : R.string.setup_paused_screen_detail), -2);
                    return;
                }
                if (reason.equals("heat")) {
                    // A heat pause waits for the user (#277): Continue anyway at any time, Continue once cooled.
                    boolean cooled = host.optBoolean("cooled");
                    phase(getString(cooled ? R.string.setup_paused : R.string.setup_paused_heat),
                        getString(cooled ? R.string.setup_paused_heat_cooled : R.string.setup_paused_heat_detail), -2);
                    return;
                }
                int text = reason.equals("battery") || reason.equals("battery_unknown") ? R.string.setup_paused_battery : R.string.setup_paused;
                phase(getString(text), getString(reason.equals("manual") || reason.isEmpty() ?
                    R.string.setup_paused_detail : R.string.setup_paused_auto_detail), -2);
                return;
            }
            case "warming shaders": phase(getString(R.string.setup_shaders), getString(R.string.setup_shaders_detail), -1); return;
            case ModelsStep.STATE: {
                File record = new File(job, ModelsStep.PROGRESS);
                JSONObject progress = record.isFile() ? SetupStore.read(record) : new JSONObject();
                phase(getString(R.string.setup_models_running), ModelPrepareActivity.texts(this, progress)[0],
                    ModelsStep.bar(progress.optDouble("fraction", Double.NaN)));
                return;
            }
            case "failed": phase(getString(R.string.setup_failed), getString(R.string.setup_failed_detail), -2); return;
            case "complete": {  // with what setup left out (for example the models step), said plainly
                String note = host == null ? "" : host.optString("error");
                phase(getString(R.string.setup_ready), getString(R.string.setup_ready_detail) + (note.isEmpty() ? "" : "\n" + note), 1000);
                return;
            }
            default: phase(getString(R.string.setup_working), null, -1);
        }
    }

    /** Current {thermal status, battery temperature in tenths of °C}; -1 where unavailable. */
    private int[] sensorReadings() {
        int thermal = -1, temperature = -1;
        try {
            android.os.PowerManager power = getSystemService(android.os.PowerManager.class);
            if (power != null) thermal = power.getCurrentThermalStatus();
        } catch (RuntimeException ignored) { /* shown as n/a */ }
        try {
            Intent battery = registerReceiver(null, new android.content.IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            if (battery != null) temperature = battery.getIntExtra(android.os.BatteryManager.EXTRA_TEMPERATURE, -1);
        } catch (RuntimeException ignored) { /* shown as n/a */ }
        return new int[] {thermal, temperature};
    }

    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            try {
                File job = SetupStore.current(SetupActivity.this);
                JSONObject source = job != null && new File(job, "source.json").isFile() ? SetupStore.read(new File(job, "source.json")) : null;
                if (source == null && job != null && new File(job, "job.json").isFile() &&
                    SetupStore.read(new File(job, "job.json")).getJSONObject("input").optString("kind").equals("image"))
                    source = SetupImport.keySource(job);
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
                // only when the chosen dump can no longer be read (its access was revoked or lost)
                boolean accessMissing = importing && !grants.contains(source.optString("uri"));
                restore.setVisibility(accessMissing ? android.view.View.VISIBLE : android.view.View.GONE);
                restore.setEnabled(accessMissing && replaceable(job));
                keysHint.setVisibility(needsKeys ? android.view.View.VISIBLE : android.view.View.GONE);
                disc.setVisibility(needsKeys ? android.view.View.VISIBLE : android.view.View.GONE);
                common.setVisibility(needsKeys ? android.view.View.VISIBLE : android.view.View.GONE);
                setTextIfChanged(disc, getString(discReady ? R.string.setup_disc_key_ready : source != null && source.has("disc_uri") ? R.string.setup_disc_key_again : R.string.setup_disc_key));
                setTextIfChanged(common, getString(commonReady ? R.string.setup_common_key_ready : source != null && source.has("common_uri") ? R.string.setup_common_key_again : R.string.setup_common_key));
                disc.setEnabled(needsKeys && replaceable(job));
                common.setEnabled(needsKeys && replaceable(job));
                resume.setEnabled(phoneSetup && job != null && (!needsKeys || (discReady && commonReady)));
                pause.setEnabled(phoneSetup && job != null);
                for (Button picker : pickers) picker.setEnabled(phoneSetup && replaceable(job) && !checkingFolder);
                File external = getExternalFilesDir(null);
                play.setEnabled(new File(AndroidGame.storage(SetupActivity.this), "active.json").isFile() ||
                    (external != null && new File(external, "game/code/cking.rpx").isFile()));
                String hostName = hostState == null ? (job == null ? "" : "selected") : hostState.optString("state");
                if (!hostName.equals("warming shaders")) shaderWarmJob = null;
                // the models step waiting for this screen: it goes on here, not while the game plays
                boolean modelsWaiting = hostName.equals("paused") && hostState != null &&
                    ModelsStep.SCREEN_REASON.equals(hostState.optString("reason"));
                boolean working = hostName.equals("activating") || hostName.equals("importing") || hostName.equals("preparing") || hostName.equals("running") ||
                    hostName.equals("warming shaders") || hostName.equals(ModelsStep.STATE);
                show(play, play.isEnabled() && !working && !modelsWaiting);
                show(choose, phoneSetup && !working);
                show(folderHint, phoneSetup && !working);
                setTextIfChanged(choose, getString(job == null ? R.string.setup_choose : R.string.setup_choose_other));
                show(pause, phoneSetup && working && !hostName.equals(ModelsStep.STATE));
                show(cancelModels, phoneSetup && hostName.equals(ModelsStep.STATE));
                if (!hostName.equals(ModelsStep.STATE)) cancelModels.setEnabled(true);
                show(models, phoneSetup);
                show(modelsDetail, phoneSetup);
                models.setEnabled(!working);
                if (!working && models.isChecked() != AndroidGame.modelsChosen(SetupActivity.this)) {
                    // a Cancel turned the step off (ticking it again continues it)
                    models.setOnCheckedChangeListener(null);
                    models.setChecked(!models.isChecked());
                    models.setOnCheckedChangeListener((view, checked) -> chooseModels(checked));
                }
                if (phoneSetup && job != null) routeModels(job, hostName);
                show(codeMods, phoneSetup);
                show(codeModsDetail, phoneSetup);
                codeMods.setEnabled(!working);
                if (!working && codeMods.isChecked() != AndroidGame.codeModsChosen(SetupActivity.this)) {
                    // changed in the game's Settings > Mods meanwhile
                    codeMods.setOnCheckedChangeListener(null);
                    codeMods.setChecked(!codeMods.isChecked());
                    codeMods.setOnCheckedChangeListener((view, checked) -> chooseCodeMods(checked));
                }
                // a heat pause offers Continue only once the device has cooled down (#277)
                boolean heatHot = hostName.equals("paused") && hostState != null &&
                    hostState.optString("reason").equals("heat") && !hostState.optBoolean("cooled");
                show(resume, phoneSetup && job != null && !working && !hostName.equals("complete") && !heatHot && !modelsWaiting);
                setTextIfChanged(resume, getString(hostName.equals("selected") ? R.string.setup_start : R.string.setup_continue));
                show(updateStatus, hostState != null && SetupPresentation.showUpdate(hostState.optBoolean("updating"), hostName));
                plainPhase(job, hostState, hostName, needsKeys, resume.isEnabled());
                show(addIcon, play.isEnabled() && !working && !modelsWaiting && GameShortcut.supported(SetupActivity.this));
                String text;
                if (!phoneSetup) {
                    text = "This APK supports the PC build route. Android device setup needs an APK containing Python and the compiler.";
                    if (play.isEnabled()) offerShortcut();
                }
                else if (job == null) text = "Choose your game dump to set up on this Android device.";
                else {
                    File host = new File(job, "host.json");
                    JSONObject value = host.isFile() ? SetupStore.read(host) : new JSONObject().put("state", "ready to start");
                    text = "Setup: " + value.getString("state");
                    if (SetupPresentation.showUpdate(value.optBoolean("updating"), value.optString("state")))
                        text = SetupPresentation.UPDATING + "\n" + text;
                    if (value.optString("state").equals("complete")) offerShortcut();
                    if (value.optString("state").equals("warming shaders")) { play.setEnabled(false); addIcon.setEnabled(false); }
                    if (value.optString("state").equals("warming shaders") &&
                        !job.getAbsolutePath().equals(shaderWarmJob)) {
                        shaderWarmJob = job.getAbsolutePath();
                        startActivity(new Intent(SetupActivity.this, ShaderPrepareActivity.class)
                            .putExtra("result", new File(job, "shader-warm-result.json").getAbsolutePath()));
                    }
                    String reason = value.optString("state").equals("paused") ? value.optString("reason", null) : null;
                    boolean cooled = "heat".equals(reason) && value.optBoolean("cooled");
                    if (value.has("reason")) {
                        int[] sensors = sensorReadings();
                        text += "\n" + SetupPolicy.description(value.getString("reason"), sensors[0], sensors[1],
                            SetupPolicy.hottestCoreSensor(new File("/sys")), cooled);
                    }
                    if (cooled)
                        text += "\nTap Continue to go on. Setup pauses again if the Android device gets too warm.";
                    else if ("heat".equals(reason))
                        text += "\nSetup waits for you. If the Android device does not feel hot, tap Continue anyway. Setup then ignores heat until this run ends; Android's critical heat level still pauses it.";
                    else if (SetupPolicy.overridable(reason))
                        text += "\nTap Continue anyway to run on battery until this run ends.";
                    continueAction = !SetupPolicy.overridable(reason) ? null :
                        reason.equals("heat") ? SetupService.CONTINUE_HEAT : SetupService.CONTINUE_BATTERY;
                    if (value.has("error")) text += "\n" + value.getString("error");
                    if (importing && !grants.contains(source.optString("uri")))
                        text += "\nDump access is missing. Restore access to the same selected dump if another file copy is needed. Completed private copies are retained.";
                    if (needsKeys) text += "\n" + (resume.isEnabled() ? correctingKeys ?
                        "Private dump retained. Change either key if needed, then tap Start / resume to retry." :
                        getString(R.string.setup_keys_ready) : getString(R.string.setup_keys_detail));
                    if (value.optString("state").equals("importing")) {
                        File imported = new File(job, "import-progress.json");
                        if (imported.isFile()) {
                            JSONObject progress = SetupStore.read(imported);
                            text += "\nCopied " + progress.getLong("copied") / (1024 * 1024) + " MiB; " + progress.getInt("complete") + "/" + progress.getInt("files") + " files";
                        }
                    }
                    File progress = new File(job, "state.json");
                    if (value.optString("state").equals("running") && progress.isFile()) {
                        JSONObject worker = SetupStore.read(progress);
                        org.json.JSONArray keySources = worker.optJSONArray("key_sources");
                        if (keySources != null) for (int i = 0; i < keySources.length(); i++) text += "\n" + keySources.getString(i);
                        JSONObject event = worker.optJSONObject("last_event");
                        String compiling = event == null || !event.optString("stage").equals("compile") ? null :
                            SetupPolicy.compileProgress(event.optString("state"), event.optInt("compiled"), event.optInt("reused"),
                                event.optInt("total"), event.optLong("eta_seconds", -1));
                        if (compiling != null) text += "\n" + compiling;
                        else if (event != null && event.optString("stage").equals("shaders") && event.has("fraction"))
                            text += "\nReading shaders from your game files: " + Math.round(100 * event.optDouble("fraction")) + "%\n" + event.optString("detail");
                        else if (event != null) text += "\n" + event.optString("stage", "setup") + ": " + event.optString("state");
                    }
                }
                if (job == null || !phoneSetup) continueAction = null;
                continueAnyway.setVisibility(continueAction != null ? android.view.View.VISIBLE : android.view.View.GONE);
                if (actionMessage != null) text += "\n" + actionMessage;
                setTextIfChanged(status, text + "\n\n" + CHECKPOINTS);
            } catch (Exception failure) { setTextIfChanged(status, "Cannot read setup progress: " + failure.getMessage()); }
            handler.postDelayed(this, 500);
        }
    };

    @Override protected void onResume() {
        super.onResume();
        if (status == null) return; // launcher is waiting for the crash-share decision
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
