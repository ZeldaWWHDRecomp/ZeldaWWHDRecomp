package org.wwhdrecomp.wwhd;

/** Ordinary Android surface, independent of the failed game's SDL/OpenXR process. */
public final class TheaterErrorActivity extends android.app.Activity {
    @Override protected void onCreate(android.os.Bundle state) {
        super.onCreate(state);
        new android.app.AlertDialog.Builder(this)
            .setTitle("VR display stopped")
            .setMessage("The VR display could not continue. Close and reopen the game to restore it.")
            .setCancelable(false)
            .setPositiveButton("Close", (dialog, which) -> finish())
            .show();
    }
}
