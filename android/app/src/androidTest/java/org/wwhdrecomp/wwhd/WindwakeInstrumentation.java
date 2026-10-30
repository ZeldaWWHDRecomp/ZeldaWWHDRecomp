// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.os.Bundle;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.nio.file.Files;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Exercises actual Android external storage, fsync, AtomicMove, session lock and bundled ARM compiler. */
public final class WindwakeInstrumentation extends Instrumentation {
    @Override public void onCreate(Bundle args) { super.onCreate(args); start(); }

    @Override public void onStart() {
        Bundle result = new Bundle();
        File fixture = null;
        try {
            Context context = getTargetContext();
            fixture = new File(WindwakeSession.root(context), ".windwake-test-" + UUID.randomUUID());
            if (!fixture.mkdir()) throw new AssertionError("Cannot create isolated test folder");
            try (SessionLease lease = SessionLease.acquire(fixture)) {
                File live = new File(fixture, "save/user");
                if (!live.mkdirs()) throw new AssertionError("Cannot create original test save");
                Files.write(new File(live, "cking.sav").toPath(), "original".getBytes(java.nio.charset.StandardCharsets.UTF_8));
                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
                    zip.putNextEntry(new ZipEntry("user/cking.sav")); zip.write("replacement".getBytes()); zip.closeEntry();
                    zip.putNextEntry(new ZipEntry("mods/state.json")); zip.write("replacement".getBytes()); zip.closeEntry();
                }
                File backup = WindwakeSession.store(fixture).importZip(new ByteArrayInputStream(bytes.toByteArray()));
                require(read(new File(fixture, "save/user/cking.sav")).equals("replacement"), "published save");
                require(read(new File(fixture, "save/mods/state.json")).equals("replacement"), "published sidecar");
                require(backup != null && read(new File(backup, "user/cking.sav")).equals("original"), "retained backup");
                File ini = new File(fixture, "config/settings.ini");
                WindwakeSettings.defaults(ini, WindwakeSession::sync);
                WindwakeSettings.update(ini, WindwakeSettings.profile(1), WindwakeSession::sync);
                require(WindwakeSettings.read(ini).get("dynResMin").equals("0.5"), "real phone profile write");
                boolean excluded = false;
                try (SessionLease second = SessionLease.acquire(fixture)) { }
                catch (java.io.IOException expected) { excluded = true; }
                require(excluded, "session excludes conflicting file operation");
            }
            AndroidToolchain.prepare(context); // verifies installed executable hashes and expands compiler data
            File clang = new File(context.getApplicationInfo().nativeLibraryDir, "libwwhd-clang-driver.so");
            Process compiler = new ProcessBuilder(clang.getAbsolutePath(), "--version").redirectErrorStream(true).start();
            require(compiler.waitFor(30, TimeUnit.SECONDS), "compiler startup timeout");
            String version = new String(compiler.getInputStream().readAllBytes());
            require(compiler.exitValue() == 0 && version.contains("clang"), "ARM compiler executes on device: " + version);
            TransactionalSaveStore.remove(fixture);
            fixture = null;
            result.putString("stream", "PASS Android save transaction, backup, fsync, settings, session exclusion and real ARM clang\n" + version);
            finish(Activity.RESULT_OK, result);
        } catch (Throwable failure) {
            result.putString("stream", "FAIL " + failure);
            finish(Activity.RESULT_CANCELED, result);
        } finally {
            if (fixture != null) try { TransactionalSaveStore.remove(fixture); } catch (Exception ignored) { }
        }
    }

    private static void require(boolean condition, String description) {
        if (!condition) throw new AssertionError(description);
    }

    private static String read(File file) throws java.io.IOException {
        return new String(Files.readAllBytes(file.toPath()), java.nio.charset.StandardCharsets.UTF_8);
    }
}
