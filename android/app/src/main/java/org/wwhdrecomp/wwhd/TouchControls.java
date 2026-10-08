package org.wwhdrecomp.wwhd;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;

import org.libsdl.app.SDLActivity;

import java.util.ArrayList;
import java.util.List;

// On-screen controls over the game: two sticks, A B X Y, L R ZL ZR, + -, a D-pad, and a small button
// under the view button (top left) that shows or hides them (remembered). They are a virtual SDL gamepad
// (runtime/src/platform/touch_pad.cpp), so the game reads them like a controller. Touches that land
// on no control go on to the game as before (the GamePad picture, the view button), the way
// SDLSurface passes them.
public class TouchControls extends View {
    private static native void nativeSetPad(int buttons, float lx, float ly, float rx, float ry, float lt, float rt);
    private static native void nativeDetachPad();

    // SDL_GamepadButton numbers
    private static final int SOUTH = 0, EAST = 1, WEST = 2, NORTH = 3, BACK = 4, START = 6,
            LEFT_SHOULDER = 9, RIGHT_SHOULDER = 10, DPAD_UP = 11, DPAD_DOWN = 12, DPAD_LEFT = 13, DPAD_RIGHT = 14;
    private static final int KIND_BUTTON = 0, KIND_STICK = 1, KIND_TRIGGER = 2, KIND_TOGGLE = 3;

    private static final class Control {
        final int kind, code;  // code: button number, stick 0/1, trigger 0/1
        final String label;
        final float fx, fy, fr;  // centre as a fraction of width / height, radius as a fraction of the short side
        float cx, cy, r;
        boolean pressed;
        float sx, sy;  // stick position -1..1
        Control(int kind, int code, String label, float fx, float fy, float fr) {
            this.kind = kind; this.code = code; this.label = label; this.fx = fx; this.fy = fy; this.fr = fr;
        }
    }

