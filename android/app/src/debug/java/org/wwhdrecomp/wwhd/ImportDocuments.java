package org.wwhdrecomp.wwhd;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract.Document;
import android.provider.DocumentsContract.Root;
import android.provider.DocumentsProvider;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.OutputStream;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

/** Authored streaming documents, compiled only into debug APKs. */
public final class ImportDocuments extends DocumentsProvider {
    public static final String AUTHORITY = "org.wwhdrecomp.wwhd.import-fixture";
    public static final ConcurrentHashMap<String, AtomicInteger> OPENS = new ConcurrentHashMap<>();
    private static final String[] DOC = {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
        Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED};
    @Override public boolean onCreate() { return true; }
    @Override public Cursor queryRoots(String[] projection) {
        MatrixCursor result = new MatrixCursor(projection == null ? new String[] {Root.COLUMN_ROOT_ID,
            Root.COLUMN_DOCUMENT_ID, Root.COLUMN_TITLE, Root.COLUMN_FLAGS} : projection);
        result.newRow().add(Root.COLUMN_ROOT_ID, "fixture").add(Root.COLUMN_DOCUMENT_ID, "root")
            .add(Root.COLUMN_TITLE, "WWHD synthetic fixture").add(Root.COLUMN_FLAGS, Root.FLAG_SUPPORTS_IS_CHILD);
        return result;
    }
    private static boolean directory(String id) {
        return id.equals("root") || id.equals("unsafe") || id.equals("root/code") || id.equals("root/content") || id.equals("root/meta");
    }
    private static int size(String id) { return id.equals("root/code/cking.rpx") ? 2 * 1024 * 1024 : id.equals("key") || id.equals("replacementkey") ? 16 : id.equals("bigkey") ? 4097 : 127; }
    private static void row(MatrixCursor result, String id) {
        String name = id.equals("unsafe/escape") ? "../escape" : id.substring(id.lastIndexOf('/') + 1);
        Object[] values = new Object[result.getColumnCount()];
        for (int index = 0; index < values.length; index++) {
            switch (result.getColumnNames()[index]) {
                case Document.COLUMN_DOCUMENT_ID: values[index] = id; break;
                case Document.COLUMN_DISPLAY_NAME: values[index] = name; break;
                case Document.COLUMN_MIME_TYPE: values[index] = directory(id) ? Document.MIME_TYPE_DIR : "application/octet-stream"; break;
                case Document.COLUMN_SIZE: values[index] = directory(id) || id.endsWith("asset") ? null : size(id) + (id.equals("short") ? 1 : 0); break;
                case Document.COLUMN_LAST_MODIFIED: values[index] = 1000L; break;
            }
        }
        result.addRow(values);
    }
    @Override public Cursor queryDocument(String id, String[] projection) {
        MatrixCursor result = new MatrixCursor(projection == null ? DOC : projection);
        row(result, id);
        return result;
    }
    @Override public Cursor queryChildDocuments(String id, String[] projection, String sort) {
        MatrixCursor result = new MatrixCursor(projection == null ? DOC : projection);
        if (id.equals("root")) { row(result, "root/code"); row(result, "root/content"); row(result, "root/meta"); }
        if (id.equals("root/code")) row(result, "root/code/cking.rpx");
        if (id.equals("root/content")) { row(result, "root/content/asset.importing"); row(result, "root/content/asset"); }
        if (id.equals("root/meta")) row(result, "root/meta/meta.xml");
        if (id.equals("unsafe")) row(result, "unsafe/escape");
        return result;
    }
    @Override public boolean isChildDocument(String parent, String child) { return child.startsWith(parent + "/"); }
    @Override public ParcelFileDescriptor openDocument(String id, String mode, CancellationSignal signal) throws FileNotFoundException {
        if (!mode.equals("r") || directory(id)) throw new FileNotFoundException();
        OPENS.computeIfAbsent(id, key -> new AtomicInteger()).incrementAndGet();
        try {
            ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createPipe();
            new Thread(() -> {
                try (OutputStream output = new ParcelFileDescriptor.AutoCloseOutputStream(pipe[1])) {
                    byte[] buffer = new byte[8192];
                    java.util.Arrays.fill(buffer, (byte)(id.equals("replacementkey") ? 43 : 42));
                    for (int remaining = size(id); remaining > 0;) {
                        int count = Math.min(remaining, buffer.length);
                        output.write(buffer, 0, count);
                        remaining -= count;
                    }
                } catch (IOException closedByPause) { }
            }, "fixture-document").start();
            return pipe[0];
        } catch (IOException failure) { throw new FileNotFoundException("Cannot open fixture pipe"); }
    }
}
