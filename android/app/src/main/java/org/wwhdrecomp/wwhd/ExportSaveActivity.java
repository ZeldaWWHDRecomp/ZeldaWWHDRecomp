package org.wwhdrecomp.wwhd;

import android.app.Activity;
import android.content.ContentValues;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.MediaStore;
import android.widget.Toast;

import java.io.OutputStream;

// Launcher shortcut "Export save": the save folder as a zip in Downloads (WWHD-save-<time>.zip), then
// the share sheet (Drive, messages, e-mail...). The zip has the layout of the PC version's save/.
public class ExportSaveActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        new Thread(() -> {
            Uri uri = null;
            String error = null;
            try (SessionLease lease = SessionLease.acquire(WindwakeSession.root(this))) {
                WindwakeSession.store(WindwakeSession.root(this)).recover();
                if (!SaveFiles.hasSave(this)) throw new java.io.IOException(getString(R.string.save_none));
                ContentValues v = new ContentValues();
                v.put(MediaStore.Downloads.DISPLAY_NAME, "WWHD-save-" + SaveFiles.stamp() + ".zip");
                v.put(MediaStore.Downloads.MIME_TYPE, "application/zip");
                v.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);
                v.put(MediaStore.Downloads.IS_PENDING, 1);
                uri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
                if (uri == null) throw new java.io.IOException("Downloads is not available");
                try (OutputStream out = getContentResolver().openOutputStream(uri)) {
                    SaveFiles.zip(SaveFiles.saveDir(this), out);
                }
                v.clear();
                v.put(MediaStore.Downloads.IS_PENDING, 0);
                getContentResolver().update(uri, v, null, null);
            } catch (Exception e) {
                error = e.getMessage();
                if (uri != null) getContentResolver().delete(uri, null, null);
                uri = null;
            }
            final Uri done = uri;
            final String failure = error;
            runOnUiThread(() -> {
                if (done == null) {
                    Toast.makeText(this, getString(R.string.save_export_failed, failure), Toast.LENGTH_LONG).show();
                } else {
                    Toast.makeText(this, R.string.save_exported, Toast.LENGTH_LONG).show();
                    Intent send = new Intent(Intent.ACTION_SEND);
                    send.setType("application/zip");
                    send.putExtra(Intent.EXTRA_STREAM, done);
                    send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    startActivity(Intent.createChooser(send, getString(R.string.save_share)));
                }
                finish();
            });
        }).start();
    }
}
