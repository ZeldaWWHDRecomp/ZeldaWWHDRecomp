package org.wwhdrecomp.wwhd;

import android.content.Context;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.UUID;
import org.json.JSONObject;

/** Small atomic control records shared by the UI, service and Python worker. */
public final class SetupStore {
    private SetupStore() {}

    public static File job(Context context, String id) throws Exception {
        if (id == null || !id.matches("[0-9a-f]{32}")) throw new IOException("Invalid setup job identity");
        return new File(new File(AndroidGame.storage(context), "jobs"), id);
    }

    public static File current(Context context) throws Exception {
        File pointer = new File(AndroidGame.storage(context), "setup.json");
        if (!pointer.isFile()) return null;
        JSONObject value = read(pointer);
        if (value.getInt("schema") != 1) throw new IOException("Unsupported setup record");
        return job(context, value.getString("job"));
    }

    public static void select(Context context, String id) throws Exception {
        File root = job(context, id);
        if (!new File(root, "job.json").isFile() && !new File(root, "source.json").isFile())
            throw new IOException("Setup job is incomplete");
        write(new File(AndroidGame.storage(context), "setup.json"), new JSONObject().put("schema", 1).put("job", id));
    }

    public static JSONObject read(File file) throws Exception {
        return new JSONObject(new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8));
    }

    public static void write(File file, JSONObject value) throws Exception {
        File parent = file.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create setup storage");
        File temporary = new File(parent, file.getName() + ".tmp-" + UUID.randomUUID());
        try {
            try (FileOutputStream stream = new FileOutputStream(temporary)) {
                stream.write(value.toString().getBytes(StandardCharsets.UTF_8));
                stream.getFD().sync();
            }
            android.system.Os.rename(temporary.getPath(), file.getPath());
            sync(parent);
        } finally { temporary.delete(); }
    }

    public static void remove(File file) throws Exception {
        if (file.exists() && !file.delete()) throw new IOException("Cannot clear setup control");
        if (file.getParentFile().isDirectory()) sync(file.getParentFile());
    }

    private static void sync(File directory) throws Exception {
        java.io.FileDescriptor descriptor = android.system.Os.open(directory.getPath(), android.system.OsConstants.O_RDONLY, 0);
        try { android.system.Os.fsync(descriptor); }
        finally { android.system.Os.close(descriptor); }
    }
}
