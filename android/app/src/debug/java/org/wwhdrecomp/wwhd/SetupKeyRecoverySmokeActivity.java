package org.wwhdrecomp.wwhd;

import android.app.Activity;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import org.json.JSONObject;

/** Multi-process-start recovery probe, with authored provider access-loss failures. */
public final class SetupKeyRecoverySmokeActivity extends Activity {
    private final SetupImport.Control control = new SetupImport.Control() {
        @Override public void checkpoint() { }
        @Override public void progress(long copied, long total, int complete, int files) { }
    };
    private Uri document(String id) { return DocumentsContract.buildDocumentUri(ImportDocuments.authority(this), id); }
    private static void require(boolean value, String message) throws IOException {
        if (!value) throw new IOException(message);
    }
    private void readKey(File job, JSONObject config, String key, int expected) throws Exception {
        try (ParcelFileDescriptor descriptor = SetupKeyAccess.open(this, job, config, key);
             ParcelFileDescriptor.AutoCloseInputStream stream = new ParcelFileDescriptor.AutoCloseInputStream(descriptor)) {
            byte[] bytes = stream.readAllBytes();
            require(bytes.length == 16 && bytes[0] == expected, "Wrong authored key input");
        }
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        final String phase = getIntent().getStringExtra("phase");
        new Thread(() -> {
            JSONObject result = new JSONObject();
            try {
                File job = new File(getNoBackupFilesDir(), "key-recovery-fixture");
                File configuration = new File(job, "job.json");
                android.content.SharedPreferences provider = getSharedPreferences("key-recovery-fixture", 0);
                if (phase.equals("prepare")) {
                    provider.edit().clear().commit();
                    SetupStore.write(new File(job, "source.json"), new JSONObject().put("schema", 1).put("kind", "image")
                        .put("uri", document("dump").toString()).put("disc_uri", document("key").toString())
                        .put("common_uri", document("key").toString()));
                    SetupImport.prepare(this, job, control);
                }
                // Exercise the actual import/resume path after process death, not just key opens.
                JSONObject config = phase.equals("restart") ? SetupImport.prepare(this, job, control) : SetupStore.read(configuration);
                if (phase.equals("prepare") || phase.equals("restart")) {
                    readKey(job, config, "disc", 42);
                    readKey(job, config, "common", 42);
                    require(!new File(job, "input/disc.key").exists(), "New job saved key bytes");
                } else if (phase.equals("revoked") || phase.equals("moved")) {
                    provider.edit().clear().putBoolean(phase, true).commit();
                    try { readKey(job, config, "disc", 42); throw new IOException("Access loss was ignored"); }
                    catch (SetupKeyAccess.Missing missing) {
                        require(missing.getMessage().contains("choose the key again"), "Missing actionable key error");
                        require(SetupKeyAccess.failed(job, "disc"), "Access loss was not persisted");
                    }
                    SetupStore.write(new File(job, "host.json"), new JSONObject().put("state", "failed"));
                } else if (phase.equals("lost-restart")) {
                    require(SetupKeyAccess.failed(job, "disc"), "Restart lost the choose-again state");
                    require(getString(R.string.setup_disc_key_again).contains("choose the key again"), "Wrong recovery label");
                } else if (phase.equals("reselect")) {
                    provider.edit().clear().commit();
                    SetupImport.selectKey(job, "disc", document("replacementkey"));
                    config = SetupImport.prepare(this, job, control);
                    readKey(job, config, "disc", 43);
                    require(!SetupKeyAccess.failed(job, "disc"), "Corrected key remains blocked");
                } else if (phase.equals("replacement-restart")) {
                    readKey(job, config, "disc", 43);
                    require(!new File(job, "input/disc.key").exists(), "Replacement saved key bytes");
                } else if (phase.equals("legacy")) {
                    provider.edit().clear().putBoolean("revoked", true).commit();
                    byte[] authored = new byte[16];
                    java.util.Arrays.fill(authored, (byte)42);
                    for (String key : new String[] {"disc", "common"}) {
                        Files.write(new File(job, "input/" + key + ".key").toPath(), authored);
                        config.getJSONObject("input").put(key + "_key", "input/" + key + ".key");
                    }
                    SetupStore.write(configuration, config);
                    SetupStore.write(new File(job, "host.json"), new JSONObject().put("state", "paused"));
                    SetupImport.prepare(this, job, control);
                    SetupKeyAccess.cleanupCompleted(job);
                    require(new File(job, "input/disc.key").length() == 16, "Migration lost an interrupted old key");
                } else if (phase.equals("legacy-restart")) {
                    config = SetupImport.prepare(this, job, control);
                    readKey(job, config, "disc", 42);
                    readKey(job, config, "common", 42);
                    require(new File(job, "input/common.key").length() == 16, "Resume lost an old key");
                } else if (phase.equals("legacy-preconfiguration")) {
                    // Model termination in the old importer before its job record was committed.
                    SetupStore.remove(configuration);
                    config = SetupImport.prepare(this, job, control);
                    readKey(job, config, "disc", 42);
                    readKey(job, config, "common", 42);
                } else if (phase.equals("legacy-complete")) {
                    SetupStore.write(new File(job, "host.json"), new JSONObject().put("state", "complete"));
                    SetupKeyAccess.cleanupCompleted(job);
                    require(!new File(job, "input/disc.key").exists() && !new File(job, "input/common.key").exists(),
                        "Completed legacy job retained copies");
                } else if (phase.equals("missing-source")) {
                    SetupStore.remove(new File(job, "source.json"));
                    config = SetupImport.prepare(this, job, control);
                    try { readKey(job, config, "disc", 42); throw new IOException("Missing source was ignored"); }
                    catch (SetupKeyAccess.Missing expected) {
                        require(SetupKeyAccess.failed(job, "disc"), "Missing source lost recovery state");
                    }
                    SetupStore.write(new File(job, "host.json"), new JSONObject().put("state", "failed"));
                } else if (phase.equals("missing-source-restart")) {
                    config = SetupImport.prepare(this, job, control);
                    require(SetupKeyAccess.failed(job, "disc"), "Restart lost missing-source recovery state");
                } else if (phase.equals("missing-source-reselect")) {
                    provider.edit().clear().commit();
                    SetupImport.selectKey(this, job, "disc", document("replacementkey"));
                    require(!SetupImport.keyReplacement(job, "common").exists(), "Single key selected both roles");
                    SetupImport.selectKey(this, job, "common", document("replacementkey"));
                    config = SetupImport.prepare(this, job, control);
                    readKey(job, config, "disc", 43);
                    readKey(job, config, "common", 43);
                    require(!new File(job, "input/disc.key").exists(), "Recovery copied key bytes");
                } else throw new IOException("Unknown recovery phase");
                result.put("passed", true).put("phase", phase);
            } catch (Exception failure) {
                try { result.put("passed", false).put("error", failure.toString()); } catch (Exception ignored) { }
            }
            try { SetupStore.write(new File(getFilesDir(), "setup-key-recovery-" + phase + ".json"), result); }
            catch (Exception ignored) { }
            runOnUiThread(this::finish);
        }, "key-recovery-smoke").start();
    }
}
