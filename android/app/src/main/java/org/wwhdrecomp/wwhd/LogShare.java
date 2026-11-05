package org.wwhdrecomp.wwhd;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import android.widget.Toast;
import androidx.core.content.FileProvider;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.util.ArrayList;

/** Share the two diagnostic logs, already filtered by the native crash redactor; with `saves` (the player ticked
 * "Include my save files", off by default) also a zip of the save files: user/cking.sav with every name made "Link"
 * and its checks made again (SaveScrub; left out when it is not a valid cking.sav) and the Picto Box photos and
 * orders (user/cking_pic*.sav). Nothing else of the save folder, and never the game, the save folder itself or
 * the captures directory. Idea from GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (MPL-2.0), CrashLogs.shareGameLog. */
final class LogShare {
    private static final long LIMIT = (64L << 20) + 65536;
    static void share(Activity activity) { share(activity, false); }
    static void share(Activity activity, boolean saves) {
        // Copy off the UI thread; a running game can keep appending after this snapshot.
        new Thread(() -> {
            try {
                Intent share = prepare(activity, saves);
                activity.runOnUiThread(() -> {
                    if (share == null) {
                        Toast.makeText(activity, "No game log yet. Start the game once, then share its log.", Toast.LENGTH_LONG).show();
                        return;
                    }
                    try { activity.startActivity(Intent.createChooser(share, "Share log")); }
                    catch (Exception error) { Toast.makeText(activity, "Cannot open the share sheet", Toast.LENGTH_LONG).show(); }
                });
            } catch (Exception error) {
                activity.runOnUiThread(() -> Toast.makeText(activity, "Cannot share game logs", Toast.LENGTH_LONG).show());
            }
        }, "share-game-log").start();
    }
    // Called on a worker thread. The whitelist is deliberately independent of directory contents.
    static Intent prepare(android.content.Context activity) throws java.io.IOException { return prepare(activity, false); }
    static synchronized Intent prepare(android.content.Context activity, boolean saves) throws java.io.IOException {
        File root = activity.getExternalFilesDir(null);
        if (root == null) throw new java.io.IOException("Game storage is unavailable");
        File folder = new File(activity.getCacheDir(), "crash-share");
        if (!folder.isDirectory() && !folder.mkdirs()) throw new java.io.IOException("Cannot create share folder");
        ArrayList<Uri> logs = new ArrayList<>();
        for (String name : new String[] {"wwhd.log", "wwhd-previous.log"}) {
            File source = new File(new File(root, "captures"), name);
            if (!source.isFile() || source.length() == 0) continue;
            File copy = new File(folder, name);
            try (FileInputStream in = new FileInputStream(source);
                 FileOutputStream out = new FileOutputStream(copy)) {
                long remaining = Math.min(source.length(), LIMIT);
                byte[] bytes = new byte[65536];
                while (remaining > 0) {
                    int count = in.read(bytes, 0, (int)Math.min(bytes.length, remaining));
                    if (count < 0) break;
                    out.write(bytes, 0, count);
                    remaining -= count;
                }
            }
            logs.add(FileProvider.getUriForFile(activity, activity.getPackageName() + ".crashlogs", copy));
        }
        if (logs.isEmpty()) return null;
        File savesZip = new File(folder, "save-files.zip");
        if (savesZip.exists() && !savesZip.delete()) throw new java.io.IOException("Cannot replace the shared save files");
        if (saves && zipSaves(SaveFiles.saveDir(activity), savesZip))
            logs.add(FileProvider.getUriForFile(activity, activity.getPackageName() + ".crashlogs", savesZip));
        Intent share = new Intent(Intent.ACTION_SEND_MULTIPLE).setType("text/plain")
            .putParcelableArrayListExtra(Intent.EXTRA_STREAM, logs)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        ClipData clip = ClipData.newRawUri("Game log", logs.get(0));
        for (int i = 1; i < logs.size(); ++i) clip.addItem(new ClipData.Item(logs.get(i)));
        share.setClipData(clip);
        return share;
    }

    // the save files for a bug report (see the class comment): true when the zip has cking.sav
    static boolean zipSaves(File saveDir, File zip) throws java.io.IOException {
        File user = new File(saveDir, "user");
        byte[] sav;
        try {
            sav = java.nio.file.Files.readAllBytes(new File(user, "cking.sav").toPath());
        } catch (java.io.IOException missing) {
            return false;
        }
        byte[] scrubbed = SaveScrub.withoutNames(sav);
        if (scrubbed == null) return false;
        try (java.util.zip.ZipOutputStream out = new java.util.zip.ZipOutputStream(new FileOutputStream(zip))) {
            out.putNextEntry(new java.util.zip.ZipEntry("user/cking.sav"));
            out.write(scrubbed);
            out.closeEntry();
            File[] pics = user.listFiles((d, n) -> n.startsWith("cking_pic") && n.endsWith(".sav"));
            if (pics != null) {
                java.util.Arrays.sort(pics);
                for (File f : pics) {
                    if (!f.isFile() || f.length() > (4L << 20)) continue;
                    out.putNextEntry(new java.util.zip.ZipEntry("user/" + f.getName()));
                    out.write(java.nio.file.Files.readAllBytes(f.toPath()));
                    out.closeEntry();
                }
            }
        }
        return true;
    }
}
