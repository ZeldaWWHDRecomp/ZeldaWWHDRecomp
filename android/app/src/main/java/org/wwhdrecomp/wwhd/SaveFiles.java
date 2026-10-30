package org.wwhdrecomp.wwhd;

import android.content.Context;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

// The game's save folder (Android/data/<package>/files/save: user/cking.sav and the Picto Box photos)
// as a zip, the same layout as the save/ folder of the PC version.
final class SaveFiles {
    private SaveFiles() {}

    static File saveDir(Context c) {
        File files = c.getExternalFilesDir(null);
        return new File(files, "save");
    }

    static String stamp() {
        return new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
    }

    static boolean hasSave(Context c) {
        return new File(saveDir(c), "user/cking.sav").isFile();
    }

    static void zip(File dir, OutputStream out) throws IOException {
        try (ZipOutputStream zip = new ZipOutputStream(out)) {
            add(zip, dir, "");
        }
    }

    private static void add(ZipOutputStream zip, File f, String path) throws IOException {
        File[] children = f.listFiles();
        if (children == null) return;
        for (File child : children) {
            String name = path.isEmpty() ? child.getName() : path + "/" + child.getName();
            if (child.isDirectory()) {
                add(zip, child, name);
            } else {
                zip.putNextEntry(new ZipEntry(name));
                try (InputStream in = new FileInputStream(child)) {
                    copy(in, zip);
                }
                zip.closeEntry();
            }
        }
    }

    static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[65536];
        for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
    }

}
