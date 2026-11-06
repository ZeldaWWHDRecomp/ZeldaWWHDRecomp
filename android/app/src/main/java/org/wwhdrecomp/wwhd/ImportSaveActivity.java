package org.wwhdrecomp.wwhd;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Toast;

import java.io.File;
import java.io.InputStream;

// Launcher shortcut "Import save": pick a zip (from Export save, or a zip of the PC version's save/
// folder); it must contain user/cking.sav. Transfer requires the game to be closed. The complete
// staged tree replaces the save after validation; the previous tree is kept as save-backup-<time>-<id>.
public class ImportSaveActivity extends Activity {
    private static final int PICK = 1;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        pick.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"application/zip", "application/x-zip-compressed"});
        startActivityForResult(pick, PICK);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK || result != RESULT_OK || data == null || data.getData() == null) {
            finish();
            return;
        }
        final Uri uri = data.getData();
        new Thread(() -> {
            String message;
            try (SessionLease lease = SessionLease.acquire(SaveSession.root(this));
                 InputStream in = getContentResolver().openInputStream(uri)) {
                File backup = SaveSession.store(SaveSession.root(this)).importZip(in);
                message = backup == null ? getString(R.string.save_imported_new)
                        : getString(R.string.save_imported, backup.getName());
            } catch (Exception e) {
                message = getString(R.string.save_import_failed, e.getMessage());
            }
            final String text = message;
            runOnUiThread(() -> {
                Toast.makeText(this, text, Toast.LENGTH_LONG).show();
                finish();
            });
        }).start();
    }
}
