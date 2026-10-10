package org.wwhdrecomp.wwhd;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import org.json.JSONArray;
import org.json.JSONObject;

/** Game-free CPython embedding probe; never included in release manifests. */
public final class PythonSmokeActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        TextView label = new TextView(this);
        label.setText("Running embedded Python translation test…");
        setContentView(label);
        boolean compiler = getIntent().getBooleanExtra("compiler", false);
        boolean activate = getIntent().getBooleanExtra("activate", false);
        boolean extract = getIntent().getBooleanExtra("extractor", false);
        new Thread(() -> {
            JSONObject result = new JSONObject();
            try {
                File output = new File(AndroidGame.storage(this), "python-fixture-" + System.nanoTime());
                JSONArray args = new JSONArray().put("--output").put(output.getAbsolutePath());
                if (extract) {
                    File configuration = new File(getFilesDir(), "extractor-smoke-config.json");
                    try (FileOutputStream stream = new FileOutputStream(configuration)) {
                        stream.write(AndroidExtractor.prepare(this).toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));
                    }
                    args.put("--extractor-json").put(configuration.getAbsolutePath());
                }
                if (compiler) {
                    File configuration = new File(getFilesDir(), "compiler-smoke-config.json");
                    try (FileOutputStream stream = new FileOutputStream(configuration)) {
                        stream.write(AndroidToolchain.prepare(this).toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));
                    }
                    args.put("--toolchain-json").put(configuration.getAbsolutePath());
                }
                EmbeddedPython.execute(this, "embedding_smoke", args.toString());
                if (activate) {
                    JSONObject metrics = new JSONObject(read(new File(output, "metrics.json")));
                    if (!metrics.getBoolean("full_runtime_load_verified"))
                        throw new IllegalStateException("Activation probe requires the full runtime");
                    File game = new File(metrics.getString("game_path"));
                    AndroidGame.activate(this, new File(metrics.getString("compiled_library_path")), game);
                    if (AndroidGame.current(this) == null) throw new IllegalStateException("Activated library was not selected");
                    checkActivation(output, new File(metrics.getString("compiled_library_path")), game);
                    result.put("game_assets", game.getAbsolutePath());
                    result.put("activated", true);
                }
                result.put("passed", true);
                result.put("output", output.getAbsolutePath());
            } catch (Throwable failure) {
                Log.e("wwhd-python", "Embedded fixture failed", failure);
                try { result.put("passed", false).put("error", failure.toString()); }
                catch (Exception ignored) { }
            }
            try {
                File pending = new File(getFilesDir(), "python-smoke.json.tmp");
                try (FileOutputStream stream = new FileOutputStream(pending)) {
                    stream.write(result.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));
                    stream.getFD().sync();
                }
                if (!pending.renameTo(new File(getFilesDir(), "python-smoke.json")))
                    throw new java.io.IOException("Cannot publish Python test result");
                runOnUiThread(() -> label.setText(result.toString()));
            } catch (Exception failure) { Log.e("wwhd-python", "Cannot publish test result", failure); }
        }, "wwhd-python-fixture").start();
    }

    private static String read(File file) throws Exception {
        return new String(java.nio.file.Files.readAllBytes(file.toPath()), java.nio.charset.StandardCharsets.UTF_8);
    }

    private static void write(File file, String text) throws Exception {
        java.nio.file.Files.write(file.toPath(), text.getBytes(java.nio.charset.StandardCharsets.UTF_8));
    }

    private void checkActivation(File output, File selected, File game) throws Exception {
        if (AndroidGame.needsRebuild(this)) throw new IllegalStateException("Fresh build needs a rebuild");
        File invalid = new File(output, "invalid.so");
        write(invalid, "authored invalid ELF fixture");
        if (!invalid.setReadOnly()) throw new IllegalStateException("Cannot protect invalid fixture");
        try {
            AndroidGame.activate(this, invalid);
            throw new AssertionError("Invalid library was activated");
        } catch (IllegalStateException expected) { /* native loader must reject it */ }
        if (!selected.equals(AndroidGame.current(this))) throw new AssertionError("Failed activation lost previous library");
        File active = new File(AndroidGame.storage(this), "active.json");
        write(new File(active.getPath() + ".new"), "interrupted atomic write");
        if (!selected.equals(AndroidGame.current(this))) throw new AssertionError("Interrupted metadata write lost active build");
        JSONObject value = new JSONObject(read(active));
        value.put("pipeline_identity", "previous-port-revision");
        write(active, value.toString());
        if (!AndroidGame.needsRebuild(this) || !selected.equals(AndroidGame.current(this)))
            throw new AssertionError("Port update did not retain compatible build and request rebuilding");
        AndroidGame.activate(this, selected, game);
        AndroidGame.rollback(this);
        if (!AndroidGame.needsRebuild(this) || !selected.equals(AndroidGame.current(this)))
            throw new AssertionError("Rollback lost compatible previous build");
        AndroidGame.activate(this, selected, game);
        JSONObject completed = new JSONObject(read(active));
        value = new JSONObject(completed.toString()).put("host_api", 999);
        write(active, value.toString());
        if (AndroidGame.current(this) != null) throw new AssertionError("Incompatible host API was accepted");
        value = new JSONObject(completed.toString()).put("sha256", "corrupt-digest");
        write(active, value.toString());
        if (AndroidGame.current(this) != null) throw new AssertionError("Corrupt library digest was accepted");
        AndroidGame.activate(this, selected, game);
        AndroidGame.rollback(this);
        if (!AndroidGame.needsRebuild(this) || !selected.equals(AndroidGame.current(this)))
            throw new AssertionError("Corrupt current metadata overwrote valid rollback history");
        AndroidGame.activate(this, selected, game);
        write(active, "interrupted JSON write");
        AndroidGame.activate(this, selected, game);
        if (AndroidGame.needsRebuild(this) || !selected.equals(AndroidGame.current(this)))
            throw new AssertionError("Could not recover corrupt active metadata");
        AndroidGame.Selection snapshot = AndroidGame.selected(this);
        if (snapshot == null || !snapshot.game.equals(game.getCanonicalFile()))
            throw new AssertionError("Activated library lost its asset tree");
        File alternate = new File(output, "alternate-game");
        new File(alternate, "code").mkdirs();
        new File(alternate, "content").mkdirs();
        new File(alternate, "meta").mkdirs();
        java.nio.file.Files.copy(new File(game, "code/cking.rpx").toPath(),
            new File(alternate, "code/cking.rpx").toPath());
        write(new File(alternate, "meta/meta.xml"), "<menu/>");
        AndroidGame.activate(this, selected, alternate);
        if (!AndroidGame.selected(this).game.equals(alternate.getCanonicalFile()) ||
            !snapshot.game.equals(game.getCanonicalFile()))
            throw new AssertionError("Concurrent activation changed a launch snapshot");
        AndroidGame.rollback(this);
        if (!AndroidGame.selected(this).game.equals(game.getCanonicalFile()))
            throw new AssertionError("Rollback did not restore the paired asset tree");
        File missing = new File(output, "missing-game");
        try {
            AndroidGame.activate(this, selected, missing);
            throw new AssertionError("Incomplete game assets were activated");
        } catch (java.io.IOException expected) { }
        if (!AndroidGame.selected(this).game.equals(game.getCanonicalFile()))
            throw new AssertionError("Failed asset activation lost the current tree");
        JSONObject paired = new JSONObject(read(active));
        write(active, new JSONObject(paired.toString()).put("game", "../outside").toString());
        if (AndroidGame.selected(this) != null) throw new AssertionError("Outside game assets were accepted");
        write(active, new JSONObject(paired.toString()).put("rpx_sha256", "corrupt").toString());
        if (AndroidGame.selected(this) != null) throw new AssertionError("Corrupt game executable digest was accepted");
        write(active, paired.toString());
        invalid.delete();
    }
}
