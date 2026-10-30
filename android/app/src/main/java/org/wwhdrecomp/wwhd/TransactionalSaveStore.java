// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.UUID;

/** Caller holds SessionLease. Recovery is derived from atomic directory renames. */
final class TransactionalSaveStore {
    interface Durability { void sync(File directory) throws IOException; }
    private final File root, live, stage, previous;
    private final Durability durability;

    TransactionalSaveStore(File root, Durability durability) {
        this.root = root;
        this.live = new File(root, "save");
        this.stage = new File(root, ".windwake-save-stage");
        this.previous = new File(root, ".windwake-save-previous");
        this.durability = durability;
    }

    void recover() throws IOException {
        if (previous.exists()) {
            if (!live.exists()) move(previous, live); // interrupted between the two renames
            else preserveBackup(); // complete commit; keep the former tree
            durability.sync(root);
        }
        remove(stage);
    }

    File importZip(InputStream input) throws IOException {
        recover();
        try {
            File selected = SafeSaveArchive.extract(input, stage);
            syncTree(stage);
            durability.sync(root);
            boolean replaced = live.exists();
            if (replaced) {
                move(live, previous);
                durability.sync(root);
            }
            try {
                move(selected, live);
                durability.sync(root);
            } catch (IOException failure) {
                // If publication succeeded but sync failed, leave both trees for recovery.
                if (!live.exists() && previous.exists()) {
                    try { move(previous, live); durability.sync(root); }
                    catch (IOException rollback) { failure.addSuppressed(rollback); }
                }
                throw failure;
            }
            remove(stage);
            if (replaced) return preserveBackup();
            return null;
        } catch (IOException | RuntimeException failure) {
            // Never remove a live or previous tree on the failure path.
            if (!live.exists() && previous.exists()) {
                try { move(previous, live); durability.sync(root); }
                catch (IOException rollback) { failure.addSuppressed(rollback); }
            }
            try { remove(stage); } catch (IOException cleanup) { failure.addSuppressed(cleanup); }
            throw failure;
        }
    }

    private File preserveBackup() throws IOException {
        File backup = new File(root, "save-backup-" + System.currentTimeMillis() + "-" + UUID.randomUUID());
        move(previous, backup);
        durability.sync(root);
        return backup;
    }

    private void syncTree(File directory) throws IOException {
        File[] children = directory.listFiles();
        if (children == null) throw new IOException("Não foi possível verificar o save preparado.");
        for (File child : children) if (child.isDirectory()) syncTree(child);
        durability.sync(directory);
    }

    private static void move(File from, File to) throws IOException {
        if (to.exists()) throw new IOException("O destino de recuperação já existe: " + to.getName());
        Files.move(from.toPath(), to.toPath(), StandardCopyOption.ATOMIC_MOVE);
    }

    static void remove(File file) throws IOException {
        if (Files.isSymbolicLink(file.toPath())) throw new IOException("Link inesperado na área de preparação.");
        if (!file.exists()) return;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children == null) throw new IOException("Não foi possível limpar a preparação.");
            for (File child : children) remove(child);
        }
        Files.delete(file.toPath());
    }
}
