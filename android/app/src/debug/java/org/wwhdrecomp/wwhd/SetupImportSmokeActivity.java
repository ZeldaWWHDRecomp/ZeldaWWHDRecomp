package org.wwhdrecomp.wwhd;

import android.app.Activity;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.widget.TextView;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.UUID;
import org.json.JSONObject;

/** Uses the real importer with pipe-backed synthetic SAF documents. */
public final class SetupImportSmokeActivity extends Activity {
    private static final class Pause extends Exception { }
    private boolean pause;
    private final SetupImport.Control control = new SetupImport.Control() {
        @Override public void checkpoint() throws Exception { if (pause) throw new Pause(); }
        @Override public void progress(long copied, long total, int complete, int files) { }
    };
    private static void require(boolean value, String message) throws IOException { if (!value) throw new IOException(message); }
    private File root;
    private File create(String name, String kind, Uri uri) throws Exception {
        File job = new File(root, name);
        SetupStore.write(new File(job, "source.json"), new JSONObject().put("schema", 1).put("kind", kind).put("uri", uri.toString()));
        return job;
    }
    private Uri tree(String id) { return DocumentsContract.buildTreeDocumentUri(ImportDocuments.authority(this), id); }
    private Uri document(String id) { return DocumentsContract.buildDocumentUri(ImportDocuments.authority(this), id); }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        TextView view = new TextView(this);
        view.setText("Testing SAF import");
        setContentView(view);
        new Thread(() -> {
            JSONObject result = new JSONObject();
            try {
                root = new File(getNoBackupFilesDir(), "import-fixture-" + UUID.randomUUID());
                ImportDocuments.OPENS.clear();
                File job = create("folder", "folder", tree("root"));
                try {
                    SetupImport.prepare(this, job, new SetupImport.Control() {
                        @Override public void checkpoint() throws Exception { control.checkpoint(); }
                        @Override public void progress(long copied, long total, int complete, int files) { if (complete == 1) pause = true; }
                    });
                    throw new IOException("Import ignored pause");
                } catch (Pause expected) { }
                require(!new File(job, "job.json").exists(), "Partial import was published");
                pause = false;
                SetupImport.prepare(this, job, control);
                require(ImportDocuments.OPENS.get("root/code/cking.rpx").get() == 1, "Resume recopied a verified file");
                require(new File(job, "input/game/content/asset").length() == 127, "Unknown-size pipe copy failed");
                require(new File(job, "input/game/content/asset.importing").length() == 127, "Import temporary collided with a real filename");
                Files.write(new File(job, "input/game/content/asset").toPath(), new byte[127]);
                SetupStore.remove(new File(job, "job.json"));
                SetupImport.prepare(this, job, control);
                require(Files.readAllBytes(new File(job, "input/game/content/asset").toPath())[0] == 42, "Corrupt checkpoint output was reused");
                // A completed private snapshot does not require the original SAF selection.
                SetupStore.remove(new File(job, "source.json"));
                SetupImport.prepare(this, job, control);
                require(new File(job, "input/game/content/asset").length() == 127,
                    "Source-less snapshot could not resume");
                File shortJob = create("short", "archive", document("short"));
                try { SetupImport.prepare(this, shortJob, control); throw new IOException("Accepted short provider output"); }
                catch (IOException expected) { require(expected.getMessage().contains("changed size"), "Wrong short-read error"); }
                require(!new File(shortJob, "job.json").exists() && !new File(shortJob, "import-partial/0").exists(), "Failed copy leaked output");
                File unsafe = create("unsafe", "folder", tree("unsafe"));
                try { SetupImport.prepare(this, unsafe, control); throw new IOException("Accepted unsafe name"); }
                catch (IOException expected) { require(expected.getMessage().contains("unsafe"), "Wrong traversal error"); }
                // a parent folder chosen by mistake imports the game folder inside it
                File parent = create("parent", "folder", tree("parent"));
                SetupImport.prepare(this, parent, control);
                require(new File(parent, "input/game/code/cking.rpx").length() == 2 * 1024 * 1024 &&
                    new File(parent, "input/game/meta/meta.xml").isFile(), "Parent folder did not import its game folder");
                // a folder inside the game is refused before anything is copied
                File inside = create("inside", "folder", tree("root/content"));
                try { SetupImport.prepare(this, inside, control); throw new IOException("Imported a folder inside the game"); }
                catch (IOException expected) { require(expected.getMessage().contains("one level up"), "Wrong inside-game error: " + expected.getMessage()); }
                require(!new File(inside, "import-inventory.json").exists() && !new File(inside, "input").exists(), "A wrong folder was copied");
                File image = create("image", "image", document("dump"));
                JSONObject source = SetupStore.read(new File(image, "source.json"));
                source.put("disc_uri", document("key").toString()).put("common_uri", document("key").toString());
                SetupStore.write(new File(image, "source.json"), source);
                SetupImport.prepare(this, image, control);
                require(!new File(image, "input/disc.key").exists() && !new File(image, "input/common.key").exists(), "Key bytes were copied");
                require(!SetupStore.read(new File(image, "job.json")).getJSONObject("input").has("disc_key"), "Private key destination retained");
                require(new File(image, "import-checkpoints").list().length == 1, "Key digest leaked into checkpoints");
                byte[] configuration = Files.readAllBytes(new File(image, "job.json").toPath());
                byte[] receipt = Files.readAllBytes(new File(image, "import-checkpoints/0.json").toPath());
                int dumpOpens = ImportDocuments.OPENS.get("dump").get();
                SetupStore.write(new File(image, "host.json"), new JSONObject().put("state", "running"));
                try { SetupImport.selectKey(image, "disc", document("replacementkey")); throw new IOException("Allowed active key replacement"); }
                catch (IOException expected) { require(expected.getMessage().contains("Pause"), "Wrong active replacement error"); }
                SetupStore.write(new File(image, "host.json"), new JSONObject().put("state", "failed"));
                // Interrupted old releases retain key bytes until the job completes.
                Files.write(new File(image, "input/disc.key").toPath(), new byte[16]);
                SetupImport.prepare(this, image, control);
                require(new File(image, "input/disc.key").length() == 16, "Interrupted legacy key copy was removed");
                SetupImport.selectKey(image, "disc", document("replacementkey"));
                final int[] keyCheckpoints = {0};
                try {
                    SetupImport.prepare(this, image, new SetupImport.Control() {
                        @Override public void checkpoint() throws Exception { if (++keyCheckpoints[0] == 1) throw new Pause(); }
                        @Override public void progress(long copied, long total, int complete, int files) { }
                    });
                    throw new IOException("Ignored pause during replacement key selection");
                } catch (Pause expected) { }
                require(new File(image, "input/disc.key").length() == 16 &&
                    SetupImport.keyReplacement(image, "disc").isFile(), "Paused replacement lost the selection or saved key bytes");
                SetupImport.prepare(this, image, control);
                JSONObject changed = SetupStore.read(new File(image, "source.json"));
                require(changed.getString("disc_uri").equals(document("replacementkey").toString()) &&
                    changed.getString("common_uri").equals(document("key").toString()) &&
                    !SetupImport.keyReplacement(image, "disc").exists(), "Replacement did not update only the selected URI");
                // Replaying a pending record models termination after URI update, before marker removal.
                SetupImport.selectKey(image, "disc", document("replacementkey"));
                SetupImport.prepare(this, image, control);
                require(ImportDocuments.OPENS.get("dump").get() == dumpOpens &&
                    java.util.Arrays.equals(configuration, Files.readAllBytes(new File(image, "job.json").toPath())) &&
                    java.util.Arrays.equals(receipt, Files.readAllBytes(new File(image, "import-checkpoints/0.json").toPath())),
                    "Key retry changed or recopied the private dump");
                SetupKeyAccess.cleanupCompleted(image);
                require(new File(image, "input/disc.key").isFile(), "Failed job lost legacy key");
                SetupStore.write(new File(image, "host.json"), new JSONObject().put("state", "complete"));
                SetupKeyAccess.cleanupCompleted(image);
                require(!new File(image, "input/disc.key").exists(), "Completed job retained legacy key copy");
                File partial = create("partial", "archive", document("root/code/cking.rpx"));
                final int[] checkpoints = {0};
                try {
                    SetupImport.prepare(this, partial, new SetupImport.Control() {
                        @Override public void checkpoint() throws Exception { if (++checkpoints[0] == 20) throw new Pause(); }
                        @Override public void progress(long copied, long total, int complete, int files) { }
                    });
                    throw new IOException("Ignored pause during stream writes");
                } catch (Pause expected) { }
                require(!new File(partial, "input/dump").exists() && !new File(partial, "import-partial/0").exists(), "Partial stream was published or retained");
                SetupImport.prepare(this, partial, control);
                require(new File(partial, "input/dump").length() == 2 * 1024 * 1024, "Interrupted stream did not restart");
                File oversized = create("oversized", "image", document("dump"));
                JSONObject oversizedSource = SetupStore.read(new File(oversized, "source.json"));
                oversizedSource.put("disc_uri", document("bigkey").toString()).put("common_uri", document("key").toString());
                SetupStore.write(new File(oversized, "source.json"), oversizedSource);
                SetupImport.prepare(this, oversized, control);
                require(!new File(oversized, "input/disc.key").exists(), "Key file copied during import");
                // Size and format are checked by the Python worker, after the read-only SAF open.
                result.put("passed", true).put("checks", "pipe streams, unknown size, pause/resume during writes, digest corruption, filename collisions, short output, unsafe names, no private key copies, legacy cleanup, failed-job URI correction, replacement pause/replay without recopying dump");
            } catch (Exception failure) {
                try { result.put("passed", false).put("error", failure.toString()); } catch (Exception ignored) { }
            } finally {
                if (root != null) delete(root);
            }
            try { SetupStore.write(new File(getFilesDir(), "setup-import-smoke.json"), result); }
            catch (Exception ignored) { }
            runOnUiThread(() -> view.setText(result.toString()));
        }, "import-smoke").start();
    }
    private static void delete(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) delete(child);
        file.delete();
    }
}
