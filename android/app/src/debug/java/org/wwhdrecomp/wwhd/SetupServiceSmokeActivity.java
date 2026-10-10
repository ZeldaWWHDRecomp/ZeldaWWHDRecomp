package org.wwhdrecomp.wwhd;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;
import android.widget.TextView;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.UUID;
import org.json.JSONObject;

/** Emulator-only harness controls a synthetic, deliberately unsupported input. */
public final class SetupServiceSmokeActivity extends Activity {
    private TextView view;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        view = new TextView(this);
        setContentView(view);
        operate();
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        operate();
    }

    private void operate() {
        JSONObject result = new JSONObject();
        try {
            String mode = getIntent().getStringExtra("mode");
            if ("create".equals(mode)) {
                String id = UUID.randomUUID().toString().replace("-", "");
                File job = SetupStore.job(this, id);
                File game = new File(job, "input/game");
                new File(game, "code").mkdirs();
                new File(game, "content").mkdirs();
                new File(game, "meta").mkdirs();
                Files.write(new File(game, "code/cking.rpx").toPath(), "authored unsupported synthetic input".getBytes(StandardCharsets.UTF_8));
                Files.write(new File(game, "meta/meta.xml").toPath(), "<menu/>".getBytes(StandardCharsets.UTF_8));
                SetupStore.write(new File(job, "job.json"), new JSONObject().put("schema", 1)
                    .put("input", new JSONObject().put("kind", "folder").put("path", "input/game")));
                SetupStore.write(new File(job, "manual-pause.json"), new JSONObject().put("schema", 1));
                SetupStore.select(this, id);
                SetupService.start(this, null);
                result.put("job", id);
            } else if ("pause".equals(mode)) SetupService.start(this, SetupService.PAUSE);
            else if ("resume".equals(mode)) SetupService.start(this, SetupService.RESUME);
            else if ("start".equals(mode)) SetupService.start(this, null);
            else if ("synthetic-build".equals(mode)) {
                File job = SetupStore.current(this);
                SetupStore.remove(new File(job, "manual-pause.json"));
                SetupStore.remove(new File(job, "pause.json"));
                startForegroundService(new Intent(this, SyntheticSetupService.class));
            }
            else if ("synthetic-recovery".equals(mode)) {
                File job = SetupStore.current(this);
                if (job == null) throw new IllegalStateException("No authored job selected");
                SetupStore.write(new File(getNoBackupFilesDir(), "setup-service-fixture.json"),
                    new JSONObject().put("schema", 1).put("job", job.getName()));
                result.put("job", job.getName());
            }
            else if ("kill-worker".equals(mode)) {
                int expected = getIntent().getIntExtra("pid", -1);
                boolean matched = false;
                for (ActivityManager.RunningAppProcessInfo process :
                        getSystemService(ActivityManager.class).getRunningAppProcesses()) {
                    if (process.uid == Process.myUid() && process.pid == expected &&
                            process.processName.equals(getPackageName() + ":setup")) matched = true;
                }
                if (!matched) throw new IllegalArgumentException("Expected live setup process is absent");
                // Same app domain: older Android SELinux policies deny run-as
                // shell signals even when the worker has the same Linux UID.
                Process.killProcess(expected);
                result.put("terminated", expected);
            }
            else if ("stop".equals(mode)) stopService(new Intent(this, SetupService.class));
            else if ("open".equals(mode)) startActivity(new Intent(this, SetupActivity.class));
            else throw new IllegalArgumentException("Unknown service probe operation");
            result.put("passed", true);
        } catch (Exception failure) {
            try { result.put("passed", false).put("error", failure.toString()); }
            catch (Exception ignored) { }
        }
        try { SetupStore.write(new File(getFilesDir(), "setup-service-smoke.json"), result); }
        catch (Exception failure) { view.setText(failure.toString()); return; }
        view.setText(result.toString());
    }
}
