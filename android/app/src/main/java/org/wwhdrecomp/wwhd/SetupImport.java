package org.wwhdrecomp.wwhd;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.HashSet;
import org.json.JSONArray;
import org.json.JSONObject;

/** Copies persisted SAF documents into an immutable, resumable private input. */
public final class SetupImport {
    private SetupImport() {}
    public interface Control {
        void checkpoint() throws Exception;
        void progress(long copied, long total, int complete, int files) throws Exception;
    }
    private static final long RESERVE = 1024L * 1024 * 1024;
    private static final String[] COLUMNS = {
        DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE,
        DocumentsContract.Document.COLUMN_LAST_MODIFIED
    };

    public static File keyReplacement(File job, String key) throws IOException {
        if (!key.equals("disc") && !key.equals("common")) throw new IOException("Unknown key selection");
        return new File(job, "replace-" + key + "-key.json");
    }

    /** Imported images can recover key selections even if their original source record is absent. */
    static JSONObject keySource(File job) throws Exception {
        File source = new File(job, "source.json");
        if (source.isFile()) return SetupStore.read(source);
        JSONObject input = SetupStore.read(new File(job, "job.json")).getJSONObject("input");
        if (!input.getString("kind").equals("image")) throw new IOException("Keys are only needed for disc images");
        return new JSONObject().put("schema", 1).put("kind", "image");
    }

    private static String selectedKeyUri(File job, JSONObject source, String key) throws Exception {
        File pending = keyReplacement(job, key);
        return pending.isFile() ? SetupStore.read(pending).getString("uri") : source.optString(key + "_uri");
    }

    private static boolean lostKey(Context context, File job, String key, String uri) throws Exception {
        if (SetupKeyAccess.failed(job, key) || uri.isEmpty()) return true;
        for (android.content.UriPermission permission : context.getContentResolver().getPersistedUriPermissions())
            if (permission.isReadPermission() && permission.getUri().toString().equals(uri)) return false;
        return true;
    }

    /** A multi-key selection repairs linked or lost roles together, using only URI metadata on disk. */
    public static void selectKey(Context context, File job, String key, Uri uri) throws Exception {
        JSONObject source = keySource(job);
        String other = SetupKeyRecoveryPolicy.other(key);
        String oldUri = selectedKeyUri(job, source, key), otherUri = selectedKeyUri(job, source, other);
        boolean multiKey;
        try (InputStream input = context.getContentResolver().openInputStream(uri)) {
            if (input == null) throw new IOException("Cannot read selected key file; choose the key again");
            multiKey = SetupKeyRecoveryPolicy.isMultiKeyDocument(input);
        }
        String[] keys = SetupKeyRecoveryPolicy.replacementKeys(key, multiKey, oldUri, otherUri,
            lostKey(context, job, key, oldUri), lostKey(context, job, other, otherUri));
        selectKeys(job, keys, uri);
    }

    /** Queue only the key URI; single-role callers retain their existing behaviour. */
    public static void selectKey(File job, String key, Uri uri) throws Exception {
        selectKeys(job, new String[] {key}, uri);
    }

    private static void selectKeys(File job, String[] keys, Uri uri) throws Exception {
        JSONObject source = keySource(job);
        if (!source.getString("kind").equals("image")) throw new IOException("Keys are only needed for disc images");
        if (new File(job, "job.json").isFile()) {
            String state = SetupStore.read(new File(job, "host.json")).optString("state");
            if (!state.equals("failed") && !(state.equals("paused") && new File(job, "manual-pause.json").isFile()))
                throw new IOException("Pause setup and wait for its checkpoint before changing keys");
            for (String key : keys)
                SetupStore.write(keyReplacement(job, key), new JSONObject().put("schema", 1).put("uri", uri.toString()));
        } else {
            for (String key : keys) source.put(key + "_uri", uri.toString());
            SetupStore.write(new File(job, "source.json"), source);
        }
        SetupKeyAccess.clearFailures(job, keys);
    }

