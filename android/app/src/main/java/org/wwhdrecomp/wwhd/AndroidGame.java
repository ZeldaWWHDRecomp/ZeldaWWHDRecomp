package org.wwhdrecomp.wwhd;

import android.content.Context;
import android.util.AtomicFile;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import org.json.JSONObject;

/** Validated, atomic selection of a locally linked runtime/game generation. */
public final class AndroidGame {
    private AndroidGame() {}
    private static native void validate(String library);

    /** One snapshot: an update cannot mix a loaded library with newer assets. */
    public static final class Selection {
        public final File library;
        public final File game;
        public final boolean codeMods;  // built with code-mod support (the setup's choice at that time)
        private Selection(File library, File game, boolean codeMods) {
            this.library = library;
            this.game = game;
            this.codeMods = codeMods;
        }
    }

    // ---- the code-mod choice (setup's Code mods switch, or Settings > Mods in the game): the next
    // build translates the game code with mod support. A build records what it was built with
    // ("code_mods" in active.json); a different choice makes the current build need a rebuild.
    private static AtomicFile choice(Context context) {
        return new AtomicFile(new File(storage(context), "code-mods.json"));
    }

    public static synchronized boolean codeModsChosen(Context context) {
        try { return read(choice(context)).optBoolean("enabled", false); }
        catch (Exception missing) { return false; }
    }

    public static synchronized void chooseCodeMods(Context context, boolean enabled) throws Exception {
        try (SelectionLock ignored = new SelectionLock(context)) {
            write(choice(context), new JSONObject().put("schema", 1).put("enabled", enabled));
        }
    }

    // ---- setup's optional models step ("Preparing shaders: models", ModelsStep): the player's choice
    private static AtomicFile modelsChoice(Context context) {
        return new AtomicFile(new File(storage(context), "models-choice.json"));
    }

    public static synchronized boolean modelsChosen(Context context) {
        // no choice yet: the step is recommended (ModelsStep.RECOMMENDED)
        try {
            JSONObject choice = read(modelsChoice(context));
            return ModelsStep.chosen(choice.has("enabled") ? choice.getBoolean("enabled") : null);
        } catch (Exception missing) { return ModelsStep.chosen(null); }
    }

    public static synchronized void chooseModels(Context context, boolean enabled) throws Exception {
        storage(context).mkdirs();
        write(modelsChoice(context), new JSONObject().put("schema", 1).put("enabled", enabled));
    }

    public static File storage(Context context) {
        return new File(context.getNoBackupFilesDir(), "ondevice");
    }

    private static JSONObject asset(Context context, String name) throws Exception {
        try (InputStream input = context.getAssets().open(name)) {
            return new JSONObject(new String(input.readAllBytes(), StandardCharsets.UTF_8));
        }
    }

    private static JSONObject expected(Context context) throws Exception {
        JSONObject runtime = asset(context, "runtime-sdk.json");
        JSONObject compiler = asset(context, "toolchain-apk.json");
        if (runtime.getInt("host_api") != 1) throw new IOException("Unsupported Android runtime host API");
        if (!runtime.getString("abi").equals(compiler.getString("abi")))
            throw new IOException("Runtime/compiler architecture mismatch");
        return new JSONObject().put("schema", 1).put("host_api", 1).put("abi", runtime.getString("abi"))
            .put("python_identity", asset(context, "python-build.json").getString("sha256"))
            .put("runtime_identity", runtime.getString("identity"))
            .put("compiler_identity", compiler.getString("compiler_identity"))
            .put("pipeline_identity", EmbeddedPython.checksum(context, "python-source.zip"));
    }

