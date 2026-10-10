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
 * The home-screen game icon. Its picture is the game's own icon from the player's files
 * (meta/iconTex.tga), made on the phone after setup: the APK carries no game artwork, and an app
 * can't change its own launcher icon. The launcher is asked once to place GameWidget (drawn as is);
 * where widgets can't be pinned, a pinned shortcut instead (which launchers badge with the app's
 * packaged icon).
 */
public final class GameShortcut {
    private GameShortcut() {}

    static final String ID = "game";

    /** Asks the launcher to pin the shortcut, unless it is pinned already or pinning is unsupported. */
    public static void offer(Context context, File game) {
        try {
            android.appwidget.AppWidgetManager widgets = android.appwidget.AppWidgetManager.getInstance(context);
            android.content.ComponentName provider = new android.content.ComponentName(context, GameWidget.class);
            if (widgets.getAppWidgetIds(provider).length > 0) { GameWidget.refresh(context); return; }
            if (widgets.isRequestPinAppWidgetSupported()) { widgets.requestPinAppWidget(provider, null, null); return; }
        } catch (Exception failure) {
            Log.w("wwhd-setup", "Game widget not offered: " + failure.getMessage());
        }
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
            if (art != null) info.setIcon(Icon.createWithAdaptiveBitmap(adaptive(art)));
            shortcuts.requestPinShortcut(info.build(), null);
        } catch (Exception failure) {
            Log.w("wwhd-setup", "Game shortcut not offered: " + failure.getMessage());
        }
    }

    /**
     * An adaptive icon is a 108 dp layer of which launchers show about the middle 72 dp (their
     * mask), so the whole picture goes into that middle part, with rounded corners, over a dimmed
     * and enlarged copy of itself that fills the edges.
     */
    static Bitmap adaptive(Bitmap art) {
        final int size = 432, inner = 264, offset = (size - inner) / 2;
        Bitmap out = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        android.graphics.Canvas canvas = new android.graphics.Canvas(out);
        android.graphics.Paint paint = new android.graphics.Paint(android.graphics.Paint.FILTER_BITMAP_FLAG);
        canvas.drawBitmap(art, null, new android.graphics.Rect(-size / 4, -size / 4, size + size / 4, size + size / 4), paint);
        canvas.drawColor(0x99000000);
        android.graphics.Path round = new android.graphics.Path();
        round.addRoundRect(offset, offset, offset + inner, offset + inner, inner / 6f, inner / 6f,
            android.graphics.Path.Direction.CW);
        canvas.clipPath(round);
        canvas.drawBitmap(art, null, new android.graphics.Rect(offset, offset, offset + inner, offset + inner), paint);
        return out;
    }

    /** The picture as a launcher draws an app icon: rounded corners, for the widget. */
    static Bitmap rounded(Bitmap art) {
        final int size = 192;
        Bitmap out = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        android.graphics.Canvas canvas = new android.graphics.Canvas(out);
        android.graphics.Paint paint = new android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG | android.graphics.Paint.FILTER_BITMAP_FLAG);
        android.graphics.Path round = new android.graphics.Path();
        round.addRoundRect(0, 0, size, size, size * 0.3f, size * 0.3f, android.graphics.Path.Direction.CW);
        canvas.clipPath(round);
        canvas.drawBitmap(art, null, new android.graphics.Rect(0, 0, size, size), paint);
        return out;
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
