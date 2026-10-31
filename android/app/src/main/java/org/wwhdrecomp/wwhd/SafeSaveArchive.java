// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

import java.io.File;
import java.io.FileOutputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Limits count actual bytes, including entries whose ZIP sizes are unknown. */
final class SafeSaveArchive {
    static final int MAX_ENTRIES = 2048;
    static final long MAX_FILE = 16L * 1024 * 1024;
    static final long MAX_TOTAL = 128L * 1024 * 1024;
    private SafeSaveArchive() { }

    static File extract(InputStream input, File stage) throws IOException {
        if (input == null) throw new IOException("Could not open the archive.");
        if (!stage.isDirectory() && !stage.mkdirs()) throw new IOException("Could not prepare the save.");
        String base = stage.getCanonicalPath() + File.separator;
        Set<String> names = new HashSet<>();
        long total = 0;
        int count = 0;
        try (ZipInputStream zip = new ZipInputStream(new BoundedInput(input, MAX_TOTAL))) {
            for (ZipEntry entry; (entry = zip.getNextEntry()) != null;) {
                if (++count > MAX_ENTRIES) throw new IOException("Too many ZIP entries.");
                String name = entry.getName().replace('\\', '/');
                if (name.endsWith("/")) name = name.substring(0, name.length() - 1);
                if (!safeName(name) || !names.add(name.toLowerCase(Locale.ROOT)))
                    throw new IOException("Invalid or duplicate ZIP path.");
                if (entry.getSize() > MAX_FILE) throw new IOException("A save file exceeds the size limit.");
                File output = new File(stage, name);
                if (!output.getCanonicalPath().startsWith(base)) throw new IOException("Path outside the save directory.");
                if (entry.isDirectory()) {
                    if (!output.isDirectory() && !output.mkdirs()) throw new IOException("Could not create a directory.");
                    if (zip.read() != -1) throw new IOException("Unexpected data in a ZIP directory.");
                    continue;
                }
                File parent = output.getParentFile();
                if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Could not prepare a directory.");
                long size = 0;
                try (FileOutputStream out = new FileOutputStream(output)) {
                    byte[] buffer = new byte[64 * 1024];
                    for (int n; (n = zip.read(buffer)) != -1;) {
                        if (Thread.currentThread().isInterrupted()) throw new IOException("Save import interrupted.");
                        size += n;
                        total += n;
                        if (size > MAX_FILE || total > MAX_TOTAL) throw new IOException("Save exceeds the extraction limit.");
                        out.write(buffer, 0, n);
                    }
                    out.getFD().sync();
                }
                zip.closeEntry(); // validates CRC before the staged tree can be committed
            }
        }
        File selected = null;
        File direct = new File(stage, "user/cking.sav");
        if (direct.isFile() && direct.length() > 0) selected = stage;
        File[] children = stage.listFiles();
        if (children != null) for (File child : children) {
            File save = new File(child, "user/cking.sav");
            if (child.isDirectory() && save.isFile() && save.length() > 0) {
                if (selected != null) throw new IOException("The ZIP contains more than one save.");
                selected = child;
            }
        }
        if (selected == null) throw new IOException("The ZIP does not contain a nonempty user/cking.sav.");
        return selected;
    }

    static boolean safeName(String name) {
        if (name.isEmpty() || name.length() > 512 || name.startsWith("/") || name.indexOf(':') >= 0 || name.indexOf('\0') >= 0) return false;
        for (String part : name.split("/", -1)) if (part.isEmpty() || part.equals(".") || part.equals("..")) return false;
        return true;
    }

    private static final class BoundedInput extends FilterInputStream {
        private long remaining;
        BoundedInput(InputStream input, long limit) { super(input); remaining = limit; }
        @Override public int read() throws IOException {
            int value = in.read();
            if (value != -1 && --remaining < 0) throw new IOException("The ZIP exceeds the input size limit.");
            return value;
        }
        @Override public int read(byte[] data, int offset, int length) throws IOException {
            int n = in.read(data, offset, length);
            if (n > 0 && (remaining -= n) < 0) throw new IOException("The ZIP exceeds the input size limit.");
            return n;
        }
    }
}
