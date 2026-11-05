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
