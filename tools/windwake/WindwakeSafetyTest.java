package org.wwhdrecomp.wwhd;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Runs the production archive, commit, settings and session code without Android stubs. */
public final class WindwakeSafetyTest {
    static int tests;
    interface Checked { void run() throws Exception; }
    static void check(boolean condition, String detail) { if (!condition) throw new AssertionError(detail); }
    static void rejects(Checked operation) throws Exception {
        boolean failed = false;
        try { operation.run(); } catch (IOException expected) { failed = true; }
        check(failed, "unsafe operation was accepted");
    }
    static byte[] zip(String... entries) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            for (int i = 0; i < entries.length; i += 2) {
                zip.putNextEntry(new ZipEntry(entries[i]));
                zip.write(entries[i + 1].getBytes(StandardCharsets.UTF_8)); zip.closeEntry();
            }
        }
        return bytes.toByteArray();
    }
    static void dataset(Path root, String version) throws IOException {
        Files.createDirectories(root.resolve("user"));
        Files.writeString(root.resolve("user/cking.sav"), version);
        Files.createDirectories(root.resolve("mods"));
        Files.writeString(root.resolve("mods/state.json"), version);
    }
    static void consistent(Path root, String expected) throws IOException {
        check(Files.readString(root.resolve("user/cking.sav")).equals(expected), "game state changed incorrectly");
        check(Files.readString(root.resolve("mods/state.json")).equals(expected), "mod state did not move with game state");
    }
    static void test(String name, Checked body) throws Exception { body.run(); tests++; System.out.println("PASS " + name); }

    public static void main(String[] args) throws Exception {
        if (args.length > 0 && args[0].equals("lease-child")) {
            try (SessionLease lease = SessionLease.acquire(new File(args[1]))) {
                Files.writeString(Path.of(args[2]), "ready");
                Thread.sleep(30000);
            }
            return;
        }
        Path temp = Files.createTempDirectory("windwake-safety-");
        try {
            test("complete save and mod dataset published together, former tree retained", () -> {
                Path root = Files.createDirectory(temp.resolve("commit")); dataset(root.resolve("save"), "old");
                TransactionalSaveStore store = new TransactionalSaveStore(root.toFile(), dir -> {});
                File backup = store.importZip(new ByteArrayInputStream(zip("user/cking.sav", "new", "mods/state.json", "new")));
                consistent(root.resolve("save"), "new"); consistent(backup.toPath(), "old");
            });
            test("failed validation preserves the original complete dataset", () -> {
                Path root = Files.createDirectory(temp.resolve("invalid")); dataset(root.resolve("save"), "old");
                TransactionalSaveStore store = new TransactionalSaveStore(root.toFile(), dir -> {});
                rejects(() -> store.importZip(new ByteArrayInputStream(zip("mods/state.json", "untrusted"))));
                consistent(root.resolve("save"), "old");
                check(!Files.exists(root.resolve(".windwake-save-stage")), "failed staging remained");
            });
            test("traversal, drive paths, duplicate names and ambiguous roots rejected", () -> {
                for (String path : new String[] {"../escape", "/escape", "C:/escape", "a/../escape", "a//escape"}) {
                    Path stage = Files.createTempDirectory(temp, "path");
                    rejects(() -> SafeSaveArchive.extract(new ByteArrayInputStream(zip(path, "bad")), stage.toFile()));
                }
                Path duplicate = Files.createTempDirectory(temp, "duplicate");
                rejects(() -> SafeSaveArchive.extract(new ByteArrayInputStream(zip("user/cking.sav", "one", "USER/CKING.SAV", "two")), duplicate.toFile()));
                Path ambiguous = Files.createTempDirectory(temp, "ambiguous");
                rejects(() -> SafeSaveArchive.extract(new ByteArrayInputStream(zip("a/user/cking.sav", "one", "b/user/cking.sav", "two")), ambiguous.toFile()));
            });
            test("streaming ZIP bomb rejected even with unknown local-header size", () -> {
                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
                    zip.putNextEntry(new ZipEntry("user/cking.sav"));
                    byte[] zeros = new byte[64 * 1024];
                    for (int i = 0; i < 257; i++) zip.write(zeros);
                    zip.closeEntry();
                }
                check(bytes.size() < 100000, "fixture is not compressed");
                rejects(() -> SafeSaveArchive.extract(new ByteArrayInputStream(bytes.toByteArray()), temp.resolve("bomb").toFile()));
            });
            test("CRC corruption never publishes a partial save", () -> {
                byte[] broken = zip("user/cking.sav", "new save content"); broken[14] ^= 0x45;
                // CRC of a data-descriptor ZIP is after compressed data, not necessarily in the local header.
                for (int i = 0; i + 16 < broken.length; i++) if (broken[i] == 0x50 && broken[i+1] == 0x4b && broken[i+2] == 7 && broken[i+3] == 8) broken[i+4] ^= 0x45;
                Path root = Files.createDirectory(temp.resolve("crc")); dataset(root.resolve("save"), "old");
                rejects(() -> new TransactionalSaveStore(root.toFile(), dir -> {}).importZip(new ByteArrayInputStream(broken)));
                consistent(root.resolve("save"), "old");
            });
            test("I/O failure after retiring old tree rolls it back", () -> {
                Path root = Files.createDirectory(temp.resolve("io")); dataset(root.resolve("save"), "old");
                TransactionalSaveStore store = new TransactionalSaveStore(root.toFile(), dir -> {
                    if (Files.exists(root.resolve(".windwake-save-previous")) && !Files.exists(root.resolve("save"))) throw new IOException("injected disk failure");
                });
                rejects(() -> store.importZip(new ByteArrayInputStream(zip("user/cking.sav", "new", "mods/state.json", "new"))));
                consistent(root.resolve("save"), "old");
            });
            test("recovery after interruption before publication restores former tree", () -> {
                Path root = Files.createDirectory(temp.resolve("interrupted")); dataset(root.resolve(".windwake-save-previous"), "old");
                dataset(root.resolve(".windwake-save-stage"), "new");
                new TransactionalSaveStore(root.toFile(), dir -> {}).recover();
                consistent(root.resolve("save"), "old");
            });
            test("recovery after publication preserves current tree and old backup", () -> {
                Path root = Files.createDirectory(temp.resolve("published")); dataset(root.resolve(".windwake-save-previous"), "old"); dataset(root.resolve("save"), "new");
                new TransactionalSaveStore(root.toFile(), dir -> {}).recover();
                consistent(root.resolve("save"), "new");
                File[] backups = root.toFile().listFiles((dir, name) -> name.startsWith("save-backup-"));
                check(backups != null && backups.length == 1, "recovery backup missing"); consistent(backups[0].toPath(), "old");
            });
            test("settings preserve unknown keys and native changes on subsequent starts", () -> {
                File ini = temp.resolve("settings/settings.ini").toFile();
                WindwakeSettings.defaults(ini, dir -> {});
                WindwakeSettings.update(ini, Map.of("unknownMod", "keep", "dynResMin", "0"), dir -> {});
                WindwakeSettings.defaults(ini, dir -> {});
                check(WindwakeSettings.read(ini).get("dynResMin").equals("0"), "defaults reset a player choice");
                WindwakeSettings.update(ini, WindwakeSettings.profile(1), dir -> {});
                Map<String, String> settings = WindwakeSettings.read(ini);
                check(settings.get("unknownMod").equals("keep"), "mod settings erased");
                check(settings.get("resScale").equals("1") && settings.get("dynResMin").equals("0.5"), "profile uses an unsupported downscale key");
                check(settings.get("fps60").equals("0") && settings.get("heatGuard").equals("normal"), "phone safety defaults changed");
            });
            test("mobile controls migrate once without resetting graphics or sensitivity choices", () -> {
                File ini = temp.resolve("migration/settings.ini").toFile();
                WindwakeSettings.update(ini, Map.of("windwakeDefaults", "1", "touchStyle", "0", "touchRight", "1",
                    "touchSwipeSpeed", "1.60", "dynResMin", "0.5", "otherMod", "keep"), dir -> {});
                WindwakeSettings.defaults(ini, dir -> {});
                Map<String, String> migrated = WindwakeSettings.read(ini);
                check(migrated.get("touchStyle").equals("1") && migrated.get("touchRight").equals("0"), "camera is not swipe");
                check(migrated.get("touchItemTap").equals("1") && migrated.get("touchAimOnly").equals("1"), "tap aim not enabled");
                check(migrated.get("touchSwipeSpeed").equals("1.60") && migrated.get("dynResMin").equals("0.5") && migrated.get("otherMod").equals("keep"), "migration erased a player choice");
                WindwakeSettings.update(ini, Map.of("touchItemTap", "0", "windwakeAimSpeed", "0.90"), dir -> {});
                WindwakeSettings.defaults(ini, dir -> {});
                check(WindwakeSettings.read(ini).get("touchItemTap").equals("0"), "migration repeated on another start");
            });
            test("OS session lock excludes another process and survives process termination", () -> {
                Path root = Files.createDirectory(temp.resolve("lease")), ready = root.resolve("ready");
                String java = Path.of(System.getProperty("java.home"), "bin", "java").toString();
                Process child = new ProcessBuilder(java, "-cp", System.getProperty("java.class.path"), WindwakeSafetyTest.class.getName(), "lease-child", root.toString(), ready.toString()).inheritIO().start();
                try {
                    long deadline = System.nanoTime() + 10_000_000_000L;
                    while (!Files.exists(ready) && child.isAlive() && System.nanoTime() < deadline) Thread.sleep(10);
                    check(Files.exists(ready), "child did not acquire the session");
                    rejects(() -> { try (SessionLease lease = SessionLease.acquire(root.toFile())) { } });
                } finally { child.destroyForcibly().waitFor(); }
                try (SessionLease lease = SessionLease.acquire(root.toFile())) { }
            });
            System.out.println(tests + " safety contracts passed");
        } finally { TransactionalSaveStore.remove(temp.toFile()); }
    }
}
