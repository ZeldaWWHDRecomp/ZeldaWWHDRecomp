package org.wwhdrecomp.wwhd;

import android.app.Activity;
import android.os.Bundle;
import java.io.File;
import org.json.JSONArray;
import org.json.JSONObject;

/** Execute the selected authored runtime after an update failure, in the setup process. */
public final class ActiveRuntimeSmokeActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        new Thread(() -> {
            File output = new File(getFilesDir(), "active-runtime-smoke.json");
            try {
                File library = AndroidGame.current(this);
                if (library == null) throw new IllegalStateException("No compatible active runtime");
                EmbeddedPython.execute(this, "service_fixture", new JSONArray()
                    .put("--inspect-library").put(library.getAbsolutePath())
                    .put("--inspect-output").put(output.getAbsolutePath()).toString());
            } catch (Exception failure) {
                try { SetupStore.write(output, new JSONObject().put("passed", false).put("error", failure.toString())); }
                catch (Exception ignored) { }
            }
            runOnUiThread(this::finish);
        }, "active-runtime-smoke").start();
    }
}