    private final List<Control> controls = new ArrayList<>();
    private final Control toggle;
    private final SparseArray<Control> owner = new SparseArray<>();  // pointer -> control (null: the game's)
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), edge = new Paint(Paint.ANTI_ALIAS_FLAG),
            text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final SharedPreferences prefs;
    private boolean shown;
    private int lastButtons = -1;
    private float lastLx, lastLy, lastRx, lastRy, lastLt, lastRt;

    public TouchControls(Context context) {
        super(context);
        prefs = context.getSharedPreferences("controls", Context.MODE_PRIVATE);
        shown = prefs.getBoolean("touch_controls", true);
        // the top left corner has the game's view button and the show/hide button: nothing else there
        controls.add(new Control(KIND_STICK, 0, "", 0.14f, 0.70f, 0.13f));
        controls.add(new Control(KIND_STICK, 1, "", 0.68f, 0.80f, 0.11f));
        // A B X Y as on a Wii U GamePad. Controllers map by position (input_map.cpp: the bottom face
        // button is the Wii U's B), so each one sends the button of the place it is drawn at
        final float ax = 0.86f, ay = 0.60f, d = 0.10f;
        controls.add(new Control(KIND_BUTTON, EAST, "A", ax + d * 0.75f, ay, 0.055f));
        controls.add(new Control(KIND_BUTTON, SOUTH, "B", ax, ay + d, 0.055f));
        controls.add(new Control(KIND_BUTTON, NORTH, "X", ax, ay - d, 0.055f));
        controls.add(new Control(KIND_BUTTON, WEST, "Y", ax - d * 0.75f, ay, 0.055f));
        final float px = 0.14f, py = 0.36f, e = 0.075f;
        controls.add(new Control(KIND_BUTTON, DPAD_UP, "▲", px, py - e, 0.04f));
        controls.add(new Control(KIND_BUTTON, DPAD_DOWN, "▼", px, py + e, 0.04f));
        controls.add(new Control(KIND_BUTTON, DPAD_LEFT, "◀", px - e * 0.75f, py, 0.04f));
        controls.add(new Control(KIND_BUTTON, DPAD_RIGHT, "▶", px + e * 0.75f, py, 0.04f));
        controls.add(new Control(KIND_TRIGGER, 0, "ZL", 0.19f, 0.15f, 0.05f));
        controls.add(new Control(KIND_BUTTON, LEFT_SHOULDER, "L", 0.30f, 0.15f, 0.05f));
        controls.add(new Control(KIND_BUTTON, RIGHT_SHOULDER, "R", 0.70f, 0.15f, 0.05f));
        controls.add(new Control(KIND_TRIGGER, 1, "ZR", 0.81f, 0.15f, 0.05f));
        controls.add(new Control(KIND_BUTTON, BACK, "−", 0.42f, 0.92f, 0.04f));
        controls.add(new Control(KIND_BUTTON, START, "+", 0.58f, 0.92f, 0.04f));
        toggle = new Control(KIND_TOGGLE, 0, "🎮", 0f, 0f, 0.04f);  // placed in onSizeChanged
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
        edge.setStyle(Paint.Style.STROKE);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        final float s = Math.min(w, h);
        for (Control c : controls) place(c, w, h, s);
        // under the game's view button (screen_layout.cpp: a square of 9% of the short side, 1.5% from
        // the top left corner)
        toggle.r = toggle.fr * s;
        toggle.cx = s * (0.015f + 0.045f);
        toggle.cy = s * (0.015f + 0.09f + 0.02f) + toggle.r;
        edge.setStrokeWidth(Math.max(2f, s * 0.004f));
    }

    private static void place(Control c, int w, int h, float s) {
        c.cx = c.fx * w; c.cy = c.fy * h; c.r = c.fr * s;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (shown) for (Control c : controls) drawControl(canvas, c);
        drawControl(canvas, toggle);
    }

    private void drawControl(Canvas canvas, Control c) {
        final boolean on = c.pressed || (c.kind == KIND_TOGGLE && shown);
        fill.setColor(on ? 0x90FFFFFF : 0x38FFFFFF);
        edge.setColor(0x80FFFFFF);
        canvas.drawCircle(c.cx, c.cy, c.r, fill);
        canvas.drawCircle(c.cx, c.cy, c.r, edge);
        if (c.kind == KIND_STICK) {
            fill.setColor(c.pressed ? 0xB0FFFFFF : 0x70FFFFFF);
            canvas.drawCircle(c.cx + c.sx * c.r * 0.6f, c.cy + c.sy * c.r * 0.6f, c.r * 0.42f, fill);
            return;
        }
        text.setColor(0xE0FFFFFF);
        text.setTextSize(c.r * 0.9f);
        canvas.drawText(c.label, c.cx, c.cy - (text.descent() + text.ascent()) / 2, text);
    }

    private Control hit(float x, float y) {
        if (near(toggle, x, y, 1.3f)) return toggle;
        if (!shown) return null;
        Control best = null;
        float bestD = Float.MAX_VALUE;
        for (Control c : controls) {
            float reach = c.kind == KIND_STICK ? 1.5f : 1.3f;
            float dx = x - c.cx, dy = y - c.cy, dd = (dx * dx + dy * dy) / (c.r * c.r);
            if (dd <= reach * reach && dd < bestD) { best = c; bestD = dd; }
        }
        return best;
    }

    private static boolean near(Control c, float x, float y, float reach) {
        float dx = x - c.cx, dy = y - c.cy;
        return dx * dx + dy * dy <= c.r * c.r * reach * reach;
    }

    private static void moveStick(Control c, float x, float y) {
        float dx = (x - c.cx) / c.r, dy = (y - c.cy) / c.r, len = (float) Math.sqrt(dx * dx + dy * dy);
        if (len > 1f) { dx /= len; dy /= len; }
        c.sx = dx; c.sy = dy;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        final int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN && event.getToolType(0) != MotionEvent.TOOL_TYPE_FINGER)
            return false;  // mouse and stylus: SDLSurface underneath as before
        final int index = event.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                final int id = event.getPointerId(index);
                final float x = event.getX(index), y = event.getY(index);
                Control c = hit(x, y);
                owner.put(id, c);
                if (c == null) {
                    forward(event, index, action);
                } else if (c == toggle) {
                    c.pressed = true;
                } else {
                    c.pressed = true;
                    if (c.kind == KIND_STICK) moveStick(c, x, y);
                }
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < event.getPointerCount(); i++) {
                    final int id = event.getPointerId(i);
                    if (owner.indexOfKey(id) < 0) continue;
                    Control c = owner.get(id);
                    if (c == null) forward(event, i, MotionEvent.ACTION_MOVE);
                    else if (c.kind == KIND_STICK) moveStick(c, event.getX(i), event.getY(i));
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL: {
                final boolean all = action == MotionEvent.ACTION_CANCEL;
                for (int i = 0; i < event.getPointerCount(); i++) {
                    if (!all && i != index) continue;
                    final int id = event.getPointerId(i);
                    if (owner.indexOfKey(id) < 0) continue;
                    Control c = owner.get(id);
                    owner.remove(id);
                    if (c == null) { forward(event, i, all ? MotionEvent.ACTION_CANCEL : action); continue; }
                    c.pressed = false;
                    if (c.kind == KIND_STICK) { c.sx = 0; c.sy = 0; }
                    if (c == toggle && !all) setShown(!shown);
                }
                break;
            }
            default:
                return true;
        }
        update();
        invalidate();
        return true;
    }

    // a touch for the game: the same call SDLSurface makes (coordinates 0..1 of the view)
    private void forward(MotionEvent event, int i, int action) {
        float x = getWidth() > 1 ? event.getX(i) / (getWidth() - 1) : 0.5f;
        float y = getHeight() > 1 ? event.getY(i) / (getHeight() - 1) : 0.5f;
        float p = Math.min(event.getPressure(i), 1.0f);
        SDLActivity.onNativeTouch(event.getDeviceId(), event.getPointerId(i), action,
                Math.max(0f, Math.min(1f, x)), Math.max(0f, Math.min(1f, y)), p);
    }

    private void setShown(boolean on) {
        shown = on;
        prefs.edit().putBoolean("touch_controls", on).apply();
        for (Control c : controls) { c.pressed = false; c.sx = 0; c.sy = 0; }
        for (int i = owner.size() - 1; i >= 0; i--) if (owner.valueAt(i) != null && owner.valueAt(i) != toggle) owner.removeAt(i);
        if (!on) { nativeDetachPad(); lastButtons = -1; }
    }

    private void update() {
        if (!shown) return;
        int buttons = 0;
        float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
        for (Control c : controls) {
            if (c.kind == KIND_BUTTON && c.pressed) buttons |= 1 << c.code;
            else if (c.kind == KIND_TRIGGER && c.pressed) { if (c.code == 0) lt = 1; else rt = 1; }
            else if (c.kind == KIND_STICK) { if (c.code == 0) { lx = c.sx; ly = c.sy; } else { rx = c.sx; ry = c.sy; } }
        }
        if (buttons == lastButtons && lx == lastLx && ly == lastLy && rx == lastRx && ry == lastRy && lt == lastLt && rt == lastRt)
            return;
        lastButtons = buttons; lastLx = lx; lastLy = ly; lastRx = rx; lastRy = ry; lastLt = lt; lastRt = rt;
        try {
            nativeSetPad(buttons, lx, ly, rx, ry, lt, rt);
        } catch (UnsatisfiedLinkError e) {
            // libmain.so not loaded yet (very first touch): the next change sends it
            lastButtons = -1;
        }
    }
}
