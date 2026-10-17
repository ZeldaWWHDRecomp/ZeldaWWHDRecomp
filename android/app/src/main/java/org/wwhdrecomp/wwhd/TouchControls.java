package org.wwhdrecomp.wwhd;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.hardware.input.InputManager;
import android.util.SparseArray;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLControllerManager;

import java.util.ArrayList;
import java.util.List;
import java.util.HashSet;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.RelativeLayout;
import android.view.ViewGroup;

// On-screen controls over the game: two sticks, A B X Y, L R ZL ZR, + -, a D-pad, and a small button
// under the view button (top left) that shows or hides them (remembered). They are a virtual SDL gamepad
// (runtime/src/platform/touch_pad.cpp), so the game reads them like a controller. Touches that land
// on no control go on to the game as before (the GamePad picture, the view button), the way
// SDLSurface passes them. While a game controller is connected they are hidden, and the next touch
// brings the floating buttons back temporarily.
public class TouchControls extends View implements InputManager.InputDeviceListener {
    private static native void nativeSetPad(int buttons, int face, float lx, float ly, float rx, float ry, float lt, float rt);
    private static native void nativeDetachPad();
    private static native int nativeTouchState();
    private static native void nativeControllerConnected(boolean connected);
    private static native void nativeScreenDpi(float dpi);
    private static native void nativeRevealButtons();
    private static native void nativeShowControls(boolean shown);

