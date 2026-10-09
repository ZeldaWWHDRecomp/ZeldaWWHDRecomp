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
    private static final int NOTIFICATION = 41;
    private static final String CHANNEL = "setup";
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final SetupPolicy policy = new SetupPolicy();
    private PowerManager power;
    private PowerManager.WakeLock wake;
    private File job;
    private boolean running;
    private volatile boolean destroyed;
    private int thermal;
    private int temperature = -1;
    private int battery = -1;
    private boolean charging;
    private String reason;
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
        try {
            File selected = SetupStore.current(this);
            if (selected == null) throw new IOException("Choose a game dump before starting setup");
            if (running && !selected.equals(job)) throw new IOException("Another setup job is still running");
            job = selected;
            String action = intent == null ? null : intent.getAction();
            if (PAUSE.equals(action)) SetupStore.write(new File(job, "manual-pause.json"), new JSONObject().put("schema", 1));
            if (RESUME.equals(action)) SetupStore.remove(new File(job, "manual-pause.json"));
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
                reason = policy.update(thermal, temperature, battery, charging, new File(job, "manual-pause.json").exists());
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
                configuration.put("port_revision", revision).put("jobs", 1);
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
                    !ready.getString("compiler_identity").equals(configuration.getJSONObject("compiler").getString("identity")))
                    throw new IOException("Completed build belongs to different setup inputs");
                if (!active.equals(SetupStore.current(this))) throw new IOException("The selected setup job changed");
                host("activating", null, null);
                checkPause(active);
                AndroidGame.activate(this, new File(ready.getString("library")), new File(ready.getString("game")));
                host("complete", null, null);
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
        EmbeddedPython.execute(this, module,
                new JSONArray().put("--job").put(configuration.getPath()).toString(),
                () -> checkPause(configuration.getParentFile()));
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
            if (failure != null) host("failed", null, failure.toString());
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
        if (pause != null) value.put("reason", pause);
        if (error != null) value.put("error", error.substring(0, Math.min(error.length(), 2048)));
        SetupStore.write(new File(job, "host.json"), value);
    }

    private void updateNotification() throws Exception {
        JSONObject status = SetupStore.read(new File(job, "host.json"));
        String state = status.getString("state");
        String text = state;
        if (reason != null) text = running ? "Pausing at the next safe checkpoint" : SetupPolicy.description(reason);
        else if (state.equals("running")) {
            File progress = new File(job, "state.json");
            if (progress.isFile()) {
                JSONObject event = SetupStore.read(progress).optJSONObject("last_event");
                if (event != null) text = event.optString("stage", "setup");
            }
        } else if (state.equals("complete")) text = "Setup complete — ready to play";
        else if (state.equals("failed")) text = "Setup failed — open setup to retry";
        if (!text.equals(lastNotification)) {
            getSystemService(NotificationManager.class).notify(NOTIFICATION, notification(text, state.equals("complete") || state.equals("failed")));
            lastNotification = text;
        }
    }

    private Notification notification(String text, boolean finished) {
        PendingIntent open = PendingIntent.getActivity(this, 0, new Intent(this, SetupActivity.class), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder result = new Notification.Builder(this, CHANNEL).setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle("Wind Waker HD setup").setContentText(text).setContentIntent(open)
            .setOnlyAlertOnce(true).setOngoing(!finished);
        if (!finished) {
            String action = reason != null && reason.equals("manual") ? RESUME : PAUSE;
            PendingIntent control = PendingIntent.getForegroundService(this, action.equals(PAUSE) ? 1 : 2,
                new Intent(this, getClass()).setAction(action), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
            result.addAction(new Notification.Action.Builder(null, action.equals(PAUSE) ? "Pause" : "Resume", control).build());
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
        super.onDestroy();
    }
}
