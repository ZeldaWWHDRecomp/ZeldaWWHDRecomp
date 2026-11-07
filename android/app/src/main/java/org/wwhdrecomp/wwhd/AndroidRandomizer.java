// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

import android.content.Context;
import android.content.res.AssetManager;
import android.os.Build;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.TimeUnit;
import org.json.JSONObject;

/**
 * The in-app randomizer (TWWHD Randomizer 1.2.0 by SuperDude88 and contributors, MIT; tools/rando's wwhd-rando built for
 * Android by tools/android/package_rando.py): its executable (verified like the extractor), its data unpacked from the
 * APK into setup storage, and its settings for the next randomized game (randomizer/settings, the same files the
 * desktop setup keeps). The randomized game itself is made by the setup job (tools/android/ondevice_setup.py
 * "randomize"); its saves are separate from the game's (the runtime, by the seed's identity).
 */
public final class AndroidRandomizer {
    private AndroidRandomizer() {}

    public static final String EXECUTABLE = "libwwhd-rando-driver.so";

    /** Whether this APK has the randomizer (a build with -PwwhdRandoDir). */
    public static boolean available(Context context) {
        try (InputStream ignored = context.getAssets().open("rando-apk.json")) { return true; }
        catch (IOException missing) { return false; }
    }

    private static JSONObject metadata(Context context) throws Exception {
        try (InputStream stream = context.getAssets().open("rando-apk.json")) {
            return new JSONObject(new String(stream.readAllBytes(), StandardCharsets.UTF_8));
        }
    }