    private static void replaceKeys(Context context, File job, JSONObject configuration, Control control) throws Exception {
        for (String key : new String[] {"disc", "common"}) {
            File pending = keyReplacement(job, key);
            if (pending.isFile()) {
                // Private snapshots need source metadata only when a new key was selected.
                JSONObject source = keySource(job);
                JSONObject selection = SetupStore.read(pending);
                if (selection.getInt("schema") != 1) throw new IOException("Unsupported key selection version");
                control.checkpoint();
                source.put(key + "_uri", selection.getString("uri"));
                configuration.getJSONObject("input").remove(key + "_key");
                SetupStore.write(new File(job, "source.json"), source);
                SetupStore.write(new File(job, "job.json"), configuration);
                SetupStore.remove(pending);
            }
            // Keep legacy copies and references through pause, failure and process death.
            // A deliberate replacement changes only its role; old bytes remain until completion.
        }
        SetupStore.write(new File(job, "job.json"), configuration);
    }

    public static JSONObject prepare(Context context, File job, Control control) throws Exception {
        File configuration = new File(job, "job.json");
        // Completed imports are private snapshots: later provider changes do not alter a rebuild.
        if (configuration.isFile()) {
            JSONObject existing = SetupStore.read(configuration);
            replaceKeys(context, job, existing, control);
            SetupKeyAccess.cleanupCompleted(job);
            return SetupStore.read(configuration);
        }
        JSONObject source = SetupStore.read(new File(job, "source.json"));
        if (source.getInt("schema") != 1) throw new IOException("Unsupported input selection version");
        String kind = source.getString("kind");
        if (!kind.equals("folder") && !kind.equals("archive") && !kind.equals("image"))
            throw new IOException("Unsupported input selection");
        control.checkpoint();
        File inventoryFile = new File(job, "import-inventory.json");
        JSONObject inventory;
        if (inventoryFile.isFile()) inventory = SetupStore.read(inventoryFile);
        else {
            JSONArray files = new JSONArray();
            JSONArray directories = new JSONArray();
            Uri uri = Uri.parse(source.getString("uri"));
            if (kind.equals("folder")) {
                // never copy a wrong folder: check for code, content and meta first (an older selection,
                // or a folder changed since it was chosen, is checked here too)
                String start = source.optString("document", DocumentsContract.getTreeDocumentId(uri));
                SetupFolderCheck.Result found = checkFolder(context, uri, start);
                if (!found.usable()) throw new IOException(found.message());
                directories.put("input/game");
                walk(context, uri, found.document, "input/game", files,
                    directories, new HashSet<>(), control, 0);
            } else files.put(document(context, uri, "input/dump"));
            inventory = new JSONObject().put("schema", 1).put("files", files).put("directories", directories);
            SetupStore.write(inventoryFile, inventory);
        }
        if (inventory.getInt("schema") != 1) throw new IOException("Unsupported import inventory");
        JSONArray directories = inventory.getJSONArray("directories");
        for (int index = 0; index < directories.length(); index++) {
            File directory = inside(job, directories.getString(index));
            if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Cannot create private input folder");
        }
        JSONArray files = inventory.getJSONArray("files");
        long total = 0;
        for (int index = 0; index < files.length(); index++) {
            long size = files.getJSONObject(index).getLong("size");
            if (size < 0) { total = -1; break; }
            total = Math.addExact(total, size);
        }
        long copied = 0;
        for (int index = 0; index < files.length(); index++) {
            control.checkpoint();
            JSONObject entry = files.getJSONObject(index);
            File target = inside(job, entry.getString("path"));
            File marker = new File(job, "import-checkpoints/" + index + ".json");
            if (!verified(target, marker, control)) {
                final long previous = copied;
                final long expected = total;
                final int complete = index;
                JSONObject receipt = copy(context, Uri.parse(entry.getString("uri")), target, new File(job, "import-partial/" + index),
                    entry.getLong("size"), entry.getLong("modified"), false, control,
                    count -> control.progress(previous + count, expected, complete, files.length()));
                SetupStore.write(marker, receipt);
            }
            copied = Math.addExact(copied, target.length());
            control.progress(copied, total, index + 1, files.length());
        }
        JSONObject input = new JSONObject().put("kind", kind).put("path", kind.equals("folder") ? "input/game" : "input/dump");
        // An older release may have been killed after copying keys, before publishing job.json.
        if (kind.equals("image")) for (String key : new String[] {"disc", "common"}) {
            if (new File(job, "input/" + key + ".key").isFile()) input.put(key + "_key", "input/" + key + ".key");
        }
        control.checkpoint();
        JSONObject result = new JSONObject().put("schema", 1).put("input", input);
        SetupStore.write(configuration, result);
        return result;
    }

