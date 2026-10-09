package org.wwhdrecomp.wwhd;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Comparator;

public final class ResourceCacheCleanupTest {
    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static Path generation(Path parent, String name) throws Exception {
        Path root = Files.createDirectories(parent.resolve(name));
        Files.write(root.resolve("complete"), new byte[]{1});
        Files.writeString(root.resolve("resource"), "authored current resources");
        return root;
    }

    public static void main(String[] args) throws Exception {
        Path temporary = Files.createTempDirectory("resource-cleanup-");
        try {
            for (int width : new int[]{64, 128}) {
                Path parent = Files.createDirectory(temporary.resolve("cache-" + width));
                Path selected = generation(parent, "a".repeat(width));
                Path old = generation(parent, "b".repeat(width));
                Path pending = generation(parent, "c".repeat(width) + ".pending");
                Path selectedPending = generation(parent, selected.getFileName() + ".pending");
                Path unknown = generation(parent, "unrecognized-owner");
                Path backup = generation(parent, old.getFileName() + ".probe-backup-authored");
                Path otherWidth = generation(parent, "d".repeat(width == 64 ? 128 : 64));
                Path outside = generation(temporary, "user-data-" + width);
                Path link = parent.resolve("e".repeat(width));
                Files.createSymbolicLink(link, outside);
                Files.createSymbolicLink(old.resolve("outside-link"), outside);
                require(ResourceCacheCleanup.prune(parent.toFile(), selected.toFile(), () -> {}), "obsolete caches not removed");
                for (Path removed : new Path[]{old, pending, selectedPending, link})
                    require(!Files.exists(removed, java.nio.file.LinkOption.NOFOLLOW_LINKS), "obsolete entry remains");
                for (Path kept : new Path[]{selected, unknown, backup, otherWidth, outside}) {
                    require(Files.readString(kept.resolve("resource")).equals("authored current resources"), "protected resource changed");
                    require(ResourceVerifier.complete(kept.toFile()), "protected marker changed");
                }
                require(!ResourceCacheCleanup.prune(parent.toFile(), selected.toFile(), () -> {}), "healthy cache cleanup not idempotent");

                Path invalidOld = generation(parent, "f".repeat(width));
                Files.write(selected.resolve("complete"), new byte[]{0});
                try {
                    ResourceCacheCleanup.prune(parent.toFile(), selected.toFile(), () -> {});
                    throw new AssertionError("cleanup ran with invalid selected cache");
                } catch (IOException expected) { }
                require(ResourceVerifier.complete(invalidOld.toFile()), "old cache removed before selected cache completed");
                Files.write(selected.resolve("complete"), new byte[]{1});
                try {
                    ResourceCacheCleanup.prune(parent.toFile(), outside.toFile(), () -> {});
                    throw new AssertionError("foreign selected path accepted");
                } catch (IOException expected) { }
                Path linkedParent = temporary.resolve("linked-parent-" + width);
                Files.createSymbolicLink(linkedParent, parent);
                try {
                    ResourceCacheCleanup.prune(linkedParent.toFile(), linkedParent.resolve(selected.getFileName()).toFile(), () -> {});
                    throw new AssertionError("symbolic cache parent accepted");
                } catch (IOException expected) { }
                require(ResourceVerifier.complete(invalidOld.toFile()), "rejected parent cleanup removed old data");
            }

            Path parent = Files.createDirectory(temporary.resolve("interrupted"));
            Path selected = generation(parent, "a".repeat(128));
            Path old = generation(parent, "b".repeat(128));
            for (int i = 0; i < 64; ++i) Files.writeString(old.resolve("file-" + i), "authored obsolete data");
            int[] checkpoints = {0};
            final class Paused extends Exception {}
            try {
                ResourceCacheCleanup.prune(parent.toFile(), selected.toFile(), () -> {
                    if (++checkpoints[0] == 20) throw new Paused();
                });
                throw new AssertionError("cleanup pause ignored");
            } catch (Paused expected) { }
            try (var files = Files.list(old)) {
                long remaining = files.count();
                require(remaining > 0 && remaining < 66, "cleanup did not pause mid-deletion");
            }
            require(ResourceVerifier.complete(selected.toFile()), "pause changed selected marker");
            require(Files.readString(selected.resolve("resource")).equals("authored current resources"), "pause changed selected data");
            require(ResourceCacheCleanup.prune(parent.toFile(), selected.toFile(), () -> {}), "retry failed to finish cleanup");
            require(!Files.exists(old), "interrupted obsolete tree remains");
            System.out.println("Resource cleanup: generation ownership, symlinks, selected-cache preservation and mid-delete pause/retry passed");
        } finally {
            try (var paths = Files.walk(temporary)) {
                for (Path path : paths.sorted(Comparator.reverseOrder()).toList()) Files.delete(path);
            }
        }
    }
}
