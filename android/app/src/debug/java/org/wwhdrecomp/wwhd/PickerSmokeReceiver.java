package org.wwhdrecomp.wwhd;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.UriPermission;
import android.net.Uri;
import android.provider.DocumentsContract;
import java.io.File;
import java.io.InputStream;
import java.util.HashSet;
import org.json.JSONObject;

/** Observe actual DocumentsUI grants; never manufactures picker results. */
public final class PickerSmokeReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        PendingResult pending = goAsync();
        new Thread(() -> {
            JSONObject result = new JSONObject();
            try {
                String fixture = intent.getStringExtra("fixture");
                if (fixture == null || !fixture.matches("wwhd-picker-fixture-[0-9a-f]{32}"))
                    throw new Exception("Invalid authored fixture identity");
                String mode = intent.getStringExtra("mode");
                HashSet<String> granted = new HashSet<>();
                int released = 0;
                for (UriPermission permission : context.getContentResolver().getPersistedUriPermissions()) {
                    Uri uri = permission.getUri();
                    if (!"com.android.externalstorage.documents".equals(uri.getAuthority()) || !uri.toString().contains(fixture)) continue;
                    if ("cleanup".equals(mode)) {
                        context.getContentResolver().releasePersistableUriPermission(uri, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);
                        released++;
                    } else if (permission.isReadPermission()) granted.add(uri.toString());
                }
                if ("verify".equals(mode)) {
                    File job = SetupStore.current(context);
                    JSONObject source = SetupStore.read(new File(job, "source.json"));
                    if (!new File(job, "manual-pause.json").isFile()) throw new Exception("Selection unexpectedly started work");
                    for (String name : new String[] {"uri", "disc_uri", "common_uri"}) {
                        if (!source.has(name)) continue;
                        String value = source.getString(name);
                        if (!value.contains(fixture) || !granted.contains(value)) throw new Exception("Missing persisted read grant for " + name);
                        Uri uri = Uri.parse(value);
                        if (name.equals("uri") && source.getString("kind").equals("folder"))
                            uri = DocumentsContract.buildDocumentUriUsingTree(uri, DocumentsContract.getTreeDocumentId(uri) + "/code/cking.rpx");
                        try (InputStream input = context.getContentResolver().openInputStream(uri)) {
                            if (input == null || input.read() < 0) throw new Exception("Persisted document is unreadable");
                        }
                    }
                    result.put("job", job.getName()).put("kind", source.getString("kind")).put("persisted_grants", granted.size());
                } else if ("import_failed_image".equals(mode) || "apply_key_replacement".equals(mode)) {
                    File job = SetupStore.current(context);
                    JSONObject source = SetupStore.read(new File(job, "source.json"));
                    if (!source.getString("kind").equals("image") || !source.getString("uri").contains(fixture) ||
                        !new File(job, "manual-pause.json").isFile()) throw new Exception("Not a paused authored image fixture");
                    SetupImport.prepare(context, job, new SetupImport.Control() {
                        @Override public void checkpoint() { }
                        @Override public void progress(long copied, long total, int complete, int files) { }
                    });
                    if (mode.equals("import_failed_image"))
                        SetupStore.write(new File(job, "host.json"), new JSONObject().put("schema", 1)
                            .put("state", "failed").put("error", "Synthetic invalid disc key file"));
                    result.put("job", job.getName()).put("private_dump_retained", new File(job, "input/dump").isFile());
                } else if ("revoke_dump".equals(mode) || "revoke_common".equals(mode)) {
                    File job = SetupStore.current(context);
                    JSONObject source = SetupStore.read(new File(job, "source.json"));
                    String field = mode.equals("revoke_dump") ? "uri" : "common_uri";
                    Uri uri = Uri.parse(source.getString(field));
                    if (!"com.android.externalstorage.documents".equals(uri.getAuthority()) || !uri.toString().contains(fixture))
                        throw new Exception("Refusing to revoke an unrelated input");
                    context.getContentResolver().releasePersistableUriPermission(uri, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    // Preservation sentinels are authored test files, not import completion markers.
                    File sentinel = new File(job, "input/picker-preservation.bin");
                    sentinel.getParentFile().mkdirs();
                    try (java.io.FileOutputStream stream = new java.io.FileOutputStream(sentinel)) {
                        stream.write("authored private-copy preservation fixture".getBytes(java.nio.charset.StandardCharsets.UTF_8));
                        stream.getFD().sync();
                    }
                    SetupStore.write(new File(job, "import-checkpoints/picker-preservation.json"), new JSONObject().put("fixture", fixture));
                    result.put("job", job.getName()).put("revoked", field);
                } else if (!"cleanup".equals(mode)) throw new Exception("Unknown picker probe mode");
                result.put("passed", true).put("released", released);
            } catch (Exception failure) {
                try { result.put("passed", false).put("error", failure.toString()); } catch (Exception ignored) { }
            }
            try { SetupStore.write(new File(context.getFilesDir(), "picker-smoke.json"), result); }
            catch (Exception ignored) { }
            pending.finish();
        }, "picker-grant-observer").start();
    }
}
