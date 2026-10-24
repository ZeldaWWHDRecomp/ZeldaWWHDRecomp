package org.wwhdrecomp.wwhd;

import android.app.Application;
import java.io.File;
import org.json.JSONObject;

/** Opt-in authored-job routing for real package-update broadcasts, absent from release. */
public final class SetupSmokeApplication extends Application implements SetupService.Host {
    @Override public void onCreate() {
        super.onCreate();
        registerActivityLifecycleCallbacks(new ActivityLifecycleCallbacks() {
            // PostCreated runs after the complete SDL/Wwhd onCreate routing hook.
            @Override public void onActivityPostCreated(android.app.Activity activity, android.os.Bundle state) {
                if (activity.getClass() != ShaderPrepareActivity.class) return;
                File result = new File(getFilesDir(), "shader-routing-smoke.json");
                try {
                    if (!result.isFile() || !SetupStore.read(result).optBoolean("pending")) return;
                    SetupStore.write(result, new JSONObject().put("pending", false)
                        .put("activity", activity.getClass().getName())
                        .put("finishing", activity.isFinishing()));
                } catch (Exception error) { throw new IllegalStateException(error); }
            }
            @Override public void onActivityCreated(android.app.Activity a, android.os.Bundle b) {}
            @Override public void onActivityStarted(android.app.Activity a) {}
            @Override public void onActivityResumed(android.app.Activity a) {}
            @Override public void onActivityPaused(android.app.Activity a) {}
            @Override public void onActivityStopped(android.app.Activity a) {}
            @Override public void onActivitySaveInstanceState(android.app.Activity a, android.os.Bundle b) {}
            @Override public void onActivityDestroyed(android.app.Activity a) {}
        });
    }
    @Override public Class<? extends SetupService> setupServiceClass() {
        try {
            JSONObject fixture = SetupStore.read(new File(getNoBackupFilesDir(), "setup-service-fixture.json"));
            File job = SetupStore.current(this);
            if (fixture.getInt("schema") == 1 && job != null &&
                    job.getName().equals(fixture.getString("job"))) return SyntheticSetupService.class;
        } catch (Exception absent) { /* Ordinary debug builds use the validating production service. */ }
        return SetupService.class;
    }
}
