package org.wwhdrecomp.wwhd;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import java.io.File;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.HashMap;

public final class SystemExitCollectorTest {
    static void check(boolean ok) { if (!ok) throw new AssertionError(); }
    static void newProcess() throws Exception {
        java.lang.reflect.Field field = SystemExitReports.class.getDeclaredField("recorded");
        field.setAccessible(true); field.setBoolean(null, false);
    }
    static final class Preferences implements SharedPreferences, SharedPreferences.Editor {
        final HashMap<String, Long> values = new HashMap<>();
        final HashMap<String, Long> pending = new HashMap<>();
        boolean fail;
        public long getLong(String key, long fallback) { return values.getOrDefault(key, fallback); }
        public Editor edit() { pending.clear(); return this; }
        public Editor putLong(String key, long value) { pending.put(key, value); return this; }
        public boolean commit() { if (fail) return false; values.putAll(pending); return true; }
    }
    static final class TestContext extends Context {
        final File dir;
        final ActivityManager manager = new ActivityManager();
        final Preferences prefs = new Preferences();
        TestContext(File dir) { this.dir = dir; }
        public File getFilesDir() { return dir; }
        public String getPackageName() { return "synthetic.test"; }
        public <T> T getSystemService(Class<T> type) { return type.cast(manager); }
        public SharedPreferences getSharedPreferences(String name, int mode) { return prefs; }
    }
    static void remove(File file) {
        if (file.isDirectory()) for (File child : file.listFiles()) remove(child);
        check(file.delete());
    }
    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("exit-collector-synthetic").toFile();
        try {
            TestContext c = new TestContext(root);
            File captures = new File(root, "captures");
            c.manager.fail = true;
            SystemExitReports.record(c, captures); // unsupported vendor API must not crash launch
            c.manager.fail = false;
            c.manager.history = Arrays.asList(new ApplicationExitInfo(2000, 3, "synthetic.test"),
                new ApplicationExitInfo(9000, 5, "synthetic.test:setup"));
            Files.write(captures.toPath(), new byte[]{0}); // storage error must not consume the event
            SystemExitReports.record(c, captures);
            check(c.prefs.getLong("systemExitSeen", 0) == 0);
            check(captures.delete());
            c.prefs.fail = true;
            SystemExitReports.record(c, captures);
            check(c.prefs.getLong("systemExitSeen", 0) == 0);
            c.prefs.fail = false;
            SystemExitReports.record(c, captures); // deterministic publication survives failed watermark commit
            check(c.prefs.getLong("systemExitSeen", 0) == 2000 && captures.listFiles().length == 1);
            File report = captures.listFiles()[0];
            String text = new String(Files.readAllBytes(report.toPath()), java.nio.charset.StandardCharsets.UTF_8);
            check(text.contains("low-memory kill") && !text.contains("synthetic.test"));
            int queries = c.manager.queries;
            SystemExitReports.record(c, captures);
            check(c.manager.queries == queries); // no repeated query on activity recreation
            newProcess();
            SystemExitReports.record(c, captures);
            check(captures.listFiles().length == 1); // no duplicate on next launch
            newProcess();
            SystemExitReports.expectStop(c, 77);
            long stop = Long.parseLong(Files.readAllLines(new File(root, "expected-game-stop").toPath()).get(1));
            c.manager.history = Arrays.asList(new ApplicationExitInfo(stop + 1, 2, "synthetic.test"));
            SystemExitReports.record(c, captures);
            check(captures.listFiles().length == 1 && c.prefs.getLong("systemExitSeen", 0) == stop + 1);
            newProcess();
            c.manager.history = Arrays.asList(new ApplicationExitInfo(stop + 2, 5, "synthetic.test"));
            SystemExitReports.record(c, captures);
            check(captures.listFiles().length == 2); // expected stop never hides a native crash
        } finally { remove(root); }
        System.out.println("Exit collector privacy, vendor/storage failure, deduplication and save-tool stop checks passed");
    }
}
