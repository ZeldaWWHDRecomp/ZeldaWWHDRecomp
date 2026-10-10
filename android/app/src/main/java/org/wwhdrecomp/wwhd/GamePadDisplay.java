package org.wwhdrecomp.wwhd;

import android.app.Activity;
import android.app.Presentation;
import android.graphics.Rect;
import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import android.widget.RelativeLayout;
import androidx.core.util.Consumer;
import androidx.window.java.layout.WindowInfoTrackerCallbackAdapter;
import androidx.window.layout.WindowInfoTracker;
import androidx.window.layout.WindowLayoutInfo;
import androidx.window.layout.DisplayFeature;
import androidx.window.layout.FoldingFeature;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;

/** Owns the Android secondary surface. All callbacks run on the UI thread.
 * Native callbacks only publish retained windows; Vulkan work stays on its render thread.
 */
final class GamePadDisplay implements DisplayManager.DisplayListener {
    private final Activity activity;
    private final DisplayManager manager;
    private Presentation presentation;
    private static long sequence;
    private long generation;
    private boolean started;
    private final ViewGroup layout;
    private final View primary;
    private SurfaceView embedded;
    private int[] embeddedPanes;
    private Rect hinge;
    private boolean verticalHinge;
    private boolean syntheticPosture;
    private long lifecycle;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private Runnable dismissalRecovery;
    private int failedDisplay = -1, invalidShows;
    private boolean invalidRetryPending;
    private int syntheticInvalidShows;
    private final WindowInfoTrackerCallbackAdapter folds;
    private Consumer<WindowLayoutInfo> foldListener;
    private final View.OnLayoutChangeListener layoutListener =
            (v, l, t, r, b, oldL, oldT, oldR, oldB) -> refresh();

    private static native void publishSurface(Surface surface, int width, int height, long generation);
    private static native boolean touch(float x, float y, boolean down, boolean dragging, long generation);

    GamePadDisplay(Activity activity, ViewGroup layout, View primary) {
        this.activity = activity;
        this.layout = layout;
        this.primary = primary;
        folds = new WindowInfoTrackerCallbackAdapter(WindowInfoTracker.getOrCreate(activity));
        manager = activity.getSystemService(DisplayManager.class);
    }

    void start() {
        if (started || manager == null) return;
        failedDisplay = -1; invalidShows = 0;
        started = true;
        final long session = ++lifecycle;
        foldListener = info -> {
            if (!started || session != lifecycle || syntheticPosture) return;
            hinge = null;
            for (DisplayFeature feature : info.getDisplayFeatures()) {
                if (!(feature instanceof FoldingFeature)) continue;
                FoldingFeature fold = (FoldingFeature) feature;
                // A physical hinge separates even when flat; a flexible flat screen stays whole.
                if (!fold.isSeparating()) continue;
                hinge = new Rect(fold.getBounds());
                verticalHinge = fold.getOrientation() == FoldingFeature.Orientation.VERTICAL;
                break;
            }
            refresh();
        };
        layout.addOnLayoutChangeListener(layoutListener);
        folds.addWindowLayoutInfoListener(activity, activity.getMainExecutor(), foldListener);
        manager.registerDisplayListener(this, new Handler(Looper.getMainLooper()));
        refresh();
    }

    void stop() {
        if (!started) return;
        started = false;
        ++lifecycle;
        folds.removeWindowLayoutInfoListener(foldListener);
        foldListener = null;
        hinge = null;
        layout.removeOnLayoutChangeListener(layoutListener);
        manager.unregisterDisplayListener(this);
        close();
    }

