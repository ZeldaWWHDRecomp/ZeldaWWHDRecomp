package org.wwhdrecomp.wwhd;

import android.content.Context;
import android.content.Intent;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.Surface;

/** Immersive presentation is a separate activity; phones retain the SDL activity. */
public class QuestActivity extends WwhdActivity {
    public static Intent gameIntent(Context context) {
        return new Intent(context, BuildConfig.QUEST_THEATER && isQuest(context) ? QuestActivity.class : WwhdActivity.class);
    }
    public static boolean isQuest(Context context) {
        String model = android.os.Build.MODEL;
        return model != null && (model.startsWith("Quest") || model.startsWith("Meta Quest"));
    }

    public boolean useQuestTheater() { return true; }

    // Invoked on the compositor thread, only while the OpenXR session is visible.
    // Repaint on hover/state changes, never on every game draw.
    public static void paintTheaterControls(Surface surface, int hovered, int state) {
        Canvas canvas = surface.lockCanvas(null);
        try {
            canvas.drawColor(Color.TRANSPARENT, PorterDuff.Mode.CLEAR);
            Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
            paint.setColor(0xFF18202C);
            canvas.drawRoundRect(new RectF(0, 0, canvas.getWidth(), canvas.getHeight()), 22, 22, paint);
            paint.setTypeface(Typeface.create("sans-serif", Typeface.BOLD));
            paint.setTextAlign(Paint.Align.CENTER);
            if ((state & 16) == 0) {
                paint.setColor(hovered == 5 ? 0xFF397FBA : 0xFF263446);
                canvas.drawRoundRect(new RectF(8, 8, canvas.getWidth() - 8, canvas.getHeight() - 8), 30, 30, paint);
                paint.setColor(Color.WHITE);
                paint.setTextSize(110);
                canvas.drawText("Adjust", canvas.getWidth() / 2f, 166, paint);
                return;
            }
            String[] labels = {(state & 1) != 0 ? "3D" : "2D",
                    (state & 2) != 0 ? "Curved" : "Flat", "Passthrough", "Recenter", "Done"};
            float cell = canvas.getWidth() / 5f;
            for (int i = 0; i < labels.length; ++i) {
                boolean selected = (i == 0 && (state & 1) != 0) || (i == 1 && (state & 2) != 0) || (i == 2 && (state & 64) != 0);
                paint.setColor(i == hovered ? 0xFF397FBA : selected ? 0xFF235773 : 0xFF2B3647);
                if (i == 1 && (state & 8) == 0) paint.setColor(0xFF252B32);
                if (i == 2 && (state & 32) == 0) paint.setColor(0xFF252B32);
                canvas.drawRoundRect(new RectF(i * cell + 8, 20, (i + 1) * cell - 8, 154), 14, 14, paint);
                paint.setColor(Color.WHITE);
                paint.setTypeface(Typeface.create("sans-serif", Typeface.BOLD));
                paint.setTextAlign(Paint.Align.CENTER);
                paint.setTextSize(i == 2 ? 28 : 34);
                canvas.drawText(labels[i], (i + .5f) * cell, 101, paint);
            }
            paint.setTypeface(Typeface.create("sans-serif", Typeface.NORMAL));
            paint.setTextSize(28);
            paint.setColor(0xFFCED8E4);
            canvas.drawText("Hold grip to move  •  While held, use the stick:", canvas.getWidth() / 2f, 199, paint);
            canvas.drawText("Left / right: size    Up / down: distance", canvas.getWidth() / 2f, 236, paint);
        } finally { surface.unlockCanvasAndPost(canvas); }
    }
}
