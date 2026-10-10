package org.wwhdrecomp.wwhd;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ShortcutInfo;
import android.content.pm.ShortcutManager;
import android.graphics.Bitmap;
import android.graphics.drawable.Icon;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * The home-screen shortcut that starts the game. Its icon is the game's own icon from the
 * player's files (meta/iconTex.tga), made on the phone after setup: the APK carries no game
 * artwork, and an app can't change its own launcher icon, so the picture comes as a pinned
 * shortcut (the launcher asks the player once).
 */
public final class GameShortcut {
    private GameShortcut() {}

    static final String ID = "game";

    /** Asks the launcher to pin the shortcut, unless it is pinned already or pinning is unsupported. */
    public static void offer(Context context, File game) {
        try {
            ShortcutManager shortcuts = context.getSystemService(ShortcutManager.class);
            if (shortcuts == null || !shortcuts.isRequestPinShortcutSupported()) return;
            for (ShortcutInfo pinned : shortcuts.getPinnedShortcuts())
                if (pinned.getId().equals(ID)) return;
            Bitmap art = icon(new File(game, "meta/iconTex.tga"));
            Intent play = new Intent(Intent.ACTION_MAIN).setClass(context, WwhdActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            ShortcutInfo.Builder info = new ShortcutInfo.Builder(context, ID)
                .setShortLabel(context.getString(R.string.app_name))
                .setIntent(play);
            // adaptive icon: the picture is the 108 dp layer, as android/make_icon.py does for the PC route
            if (art != null) info.setIcon(Icon.createWithAdaptiveBitmap(Bitmap.createScaledBitmap(art, 432, 432, true)));
            shortcuts.requestPinShortcut(info.build(), null);
        } catch (Exception failure) {
            Log.w("wwhd-setup", "Game shortcut not offered: " + failure.getMessage());
        }
    }

    /** The game's 128x128 icon (TgaImage), or null when it can't be read. */
    static Bitmap icon(File tga) {
        try (InputStream input = new FileInputStream(tga)) {
            TgaImage image = TgaImage.decode(input.readAllBytes());
            return Bitmap.createBitmap(image.argb, image.width, image.height, Bitmap.Config.ARGB_8888);
        } catch (IOException | RuntimeException failure) {
            Log.w("wwhd-setup", "Game icon unreadable: " + failure.getMessage());
            return null;
        }
    }
}
