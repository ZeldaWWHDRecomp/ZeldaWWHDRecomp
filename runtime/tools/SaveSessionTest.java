package org.wwhdrecomp.wwhd;

import android.content.Context;
import android.system.Os;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.TimeUnit;

/** Production SaveSession/SessionLease/recovery, with only Android context/fsync bindings stubbed. */
public final class SaveSessionTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    private static Context context(Path root) {
        return new Context() {
            @Override public File getExternalFilesDir(String type) { return root.toFile(); }
        };
    }

    private static void held(Path root) throws Exception {
        try (SessionLease unexpected = SessionLease.acquire(root.toFile())) {
            throw new AssertionError("Game lease was released");
        } catch (IOException expected) {
            check(expected.getMessage().contains("transfer"), "Expected the same-process lease collision");
        }
    }

    private static void previous(Path root) throws IOException {
        Path previous = Files.createDirectories(root.resolve(".wwhd-save-previous/user"));
        Files.writeString(previous.resolve("cking.sav"), "original-save");
    }

    private static void restored(Path root) throws IOException {
        check(Files.readString(root.resolve("save/user/cking.sav")).equals("original-save"),
                "Interrupted import did not restore the real save");
        check(!Files.exists(root.resolve(".wwhd-save-previous")), "Old tree was not recovered");
        try (var children = Files.list(root)) {
            check(children.noneMatch(p -> p.getFileName().toString().startsWith("save-backup-")),
                    "Recovery mistook an empty live directory for a completed import");
        }
    }

    private static void scenario(String name, Path root) throws Exception {
        switch (name) {
            case "repeat": {
                previous(root);
                SaveSession.enterGame(context(root));
                check(!Files.exists(root.resolve("save")), "Acquiring the lease prepared an empty live save");
                SaveSession.prepareSelectedGameSave(root.resolve("save").toString());
                restored(root);
                int syncs = Os.syncCalls;
                Path pending = Files.createDirectories(root.resolve(".wwhd-save-stage"));
                Files.writeString(pending.resolve("not-recovered-again"), "sentinel");
                // Canonically equivalent spelling must be idempotent, not a second recovery pass.
                SaveSession.prepareSelectedGameSave(root.resolve("save/../save").toString());
                check(Files.exists(pending.resolve("not-recovered-again")), "Repeated preparation reran recovery");
                check(Os.syncCalls == syncs, "Repeated preparation issued new durability operations");
                held(root);
                break;
            }
            case "changed-root": {
                SaveSession.enterGame(context(root));
                Path original = root.resolve("selected");
                SaveSession.prepareSelectedGameSave(original.toString());
                Files.writeString(original.resolve("sentinel"), "unchanged");
                Path other = root.resolve("other");
                boolean rejected = false;
                try { SaveSession.prepareSelectedGameSave(other.toString()); }
                catch (IOException expected) {
                    rejected = expected.getMessage().contains("cannot change");
                }
                check(rejected, "A running session accepted another save root");
                check(!Files.exists(other), "Rejected root was created before validation");
                check(Files.readString(original.resolve("sentinel")).equals("unchanged"), "Original save changed");
                held(root);
                break;
            }
            case "recreation": {
                SaveSession.enterGame(context(root));
                SaveSession.prepareSelectedGameSave(root.resolve("save").toString());
                Path replacement = root.resolve("new-activity-storage");
                // A recreated Activity must reuse process-static ownership without consulting a new Context.
                SaveSession.enterGame(new Context() {
                    @Override public File getExternalFilesDir(String type) {
                        throw new AssertionError("Recreation looked up storage/acquired another lease");
                    }
                });
                SaveSession.enterGame(context(replacement));
                check(!Files.exists(replacement), "Recreation acquired a different storage lease");
                held(root);
                SaveSession.prepareLegacyGame(); // Still the original prepared root.
                break;
            }
            case "legacy": {
                previous(root);
                SaveSession.enterGame(context(root));
                check(!Files.exists(root.resolve("save")), "Legacy entry created save before recovery");
                SaveSession.prepareLegacyGame();
                restored(root);
                held(root);
                break;
            }
            default: throw new AssertionError("Unknown scenario: " + name);
        }
        System.out.println("PASS SaveSession " + name);
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 2) {
            scenario(args[0], Path.of(args[1]));
            return; // OS releases the actual production lease when this JVM exits.
        }
        Path temp = Files.createTempDirectory("wwhd-save-session-");
        try {
            for (String name : new String[] {"repeat", "changed-root", "recreation", "legacy"}) {
                Path root = Files.createDirectory(temp.resolve(name));
                String java = Path.of(System.getProperty("java.home"), "bin", "java").toString();
                Process child = new ProcessBuilder(java, "-ea", "-cp", System.getProperty("java.class.path"),
                        SaveSessionTest.class.getName(), name, root.toString()).inheritIO().start();
                try {
                    check(child.waitFor(20, TimeUnit.SECONDS), "Scenario timed out: " + name);
                    check(child.exitValue() == 0, "Scenario failed: " + name);
                } finally {
                    if (child.isAlive()) child.destroyForcibly().waitFor();
                }
                // Parent can acquire only after process death; no reflection/reset bypasses the lifetime contract.
                try (SessionLease released = SessionLease.acquire(root.toFile())) {}
            }
            System.out.println("4 SaveSession lifecycle contracts passed");
        } finally {
            TransactionalSaveStore.remove(temp.toFile());
        }
    }
}