    private void close() {
        invalidRetryPending = false;
        if (dismissalRecovery != null) {
            mainHandler.removeCallbacks(dismissalRecovery);
            dismissalRecovery = null;
        }
        // Invalidate callbacks before dismiss(), which may synchronously destroy the surface.
        generation = ++sequence;
        publishSurface(null, 0, 0, generation);
        Presentation old = presentation;
        presentation = null;
        if (old != null) old.dismiss();
        if (embedded != null) {
            layout.removeView(embedded);
            embedded = null;
            embeddedPanes = null;
            primary.setLayoutParams(new RelativeLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        }
    }

    // The renderer may lose WSI independently of DisplayManager/SurfaceHolder.
    // Ignore a delayed failure from an older host; close() also cancels touch.
    void recoverSurface(long failedGeneration) {
        if (!started || generation != failedGeneration) return;
        close();
        final long token = generation;
        dismissalRecovery = () -> {
            dismissalRecovery = null;
            if (started && generation == token && presentation == null) refresh();
        };
        mainHandler.postDelayed(dismissalRecovery, 500);
    }

    private void refresh() {
        if (!started) return;
        Display[] displays = manager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        Display pick = null;
        int mainId = activity.getDisplay() != null ? activity.getDisplay().getDisplayId() : Display.DEFAULT_DISPLAY;
        for (Display display : displays) {
            if (display.getDisplayId() == mainId || !display.isValid()) continue;
            if (pick == null || display.getDisplayId() < pick.getDisplayId()) pick = display;
        }
        if (presentation != null && pick != null &&
                presentation.getDisplay().getDisplayId() == pick.getDisplayId()) return;
        if (pick == null) {
            refreshFold();
            return;
        }
        if (failedDisplay == pick.getDisplayId() && (invalidRetryPending || invalidShows >= 4)) return;
        if (failedDisplay != pick.getDisplayId()) invalidShows = 0;
        close();
        if (!started) return;
        final long token = generation;
        Log.i("wwhd-display", "secondary display=" + pick.getDisplayId());
        Presentation next = new Presentation(activity, pick) {
            @Override protected void onCreate(Bundle state) {
                super.onCreate(state);
                // Match the fork's DrcDisplay: controller keys belong to the game activity.
                setCancelable(false);
                getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON |
                        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
                setContentView(createView(getContext(), token));
            }
        };
        // Android can dismiss a Presentation independently of display removal. Invalidate its
        // callbacks immediately and recover even without another display event. At most one
        // delayed attempt exists; close/stop/replacement cancels it. Invalid window
        // creation uses a separate bounded budget and then waits for a display event.
        // Identity guards exclude dismissals from close() and older hosts.
        next.setOnDismissListener(dialog -> {
            if (presentation == next) {
                presentation = null;
                generation = ++sequence;
                publishSurface(null, 0, 0, generation);
                final long dismissed = generation;
                dismissalRecovery = () -> {
                    dismissalRecovery = null;
                    if (started && generation == dismissed && presentation == null) refresh();
                };
                mainHandler.postDelayed(dismissalRecovery, 500);
            }
        });
        presentation = next;
        try {
            if (syntheticInvalidShows > 0) {
                --syntheticInvalidShows;
                throw new WindowManager.InvalidDisplayException("Authored startup rejection");
            }
            next.show();
            failedDisplay = -1; invalidShows = 0;
        } catch (WindowManager.InvalidDisplayException exception) {
            close(); // A display may disappear or be temporarily unready after enumeration.
            failedDisplay = pick.getDisplayId();
            ++invalidShows;
            Log.w("wwhd-display", "secondary host rejected display=" + failedDisplay + " attempt=" + invalidShows + "/4");
            if (!pick.isValid() || invalidShows >= 4) return;
            final long retryGeneration = generation;
            invalidRetryPending = true;
            dismissalRecovery = () -> {
                dismissalRecovery = null;
                invalidRetryPending = false;
                if (started && generation == retryGeneration && presentation == null) refresh();
            };
            mainHandler.postDelayed(dismissalRecovery, invalidShows * 500L);
        }
    }

    private SurfaceView createView(Context context, long token) {
        SurfaceView view = new SurfaceView(context);
        view.setOnTouchListener(new android.view.View.OnTouchListener() {
            private int pointer = -1;
            public boolean onTouch(android.view.View target, MotionEvent event) {
                if (!started || generation != token) return false;
                int action = event.getActionMasked();
                if (action == MotionEvent.ACTION_DOWN) {
                    int index = event.getActionIndex();
                    if (target.getWidth() == 0 || target.getHeight() == 0) return false;
                    boolean accepted = touch(event.getX(index) / target.getWidth(),
                            event.getY(index) / target.getHeight(), true, false, token);
                    if (accepted) pointer = event.getPointerId(index);
                    return accepted;
                }
                if (pointer == -1) return false;
                if (action == MotionEvent.ACTION_CANCEL || action == MotionEvent.ACTION_UP ||
                        (action == MotionEvent.ACTION_POINTER_UP &&
                         event.getPointerId(event.getActionIndex()) == pointer)) {
                    touch(0, 0, false, true, token);
                    pointer = -1;
                    return true;
                }
                int index = event.findPointerIndex(pointer);
                if (index < 0 || target.getWidth() == 0 || target.getHeight() == 0) {
                    touch(0, 0, false, true, token);
                    pointer = -1;
                } else if (action == MotionEvent.ACTION_MOVE) {
                    touch(event.getX(index) / target.getWidth(),
                            event.getY(index) / target.getHeight(), true, true, token);
                }
                return true;
            }
        });
        view.getHolder().addCallback(new SurfaceHolder.Callback() {
            public void surfaceCreated(SurfaceHolder holder) { }
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                if (started && generation == token)
                    publishSurface(holder.getSurface(), width, height, token);
            }
            public void surfaceDestroyed(SurfaceHolder holder) {
                if (generation == token) publishSurface(null, 0, 0, token);
            }
        });
        return view;
    }

