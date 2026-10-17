package org.wwhdrecomp.wwhd;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.system.Os;
import android.util.AtomicFile;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import org.json.JSONArray;
import org.json.JSONObject;

/** Code mods on the phone: the tools that build a guest mod in the game's process.
 *
 *  A guest mod is the game's kind of code (PowerPC). Like on a computer, the game translates it to C
 *  and compiles that with the local compiler when the mod is switched on (tools/guestmod). On the
 *  phone the compiler is the setup's own (AndroidToolchain) and Python is the APK's, started as a
 *  program (android/python/launcher.c). Both run from the native library folder: Android starts
 *  no program from the app's writable folders. The built module is a library the game loads. */
final class AndroidGuestTools {
    private AndroidGuestTools() {}
    static final String PYTHON_PROGRAM = "libwwhd-python-driver.so";  // tools/android/prepare_python.py

    private static String written;  // this process: the file is current

    /** Writes guest-sdk.json for this app installation and returns its path. The runtime calls this
     *  (JNI, a game thread) the first time a code mod needs its tools in a session. */
    static synchronized String prepare(Context context) throws Exception {
        if (written != null) return written;
        File directory = new File(context.getNoBackupFilesDir(), "guest-build");
        File config = new File(directory, "guest-sdk.json");
        File stamp = new File(directory, "installation.json");
        JSONObject installation = installation(context);
        File python = null;
        // A session after the first reuses the verified tools of this installation (no checksums of
        // ~200 MB on every game start); an app update or missing folders prepare them again.
        try {
            JSONObject previous = read(stamp);
            File home = new File(previous.getString("python"));
            if (previous.getJSONObject("installation").toString().equals(installation.toString()) && config.isFile() &&
                    ResourceVerifier.complete(home.getParentFile()) &&
                    ResourceVerifier.complete(new File(previous.getString("toolchain"))))
                python = home;
        } catch (Exception missing) { /* first session, or an older installation */ }
        if (python == null) {
            long started = android.os.SystemClock.elapsedRealtime();
            File installed = EmbeddedPython.prepare(context, () -> {});
            JSONObject toolchain = AndroidToolchain.prepare(context);
            python = new File(installed, "home");
            File source = new File(installed, "source");
            File program = new File(context.getApplicationInfo().nativeLibraryDir, PYTHON_PROGRAM);
            if (!program.isFile() || !program.canExecute())
                throw new IOException("The Python program for code mods was not installed with the app");
            File builder = new File(source, "tools/guestmod/build_guest_mod.py");
            File include = new File(source, "sdk/include");
            if (!builder.isFile() || !new File(include, "wwhd_guest_abi.h").isFile())
                throw new IOException("This app has no code-mod builder");
            List<String> cc = new ArrayList<>();
            JSONArray command = toolchain.getJSONArray("cc");
            for (int i = 0; i < command.length(); i++) cc.add(command.getString(i));
            String text = GuestToolsConfig.json(program.getAbsolutePath(),
                GuestToolsConfig.compiler(cc, toolchain.getString("ld")),
                builder.getAbsolutePath(), include.getAbsolutePath());
            if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Cannot create " + directory);
            write(config, text);
            write(stamp, new JSONObject().put("installation", installation)
                .put("python", python.getAbsolutePath()).put("toolchain", toolchain.getString("data")).toString());
            Log.i("wwhd-game", "code-mod tools prepared in " + (android.os.SystemClock.elapsedRealtime() - started) + " ms");
        }
        // the launcher's standard library (launcher.c); inherited by the tools the game starts
        Os.setenv("WWHD_PYTHON_HOME", python.getAbsolutePath(), true);
        written = config.getAbsolutePath();
        return written;
    }

    private static JSONObject installation(Context context) throws Exception {
        PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
        return new JSONObject().put("updated", info.lastUpdateTime).put("version", info.getLongVersionCode())
            .put("native", context.getApplicationInfo().nativeLibraryDir);
    }

    private static JSONObject read(File file) throws Exception {
        try (InputStream input = new AtomicFile(file).openRead()) {
            return new JSONObject(new String(input.readAllBytes(), StandardCharsets.UTF_8));
        }
    }

    private static void write(File file, String text) throws IOException {
        AtomicFile atomic = new AtomicFile(file);
        FileOutputStream stream = atomic.startWrite();
        try {
            stream.write(text.getBytes(StandardCharsets.UTF_8));
            stream.getFD().sync();
            atomic.finishWrite(stream);
        } catch (IOException | RuntimeException failure) {
            atomic.failWrite(stream);
            throw failure;
        }
    }
}
