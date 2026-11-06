// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.Map;

/** OS-owned lease: save transfer cannot race a live native game. */
final class SessionLease implements AutoCloseable {
    // Closing any descriptor for a locked file can release this process's locks
    // on it. Reject our own contenders before opening another descriptor.
    private static final Map<File, SessionLease> active = new HashMap<>();
    private final File path;
    private final RandomAccessFile file;
    private final FileLock lock;

    static synchronized SessionLease acquire(File root) throws IOException {
        File path = new File(root, ".wwhd-session.lock").getCanonicalFile();
        for (File owned : active.keySet()) {
            if (path.equals(owned) || (path.exists() && Files.isSameFile(path.toPath(), owned.toPath())))
                throw transferBusy();
        }
        SessionLease lease = new SessionLease(root, path);
        active.put(path, lease);
        return lease;
    }

    private SessionLease(File root, File path) throws IOException {
        this.path = path;
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Storage is unavailable.");
        file = new RandomAccessFile(path, "rw");
        FileLock acquired;
        try {
            acquired = file.getChannel().tryLock();
            if (acquired == null) throw busy();
        } catch (OverlappingFileLockException failure) {
            file.close();
            // Import and export activities share :tools; overlapping locks belong to a transfer.
            throw transferBusy();
        } catch (IOException | RuntimeException failure) {
            file.close();
            throw failure;
        }
        lock = acquired;
    }

    private static IOException busy() {
        return new IOException("Choose Settings › Quit Game before importing or exporting saves.");
    }

    private static IOException transferBusy() {
        return new IOException("Another save transfer is in progress. Wait for it to finish before trying again.");
    }

    @Override public void close() throws IOException {
        synchronized (SessionLease.class) {
            if (active.get(path) != this) return;
            try { lock.release(); } finally {
                file.close();
                active.remove(path);
            }
        }
    }
}