    private static String hash(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream input = new FileInputStream(file)) {
            byte[] buffer = new byte[65536];
            int count;
            while ((count = input.read(buffer)) != -1) digest.update(buffer, 0, count);
        }
        StringBuilder result = new StringBuilder();
        for (byte value : digest.digest()) result.append(String.format("%02x", value & 255));
        return result.toString();
    }

    private static AtomicFile record(Context context, String name) {
        return new AtomicFile(new File(storage(context), name + ".json"));
    }

    private static final class SelectionLock implements AutoCloseable {
        private final java.io.RandomAccessFile file;
        private final java.nio.channels.FileLock lock;
        SelectionLock(Context context) throws IOException {
            File root = storage(context);
            if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Cannot create setup storage");
            file = new java.io.RandomAccessFile(new File(root, "selection.lock"), "rw");
            try { lock = file.getChannel().lock(); }
            catch (IOException | RuntimeException failure) { file.close(); throw failure; }
        }
        @Override public void close() throws IOException {
            try { lock.release(); }
            finally { file.close(); }
        }
    }

    private static JSONObject read(AtomicFile file) throws Exception {
        try (InputStream input = file.openRead()) {
            return new JSONObject(new String(input.readAllBytes(), StandardCharsets.UTF_8));
        }
    }

    private static void write(AtomicFile file, JSONObject value) throws Exception {
        FileOutputStream stream = file.startWrite();
        try {
            stream.write(value.toString().getBytes(StandardCharsets.UTF_8));
            stream.getFD().sync();
            file.finishWrite(stream);
        } catch (Exception failure) {
            file.failWrite(stream);
            throw failure;
        }
        if (!read(file).toString().equals(value.toString())) throw new IOException("Cannot publish game selection");
        java.io.FileDescriptor directory = android.system.Os.open(file.getBaseFile().getParent(),
            android.system.OsConstants.O_RDONLY, 0);
        try { android.system.Os.fsync(directory); }
        finally { android.system.Os.close(directory); }
    }

    private static File library(Context context, JSONObject value, boolean requireCurrentInputs) throws Exception {
        JSONObject expected = expected(context);
        String[] keys = requireCurrentInputs ?
            new String[] {"schema", "host_api", "abi", "python_identity", "runtime_identity", "compiler_identity", "pipeline_identity"} :
            new String[] {"schema", "host_api", "abi"};
        for (String key : keys)
            if (!expected.get(key).equals(value.get(key))) throw new IOException("Build needs an update");
        File library = new File(storage(context), value.getString("library")).getCanonicalFile();
        if (!library.getPath().startsWith(storage(context).getCanonicalPath() + File.separator) ||
                !library.isFile() || library.canWrite())
            throw new IOException("Invalid completed game library");
        if (!hash(library).equals(value.getString("sha256"))) throw new IOException("Game library checksum mismatch");
        if (value.has("game")) {
            File game = gameFolder(context, new File(storage(context), value.getString("game")));
            if (!hash(new File(game, "code/cking.rpx")).equals(value.getString("rpx_sha256")))
                throw new IOException("Selected game executable checksum mismatch");
        } else if (value.has("rpx_sha256")) throw new IOException("Game asset selection is incomplete");
        return library;
    }

    private static File gameFolder(Context context, File game) throws Exception {
        File root = storage(context).getCanonicalFile();
        game = game.getCanonicalFile();
        if (!game.getPath().startsWith(root.getPath() + File.separator) || !game.isDirectory())
            throw new IOException("Game assets are outside setup storage");
        for (String name : new String[] {"code/cking.rpx", "content", "meta/meta.xml"}) {
            File child = new File(game, name);
            if (!child.getCanonicalFile().equals(child.getAbsoluteFile()) ||
                (name.equals("content") ? !child.isDirectory() : !child.isFile()))
                throw new IOException("Game asset tree is incomplete or redirected");
        }
        return game;
    }

    /** Returns null for a missing, interrupted or corrupt build. */
    public static synchronized File current(Context context) {
        Selection selection = selected(context);
        return selection == null ? null : selection.library;
    }

    public static synchronized Selection selected(Context context) {
        if (!storage(context).isDirectory()) return null;
        // AtomicFile requires external synchronization. Readers in the game
        // process must not remove a setup process's in-progress .new file.
        try (SelectionLock ignored = new SelectionLock(context)) {
            JSONObject value = read(record(context, "active"));
            File library = library(context, value, false);
            File game = value.has("game") ? new File(storage(context), value.getString("game")).getCanonicalFile() : null;
            return new Selection(library, game, value.optBoolean("code_mods", false));
        } catch (java.io.FileNotFoundException missing) {
            return null;
        } catch (Exception failure) {
            Log.w("wwhd-game", "On-device build unavailable: " + failure.getMessage());
            return null;
        }
    }

    /** A compatible previous runtime can still launch while its replacement builds. */
    public static synchronized boolean needsRebuild(Context context) {
        boolean chosen = codeModsChosen(context);
        try (SelectionLock ignored = new SelectionLock(context)) {
            JSONObject active = read(record(context, "active"));
            library(context, active, true);
            return active.optBoolean("code_mods", false) != chosen;
        } catch (Exception failure) { return true; }
    }

    /** Called only in the dedicated setup process, after all build stages pass. */
    public static synchronized void activate(Context context, File candidate) throws Exception {
        activate(context, candidate, null);
    }

    /** Publish the linked library and its validated extracted assets together. */
    public static synchronized void activate(Context context, File candidate, File game) throws Exception {
        activate(context, candidate, game, false);
    }

    /** codeMods: the candidate was built with code-mod support. */
    public static synchronized void activate(Context context, File candidate, File game, boolean codeMods) throws Exception {
        try (SelectionLock ignored = new SelectionLock(context)) {
            activateLocked(context, candidate, game, codeMods);
        }
    }

    private static void activateLocked(Context context, File candidate, File game, boolean codeMods) throws Exception {
        JSONObject value = expected(context);
        if (codeMods) value.put("code_mods", true);
        File root = storage(context).getCanonicalFile();
        candidate = candidate.getCanonicalFile();
        if (!candidate.getPath().startsWith(root.getPath() + File.separator) ||
                !candidate.isFile() || candidate.canWrite())
            throw new IOException("Candidate is outside completed setup storage");
        value.put("library", root.toPath().relativize(candidate.toPath()).toString())
            .put("sha256", hash(candidate));
        if (game != null) {
            game = gameFolder(context, game);
            value.put("game", root.toPath().relativize(game.toPath()).toString())
                .put("rpx_sha256", hash(new File(game, "code/cking.rpx")));
        }
        System.loadLibrary("wwhdpython");
        validate(candidate.getAbsolutePath()); // RTLD_NOW: dependencies and SDL_main must resolve.
        // Recheck immediately before publication; failed validation never moves
        // the current selection. Older libraries and saves remain untouched.
        library(context, value, true);
        JSONObject previous = null;
        try {
            JSONObject existing = read(record(context, "active"));
            library(context, existing, false);
            previous = existing;
        }
        catch (java.io.FileNotFoundException missing) { /* first build */ }
        catch (Exception corrupt) { Log.w("wwhd-game", "Replacing unreadable active build metadata"); }
        if (previous != null && !previous.toString().equals(value.toString())) write(record(context, "previous"), previous);
        write(record(context, "active"), value);
    }

    public static synchronized boolean rollback(Context context) throws Exception {
        try (SelectionLock ignored = new SelectionLock(context)) {
            JSONObject previous = read(record(context, "previous"));
            library(context, previous, false); // Only restore a runtime compatible with the Java host.
            write(record(context, "active"), previous);
            return true;
        }
    }
}