    /** Checks a chosen folder (document {@code start} of the granted {@code tree}) for the game. */
    public static SetupFolderCheck.Result checkFolder(Context context, Uri tree, String start) throws Exception {
        String name = null;
        try (Cursor cursor = context.getContentResolver().query(DocumentsContract.buildDocumentUriUsingTree(tree, start),
                new String[] {DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) name = cursor.getString(0);
        }
        // the chosen folder's own names must be safe to copy; folders searched below it may hold anything
        return SetupFolderCheck.check(id -> list(context, tree, id, id.equals(start)), start, name);
    }

    private static java.util.List<SetupFolderCheck.Entry> list(Context context, Uri tree, String id, boolean strict) throws IOException {
        java.util.ArrayList<SetupFolderCheck.Entry> entries = new java.util.ArrayList<>();
        try (Cursor cursor = context.getContentResolver().query(DocumentsContract.buildChildDocumentsUriUsingTree(tree, id),
                COLUMNS, null, null, null)) {
            if (cursor == null) throw new IOException("Cannot list the chosen folder; choose it again");
            HashSet<String> names = new HashSet<>();
            while (cursor.moveToNext()) {
                String name = cursor.getString(1), child = cursor.getString(0);
                if (!safeName(name) || !names.add(name)) {
                    if (strict) throw new IOException("Selected folder contains an unsafe or duplicate filename");
                    continue;
                }
                if (child == null) continue;
                entries.add(new SetupFolderCheck.Entry(name, child, DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2))));
            }
        }
        return entries;
    }

    private static boolean safeName(String name) {
        return name != null && !name.isEmpty() && !name.equals(".") && !name.equals("..") &&
            name.indexOf('/') < 0 && name.indexOf('\\') < 0 && name.indexOf('\0') < 0;
    }

    private static void walk(Context context, Uri tree, String id, String parent, JSONArray files,
                             JSONArray directories, HashSet<String> seen, Control control, int depth) throws Exception {
        control.checkpoint();
        if (depth > 64 || !seen.add(id)) throw new IOException("Input folder contains a cycle or excessive nesting");
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
        try (Cursor cursor = context.getContentResolver().query(children, COLUMNS, null, null, null)) {
            if (cursor == null) throw new IOException("Cannot list selected folder; select it again");
            HashSet<String> names = new HashSet<>();
            while (cursor.moveToNext()) {
                control.checkpoint();
                String name = cursor.getString(1);
                if (!safeName(name) || !names.add(name))
                    throw new IOException("Selected folder contains an unsafe or duplicate filename");
                String child = cursor.getString(0);
                String path = parent + "/" + name;
                if (files.length() + directories.length() >= 200000) throw new IOException("Selected folder has too many entries");
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2))) {
                    directories.put(path);
                    walk(context, tree, child, path, files, directories, seen, control, depth + 1);
                } else files.put(new JSONObject().put("path", path)
                    .put("uri", DocumentsContract.buildDocumentUriUsingTree(tree, child).toString())
                    .put("size", cursor.isNull(3) ? -1 : cursor.getLong(3))
                    .put("modified", cursor.isNull(4) ? -1 : cursor.getLong(4)));
            }
        }
    }

    private static JSONObject document(Context context, Uri uri, String path) throws Exception {
        long size = -1, modified = -1;
        try (Cursor cursor = context.getContentResolver().query(uri,
                new String[] {OpenableColumns.SIZE, DocumentsContract.Document.COLUMN_LAST_MODIFIED}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                if (!cursor.isNull(0)) size = cursor.getLong(0);
                if (!cursor.isNull(1)) modified = cursor.getLong(1);
            }
        }
        return new JSONObject().put("uri", uri.toString()).put("path", path).put("size", size).put("modified", modified);
    }

    private interface Bytes { void progress(long count) throws Exception; }
    private static JSONObject copy(Context context, Uri uri, File target, File temporary, long expectedSize, long modified,
                                   boolean key, Control control, Bytes progress) throws Exception {
        File parent = target.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create private input storage");
        File scratch = temporary.getParentFile();
        if (!scratch.isDirectory() && !scratch.mkdirs()) throw new IOException("Cannot create import staging folder");
        if (temporary.exists() && !temporary.delete()) throw new IOException("Cannot clear interrupted input copy");
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        long count = 0, reported = 0;
        try {
            if (expectedSize >= 0 && parent.getUsableSpace() - RESERVE < expectedSize)
                throw new IOException("Not enough storage to copy the selected input plus 1 GiB working reserve");
            try (InputStream input = context.getContentResolver().openInputStream(uri);
                 FileOutputStream output = new FileOutputStream(temporary)) {
                if (input == null) throw new IOException("Cannot open selected input; select it again");
                byte[] buffer = new byte[key ? 4097 : 262144];
                int length;
                while ((length = input.read(buffer)) != -1) {
                    control.checkpoint();
                    count = Math.addExact(count, length);
                    if (key && count > 4096) throw new IOException("Key file must be at most 4096 bytes");
                    if (parent.getUsableSpace() < RESERVE + length) throw new IOException("Not enough storage; free space and retry setup");
                    output.write(buffer, 0, length);
                    if (!key) digest.update(buffer, 0, length);
                    if (count - reported >= 4 * 1024 * 1024) { progress.progress(count); reported = count; }
                }
                output.getFD().sync();
            }
            if (expectedSize >= 0 && count != expectedSize) throw new IOException("Selected input changed size while importing; select it again");
            if (!key) {
                JSONObject current = document(context, uri, "");
                if ((current.getLong("size") >= 0 && current.getLong("size") != count) ||
                    (modified > 0 && current.getLong("modified") > 0 && current.getLong("modified") != modified))
                    throw new IOException("Selected input changed while importing; select it again");
            }
            control.checkpoint();
            android.system.Os.rename(temporary.getPath(), target.getPath());
            sync(parent);
            return key ? new JSONObject() : new JSONObject().put("schema", 1).put("size", count).put("sha256", hex(digest.digest()));
        } finally { temporary.delete(); }
    }

    private static boolean verified(File target, File marker, Control control) throws Exception {
        if (!target.isFile() || !marker.isFile()) return false;
        JSONObject receipt;
        try { receipt = SetupStore.read(marker); }
        catch (Exception invalid) { return false; }
        if (receipt.optInt("schema") != 1 || receipt.optLong("size", -1) != target.length()) return false;
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream input = new FileInputStream(target)) {
            byte[] buffer = new byte[262144];
            int count;
            while ((count = input.read(buffer)) != -1) { control.checkpoint(); digest.update(buffer, 0, count); }
        }
        return hex(digest.digest()).equals(receipt.optString("sha256"));
    }

    private static String hex(byte[] bytes) {
        StringBuilder result = new StringBuilder();
        for (byte value : bytes) result.append(String.format("%02x", value & 255));
        return result.toString();
    }
    private static File inside(File root, String relative) throws Exception {
        File result = new File(root, relative).getCanonicalFile();
        if (!result.getPath().startsWith(root.getCanonicalPath() + File.separator)) throw new IOException("Input path escapes its private job");
        return result;
    }
    private static void sync(File directory) throws Exception {
        java.io.FileDescriptor descriptor = android.system.Os.open(directory.getPath(), android.system.OsConstants.O_RDONLY, 0);
        try { android.system.Os.fsync(descriptor); }
        finally { android.system.Os.close(descriptor); }
    }
}
