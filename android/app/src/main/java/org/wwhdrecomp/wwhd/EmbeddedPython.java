package org.wwhdrecomp.wwhd;

import android.content.Context;
import android.system.Os;
import android.system.OsConstants;
import android.system.ErrnoException;
import java.io.FileDescriptor;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.IOException;
import java.security.MessageDigest;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipEntry;

/** Resources and JNI for the dedicated setup-process Python worker. */
public final class EmbeddedPython {
    private EmbeddedPython() {}
    interface Control { void checkpoint() throws Exception; }
    private static final Control UNCONTROLLED = () -> {};
    private static native void run(String home, String source, String module, String arguments);

    public static synchronized void execute(Context context, String module, String arguments) throws Exception {
        execute(context, module, arguments, UNCONTROLLED);
    }

    static synchronized void execute(Context context, String module, String arguments, Control control) throws Exception {
        control.checkpoint();
        System.loadLibrary("wwhdpython");
        String revision = checksum(context, "python-home.zip", control) + checksum(context, "python-source.zip", control);
        File parent = new File(context.getNoBackupFilesDir(), "embedded-python");
        File installed = new File(parent, revision);
        if (!ResourceVerifier.complete(installed) ||
                !resourcesMatch(context, "python-home.zip", new File(installed, "home"), false, control) ||
                !resourcesMatch(context, "python-source.zip", new File(installed, "source"), false, control)) {
            File pending = new File(parent, revision + ".pending");
            remove(pending);
            try {
                if (!pending.mkdirs()) throw new IOException("Cannot prepare Python storage");
                extract(context, "python-home.zip", new File(pending, "home"), control);
                extract(context, "python-source.zip", new File(pending, "source"), control);
                publishResources(pending, installed, control);
            } finally {
                remove(pending);
            }
        }
        // APK resources are fixed for this setup process; execute() serializes
        // Python tasks. Old preparation trees are not dependencies of active games.
        if (ResourceCacheCleanup.prune(parent, installed, control::checkpoint)) syncDirectory(parent);
        control.checkpoint();
        run(new File(installed, "home").getAbsolutePath(),
            new File(installed, "source").getAbsolutePath(), module, arguments);
    }

    static String checksum(Context context, String asset) throws Exception {
        return checksum(context, asset, UNCONTROLLED);
    }

    static String checksum(Context context, String asset, Control control) throws Exception {
        control.checkpoint();
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream input = context.getAssets().open(asset)) {
            byte[] buffer = new byte[65536];
            int size;
            while ((size = input.read(buffer)) != -1) {
                control.checkpoint();
                digest.update(buffer, 0, size);
            }
        }
        StringBuilder result = new StringBuilder();
        for (byte value : digest.digest()) result.append(String.format("%02x", value & 255));
        return result.toString();
    }

    static void extract(Context context, String asset, File directory, Control control) throws Exception {
        control.checkpoint();
        if (!directory.mkdirs()) throw new IOException("Cannot create Python resource directory");
        String prefix = directory.getCanonicalPath() + File.separator;
        try (ZipInputStream zip = new ZipInputStream(context.getAssets().open(asset))) {
            ZipEntry entry;
            byte[] buffer = new byte[65536];
            while ((entry = zip.getNextEntry()) != null) {
                control.checkpoint();
                File target = new File(directory, entry.getName());
                if (!target.getCanonicalPath().startsWith(prefix)) throw new IOException("Invalid Python resource path");
                if (entry.isDirectory()) {
                    if (!target.isDirectory() && !target.mkdirs()) throw new IOException("Cannot create resource directory");
                    continue;
                }
                if (!target.getParentFile().isDirectory() && !target.getParentFile().mkdirs())
                    throw new IOException("Cannot create resource parent");
                try (FileOutputStream output = new FileOutputStream(target)) {
                    int size;
                    while ((size = zip.read(buffer)) != -1) {
                        control.checkpoint();
                        output.write(buffer, 0, size);
                    }
                    output.getFD().sync();
                }
            }
        }
    }

    static boolean resourcesMatch(Context context, String asset, File directory, boolean completionMarker, Control control) throws Exception {
        try (InputStream archive = context.getAssets().open(asset)) {
            return ResourceVerifier.matches(archive, directory, completionMarker, control::checkpoint);
        }
    }

    static void syncDirectory(File directory) throws IOException {
        try {
            FileDescriptor descriptor = Os.open(directory.getAbsolutePath(),
                    OsConstants.O_RDONLY | OsConstants.O_CLOEXEC, 0);
            try { Os.fsync(descriptor); }
            finally { Os.close(descriptor); }
        } catch (ErrnoException failure) {
            throw new IOException("Cannot sync prepared resource directory", failure);
        }
    }

    private static void syncDirectories(File directory, Control control) throws Exception {
        control.checkpoint();
        File[] children = directory.listFiles();
        if (children == null) throw new IOException("Cannot inspect prepared resource directory");
        for (File child : children) if (child.isDirectory()) syncDirectories(child, control);
        syncDirectory(directory);
    }

    static void publishResources(File pending, File installed, Control control) throws Exception {
        // Each extracted file is synced before close; persist directory entries
        // before the completion marker can make the generation reusable.
        syncDirectories(pending, control);
        control.checkpoint();
        // Finish publication atomically with respect to pause requests. A later
        // checkpoint may pause with this complete generation safely reusable.
        try (FileOutputStream marker = new FileOutputStream(new File(pending, "complete"))) {
            marker.write(1);
            marker.getFD().sync();
        }
        syncDirectory(pending);
        remove(installed);
        if (!pending.renameTo(installed)) throw new IOException("Cannot publish prepared resources");
        syncDirectory(installed.getParentFile());
    }

    static void remove(File path) throws IOException {
        if (java.nio.file.Files.isSymbolicLink(path.toPath())) {
            java.nio.file.Files.delete(path.toPath());
            return;
        }
        if (!path.exists()) return;
        File[] children = path.listFiles();
        if (children != null) for (File child : children) remove(child);
        if (!path.delete()) throw new IOException("Cannot remove incomplete Python resources");
    }
}
