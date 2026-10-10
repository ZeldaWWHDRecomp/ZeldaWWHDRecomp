package org.wwhdrecomp.wwhd;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

/** Remove obsolete preparation caches only after the selected cache is verified. */
final class ResourceCacheCleanup {
    interface Control { void checkpoint() throws Exception; }

    // Caller must have verified the selected cache against its trusted APK archive.
    // Cache roots are separate from active libraries, job checkpoints and saves.
    static boolean prune(File parent, File selected, Control control) throws Exception {
        control.checkpoint();
        String name = selected.getName();
        if (!name.matches("[0-9a-f]{64}(?:[0-9a-f]{64})?") ||
                selected.getParentFile() == null ||
                Files.isSymbolicLink(parent.toPath()) ||
                !parent.getCanonicalFile().equals(selected.getParentFile().getCanonicalFile()) ||
                !ResourceVerifier.complete(selected))
            throw new IOException("Cannot clean caches without a completed selected generation");
        File[] entries = parent.listFiles();
        if (entries == null) throw new IOException("Cannot inspect resource cache generations");
        String owned = "[0-9a-f]{" + name.length() + "}(?:\\.pending)?";
        boolean changed = false;
        for (File entry : entries) {
            control.checkpoint();
            if (entry.getName().equals(name) || !entry.getName().matches(owned)) continue;
            remove(entry, control);
            changed = true;
        }
        return changed;
    }

    private static void remove(File entry, Control control) throws Exception {
        control.checkpoint();
        if (Files.isSymbolicLink(entry.toPath())) {
            Files.delete(entry.toPath());
            return;
        }
        if (!entry.exists()) return;
        if (entry.isDirectory()) {
            File[] children = entry.listFiles();
            if (children == null) throw new IOException("Cannot inspect obsolete resource cache");
            for (File child : children) remove(child, control);
        }
        if (!entry.delete()) throw new IOException("Cannot remove obsolete resource cache");
    }
}
