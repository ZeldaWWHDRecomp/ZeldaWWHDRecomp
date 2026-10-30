// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

import android.content.Context;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import java.io.File;
import java.io.FileDescriptor;
import java.io.IOException;

final class WindwakeSession {
    // Retained until the dedicated :game process exits, including Activity recreation.
    private static SessionLease gameLease;
    static File root(Context context) throws IOException {
        File root = context.getExternalFilesDir(null);
        if (root == null) throw new IOException("O armazenamento do aplicativo não está disponível.");
        return root;
    }

    static synchronized void enterGame(Context context) throws IOException {
        if (gameLease != null) return;
        File root = root(context);
        SessionLease lease = SessionLease.acquire(root);
        try {
            store(root).recover();
            WindwakeSettings.defaults(new File(AndroidUserDirectories.config(root), "settings.ini"), WindwakeSession::sync);
            gameLease = lease;
        } finally { if (gameLease == null) lease.close(); }
    }

    static TransactionalSaveStore store(File root) {
        return new TransactionalSaveStore(root, WindwakeSession::sync);
    }

    static boolean inUse(Context context) {
        if (gameLease != null) return true;
        try (SessionLease ignored = SessionLease.acquire(root(context))) { return false; }
        catch (IOException unavailable) { return true; } // conservatively defer shared-cache work
    }

    static void sync(File directory) throws IOException {
        try {
            if (!directory.isDirectory()) throw new IOException("Pasta de sincronização indisponível.");
            FileDescriptor fd = Os.open(directory.getPath(), OsConstants.O_RDONLY, 0);
            try { Os.fsync(fd); } finally { Os.close(fd); }
        } catch (ErrnoException failure) { throw new IOException("Não foi possível sincronizar os arquivos.", failure); }
    }
}
