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
        public final File randomizerFiles;  // a randomized game's changed files (played over the game), else null
        public final boolean decomp;  // library is the decomp version (setup's choice, GameVersion)
        private Selection(File library, File game, boolean codeMods, File randomizerFiles, boolean decomp) {
            this.library = library;
            this.game = game;
            this.codeMods = codeMods;
            this.randomizerFiles = randomizerFiles;
            this.decomp = decomp;
        }
    }

    // ---- setup's one choice of game version (GameVersion; docs/decomp-build.md; tools/android/decomp_android.py):
    // the recompiled version (the default) or the decomp version, only the chosen one is built and started
    // ({"version": "recomp"|"decomp"}). A file from an earlier app version ({"enabled": true, "start"}: "Also build
    // the decomp version") is "both": both stay built and "start" picks one, until the player chooses.
    private static AtomicFile decompChoice(Context context) {
        return new AtomicFile(new File(storage(context), "decomp-choice.json"));
    }

    private static JSONObject decompChoiceValue(Context context) {
        try { return read(decompChoice(context)); }
        catch (Exception missing) { return new JSONObject(); }
    }

    /** "recomp", "decomp" or (an install from before the choice) "both". */
    public static synchronized String chosenVersion(Context context) {
        JSONObject value = decompChoiceValue(context);
        return GameVersion.chosen(value.has("version") ? value.optString("version") : null,
                                  value.optBoolean("enabled", false));
    }

    /** The decomp version is the chosen one. */
    public static synchronized boolean decompChosen(Context context) {
        return GameVersion.DECOMP.equals(chosenVersion(context));
    }

    /** The game starts the decomp version: the decomp choice, or for "both" its earlier "start" choice. */
    public static synchronized boolean startDecompChosen(Context context) {
        return GameVersion.startsDecomp(chosenVersion(context), decompChoiceValue(context).optBoolean("start", false));
    }

    /** The player's choice of version ("recomp" or "decomp"); it replaces an earlier "both". */
    public static synchronized void chooseVersion(Context context, String version) throws Exception {
        if (!GameVersion.RECOMP.equals(version) && !GameVersion.DECOMP.equals(version))
            throw new IllegalArgumentException("The game version must be recomp or decomp");
        try (SelectionLock ignored = new SelectionLock(context)) {
            write(decompChoice(context), new JSONObject().put("schema", 2).put("version", version));
        }
    }

    /** An install with both versions (from before the choice): which one starts, until the player chooses. */
    public static synchronized void chooseStartDecomp(Context context, boolean start) throws Exception {
        try (SelectionLock ignored = new SelectionLock(context)) {
            if (!GameVersion.BOTH.equals(chosenVersion(context))) throw new IOException("Only an install with both versions");
            write(decompChoice(context), new JSONObject().put("schema", 1).put("enabled", true).put("start", start));
        }
    }

    /** The randomizer's refusal under the decomp choice (thrown as IOException with the player's message). */
    public static void checkRandomizerVersion(Context context) throws IOException {
        String refusal = GameVersion.randomizerRefusal(chosenVersion(context));
        if (refusal != null) throw new IOException(refusal);
    }

    /** The setup job the game as it is was built in (normal.json's library, jobs/<id>/...), or null. */
    public static synchronized String normalJob(Context context) {
        try (SelectionLock ignored = new SelectionLock(context)) {
            java.util.regex.Matcher id = java.util.regex.Pattern.compile("^jobs/([0-9a-f]{32})/")
                .matcher(read(record(context, "normal")).getString("library"));
            return id.find() ? id.group(1) : null;
        } catch (Exception none) { return null; }
    }

    private static String builtVersion(JSONObject record) {
        return GameVersion.built(record.has("game_version") ? record.optString("game_version") : null,
                                 record.has("decomp_library"), record.has("decomp_error"));
    }

    /** The active build's version ("recomp", "decomp", "both"), or null when there is none. */
    public static synchronized String activeVersion(Context context) {
        try (SelectionLock ignored = new SelectionLock(context)) {
            return builtVersion(read(record(context, "active")));
        } catch (Exception missing) { return null; }
    }

    /** The built decomp version's state for setup's screen (an install with both versions): "built", an error
     *  message, or null (not built). */
    public static synchronized String decompState(Context context) {
        try (SelectionLock ignored = new SelectionLock(context)) {
            JSONObject active = read(record(context, "active"));
            if (active.has("decomp_library")) return "built";
            return active.has("decomp_error") ? active.getString("decomp_error") : null;
        } catch (Exception missing) { return null; }
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
        if (value.has("randomizer_files")) {  // the randomized game's own executable, as built
            File files = randomizerFiles(context, value);
            if (!hash(new File(files, "code/cking.rpx")).equals(value.getString("randomizer_rpx_sha256")))
                throw new IOException("Randomized game executable checksum mismatch");
        }
        return library;
    }

    /** The record's decomp library, checked as the game library is (inside setup storage, read-only, its checksum). */
    private static File decompLibrary(Context context, JSONObject value) throws Exception {
        File library = new File(storage(context), value.getString("decomp_library")).getCanonicalFile();
        if (!library.getPath().startsWith(storage(context).getCanonicalPath() + File.separator) ||
                !library.isFile() || library.canWrite())
            throw new IOException("Invalid decomp library");
        if (!hash(library).equals(value.getString("decomp_sha256"))) throw new IOException("Decomp library checksum mismatch");
        return library;
    }

    private static File randomizerFiles(Context context, JSONObject value) throws Exception {
        File root = storage(context).getCanonicalFile();
        File files = new File(storage(context), value.getString("randomizer_files")).getCanonicalFile();
        if (!files.getPath().startsWith(root.getPath() + File.separator) || !new File(files, "code/cking.rpx").isFile())
            throw new IOException("Randomized game files are outside setup storage or incomplete");
        return files;
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
            File files = value.has("randomizer_files") ? randomizerFiles(context, value) : null;
            if (GameVersion.DECOMP.equals(builtVersion(value)))  // only the decomp version: its library is the game
                return new Selection(library, game, value.optBoolean("code_mods", false), null, true);
            if (files == null && value.has("decomp_library") && startDecompChosen(context)) {  // both versions
                try {
                    return new Selection(decompLibrary(context, value), game, value.optBoolean("code_mods", false), null, true);
                } catch (Exception failure) {
                    Log.w("wwhd-game", "Decomp version unavailable, starting the normal game: " + failure.getMessage());
                }
            }
            return new Selection(library, game, value.optBoolean("code_mods", false), files, false);
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
        String version = chosenVersion(context);
        try (SelectionLock ignored = new SelectionLock(context)) {
            JSONObject active = read(record(context, "active"));
            library(context, active, true);
            // the chosen version against the built one (GameVersion: a failed decomp build is not built)
            return active.optBoolean("code_mods", false) != chosen ||
                GameVersion.needsRebuild(version, builtVersion(active), active.has("decomp_library"),
                                         active.has("randomizer_files"));
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
        activate(context, candidate, game, codeMods, null);
    }

    /** randomizerFiles: a randomized game's changed files (the in-app randomizer), played over game; null for none. */
    public static synchronized void activate(Context context, File candidate, File game, boolean codeMods,
                                             File randomizerFiles) throws Exception {
        activate(context, candidate, game, codeMods, randomizerFiles, null);
    }

    /** decomp: setup's decomp result (ready.json "decomp": {library, mode, ...} or {error}), null when not asked for:
     *  the recompiled version, or (an install with both) both versions. */
    public static synchronized void activate(Context context, File candidate, File game, boolean codeMods,
                                             File randomizerFiles, JSONObject decomp) throws Exception {
        activate(context, candidate, game, codeMods, randomizerFiles, decomp,
                 decomp == null ? GameVersion.RECOMP : GameVersion.BOTH);
    }

    /** version: what setup built (GameVersion). "decomp": candidate is the decomp library (decomp has its library,
     *  the same file); "both": decomp is the second library's result; "recomp": decomp is null. */
    public static synchronized void activate(Context context, File candidate, File game, boolean codeMods,
                                             File randomizerFiles, JSONObject decomp, String version) throws Exception {
        try (SelectionLock ignored = new SelectionLock(context)) {
            activateLocked(context, candidate, game, codeMods, randomizerFiles, decomp, version);
        }
    }

    private static void activateLocked(Context context, File candidate, File game, boolean codeMods,
                                       File randomizerFiles, JSONObject decomp, String version) throws Exception {
        if (!GameVersion.known(version)) throw new IOException("Unknown game version");
        if (GameVersion.DECOMP.equals(version) && (randomizerFiles != null || decomp == null || !decomp.has("library")))
            throw new IOException("The decomp version's build is incomplete");
        JSONObject value = expected(context);
        value.put("game_version", version);
        if (codeMods) value.put("code_mods", true);
        if (randomizerFiles != null) {
            File root = storage(context).getCanonicalFile();
            File files = randomizerFiles.getCanonicalFile();
            value.put("randomizer_files", root.toPath().relativize(files.toPath()).toString())
                .put("randomizer_rpx_sha256", hash(new File(files, "code/cking.rpx")));
        }
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
        if (GameVersion.DECOMP.equals(version)) {  // only the decomp version: the candidate (validated above) is it
            if (!new File(decomp.getString("library")).getCanonicalFile().equals(candidate))
                throw new IOException("The decomp version's library is not the candidate");
            value.put("decomp_library", value.getString("library")).put("decomp_sha256", value.getString("sha256"))
                .put("decomp_mode", decomp.optString("mode", "fast"));
        } else if (decomp != null && decomp.has("library")) {
            File library = new File(decomp.getString("library")).getCanonicalFile();
            if (!library.getPath().startsWith(root.getPath() + File.separator) || !library.isFile() || library.canWrite())
                throw new IOException("Decomp library is outside completed setup storage");
            try {
                validate(library.getAbsolutePath());  // (a separate namespace load: the normal game is not loaded here)
                value.put("decomp_library", root.toPath().relativize(library.toPath()).toString())
                    .put("decomp_sha256", hash(library)).put("decomp_mode", decomp.optString("mode", "fast"));
            } catch (Throwable failure) {
                value.put("decomp_error", "The decomp version does not load: " + failure.getMessage());
            }
        } else if (decomp != null) {
            value.put("decomp_error", decomp.optString("error", "The decomp version could not be built"));
        }
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
        // a switch to the other version (GameVersion.switched): the one before is no rollback (the desktop removes the
        // other version's game, its bin.previous copy too); its stage goes after this activation
        boolean switched = previous != null && GameVersion.switched(builtVersion(previous), version);
        if (switched) {
            File old = record(context, "previous").getBaseFile();
            if (old.exists() && !old.delete()) throw new IOException("Cannot remove the other version's rollback record");
        } else if (previous != null && !previous.toString().equals(value.toString())) write(record(context, "previous"), previous);
        write(record(context, "active"), value);
        // the game as it is stays one choice away from any randomized game: its newest build is kept as "normal"
        // (SetupJobCleanup keeps every job a record in setup storage names)
        if (!value.has("randomizer_files")) write(record(context, "normal"), value);
        else if (previous != null && !previous.has("randomizer_files")) {
            JSONObject normal = null;
            try { normal = read(record(context, "normal")); } catch (java.io.FileNotFoundException none) { /* none yet */ }
            if (normal == null) write(record(context, "normal"), previous);  // (builds made before this record existed)
        }
        if (switched) {
            // (a randomized build right after a switch: "the game as it is" of the other version goes too)
            try {
                if (!builtVersion(read(record(context, "normal"))).equals(version)) {
                    File normal = record(context, "normal").getBaseFile();
                    if (normal.exists() && !normal.delete()) throw new IOException("Cannot remove the other version's record");
                }
            } catch (java.io.FileNotFoundException none) { /* none */ }
            removeStages(context, previous, value);
        }
    }

    /** After a switch: the other version's library stages (checkpoints/link-<hex>, decomp-<hex>) that the new active
     *  record does not use (setup_adapter.cleanup_stages removes the rest of the job's old stages on its next run). */
    private static void removeStages(Context context, JSONObject previous, JSONObject active) {
        java.util.HashSet<File> used = new java.util.HashSet<>();
        for (String key : new String[] {"library", "decomp_library"})
            if (active.has(key)) {
                try { used.add(new File(storage(context), active.optString(key)).getCanonicalFile().getParentFile()); }
                catch (IOException unreadable) { return; }
            }
        for (String key : new String[] {"library", "decomp_library"}) {
            if (!previous.has(key)) continue;
            try {
                File stage = new File(storage(context), previous.getString(key)).getCanonicalFile().getParentFile();
                if (used.contains(stage) ||
                        !stage.getPath().startsWith(storage(context).getCanonicalPath() + File.separator) ||
                        !stage.getName().matches("(link|decomp)-[0-9a-f]{64}") ||
                        !stage.getParentFile().getName().equals("checkpoints"))
                    continue;
                SetupJobCleanup.remove(stage.toPath());
                Log.i("wwhd-game", "removed the other version's stage " + stage.getName());
            } catch (Exception failure) {
                Log.w("wwhd-game", "the other version's stage kept: " + failure.getMessage());
            }
        }
    }

    /** The game as it is (not randomized) from the next start: its newest build. False when there is none. */
    public static synchronized boolean playNormal(Context context) throws Exception {
        try (SelectionLock ignored = new SelectionLock(context)) {
            JSONObject normal;
            try { normal = read(record(context, "normal")); } catch (java.io.FileNotFoundException none) { return false; }
            if (normal.has("randomizer_files")) throw new IOException("The normal game's record names a randomized game");
            library(context, normal, false);  // only a runtime compatible with the Java host
            JSONObject active = null;
            try { active = read(record(context, "active")); } catch (java.io.FileNotFoundException none) { /* first */ }
            if (active != null && !active.toString().equals(normal.toString())) write(record(context, "previous"), active);
            write(record(context, "active"), normal);
            return true;
        }
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
