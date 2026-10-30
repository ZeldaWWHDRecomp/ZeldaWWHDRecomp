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
        if (input == null) throw new IOException("Não foi possível abrir o arquivo.");
        if (!stage.isDirectory() && !stage.mkdirs()) throw new IOException("Não foi possível preparar o save.");
        String base = stage.getCanonicalPath() + File.separator;
        Set<String> names = new HashSet<>();
        long total = 0;
        int count = 0;
        try (ZipInputStream zip = new ZipInputStream(new BoundedInput(input, MAX_TOTAL))) {
            for (ZipEntry entry; (entry = zip.getNextEntry()) != null;) {
                if (++count > MAX_ENTRIES) throw new IOException("O ZIP tem arquivos demais.");
                String name = entry.getName().replace('\\', '/');
                if (name.endsWith("/")) name = name.substring(0, name.length() - 1);
                if (!safeName(name) || !names.add(name.toLowerCase(Locale.ROOT)))
                    throw new IOException("O ZIP contém um caminho inválido ou repetido.");
                if (entry.getSize() > MAX_FILE) throw new IOException("Um arquivo do save é grande demais.");
                File output = new File(stage, name);
                if (!output.getCanonicalPath().startsWith(base)) throw new IOException("Caminho fora do save.");
                if (entry.isDirectory()) {
                    if (!output.isDirectory() && !output.mkdirs()) throw new IOException("Não foi possível criar uma pasta.");
                    if (zip.read() != -1) throw new IOException("Uma pasta do ZIP contém dados inesperados.");
                    continue;
                }
                File parent = output.getParentFile();
                if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Não foi possível preparar uma pasta.");
                long size = 0;
                try (FileOutputStream out = new FileOutputStream(output)) {
                    byte[] buffer = new byte[64 * 1024];
                    for (int n; (n = zip.read(buffer)) != -1;) {
                        if (Thread.currentThread().isInterrupted()) throw new IOException("Importação interrompida.");
                        size += n;
                        total += n;
                        if (size > MAX_FILE || total > MAX_TOTAL) throw new IOException("O save excede o limite de descompactação.");
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
                if (selected != null) throw new IOException("O ZIP contém mais de um save.");
                selected = child;
            }
        }
        if (selected == null) throw new IOException("O ZIP não contém user/cking.sav válido.");
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
            if (value != -1 && --remaining < 0) throw new IOException("O ZIP é grande demais.");
            return value;
        }
        @Override public int read(byte[] data, int offset, int length) throws IOException {
            int n = in.read(data, offset, length);
            if (n > 0 && (remaining -= n) < 0) throw new IOException("O ZIP é grande demais.");
            return n;
        }
    }
}
