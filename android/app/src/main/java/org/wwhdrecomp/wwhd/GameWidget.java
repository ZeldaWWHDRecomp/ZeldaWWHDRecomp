package org.wwhdrecomp.wwhd;

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.widget.RemoteViews;
import java.io.File;

/**
 * The home-screen game "icon": a 1x1 widget that shows the game's own icon from the player's
 * files (meta/iconTex.tga) and starts the game. A launcher shortcut would do the same, but
 * launchers badge shortcuts with the app's packaged icon; a widget is drawn as the app chose.
 */
public class GameWidget extends AppWidgetProvider {
    @Override public void onUpdate(Context context, AppWidgetManager manager, int[] ids) {
        PendingResult pending = goAsync();
        new Thread(() -> {
            try {
                RemoteViews views = views(context);
                for (int id : ids) manager.updateAppWidget(id, views);
            } finally { pending.finish(); }
        }, "wwhd-game-widget").start();
    }

    /** Redraws the widgets on the home screen, e.g. after a setup produced a new game folder. */
    static void refresh(Context context) {
        AppWidgetManager manager = AppWidgetManager.getInstance(context);
        int[] ids = manager.getAppWidgetIds(new android.content.ComponentName(context, GameWidget.class));
        if (ids.length > 0) {
            RemoteViews views = views(context);
            for (int id : ids) manager.updateAppWidget(id, views);
        }
    }

    private static RemoteViews views(Context context) {
        RemoteViews views = new RemoteViews(context.getPackageName(), R.layout.game_widget);
        File game = activeGame(context);
        Bitmap art = game == null ? null : GameShortcut.icon(new File(game, "meta/iconTex.tga"));
        if (art != null) views.setImageViewBitmap(R.id.game_widget_icon, GameShortcut.rounded(art));
        Intent play = new Intent(Intent.ACTION_MAIN).setClass(context, SetupActivity.class)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        views.setOnClickPendingIntent(R.id.game_widget_root, PendingIntent.getActivity(context, 0, play,
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT));
        return views;
    }

    /** The active phone build's game folder, from active.json (no checksum: only the icon is read),
     *  or the PC route's game folder. */
    static File activeGame(Context context) {
        try {
            File active = new File(AndroidGame.storage(context), "active.json");
            if (!active.isFile()) {
                File external = context.getExternalFilesDir(null);
                File game = external == null ? null : new File(external, "game");
                return game != null && new File(game, "meta/iconTex.tga").isFile() ? game : null;
            }
            org.json.JSONObject value = SetupStore.read(active);
            return value.has("game") ? new File(AndroidGame.storage(context), value.getString("game")) : null;
        } catch (Exception unreadable) { return null; }
    }
}