    static String hash(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream input = new FileInputStream(file)) {
            byte[] buffer = new byte[65536];
            int count;
            while ((count = input.read(buffer)) != -1) digest.update(buffer, 0, count);
        }
        StringBuilder actual = new StringBuilder();
        for (byte value : digest.digest()) actual.append(String.format("%02x", value & 255));
        return actual.toString();
    }

    public static File root(Context context) { return new File(AndroidGame.storage(context), "randomizer"); }
    public static File settings(Context context) { return new File(root(context), "settings"); }

    /** The job's "randomize" section: {exe, data, storage}, after checking the executable and unpacking the data. */
    public static synchronized JSONObject prepare(Context context) throws Exception {
        JSONObject metadata = metadata(context);
        String abi = metadata.getString("abi");
        if (metadata.getInt("schema") != 1 || metadata.getInt("api") != 33 || !Arrays.asList(Build.SUPPORTED_ABIS).contains(abi))
            throw new IOException("Unsupported randomizer package");
        String expected = metadata.getString("sha256");
        if (!metadata.getString("executable").equals(EXECUTABLE) || !expected.matches("[0-9a-f]{64}"))
            throw new IOException("Invalid randomizer identity");
        File executable = new File(context.getApplicationInfo().nativeLibraryDir, EXECUTABLE);
        if (!executable.isFile() || !executable.canExecute()) throw new IOException("The randomizer was not installed by Android");
        if (!hash(executable).equals(expected)) throw new IOException("Randomizer checksum mismatch");
        File data = new File(root(context), "data");
        File stamp = new File(data, ".apk-data-sha256");
        String want = metadata.getString("data_sha256");
        boolean current = false;
        try { current = stamp.isFile() && new String(java.nio.file.Files.readAllBytes(stamp.toPath()), StandardCharsets.UTF_8).equals(want); }
        catch (IOException ignored) { }
        if (!current) {
            File fresh = new File(root(context), "data.new");
            Backups.deleteTree(fresh);  // (an interrupted unpack of this same step)
            copyAssets(context.getAssets(), "rando-data", fresh);
            java.nio.file.Files.write(new File(fresh, ".apk-data-sha256").toPath(), want.getBytes(StandardCharsets.UTF_8));
            File old = new File(root(context), "data.old");
            Backups.deleteTree(old);
            if (data.exists() && !data.renameTo(old)) throw new IOException("Cannot replace the randomizer's data");
            if (!fresh.renameTo(data)) throw new IOException("Cannot install the randomizer's data");
            Backups.deleteTree(old);
        }
        if (!settings(context).isDirectory() && !settings(context).mkdirs()) throw new IOException("Cannot create randomizer settings");
        return new JSONObject().put("exe", executable.getAbsolutePath()).put("data", data.getAbsolutePath())
            .put("storage", AndroidGame.storage(context).getAbsolutePath()).put("identity", expected);
    }

    /** One settings command (preset, set-seed, new-seed, load-permalink, permalink, settings): its JSON result. Off the UI thread. */
    public static JSONObject run(Context context, String... command) throws Exception {
        JSONObject prepared = prepare(context);
        List<String> args = new ArrayList<>(Arrays.asList(prepared.getString("exe"), "--data", prepared.getString("data"),
            "--work", settings(context).getAbsolutePath()));
        args.addAll(Arrays.asList(command));
        Process process = new ProcessBuilder(args).redirectErrorStream(false).start();
        String out;
        try (InputStream stdout = process.getInputStream()) { out = new String(stdout.readAllBytes(), StandardCharsets.UTF_8); }
        if (!process.waitFor(60, TimeUnit.SECONDS)) { process.destroyForcibly(); throw new IOException("The randomizer did not answer"); }
        String last = null;
        for (String line : out.split("\n")) if (line.startsWith("{")) last = line;
        if (last == null) throw new IOException("The randomizer stopped (exit code " + process.exitValue() + ")");
        JSONObject result = new JSONObject(last);
        if (!result.optBoolean("ok")) throw new IOException(result.optString("error", "The randomizer refused"));
        return result;
    }

    /** Whether the setup can start a new job now: none runs (a finished, failed, chosen or paused-by-the-player job). */
    public static boolean replaceable(File job) throws Exception {
        if (job == null) return true;
        File host = new File(job, "host.json");
        if (!host.isFile()) return true;
        String state = SetupStore.read(host).optString("state");
        return state.equals("selected") || state.equals("failed") || state.equals("complete") ||
            (state.equals("paused") && new File(job, "manual-pause.json").isFile());
    }

    /** A randomized game from the installed game (the setup screen's Randomized game... > Make it, and the game's
     *  Settings > Randomizer): the settings for the next seed (a permalink, else a preset and a seed name, a new random
     *  one when empty), then a setup job that makes it, checks it and builds it; the game plays it from its next start.
     *  Off the UI thread; returns the new job's id (selected; SetupService.start runs it). */
    public static String makeJob(Context context, String preset, String seed, String permalink) throws Exception {
        AndroidGame.Selection current = AndroidGame.selected(context);
        if (current == null || current.game == null) throw new IOException("No installed game");
        if (!replaceable(SetupStore.current(context))) throw new IOException("Setup is running: wait for it, or pause it in the setup screen");
        if (!permalink.isEmpty()) run(context, "load-permalink", permalink);
        else {
            run(context, "preset", preset);
            if (seed.isEmpty()) run(context, "new-seed");
            else run(context, "set-seed", seed);
        }
        synchronized (SetupJobCleanup.CREATION) {
            String id = java.util.UUID.randomUUID().toString().replace("-", "");
            File job = SetupStore.job(context, id);
            SetupStore.write(new File(job, "source.json"), new JSONObject().put("schema", 1)
                .put("kind", "installed").put("path", current.game.getAbsolutePath()));
            SetupStore.write(new File(job, "host.json"), new JSONObject().put("schema", 1).put("state", "selected"));
            SetupStore.select(context, id);
            return id;
        }
    }

    /** Import a seed: the chosen output folder of the official randomizer, checked like a game folder; a setup job then
     *  copies it privately, keeps only its files that differ from the installed game (tools/android/ondevice_setup.py
     *  seed_import), checks and builds it. Off the UI thread; returns the new job's id, or throws with the message for
     *  the player (a folder that is no game says which folder to choose). */
    public static String importJob(Context context, android.net.Uri uri) throws Exception {
        AndroidGame.Selection current = AndroidGame.selected(context);
        if (current == null || current.game == null) throw new IOException("No installed game");
        SetupFolderCheck.Result found = SetupImport.checkFolder(context, uri,
            android.provider.DocumentsContract.getTreeDocumentId(uri));
        if (!found.usable()) throw new IOException(folderMessage(context, found));
        if (!replaceable(SetupStore.current(context))) throw new IOException("Pause setup before changing input");
        context.getContentResolver().takePersistableUriPermission(uri, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);
        synchronized (SetupJobCleanup.CREATION) {
            String id = java.util.UUID.randomUUID().toString().replace("-", "");
            File job = SetupStore.job(context, id);
            JSONObject source = new JSONObject().put("schema", 1).put("kind", "folder").put("uri", uri.toString())
                .put("seed_import", new JSONObject().put("installed", current.game.getAbsolutePath()));
            if (found.kind == SetupFolderCheck.Kind.SUBFOLDER) source.put("document", found.document);
            SetupStore.write(new File(job, "source.json"), source);
            SetupStore.write(new File(job, "host.json"), new JSONObject().put("schema", 1).put("state", "selected"));
            SetupStore.select(context, id);
            return id;
        }
    }

    /** The setup screen's words for a folder that is not a game folder. */
    public static String folderMessage(Context context, SetupFolderCheck.Result found) {
        switch (found.kind) {
            case SUBFOLDER: return context.getString(R.string.setup_folder_subfolder, found.path);
            case INSIDE_GAME: return context.getString(R.string.setup_folder_inside, found.path);
            case NOT_THIS_GAME: return found.path.isEmpty() ? context.getString(R.string.setup_folder_not_game_here) :
                context.getString(R.string.setup_folder_not_game, found.path);
            case SEVERAL: return context.getString(R.string.setup_folder_several, found.path);
            default: return context.getString(R.string.setup_folder_none);
        }
    }

    // ---- the game's Settings > Randomizer (WwhdActivity.requestRandomizer / randomizerStatus; RandomizerProgress)
    private static final Object REQUEST = new Object();
    private static RandomizerProgress.Request request;  // the page's last request (REQUEST)

    /** The game page's request: the folder picker opens (Import seed...). */
    static void picking() {
        synchronized (REQUEST) { request = new RandomizerProgress.Request(); request.picking = true; request.importing = true; }
    }

    /** The game page's new game: its settings and job are being made. */
    static void starting() {
        synchronized (REQUEST) { request = new RandomizerProgress.Request(); request.starting = true; }
    }

    /** A failure's message for the player (never empty). */
    static String message(Exception failure) {
        String text = failure.getMessage();
        return text == null || text.isEmpty() ? failure.getClass().getSimpleName() : text;
    }

    /** The page's request has its job, or was refused or cancelled (refusal "" and job null: closed without a folder). */
    static void requested(String job, String refusal, boolean importing) {
        synchronized (REQUEST) {
            request = new RandomizerProgress.Request();
            request.job = job == null ? "" : job;
            request.refusal = refusal == null ? "" : refusal;
            request.cancelled = job == null && (refusal == null || refusal.isEmpty());
            request.importing = importing;
            request.since = System.currentTimeMillis() - 1000;  // (host.json's clock: the same, at millisecond steps)
        }
    }

    /** The page's request as JSON: state, stage, done, total, error, and the seed when ready. */
    static String status(Context context) {
        RandomizerProgress.Request asked;
        synchronized (REQUEST) { asked = request; }
        JSONObject out = new JSONObject();
        try {
            if (asked == null) return out.put("state", "cancelled").toString();
            RandomizerProgress.Job job = new RandomizerProgress.Job();
            JSONObject ready = null;
            if (asked.job != null && !asked.job.isEmpty()) {
                File current = SetupStore.current(context);
                File dir = SetupStore.job(context, asked.job);
                job.selected = current != null && current.getName().equals(asked.job);
                File host = new File(dir, "host.json");
                if (host.isFile()) {
                    JSONObject h = SetupStore.read(host);
                    job.host = h.optString("state");
                    job.reason = h.optString("reason");
                    job.hostError = h.optString("error");
                    job.updated = h.optLong("updated");
                }
                File progress = new File(dir, "state.json");
                JSONObject event = progress.isFile() ? SetupStore.read(progress).optJSONObject("last_event") : null;
                if (event != null) {
                    job.event = event.optString("stage");
                    job.compiled = event.optInt("compiled");
                    job.reused = event.optInt("reused");
                    job.eventTotal = event.optInt("total");
                }
                File record = new File(dir, "ready.json");
                ready = record.isFile() ? SetupStore.read(record) : null;
                JSONObject randomized = ready == null ? null : ready.optJSONObject("randomizer");
                AndroidGame.Selection selected = randomized == null ? null : AndroidGame.selected(context);
                job.active = selected != null && selected.randomizerFiles != null &&
                    selected.randomizerFiles.getCanonicalFile().equals(new File(randomized.getString("files")).getCanonicalFile());
            }
            RandomizerProgress p = RandomizerProgress.of(asked, job);
            out.put("state", p.state).put("stage", p.stage).put("error", p.error).put("done", p.done).put("total", p.total);
            if (p.state.equals("ready")) {
                JSONObject randomized = ready.getJSONObject("randomizer");
                out.put("seed", randomized.optString("seed")).put("hash", randomized.optString("hash"))
                    .put("imported", asked.importing);
            }
        } catch (Exception failure) {
            try { out.put("state", "building").put("stage", "Reading setup progress"); } catch (Exception ignored) { }
        }
        return out.toString();
    }

    private static void copyAssets(AssetManager assets, String path, File to) throws IOException {
        String[] names = assets.list(path);
        if (names == null || names.length == 0) {
            File parent = to.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create " + parent);
            try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(to)) {
                byte[] buffer = new byte[65536];
                for (int n; (n = in.read(buffer)) > 0; ) out.write(buffer, 0, n);
            }
            return;
        }
        if (!to.isDirectory() && !to.mkdirs()) throw new IOException("Cannot create " + to);
        for (String name : names) copyAssets(assets, path + "/" + name, new File(to, name));
    }

    /** Removing only the randomizer's own temporary data folders (made by prepare). */
    private static final class Backups {
        static void deleteTree(File file) {
            File[] children = java.nio.file.Files.isSymbolicLink(file.toPath()) ? null : file.listFiles();
            if (children != null) for (File child : children) deleteTree(child);
            //noinspection ResultOfMethodCallIgnored
            file.delete();
        }
    }
}
