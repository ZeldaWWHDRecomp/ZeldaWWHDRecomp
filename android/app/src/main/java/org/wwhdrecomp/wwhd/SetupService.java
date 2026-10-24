package org.wwhdrecomp.wwhd;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ServiceInfo;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;
import java.io.File;
import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import org.json.JSONArray;
import org.json.JSONObject;

/** One user-visible setup worker, kept separate from the SDL/game process. */
public class SetupService extends Service {
    public static final String RESUME = "org.wwhdrecomp.wwhd.SETUP_RESUME";
    public static final String PAUSE = "org.wwhdrecomp.wwhd.SETUP_PAUSE";
    /** "Continue anyway": skip heat (or battery) pauses for the rest of this setup run. */
    public static final String CONTINUE_HEAT = "org.wwhdrecomp.wwhd.SETUP_CONTINUE_HEAT";
    public static final String CONTINUE_BATTERY = "org.wwhdrecomp.wwhd.SETUP_CONTINUE_BATTERY";
    /** Job-dir records of the user's choice; kept across service restarts, cleared when the run ends. */
    public static final String HEAT_OVERRIDE = "continue-heat.json";
    public static final String BATTERY_OVERRIDE = "continue-battery.json";
    /**
     * Job-dir record of a heat pause that waits for the user (#277): it survives service restarts
     * (screen off, app in the background, process killed) and goes with Continue, Continue anyway
     * or the end of the run. host.json then carries "cooled" once Continue may end it.
     */
    public static final String HEAT_WAIT = "heat-pause.json";
    private static final int NOTIFICATION = 41;
    private static final String CHANNEL = "setup";
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final SetupPolicy policy = new SetupPolicy();
    private PowerManager power;
    private PowerManager.WakeLock wake;
    private File job;
    /** The job whose recorded heat pause {@link #policy} holds. */
    private File heatJob;
    /** This service's lock on {@link #job}: old-job cleanup leaves a held job alone. */
    private SetupJobCleanup.Hold hold;
    private boolean running;
    private boolean updating;
    private volatile boolean destroyed;
    private int thermal;
    private int temperature = -1;
    /** Hottest CPU/GPU sensor in °C (SetupPolicy.hottestCoreSensor), NaN while unknown. */
    private volatile float hottestCore = Float.NaN;
    private Boolean loggedUnreliable;
    private int battery = -1;
    private boolean charging;
    private String reason;
    private String loggedReason;
    private String lastNotification;
    private long renewed;

    /** An application may supply a service implementation; release uses this service. */
    public interface Host { Class<? extends SetupService> setupServiceClass(); }

    public static void start(Context context, String action) {
        Context application = context.getApplicationContext();
        Class<? extends SetupService> service = application instanceof Host ?
            ((Host)application).setupServiceClass() : SetupService.class;
        context.startForegroundService(new Intent(context, service).setAction(action));
    }

