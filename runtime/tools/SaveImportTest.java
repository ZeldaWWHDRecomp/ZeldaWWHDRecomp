package org.wwhdrecomp.wwhd;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Runs the production archive, commit and session code without Android stubs. */
public final class SaveImportTest {
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
        Path temp = Files.createTempDirectory("wwhd-save-import-");
        try {
            test("complete save and mod dataset published together, former tree retained", () -> {
                Path root = Files.createDirectory(temp.resolve("commit")); dataset(root.resolve("save"), "old");
                TransactionalSaveStore store = new TransactionalSaveStore(root.toFile(), dir -> {});
                File backup = store.importZip(new ByteArrayInputStream(zip("user/cking.sav", "new", "mods/state.json", "new")));
                consistent(root.resolve("save"), "new"); consistent(backup.toPath(), "old");
            });
            test("first import accepts one wrapping folder and publishes its complete dataset", () -> {
                Path root = Files.createDirectory(temp.resolve("wrapped"));
                File backup = new TransactionalSaveStore(root.toFile(), dir -> {}).importZip(new ByteArrayInputStream(
                        zip("save/user/cking.sav", "new", "save/mods/state.json", "new")));
                check(backup == null, "first import unexpectedly created a backup");
                consistent(root.resolve("save"), "new");
                check(!Files.exists(root.resolve(".wwhd-save-stage")), "staging remained after publication");
            });
            test("empty save marker and directory payload are rejected", () -> {
                rejects(() -> SafeSaveArchive.extract(new ByteArrayInputStream(zip("user/cking.sav", "")),
                        temp.resolve("empty").toFile()));
                rejects(() -> SafeSaveArchive.extract(new ByteArrayInputStream(zip("user/", "unexpected data")),
                        temp.resolve("directory-data").toFile()));
            });
            test("entry count limit stops archives with many empty entries", () -> {
                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                try (ZipOutputStream archive = new ZipOutputStream(bytes)) {
                    for (int i = 0; i <= SafeSaveArchive.MAX_ENTRIES; i++) {
                        archive.putNextEntry(new ZipEntry("empty-" + i)); archive.closeEntry();
                    }
                }
                rejects(() -> SafeSaveArchive.extract(new ByteArrayInputStream(bytes.toByteArray()),
                        temp.resolve("entry-limit").toFile()));
            });
            test("aggregate byte limit stops otherwise permitted individual files", () -> {
                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                try (ZipOutputStream archive = new ZipOutputStream(bytes)) {
                    byte[] block = new byte[64 * 1024];
                    for (int i = 0; i < 8; i++) {
                        archive.putNextEntry(new ZipEntry(i == 0 ? "user/cking.sav" : "data-" + i));
                        for (int n = 0; n < 256; n++) archive.write(block);
                        archive.closeEntry();
                    }
                    archive.putNextEntry(new ZipEntry("over-limit")); archive.write(1); archive.closeEntry();
                }
                Path root = Files.createDirectory(temp.resolve("total-limit")); dataset(root.resolve("save"), "old");
                rejects(() -> new TransactionalSaveStore(root.toFile(), dir -> {}).importZip(
                        new ByteArrayInputStream(bytes.toByteArray())));
                consistent(root.resolve("save"), "old");
            });
            test("failed validation preserves the original complete dataset", () -> {
                Path root = Files.createDirectory(temp.resolve("invalid")); dataset(root.resolve("save"), "old");
                TransactionalSaveStore store = new TransactionalSaveStore(root.toFile(), dir -> {});
                rejects(() -> store.importZip(new ByteArrayInputStream(zip("mods/state.json", "untrusted"))));
                consistent(root.resolve("save"), "old");
                check(!Files.exists(root.resolve(".wwhd-save-stage")), "failed staging remained");
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
                    if (Files.exists(root.resolve(".wwhd-save-previous")) && !Files.exists(root.resolve("save"))) throw new IOException("injected disk failure");
                });
                rejects(() -> store.importZip(new ByteArrayInputStream(zip("user/cking.sav", "new", "mods/state.json", "new"))));
                consistent(root.resolve("save"), "old");
            });
            test("recovery after interruption before publication restores former tree", () -> {
                Path root = Files.createDirectory(temp.resolve("interrupted")); dataset(root.resolve(".wwhd-save-previous"), "old");
                dataset(root.resolve(".wwhd-save-stage"), "new");
                new TransactionalSaveStore(root.toFile(), dir -> {}).recover();
                consistent(root.resolve("save"), "old");
            });
            test("recovery after publication preserves current tree and old backup", () -> {
                Path root = Files.createDirectory(temp.resolve("published")); dataset(root.resolve(".wwhd-save-previous"), "old"); dataset(root.resolve("save"), "new");
                new TransactionalSaveStore(root.toFile(), dir -> {}).recover();
                consistent(root.resolve("save"), "new");
                File[] backups = root.toFile().listFiles((dir, name) -> name.startsWith("save-backup-"));
                check(backups != null && backups.length == 1, "recovery backup missing"); consistent(backups[0].toPath(), "old");
            });
            test("OS session lock excludes another process and survives process termination", () -> {
                Path root = Files.createDirectory(temp.resolve("lease")), ready = root.resolve("ready");
                String java = Path.of(System.getProperty("java.home"), "bin", "java").toString();
                Process child = new ProcessBuilder(java, "-cp", System.getProperty("java.class.path"), SaveImportTest.class.getName(), "lease-child", root.toString(), ready.toString()).inheritIO().start();
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