    private void refreshFold() {
        int[] panes = null;
        if (hinge != null && layout instanceof RelativeLayout) {
            int[] location = new int[2];
            layout.getLocationInWindow(location);
            panes = FoldLayout.panes(layout.getWidth(), layout.getHeight(), location[0], location[1],
                    hinge.left, hinge.top, hinge.right, hinge.bottom, verticalHinge);
        }
        if (embedded != null && java.util.Arrays.equals(panes, embeddedPanes)) return;
        if (panes == null && embedded == null && presentation == null) return;
        // Invalidate the old touch/surface generation before changing either pane.
        close();
        if (panes == null || !started) return;
        embeddedPanes = panes;
        embedded = createView(activity, generation);
        primary.setLayoutParams(paneParams(panes, 0));
        layout.addView(embedded, paneParams(panes, 4));
        Log.i("wwhd-display", "fold panes=" + java.util.Arrays.toString(panes));
    }

    private static RelativeLayout.LayoutParams paneParams(int[] panes, int offset) {
        RelativeLayout.LayoutParams params = new RelativeLayout.LayoutParams(
                panes[offset + 2], panes[offset + 3]);
        params.leftMargin = panes[offset];
        params.topMargin = panes[offset + 1];
        return params;
    }

    // Used by the debug activity to exercise actual surfaces without a foldable emulator image.
    void syntheticFold(Rect bounds, boolean vertical) {
        syntheticPosture = true;
        hinge = bounds == null ? null : new Rect(bounds);
        verticalHinge = vertical;
        refresh();
    }

    // Called only by the debug fixture before start().
    void syntheticRejectShows(int count) { syntheticInvalidShows = Math.max(0, Math.min(count, 100)); }

    // Debug fixture exercises a delayed failure from an obsolete host.
    void syntheticStaleRecovery() { recoverSurface(generation - 1); }

    // Debug fixture exercises independent system-style dismissal without changing displays.
    void syntheticDismiss() {
        if (presentation == null) throw new IllegalStateException("No secondary presentation");
        presentation.dismiss();
    }

    private void displayEvent(int id) {
        // A real capability/topology event permits a new bounded attempt budget.
        if (id == failedDisplay) {
            failedDisplay = -1; invalidShows = 0;
            if (invalidRetryPending) {
                if (dismissalRecovery != null) mainHandler.removeCallbacks(dismissalRecovery);
                dismissalRecovery = null; invalidRetryPending = false;
            }
        }
        refresh();
    }
    public void onDisplayAdded(int id) { displayEvent(id); }
    public void onDisplayRemoved(int id) { displayEvent(id); }
    public void onDisplayChanged(int id) { displayEvent(id); }
}
