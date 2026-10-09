package org.wwhdrecomp.wwhd;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.util.HashSet;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Compare a writable cache with its trusted, already-checksummed APK archive. */
final class ResourceVerifier {
    interface Control { void checkpoint() throws Exception; }

    static boolean complete(File directory) throws IOException {
        File marker = new File(directory, "complete");
        if (Files.isSymbolicLink(directory.toPath()) || Files.isSymbolicLink(marker.toPath()) ||
                !marker.isFile() || marker.length() != 1) return false;
        try (InputStream input = new FileInputStream(marker)) { return input.read() == 1; }
    }

    static boolean matches(InputStream archive, File directory, boolean completionMarker, Control control) throws Exception {
        control.checkpoint();
        if (!directory.isDirectory() || Files.isSymbolicLink(directory.toPath())) return false;
        String root = directory.getCanonicalPath();
        String prefix = root + File.separator;
        Set<String> files = new HashSet<>(), directories = new HashSet<>();
        directories.add(root);
        byte[] expected = new byte[65536], actual = new byte[65536];
        try (ZipInputStream zip = new ZipInputStream(archive)) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                control.checkpoint();
                String name = entry.getName();
                if (new File(name).isAbsolute()) throw new IOException("Invalid bundled resource path");
                for (String part : name.split("/"))
                    if (part.equals("..") || part.equals(".")) throw new IOException("Invalid bundled resource path");
                File target = new File(directory, name);
                String path = target.getCanonicalPath();
                if (!path.startsWith(prefix)) return false; // damaged cache contains an escaping symlink
                if (Files.isSymbolicLink(target.toPath())) return false;
                for (File parent = target.getParentFile(); parent != null; parent = parent.getParentFile()) {
                    String parentPath = parent.getCanonicalPath();
                    if (!parentPath.startsWith(prefix)) break;
                    directories.add(parentPath);
                }
                if (entry.isDirectory()) {
                    directories.add(path);
                    if (!target.isDirectory()) return false;
                    continue;
                }
                if (!files.add(path)) throw new IOException("Duplicate bundled resource path");
                if (!target.isFile()) return false;
                try (InputStream input = new FileInputStream(target)) {
                    int size;
                    while ((size = zip.read(expected)) != -1) {
                        control.checkpoint();
                        int offset = 0;
                        while (offset < size) {
                            control.checkpoint();
                            int count = input.read(actual, offset, size - offset);
                            if (count == -1) return false;
                            offset += count;
                        }
                        for (int i = 0; i < size; i++) if (expected[i] != actual[i]) return false;
                    }
                    if (input.read() != -1) return false;
                }
            }
        }
        if (completionMarker) files.add(new File(directory, "complete").getCanonicalPath());
        return inventory(directory, files, directories, control);
    }

    private static boolean inventory(File directory, Set<String> files, Set<String> directories, Control control) throws Exception {
        control.checkpoint();
        File[] children = directory.listFiles();
        if (children == null) return false;
        for (File child : children) {
            control.checkpoint();
            if (Files.isSymbolicLink(child.toPath())) return false;
            String path = child.getCanonicalPath();
            if (child.isDirectory()) {
                if (!directories.contains(path) || !inventory(child, files, directories, control)) return false;
            } else if (!files.contains(path)) return false;
        }
        return true;
    }
}
