// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;

/** OS-owned lease: an import/profile edit cannot race a live native game. */
final class SessionLease implements AutoCloseable {
    private final RandomAccessFile file;
    private final FileLock lock;

    static SessionLease acquire(File root) throws IOException { return new SessionLease(root); }

    private SessionLease(File root) throws IOException {
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Armazenamento indisponível.");
        file = new RandomAccessFile(new File(root, ".windwake-session.lock"), "rw");
        FileLock acquired;
        try {
            acquired = file.getChannel().tryLock();
            if (acquired == null) throw busy();
        } catch (OverlappingFileLockException failure) {
            file.close();
            throw busy();
        } catch (IOException | RuntimeException failure) {
            file.close();
            throw failure;
        }
        lock = acquired;
    }

    private static IOException busy() {
        return new IOException("Feche o jogo pelo menu antes de alterar configurações ou transferir saves.");
    }

    @Override public void close() throws IOException {
        try { lock.release(); } finally { file.close(); }
    }
}