    // SDL_GamepadButton numbers
    private static final int BACK = 4, START = 6,
            LEFT_SHOULDER = 9, RIGHT_SHOULDER = 10, DPAD_UP = 11, DPAD_DOWN = 12, DPAD_LEFT = 13, DPAD_RIGHT = 14;
    private static final int KIND_BUTTON = 0, KIND_STICK = 1, KIND_TRIGGER = 2, KIND_TOGGLE = 3, KIND_FACE = 4;
    // KIND_FACE codes: the Wii U button drawn on it (touch_face.h sends the pad button the controls
    // mapping binds to it, so the drawn A is the Wii U's A with either face-button preset)
    private static final int FACE_A = 0, FACE_B = 1, FACE_X = 2, FACE_Y = 3;

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
    private final TouchLayout layout;
    private final TouchLayout.EditSession editor = new TouchLayout.EditSession();
    private LinearLayout editorBar;
    private Control selected;
    private int dragPointer = -1;
    private float dragOffsetX, dragOffsetY;
    private boolean longPressConsumed;
    private final Runnable editLongPress;
    private final SparseArray<Control> owner = new SparseArray<>();  // pointer -> control (null: the game's)
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), edge = new Paint(Paint.ANTI_ALIAS_FLAG),
            text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final SharedPreferences prefs;
    private final InputManager inputs;
    private ControllerHintPolicy hintPolicy;
    private Runnable refresh;
    private boolean shown;      // the player's choice (the show/hide button), remembered
    private boolean controller; // a game controller is connected and no touch came since
    private boolean buttonsVisible = true, revealGesture, settingsOpen;
    private boolean toggleTo;   // what the show/hide button sets when released (shown before the touch: hide)
    private int lastButtons = -1, lastTouchDevice;
    private float lastLx, lastLy, lastRx, lastRy, lastLt, lastRt;

    public TouchControls(Context context) {
        super(context);
        prefs = context.getSharedPreferences("controls", Context.MODE_PRIVATE);
        shown = prefs.getBoolean("touch_controls", true);
        hintPolicy = new ControllerHintPolicy(prefs.getBoolean("settings_shortcut_hint_shown", false));
        inputs = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
        if (inputs != null) {
            inputs.registerInputDeviceListener(this, null);
            for (int id : inputs.getInputDeviceIds()) controller |= isController(InputDevice.getDevice(id));
            SDLControllerManager.requestDeviceRefresh();
        }
        // the top left corner has the game's view button and the show/hide button: nothing else there
        controls.add(new Control(KIND_STICK, 0, "", 0.14f, 0.70f, 0.13f));
        controls.add(new Control(KIND_STICK, 1, "", 0.68f, 0.80f, 0.11f));
        // A B X Y where a Wii U GamePad has them
        final float ax = 0.86f, ay = 0.60f, d = 0.10f;
        controls.add(new Control(KIND_FACE, FACE_A, "A", ax + d * 0.75f, ay, 0.055f));
        controls.add(new Control(KIND_FACE, FACE_B, "B", ax, ay + d, 0.055f));
        controls.add(new Control(KIND_FACE, FACE_X, "X", ax, ay - d, 0.055f));
        controls.add(new Control(KIND_FACE, FACE_Y, "Y", ax - d * 0.75f, ay, 0.055f));
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
        editLongPress = () -> {
            if (toggle.pressed && owner.size()==1 && !settingsOpen) {
                longPressConsumed=true;
                beginEditing();
            }
        };
        HashSet<String> keys=new HashSet<>();
        for (Control c : controls) keys.add(key(c));
        layout=new TouchLayout(keys);
        layout.decode(prefs.getString("touch_layout_v1", ""));
        setContentDescription(context.getString(R.string.touch_layout_access));
        refresh = new Runnable() {
            @Override public void run() {
                syncTouchState();
                maybeShowHint();
                invalidate();
                postDelayed(this, 100);
            }
        };
        post(refresh);
        text.setTextAlign(Paint.Align.CENTER);
        text.setFakeBoldText(true);
        edge.setStyle(Paint.Style.STROKE);
    }

    @Override protected void onDetachedFromWindow() {
        endEditing();
        releaseInputs();
        removeCallbacks(editLongPress);
        removeCallbacks(refresh);
        if (inputs != null) inputs.unregisterInputDeviceListener(this);
        try { nativeDetachPad(); } catch (UnsatisfiedLinkError ignored) {}
        super.onDetachedFromWindow();
    }

    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        if (inputs != null) inputs.registerInputDeviceListener(this, null);
        removeCallbacks(refresh);
        post(refresh);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        releaseInputs();
        dragPointer=-1;
        final float s = Math.min(w, h);
        for (Control c : controls) place(c, w, h, s);
        // under the game's view button (screen_layout.cpp: a square of 9% of the short side, 1.5% from
        // the top left corner)
        toggle.r = toggle.fr * s;
        toggle.cx = s * (0.015f + 0.045f);
        toggle.cy = s * (0.015f + 0.09f + 0.02f) + toggle.r;
        edge.setStrokeWidth(Math.max(2f, s * 0.004f));
    }

    private static String key(Control c) { return c.kind+":"+c.code; }
    private void place(Control c, int w, int h, float s) {
        TouchLayout.Placement p=layout.get(key(c));
        if (p==null && !editor.editing()) {
            c.cx=c.fx*w; c.cy=c.fy*h; c.r=c.fr*s; // unchanged default path
        } else {
            if (p==null) p=new TouchLayout.Placement(c.fx,c.fy,c.fr);
            p=TouchLayout.fit(p,w,h,editor.editing() && editorBar!=null ? editorBar.getBottom():0);
            c.cx=p.x*w; c.cy=p.y*h; c.r=p.radius*s;
        }
    }
    private void placeControls() {
        for (Control c:controls) place(c,getWidth(),getHeight(),Math.min(getWidth(),getHeight()));
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (editor.editing() || active()) for (Control c : controls) drawControl(canvas, c);
        if (!editor.editing() && buttonsVisible && !settingsOpen) drawControl(canvas, toggle);
    }

    private void drawControl(Canvas canvas, Control c) {
        final boolean on = (editor.editing() && c==selected) || c.pressed || (c.kind == KIND_TOGGLE && active());
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
        if (buttonsVisible && !settingsOpen && near(toggle, x, y, 1.3f)) return toggle;
        if (!active()) return null;
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
        lastTouchDevice = event.getDeviceId();
        if (editor.editing()) return editTouch(event);
        if (editor.discarding()) {
            editor.pointers(action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_CANCEL ? 0:
                    event.getPointerCount()-(action==MotionEvent.ACTION_POINTER_UP ? 1:0));
            return true;
        }
        editor.pointers(action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_CANCEL ? 0:
                    event.getPointerCount()-(action==MotionEvent.ACTION_POINTER_UP ? 1:0));
        if (action == MotionEvent.ACTION_DOWN && event.getToolType(0) != MotionEvent.TOOL_TYPE_FINGER) {
            editor.pointers(0);
            return false;
        }  // mouse and stylus: SDLSurface underneath as before
        syncTouchState();
        if (settingsOpen) { editor.pointers(0);return false; }
        if (revealGesture) {
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) revealGesture = false;
            return true;
        }
        final float corner = Math.min(getWidth(), getHeight()) * 0.24f;
        if (!buttonsVisible && action == MotionEvent.ACTION_DOWN && event.getX() < corner && event.getY() < corner) {
            nativeRevealButtons();
            syncTouchState();
            revealGesture = true;
            invalidate();
            return true;
        }
        final int index = event.getActionIndex();
        final boolean wasActive = active();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                if (action==MotionEvent.ACTION_POINTER_DOWN) removeCallbacks(editLongPress);
                final int id = event.getPointerId(index);
                final float x = event.getX(index), y = event.getY(index);
                Control c = hit(x, y);
                owner.put(id, c);
                if (c == null && action == MotionEvent.ACTION_DOWN) {
                    // The first finger on no control: decline the gesture so the view under it takes it
                    // (SDLSurface, or on a fold the primary pane, which is smaller than this view and
                    // maps the touch to its own size; forward() can only use this view's size).
                    owner.remove(id);
                    editor.pointers(0);
                    update();
                    invalidate();
                    return false;
                } else if (c == null) {
                    forward(event, index, action);
                } else if (c == toggle) {
                    c.pressed = true;
                    longPressConsumed=false;
                    postDelayed(editLongPress, android.view.ViewConfiguration.getLongPressTimeout());
                    toggleTo = !wasActive;  // hidden for a controller: the button brings them back
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
                    else if (c == toggle && !near(c,event.getX(i),event.getY(i),1.3f)) removeCallbacks(editLongPress);
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
                    if (c == toggle) {
                        removeCallbacks(editLongPress);
                        if (!all && !longPressConsumed) setShown(toggleTo);
                    }
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

    private void releaseInputs() {
        removeCallbacks(editLongPress);
        for (Control c:controls) { c.pressed=false; c.sx=c.sy=0; }
        toggle.pressed=false;
        for (int i=0;i<owner.size();++i) if (owner.valueAt(i)==null)
            try { SDLActivity.onNativeTouch(lastTouchDevice,owner.keyAt(i),MotionEvent.ACTION_CANCEL,0,0,0); }
            catch (UnsatisfiedLinkError ignored) {}
        owner.clear();
        try { nativeDetachPad(); } catch (UnsatisfiedLinkError ignored) {}
        lastButtons=-1;
    }

    private void beginEditing() {
        if (!(getParent() instanceof RelativeLayout) || settingsOpen) return;
        editor.setEditing(true,this::releaseInputs);
        selected=null;dragPointer=-1;
        editorBar=new LinearLayout(getContext());
        editorBar.setOrientation(LinearLayout.VERTICAL);
        editorBar.setBackgroundColor(0xEE123340);
        editorBar.addOnLayoutChangeListener((v,left,top,right,bottom,oldLeft,oldTop,oldRight,oldBottom)->{
            if (bottom!=oldBottom || right!=oldRight) placeControls();
        });
        TextView hint=new TextView(getContext());hint.setText(R.string.touch_layout_hint);
        hint.setTextColor(0xFFFFFFFF);hint.setGravity(android.view.Gravity.CENTER);
        editorBar.addView(hint);
        LinearLayout actions=new LinearLayout(getContext());
        editorButton(actions,R.string.touch_layout_smaller,()->resizeSelected(.9f));
        editorButton(actions,R.string.touch_layout_larger,()->resizeSelected(1.1f));
        editorButton(actions,R.string.touch_layout_reset,()->{ layout.reset();selected=null;saveLayout();placeControls(); });
        editorButton(actions,R.string.touch_layout_done,this::endEditing);
        editorBar.addView(actions);
        RelativeLayout.LayoutParams lp=new RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.addRule(RelativeLayout.ALIGN_PARENT_TOP);
        ((RelativeLayout)getParent()).addView(editorBar,lp);
        editorBar.post(this::placeControls);
        placeControls();announceForAccessibility(getContext().getString(R.string.touch_layout_hint));
    }
    private void editorButton(LinearLayout bar,int label,Runnable action) {
        Button b=new Button(getContext());b.setText(label);b.setOnClickListener(v->action.run());
        b.setMinHeight((int)(48*getResources().getDisplayMetrics().density));
        bar.addView(b,new LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.WRAP_CONTENT,1));
    }
    private void endEditing() {
        if (!editor.editing()) return;
        saveLayout();editor.setEditing(false,this::releaseInputs);selected=null;dragPointer=-1;
        if (editorBar!=null && editorBar.getParent() instanceof ViewGroup)
            ((ViewGroup)editorBar.getParent()).removeView(editorBar);
        editorBar=null;placeControls();
    }
    private void saveLayout() { prefs.edit().putString("touch_layout_v1",layout.encode()).apply(); }
    private void moveSelected(float x,float y,float radius) {
        int w=getWidth(),h=getHeight();if (selected==null || w<=0 || h<=0) return;
        TouchLayout.Placement p=TouchLayout.fit(new TouchLayout.Placement(x/w,y/h,radius/Math.min(w,h)),
                w,h,editorBar!=null ? editorBar.getBottom():0);
        if (layout.put(key(selected),p.x,p.y,p.radius)) placeControls();
    }
    private void resizeSelected(float scale) {
        if (selected!=null) { moveSelected(selected.cx,selected.cy,selected.r*scale);saveLayout(); }
    }
    private boolean editTouch(MotionEvent e) {
        int action=e.getActionMasked();
        editor.pointers(action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_CANCEL ? 0:
                e.getPointerCount()-(action==MotionEvent.ACTION_POINTER_UP ? 1:0));
        if (action==MotionEvent.ACTION_DOWN) {
            selected=null;float best=Float.MAX_VALUE;
            for (Control c:controls) {
                float dx=e.getX()-c.cx,dy=e.getY()-c.cy,d=(dx*dx+dy*dy)/(c.r*c.r);
                if (d<=1.69f && d<best) { best=d;selected=c; }
            }
            if (selected!=null) { dragPointer=e.getPointerId(0);dragOffsetX=e.getX()-selected.cx;dragOffsetY=e.getY()-selected.cy; }
        } else if (action==MotionEvent.ACTION_MOVE && selected!=null && dragPointer>=0) {
            int i=e.findPointerIndex(dragPointer);
            if (i>=0) moveSelected(e.getX(i)-dragOffsetX,e.getY(i)-dragOffsetY,selected.r);
        } else if (action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_CANCEL ||
                (action==MotionEvent.ACTION_POINTER_UP && e.getPointerId(e.getActionIndex())==dragPointer)) {
            dragPointer=-1;saveLayout();
        }
        invalidate();return true;
    }
    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (!hasFocus) { endEditing();releaseInputs();editor.pointers(0); }
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
        nativeShowControls(on);
        if (!on) { nativeDetachPad(); lastButtons = -1; }
    }

    // shown, and not hidden for a connected controller
    private boolean active() { return !editor.editing() && shown && buttonsVisible && !settingsOpen; }

    private void syncTouchState() {
        try {
            android.util.DisplayMetrics metrics = getResources().getDisplayMetrics();
            float dpi = Math.min(metrics.xdpi, metrics.ydpi);
            if (!Float.isFinite(dpi) || dpi < 72 || dpi > 1000) dpi = metrics.densityDpi;
            nativeScreenDpi(dpi);
            nativeControllerConnected(controller);
            final int state = nativeTouchState();
            final boolean visible = (state & 1) != 0, controlsShown = (state & 2) != 0, open = (state & 4) != 0;
            if (buttonsVisible == visible && shown == controlsShown && settingsOpen == open) return;
            buttonsVisible = visible; shown = controlsShown; settingsOpen = open;
            if (open && editor.editing()) endEditing();
            for (Control c : controls) { c.pressed = false; c.sx = 0; c.sy = 0; }
            for (int i = owner.size() - 1; i >= 0; --i) {
                if (owner.valueAt(i) != null) owner.removeAt(i);
                else if (settingsOpen) {
                    SDLActivity.onNativeTouch(lastTouchDevice, owner.keyAt(i), MotionEvent.ACTION_CANCEL, 0, 0, 0);
                    owner.removeAt(i);
                }
            }
            nativeDetachPad(); lastButtons = -1;
            if (active()) update();
        } catch (UnsatisfiedLinkError ignored) { /* SDL has not loaded the native library yet. */ }
    }

    private void maybeShowHint() {
        if (!(getParent() instanceof android.widget.RelativeLayout) || !hintPolicy.claim(controller)) return;
        final android.widget.RelativeLayout parent = (android.widget.RelativeLayout) getParent();
        final android.widget.TextView hint = new android.widget.TextView(getContext());
        boolean home = false;
        if (inputs != null) for (int id : inputs.getInputDeviceIds()) {
            InputDevice d = InputDevice.getDevice(id);
            if (isController(d)) home |= d.hasKeys(android.view.KeyEvent.KEYCODE_BUTTON_MODE)[0];
        }
        hint.setText(getContext().getString(home ? R.string.controller_settings_hint_home : R.string.controller_settings_hint));
        hint.setTextSize(18); hint.setTextColor(0xFFFFFFFF); hint.setBackgroundColor(0xEE123340);
        hint.setGravity(android.view.Gravity.CENTER);
        final int pad = (int)(16 * getResources().getDisplayMetrics().density);
        hint.setPadding(pad, pad, pad, pad);
        android.widget.RelativeLayout.LayoutParams lp = new android.widget.RelativeLayout.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.addRule(android.widget.RelativeLayout.ALIGN_PARENT_BOTTOM);
        lp.setMargins(pad, pad, pad, pad);
        parent.addView(hint, lp);
        hint.setOnClickListener(v -> parent.removeView(hint));
        hint.postDelayed(() -> parent.removeView(hint), 8000);
        prefs.edit().putBoolean("settings_shortcut_hint_shown", true).apply();
    }

    private static boolean isController(InputDevice d) {
        if (d == null || d.isVirtual()) return false;
        final int s = d.getSources();
        return (s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    @Override public void onInputDeviceAdded(int id) {
        SDLControllerManager.requestDeviceRefresh();
        if (controller || !isController(InputDevice.getDevice(id))) return;
        controller = true;
        for (Control c : controls) { c.pressed = false; c.sx = 0; c.sy = 0; }
        for (int i = owner.size() - 1; i >= 0; i--) if (owner.valueAt(i) != null && owner.valueAt(i) != toggle) owner.removeAt(i);
        if (shown) { nativeDetachPad(); lastButtons = -1; }
        invalidate();
    }
    @Override public void onInputDeviceRemoved(int id) {
        SDLControllerManager.requestDeviceRefresh();
        if (!controller || inputs == null) return;
        boolean any = false;
        for (int other : inputs.getInputDeviceIds()) any |= other != id && isController(InputDevice.getDevice(other));
        if (!any) { controller = false; invalidate(); }
    }
    @Override public void onInputDeviceChanged(int id) {
        SDLControllerManager.requestDeviceRefresh();
        controller = false;
        if (inputs != null) for (int other : inputs.getInputDeviceIds()) controller |= isController(InputDevice.getDevice(other));
        invalidate();
    }

    private void update() {
        if (!active()) return;
        int buttons = 0, face = 0;
        float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
        for (Control c : controls) {
            if (c.kind == KIND_BUTTON && c.pressed) buttons |= 1 << c.code;
            else if (c.kind == KIND_FACE && c.pressed) face |= 1 << c.code;
            else if (c.kind == KIND_TRIGGER && c.pressed) { if (c.code == 0) lt = 1; else rt = 1; }
            else if (c.kind == KIND_STICK) { if (c.code == 0) { lx = c.sx; ly = c.sy; } else { rx = c.sx; ry = c.sy; } }
        }
        buttons |= face << 16;  // (only for the change check below)
        if (buttons == lastButtons && lx == lastLx && ly == lastLy && rx == lastRx && ry == lastRy && lt == lastLt && rt == lastRt)
            return;
        lastButtons = buttons; lastLx = lx; lastLy = ly; lastRx = rx; lastRy = ry; lastLt = lt; lastRt = rt;
        try {
            nativeSetPad(buttons & 0xFFFF, face, lx, ly, rx, ry, lt, rt);
        } catch (UnsatisfiedLinkError e) {
            // libmain.so not loaded yet (very first touch): the next change sends it
            lastButtons = -1;
        }
    }
}