    private final PowerManager.OnThermalStatusChangedListener thermalListener = value -> thermal = value;
    private final BroadcastReceiver batteryListener = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) { updateBattery(intent); }
    };

    private void updateBattery(Intent intent) {
        int level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
        int scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1);
        battery = level >= 0 && scale > 0 ? (int)(100L * level / scale) : -1;
        temperature = intent.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, -1);
        charging = intent.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0;
    }

    @Override public void onCreate() {
        super.onCreate();
        getSystemService(NotificationManager.class).createNotificationChannel(
            new NotificationChannel(CHANNEL, "Game setup", NotificationManager.IMPORTANCE_LOW));
        power = getSystemService(PowerManager.class);
        thermal = power.getCurrentThermalStatus();
        power.addThermalStatusListener(thermalListener);
        Intent initialBattery = registerReceiver(batteryListener,
                new IntentFilter(Intent.ACTION_BATTERY_CHANGED), Context.RECEIVER_NOT_EXPORTED);
        // Consume the returned sticky snapshot before the first policy tick.
        // Some API 33 images do not immediately deliver it to onReceive.
        if (initialBattery != null) updateBattery(initialBattery);
        wake = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "wwhd:setup");
        wake.setReferenceCounted(false);
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        Notification initial = notification("Preparing setup", false);
        if (Build.VERSION.SDK_INT >= 34) startForeground(NOTIFICATION, initial, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        else startForeground(NOTIFICATION, initial);
        lastNotification = null; // Replace the initial text even when resuming the same phase.
        try {
            File selected = SetupStore.current(this);
            if (selected == null) throw new IOException("Choose a game dump before starting setup");
            if (running && !selected.equals(job)) throw new IOException("Another setup job is still running");
            job = selected;
            if (hold == null || !hold.job.equals(job)) {
                if (hold != null) { hold.close(); hold = null; }
                hold = SetupJobCleanup.hold(job);
                if (hold == null) throw new IOException("The chosen setup job is in use");
            }
            File previousHost = new File(job, "host.json");
            if (!running) updating = false;
            if (!running && previousHost.isFile()) {
                JSONObject previous = SetupStore.read(previousHost);
                updating = previous.optString("state").equals("complete") ? AndroidGame.needsRebuild(this) :
                    previous.optBoolean("updating");
            }
            if (!job.equals(heatJob)) {
                policy.restoreHeatWait(new File(job, HEAT_WAIT).isFile());
                heatJob = job;
                if (policy.heatWaiting()) Log.i("wwhd-setup", "heat pause restored: waiting for the user");
            }
            String action = intent == null ? null : intent.getAction();
            if (PAUSE.equals(action)) SetupStore.write(new File(job, "manual-pause.json"), new JSONObject().put("schema", 1));
            if (RESUME.equals(action)) {
                SetupStore.remove(new File(job, "manual-pause.json"));
                // Continue on a heat pause: only once the device has cooled down (#277)
                if (policy.continueAfterCooling()) {
                    SetupStore.remove(new File(job, HEAT_WAIT));
                    Log.i("wwhd-setup", "continue after cooling down (" +
                        SetupPolicy.readings(thermal, temperature, hottestCore) + ")");
                }
            }
            if (CONTINUE_HEAT.equals(action) || CONTINUE_BATTERY.equals(action)) {
                SetupStore.write(new File(job, CONTINUE_HEAT.equals(action) ? HEAT_OVERRIDE : BATTERY_OVERRIDE),
                    new JSONObject().put("schema", 1).put("since", System.currentTimeMillis()));
                Log.i("wwhd-setup", "continue anyway: " + (CONTINUE_HEAT.equals(action) ? "heat" : "battery") +
                    " pauses skipped for this setup run (" + SetupPolicy.readings(thermal, temperature, hottestCore) +
                    ", battery " + battery + "%, charging " + charging + ")");
            }
            main.removeCallbacks(tick);
            tick.run();
            return START_STICKY;
        } catch (Exception failure) {
            Log.e("wwhd-setup", "Cannot start setup", failure);
            if (!running) { stopForeground(STOP_FOREGROUND_REMOVE); stopSelf(); }
            return running ? START_STICKY : START_NOT_STICKY;
        }
    }

    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (destroyed || job == null) return;
            try {
                boolean heatOverride = new File(job, HEAT_OVERRIDE).exists();
                boolean batteryOverride = new File(job, BATTERY_OVERRIDE).exists();
                hottestCore = SetupPolicy.hottestCoreSensor(new File("/sys"));
                // A SEVERE status no temperature backs up is ignored (issue #254, SetupPolicy).
                boolean unreliable = SetupPolicy.thermalStatusUnreliable(thermal, temperature, hottestCore);
                if (!Boolean.valueOf(unreliable).equals(loggedUnreliable)) {
                    if (unreliable || loggedUnreliable != null)
                        Log.i("wwhd-setup", "thermal status " + (unreliable ? "looks unreliable, ignored" : "believed") +
                            " (" + SetupPolicy.readings(thermal, temperature, hottestCore) + ")");
                    loggedUnreliable = unreliable;
                }
                reason = policy.update(thermal, temperature, hottestCore, battery, charging,
                    new File(job, "manual-pause.json").exists(), heatOverride, batteryOverride, android.os.SystemClock.elapsedRealtime());
                if (!java.util.Objects.equals(reason, loggedReason)) {
                    Log.i("wwhd-setup", (reason == null ? "resume" : "pause: " + reason) + " (" +
                        SetupPolicy.readings(thermal, temperature, hottestCore) + ", battery " + battery + "%, charging " + charging +
                        ", continue anyway: heat " + heatOverride + ", battery " + batteryOverride + ")");
                    loggedReason = reason;
                }
                File heatWait = new File(job, HEAT_WAIT);
                if (policy.heatWaiting() && !heatWait.isFile())
                    SetupStore.write(heatWait, new JSONObject().put("schema", 1).put("since", System.currentTimeMillis()));
                else if (!policy.heatWaiting() && heatWait.exists()) SetupStore.remove(heatWait);
                File pause = new File(job, "pause.json");
                if (reason == null) {
                    if (pause.exists()) SetupStore.remove(pause);
                } else if (!pause.isFile() || !reason.equals(SetupStore.read(pause).optString("reason"))) {
                    SetupStore.write(pause, new JSONObject().put("schema", 1).put("reason", reason));
                }
                if (!running && reason == null) launch();
                if (!running && reason != null) host("paused", reason, null);
                if (running && android.os.SystemClock.elapsedRealtime() - renewed > 60000) holdWake();
                updateNotification();
            } catch (Exception failure) {
                Log.e("wwhd-setup", "Cannot update setup controls", failure);
                // Stop launching work when durable control writes fail.
                if (!running) { stopForeground(STOP_FOREGROUND_REMOVE); stopSelf(); return; }
            }
            main.postDelayed(this, 1000);
        }
    };

    private void holdWake() {
        wake.acquire(10 * 60 * 1000L);
        renewed = android.os.SystemClock.elapsedRealtime();
    }

    private void launch() throws Exception {
        final File active = job;
        host("preparing", null, null);
        running = true;
        holdWake();
        worker.execute(() -> {
            final long started = System.currentTimeMillis();
            boolean paused = false;
            Exception error = null;
            try {
                checkPause(active);
                if (!new File(active, "job.json").isFile()) host("importing", null, null);
                JSONObject configuration = SetupImport.prepare(this, active, new SetupImport.Control() {
                    @Override public void checkpoint() throws Exception { checkPause(active); }
                    @Override public void progress(long copied, long total, int complete, int files) throws Exception {
                        SetupStore.write(new File(active, "import-progress.json"), new JSONObject().put("schema", 1)
                            .put("copied", copied).put("total", total).put("complete", complete).put("files", files));
                    }
                });
                host("preparing", null, null);
                if (configuration.getInt("schema") != 1) throw new IOException("Unsupported setup job version");
                configuration.put("compiler", AndroidToolchain.prepare(this, () -> checkPause(active)));
                checkPause(active);
                String kind = configuration.getJSONObject("input").getString("kind");
                if (!kind.equals("folder")) configuration.put("extractor", AndroidExtractor.prepare(this));
                String revision = EmbeddedPython.checksum(this, "python-source.zip", () -> checkPause(active));
                // the APK's native Yaz0 decoder for shader preparation (shaderprep.py; android/build_native.sh)
                File yaz0 = new File(getApplicationInfo().nativeLibraryDir, "libyaz0.so");
                if (yaz0.isFile()) configuration.put("yaz0", yaz0.getAbsolutePath());
                // the setup's Code mods switch: game code with mod support (read at every start, so a
                // changed choice rebuilds)
                configuration.put("code_mods", AndroidGame.codeModsChosen(this));
                configuration.put("port_revision", revision).put("jobs", compileJobs())
                    .put("shader_cache_root", AndroidUserDirectories.shaderPrebuild(getExternalFilesDir(null)).getAbsolutePath());
                SetupStore.write(new File(active, "job.json"), configuration);
                checkPause(active);
                host("running", null, null);
                executeBuild(new File(active, "job.json"));
                checkPause(active);
                JSONObject status = SetupStore.read(new File(active, "state.json"));
                if (status.getString("state").equals("paused")) throw new Pause();
                if (!status.getString("state").equals("ready_to_activate")) throw new IOException("Setup did not finish building");
                JSONObject ready = SetupStore.read(new File(active, "ready.json"));
                if (ready.getInt("schema") != 1 || !ready.getString("port_revision").equals(revision) ||
                    !ready.getString("compiler_identity").equals(configuration.getJSONObject("compiler").getString("identity")) ||
                    ready.optBoolean("code_mods", false) != configuration.getBoolean("code_mods"))
                    throw new IOException("Completed build belongs to different setup inputs");
                if (!active.equals(SetupStore.current(this))) throw new IOException("The selected setup job changed");
                host("activating", null, null);
                checkPause(active);
                AndroidGame.activate(this, new File(ready.getString("library")), new File(ready.getString("game")),
                    configuration.getBoolean("code_mods"));
                JSONObject shaders = ready.optJSONObject("shader_prebuild");
                if (shaders != null && shaders.optString("status").equals("warm_pending")) {
                    File result = new File(active, "shader-warm-result.json");
                    if (result.isFile() && !result.delete()) throw new IOException("Cannot replace shader preparation result");
                    host("warming shaders", null, null);
                    long deadline = android.os.SystemClock.elapsedRealtime() + 300000;
                    while (!result.isFile() && android.os.SystemClock.elapsedRealtime() < deadline) {
                        checkPause(active);
                        Thread.sleep(200);
                    }
                    if (!result.isFile() || !SetupStore.read(result).optString("status").equals("ready")) {
                        host("complete", null, "Shader preparation skipped or failed; shaders will compile during play.");
                    } else host("complete", null, null);
                } else host("complete", null, null);
                SetupKeyAccess.cleanupCompleted(active);
            } catch (Pause requested) { paused = true; }
            catch (Exception failure) {
                error = failure;
                try {
                    JSONObject status = SetupStore.read(new File(active, "state.json"));
                    if (status.optString("state").equals("failed") && status.optDouble("updated", 0) * 1000 >= started)
                        error = new IOException(status.optString("error", failure.toString()));
                } catch (Exception ignored) { }
            }
            final boolean wasPaused = paused;
            final Exception failure = error;
            main.post(() -> finished(wasPaused, failure));
        });
    }

    private static final class Pause extends Exception { }
    /** The release service always invokes the validating production entry point. */
    protected void executeBuild(File configuration) throws Exception {
        executePythonBuild("ondevice_setup", configuration);
    }

    protected final void executePythonBuild(String module, File configuration) throws Exception {
        JSONArray arguments = new JSONArray().put("--job").put(configuration.getPath());
        java.util.ArrayList<android.os.ParcelFileDescriptor> descriptors = new java.util.ArrayList<>();
        try {
            File active = configuration.getParentFile();
            JSONObject value = SetupStore.read(configuration);
            if (value.getJSONObject("input").optString("kind").equals("image")) {
                java.util.Map<String, android.os.ParcelFileDescriptor> opened = SetupKeyRecoveryPolicy.openBoth(
                    key -> SetupKeyAccess.open(this, active, value, key));
                descriptors.addAll(opened.values());
                for (java.util.Map.Entry<String, android.os.ParcelFileDescriptor> entry : opened.entrySet())
                    arguments.put("--" + entry.getKey() + "-key-fd").put(Integer.toString(entry.getValue().getFd()));
            }
            EmbeddedPython.execute(this, module, arguments.toString(), () -> checkPause(configuration.getParentFile()));
        } finally {
            for (android.os.ParcelFileDescriptor descriptor : descriptors) descriptor.close();
        }
    }

    /** Parallel compiler processes for this phone (SetupPolicy.compileJobs). */
    private int compileJobs() {
        android.app.ActivityManager activities = (android.app.ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        android.app.ActivityManager.MemoryInfo memory = new android.app.ActivityManager.MemoryInfo();
        boolean lowRam = true;
        if (activities != null) { activities.getMemoryInfo(memory); lowRam = activities.isLowRamDevice(); }
        PowerManager power = (PowerManager) getSystemService(Context.POWER_SERVICE);
        int reported = power != null && Build.VERSION.SDK_INT >= 29 ? power.getCurrentThermalStatus() : 0;
        int thermal = SetupPolicy.trustedThermal(reported, temperature, hottestCore);
        int jobs = SetupPolicy.compileJobs(Runtime.getRuntime().availableProcessors(), memory.availMem, lowRam, thermal);
        Log.i("wwhd-setup", "compile workers " + jobs + " (cores " + Runtime.getRuntime().availableProcessors() +
            ", free memory " + (memory.availMem >> 20) + " MB, low-RAM " + lowRam + ", thermal " + reported +
            (thermal != reported ? " (looks unreliable, ignored)" : "") + ")");
        return jobs;
    }

    private void checkPause(File active) throws Pause {
        if (destroyed || new File(active, "pause.json").exists() || new File(active, "manual-pause.json").exists()) throw new Pause();
    }

    private void finished(boolean paused, Exception failure) {
        running = false;
        if (wake.isHeld()) wake.release();
        if (destroyed) return;
        try {
            if (paused) {
                host("paused", reason == null ? "checkpoint" : reason, null);
                updateNotification();
                return; // Sensor changes or a notification action can resume.
            }
            if (failure != null) host("failed", null, failure instanceof SetupKeyAccess.Missing ? failure.getMessage() : failure.toString());
            // The run ended: a later run asks again before skipping heat or battery pauses.
            SetupStore.remove(new File(job, HEAT_OVERRIDE));
            SetupStore.remove(new File(job, BATTERY_OVERRIDE));
            SetupStore.remove(new File(job, HEAT_WAIT));
            policy.restoreHeatWait(false);
            main.removeCallbacks(tick);
            updateNotification();
            stopForeground(STOP_FOREGROUND_DETACH);
            stopSelf();
        } catch (Exception problem) {
            Log.e("wwhd-setup", "Cannot finish setup", problem);
            stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf();
        }
    }

    private void host(String state, String pause, String error) throws Exception {
        JSONObject value = new JSONObject().put("schema", 1).put("state", state)
            .put("updated", System.currentTimeMillis());
        value.put("updating", updating);
        if (pause != null) value.put("reason", pause);
        if ("heat".equals(pause)) value.put("cooled", policy.heatCooled());
        if (error != null) value.put("error", error.substring(0, Math.min(error.length(), 2048)));
        SetupStore.write(new File(job, "host.json"), value);
    }

    private void updateNotification() throws Exception {
        JSONObject status = SetupStore.read(new File(job, "host.json"));
        String state = status.getString("state");
        String text = state;
        if (reason != null) text = running ? "Pausing at the next safe checkpoint" :
            SetupPolicy.description(reason, thermal, temperature, hottestCore, "heat".equals(reason) && policy.heatCooled());
        else if (state.equals("running")) {
            File progress = new File(job, "state.json");
            if (progress.isFile()) {
                JSONObject event = SetupStore.read(progress).optJSONObject("last_event");
                if (event != null) text = SetupPresentation.stageLabel(event.optString("stage", "setup"));
            }
        } else if (state.equals("warming shaders")) text = "Tap to prepare shaders for this device";
        else if (state.equals("complete")) text = "Setup complete — ready to play";
        else if (state.equals("failed")) text = "Setup failed — open setup to retry";
        if (SetupPresentation.showUpdate(updating, state))
            text = SetupPresentation.UPDATING + " · " + text;
        if (!text.equals(lastNotification)) {
            getSystemService(NotificationManager.class).notify(NOTIFICATION, notification(text, state.equals("complete") || state.equals("failed")));
            lastNotification = text;
        }
    }

    static Intent notificationIntent(Context context) {
        // Always show setup; onResume routes a warming job to ShaderPrepareActivity.
        return new Intent(context, SetupActivity.class).putExtra(SetupActivity.EXTRA_SHOW_SETUP, true);
    }

    private Notification notification(String text, boolean finished) {
        PendingIntent open = PendingIntent.getActivity(this, 0, notificationIntent(this), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder result = new Notification.Builder(this, CHANNEL).setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle("Wind Waker HD setup").setContentText(text)
            .setStyle(new Notification.BigTextStyle().bigText(text)).setContentIntent(open)
            .setOnlyAlertOnce(true).setOngoing(!finished);
        if (!finished) {
            boolean cooled = !running && "heat".equals(reason) && policy.heatCooled();
            String action = (reason != null && reason.equals("manual")) || cooled ? RESUME : PAUSE;
            PendingIntent control = PendingIntent.getForegroundService(this, action.equals(PAUSE) ? 1 : 2,
                new Intent(this, getClass()).setAction(action), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
            result.addAction(new Notification.Action.Builder(null, action.equals(PAUSE) ? "Pause" : cooled ? "Continue" : "Resume", control).build());
            if (!running && SetupPolicy.overridable(reason)) {
                String skip = reason.equals("heat") ? CONTINUE_HEAT : CONTINUE_BATTERY;
                PendingIntent anyway = PendingIntent.getForegroundService(this, skip.equals(CONTINUE_HEAT) ? 3 : 4,
                    new Intent(this, getClass()).setAction(skip), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
                result.addAction(new Notification.Action.Builder(null, "Continue anyway", anyway).build());
            }
        }
        return result.build();
    }

    @Override public IBinder onBind(Intent intent) { return null; }
    @Override public void onDestroy() {
        destroyed = true;
        main.removeCallbacks(tick);
        power.removeThermalStatusListener(thermalListener);
        unregisterReceiver(batteryListener);
        if (running && job != null) {
            try { SetupStore.write(new File(job, "pause.json"), new JSONObject().put("schema", 1).put("reason", "service_stopped")); }
            catch (Exception failure) { Log.w("wwhd-setup", "Cannot persist stop checkpoint", failure); }
        }
        if (wake.isHeld()) wake.release();
        worker.shutdown();
        if (hold != null && !running) {
            try { hold.close(); }
            catch (IOException failure) { Log.w("wwhd-setup", "Cannot release the setup job", failure); }
            hold = null;
        }
        super.onDestroy();
    }
}
