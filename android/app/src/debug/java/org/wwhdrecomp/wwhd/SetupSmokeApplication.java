package org.wwhdrecomp.wwhd;

import android.app.Application;
import java.io.File;
import org.json.JSONObject;

/** Opt-in authored-job routing for real package-update broadcasts, absent from release. */
public final class SetupSmokeApplication extends Application implements SetupService.Host {
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
