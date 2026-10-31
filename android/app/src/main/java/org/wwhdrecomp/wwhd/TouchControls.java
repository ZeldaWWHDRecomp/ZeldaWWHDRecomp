package org.wwhdrecomp.wwhd;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.hardware.input.InputManager;
import android.net.Uri;
import android.os.SystemClock;
import android.util.SparseArray;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.widget.EditText;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLControllerManager;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.HashSet;
import java.util.Map;
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
//
// Styles (the options are the C++ side's, platform/touch_style.h, polled with nativeTouchOptions):
// Classic keeps both sticks where the layout puts them. Flexible keeps the buttons there, but a finger
// landing on no control becomes a stick under the thumb (nativeFreeTouch decides: left half the
// movement stick, right half a camera stick or the swipe camera through the mouse camera's path,
// nativeSwipe; the game's own touch areas keep their touches). Each style has its own layout and saved
// layouts (TouchLayout, TouchPresets); hold the controller button to edit them. Every finger loss
// (up, cancel, focus, hiding, a controller, a style change) releases the sticks (TouchStick.release).
// Flexible style, invisible and following sticks, swipe camera, per-control look, mirrored grid,
// saved layouts and the Arc layout: idea from the GreenNaugahyde Android fork (MPL-2.0); reimplemented.
//
// Context buttons (controls plan P3; ContextPack, polled with nativeContextButtons): while native code
// says the rules apply, A B X Y R ZR ZL + and the D-pad's up and left/right show only when they do
// something, with the game's word (an action), item icon or ▶▶ (fast forward) and the button's own name
// small beside it; L and D-pad down hide. Buttons native code holds (a latched item, the ZL latch) are
// drawn pressed, and A plays the golden rings when it gains a new action (GoldenRings). The words and
// icons are the game's, exported once into the app cache (ContextAssets). Items by tap, aim-only buttons,
// the auto put-away and the ZL latch are native: the fingers go to the pad as before. A control that
// hides under a finger lets go. The layout editor shows every control.
// Context buttons and the golden rings: idea from the GreenNaugahyde Android fork (MPL-2.0); reimplemented.
// Link in the button pictures (controls plan P8, option "Link in the button pictures"): an action whose pose
// native code drew from the game's model (pose_HH.png, runtime/src/mods/link_poses.h) shows Link in that pose
// with the word small under him; the Jump button shows the jump. Without a picture the word shows as before.
// Idea from the GreenNaugahyde Android fork (MPL-2.0), its "Link in the button icons"; reimplemented.
//
// Talks, answers, shops by touch (controls plan P5): while native code says a talk, a choice, the auction's
// number entry, a shop's item choice or the wind compass claims the TV picture (nativeFreeTouch answers
// FREE_TALK, in either style), a finger landing on no control goes to it (nativeTalkFinger) instead of
// becoming a stick; native code decides what the finger does. Idea from the GreenNaugahyde Android fork
// (MPL-2.0); reimplemented.
public class TouchControls extends View implements InputManager.InputDeviceListener {
    private static native void nativeSetPad(int buttons, int face, float lx, float ly, float rx, float ry, float lt, float rt);
    private static native void nativeDetachPad();
    private static native int nativeTouchState();
    private static native void nativeControllerConnected(boolean connected);
    private static native void nativeScreenDpi(float dpi);
    private static native void nativeRevealButtons();
    private static native void nativeShowControls(boolean shown);
    // Jump anywhere and the moon jump (P6; idea from the GreenNaugahyde Android fork): buttons of their own
    // that never press the game's L. Which show (bit 0 Jump, bit 1 Moon jump: hidden in water, cutscenes,
    // talks, menus, the Picto Box and the telescope) is decided natively from the game's state
    // (mods/context_buttons.h jump_buttons); their presses go in as virtual inputs.
    private static native int nativeJumpButtons();
    private static native void nativeSetVirtual(int bits);
    // a real game controller connected (ControllerHintPolicy): native code offers it the controller setup
    // on its first press
    private static native void nativeControllerSetupOffer();
    // the item ring (runtime/src/mods/item_ring.cpp): the finger held on X (0), Y (1) or R (2) and its drag
    // from where it landed, in the button's radii (+y down). Native code decides when the ring opens and
    // what the drag picks, and draws the ring; this view only passes the finger on.
    private static native void nativeItemRingFinger(int button, boolean down, float dx, float dy);
    // the ring is open: its finger's item button stays (shown, pressed) even when the context rules hide it
    private static native boolean nativeItemRingOpen();
    // context buttons (P3; runtime/src/mods/context_touch.h, the pack's layout): which buttons show, with the
    // game's word or item icon, which are held natively (items by tap, the ZL latch) and the golden rings;
    // nativeContextAssets(dir) exports the game's words and item icons into dir once (0 not yet, -1 failed,
    // > 0 the files' generation)
    private static native int nativeContextButtons(int[] out);
    private static native void nativeStartupTitleTap();
    private static native int nativeContextAssets(String dir);
    private static native int nativeTouchOptions(int[] out);
    private static native int nativeFreeTouch(float x, float y);
    private static native void nativeSwipe(float dx, float dy, float dpi);
    // talks, answers, shops... by touch (P5; runtime/src/mods/touch_talk.h): a finger nativeFreeTouch gave the
    // talk (FREE_TALK) goes there: 0 down, 1 move, 2 up, 3 cancel, at 0..1 of this view. Native code decides
    // what it does (go on, choose a line, steer to a shop's item...).
    private static native void nativeTalkFinger(int action, float x, float y);
    // a pinch for the field of view (P7; runtime/src/mods/steer.h): 0 begins (returns 1 when the option is on),
    // 1 moves, 2 ends; the fingers' distance in pixels
    private static native int nativePinch(int phase, float distance);
    // a code mod's pinch (Mod SDK): a second finger next to a talk finger a mod has; phase 0 begin (true: a mod has
    // it), 1 move, 2 end; spread now / at the start; the fingers' middle, 0..1 of the view
    private static native boolean nativeModPinch(int phase, float spread, float x, float y);

    static final int EXPORT_REQUEST = 4973, IMPORT_REQUEST = 4974;  // WwhdActivity.onActivityResult

    // SDL_GamepadButton numbers
    private static final int BACK = 4, START = 6,
            LEFT_SHOULDER = 9, RIGHT_SHOULDER = 10, DPAD_UP = 11, DPAD_DOWN = 12, DPAD_LEFT = 13, DPAD_RIGHT = 14;
    private static final int KIND_BUTTON = 0, KIND_STICK = 1, KIND_TRIGGER = 2, KIND_TOGGLE = 3, KIND_FACE = 4,
            KIND_VIRTUAL = 5,  // code: the virtual input bit (VIRT_JUMP, VIRT_MOON)
            KIND_SWIPE = 6, KIND_DEAD = 7, KIND_TALK = 8, KIND_PINCH = 9;  // (the last four: finger owners only, never drawn)
    private static final int VIRT_JUMP = 1, VIRT_MOON = 2;
    // KIND_FACE codes: the Wii U button drawn on it (touch_face.h sends the pad button the controls
    // mapping binds to it, so the drawn A is the Wii U's A with either face-button preset)
    private static final int FACE_A = 0, FACE_B = 1, FACE_X = 2, FACE_Y = 3;
    // touch_style.h
    private static final int STYLE_CLASSIC = 0, STYLE_FLEXIBLE = 1, RIGHT_SWIPE = 0, RIGHT_STICK = 1;
    private static final int FREE_GAME = 0, FREE_LEFT = 1, FREE_RIGHT = 2, FREE_SWIPE = 3, FREE_TALK = 4;

    private static final class Control {
        final int kind, code;  // code: button number, stick 0/1, trigger 0/1
        final String label;
        final float fx, fy, fr;  // centre as a fraction of width / height, radius as a fraction of the short side
        float cx, cy, r;
        boolean pressed;
        float downX, downY;  // where the finger landed (the item ring's drag)
        int shape = TouchLayout.SHAPE_CIRCLE, opacity = TouchLayout.OPACITY_DEFAULT;
        boolean filled = true;
        final TouchStick stick;  // sticks only
        Control(int kind, int code, String label, float fx, float fy, float fr) {
            this.kind = kind; this.code = code; this.label = label; this.fx = fx; this.fy = fy; this.fr = fr;
            stick = kind == KIND_STICK ? new TouchStick() : null;
        }
    }

    private final List<Control> controls = new ArrayList<>();
    private final StartupTouch startup = new StartupTouch();
    private final Control toggle, leftStick, rightStick;
    private final Control swipe = new Control(KIND_SWIPE, 0, "", 0, 0, 0), dead = new Control(KIND_DEAD, 0, "", 0, 0, 0),
            talk = new Control(KIND_TALK, 0, "", 0, 0, 0);
    private int talkPointer = -1;  // the finger the talk has (one at a time)
    // a pinch: two fingers that landed on no control within PINCH_MS of each other (Flexible style)
    private final Control pinch = new Control(KIND_PINCH, 0, "", 0, 0, 0);
    private static final long PINCH_MS = 250;
    private int pinchA = -1, pinchB = -1;
    private final SparseArray<Long> freeDown = new SparseArray<>();  // fingers freeTouch gave a stick or the swipe: when
    private final TouchLayout layout;
    private final TouchPresets presets = new TouchPresets();
    private final Map<String, Float> baseRadius = new HashMap<>();
    private final TouchLayout.EditSession editor = new TouchLayout.EditSession();
    private LinearLayout editorBar;
    private TextView editorStatus;
    private Control selected;
    private int dragPointer = -1;
    private float dragOffsetX, dragOffsetY;
    private boolean longPressConsumed, grid, dialogOpen;
    private final Runnable editLongPress;
    private final SparseArray<Control> owner = new SparseArray<>();  // pointer -> control (null: the game's)
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), edge = new Paint(Paint.ANTI_ALIAS_FLAG),
            text = new Paint(Paint.ANTI_ALIAS_FLAG), gridPaint = new Paint();
    private final Path shapePath = new Path();
    private final RectF shapeRect = new RectF();
    private final SharedPreferences prefs;
    private final InputManager inputs;
    private ControllerHintPolicy hintPolicy;
    private Runnable refresh;
    private boolean shown;      // the player's choice (the show/hide button), remembered
    private boolean controller; // a game controller is connected and no touch came since
    private boolean buttonsVisible = true, revealGesture, settingsOpen;
    private boolean toggleTo;   // what the show/hide button sets when released (shown before the touch: hide)
    private int lastButtons = -1, lastTouchDevice, lastVirtual = -1;
    private int jumpShown;  // nativeJumpButtons: the virtual buttons that show now
    private float lastLx, lastLy, lastRx, lastRy, lastLt, lastRt;
    // the touch options (C++), as last polled
    private final int[] options = new int[8];
    private int style = STYLE_FLEXIBLE, rightMode = RIGHT_SWIPE;
    private boolean showSticks = true, follow = true;
    private int swipePointer = -1;
    private float swipeX, swipeY, screenDpi = 400;
    // context buttons (P3)
    private final int[] contextRaw = new int[ContextPack.LEN];
    private final ContextPack contextPack = new ContextPack();
    private final GoldenRings rings = new GoldenRings(ContextPack.COUNT);
    private final ContextAssets assets = new ContextAssets();
    private File assetDir;
    private Map<Integer, String> words = new HashMap<>();
    private final Map<Integer, Bitmap> icons = new HashMap<>();  // null values: no such file
    private final Map<Integer, Bitmap> poses = new HashMap<>();  // Link's pose per action code (P8); null: none
    private final Paint ringPaint = new Paint(Paint.ANTI_ALIAS_FLAG), iconPaint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final RectF iconRect = new RectF();
    private long slowSyncAt;
    private boolean ringOpen;  // nativeItemRingOpen, as last polled
    private static final long FAST_TICK_MS = 33, SLOW_TICK_MS = 100;

    public TouchControls(Context context) {
        super(context);
        prefs = context.getSharedPreferences("controls", Context.MODE_PRIVATE);
        shown = prefs.getBoolean("touch_controls", true);
        hintPolicy = new ControllerHintPolicy();
        inputs = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
        if (inputs != null) {
            inputs.registerInputDeviceListener(this, null);
            for (int id : inputs.getInputDeviceIds()) controller |= isController(InputDevice.getDevice(id));
            SDLControllerManager.requestDeviceRefresh();
        }
        // the top left corner has the game's view button and the show/hide button: nothing else there
        leftStick = new Control(KIND_STICK, 0, "", 0.14f, 0.70f, 0.13f);
        rightStick = new Control(KIND_STICK, 1, "", 0.68f, 0.80f, 0.11f);
        controls.add(leftStick);
        controls.add(rightStick);
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
        controls.add(new Control(KIND_VIRTUAL, VIRT_JUMP, "Jump", 0.41f, 0.15f, 0.05f));
        controls.add(new Control(KIND_VIRTUAL, VIRT_MOON, "Moon", 0.52f, 0.15f, 0.045f));
        toggle = new Control(KIND_TOGGLE, 0, "🎮", 0f, 0f, 0.04f);  // placed in onSizeChanged
        editLongPress = () -> {
            if (toggle.pressed && owner.size()==1 && !settingsOpen) {
                longPressConsumed=true;
                beginEditing();
            }
        };
        HashSet<String> keys=new HashSet<>();
        for (Control c : controls) { keys.add(key(c)); baseRadius.put(key(c), c.fr); }
        layout=new TouchLayout(keys);
        // a format 1 layout (one, for the only style there was) seeds both styles' layouts, so the buttons
        // stay where the player put them in the default Flexible style too; the old key stays
        if (prefs.contains("touch_layout_v1")) {
            SharedPreferences.Editor ed = prefs.edit();
            for (int s : new int[] {STYLE_CLASSIC, STYLE_FLEXIBLE})
                if (!prefs.contains(layoutKey(s))) ed.putString(layoutKey(s), prefs.getString("touch_layout_v1", ""));
            ed.apply();
        }
        presets.fromJson(prefs.getString("touch_layout_presets", null), null);
        // the style the game last reported (Flexible by default, touch_style.h) until the first poll
        style = prefs.getInt("touch_style_seen", STYLE_FLEXIBLE) == STYLE_CLASSIC ? STYLE_CLASSIC : STYLE_FLEXIBLE;
        layout.decode(prefs.getString(layoutKey(style), ""));
        setContentDescription(context.getString(R.string.touch_layout_access));
        refresh = new Runnable() {
            @Override public void run() {
                // the context buttons each frame-ish while they apply (they change with every step Link
                // takes), the rest at the old pace
                final long now = SystemClock.uptimeMillis();
                if (now - slowSyncAt >= SLOW_TICK_MS - FAST_TICK_MS / 2) {
                    slowSyncAt = now;
                    syncTouchState();
                    announceControllers();
                    syncJumpButtons();
                    invalidate();
                }
                syncContext(now);
                postDelayed(this, active() && contextPack.on() ? FAST_TICK_MS : SLOW_TICK_MS);
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
        try { nativeDetachPad(); nativeSetVirtual(0); } catch (UnsatisfiedLinkError ignored) {}
        super.onDetachedFromWindow();
    }

    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        if (getContext() instanceof WwhdActivity) ((WwhdActivity) getContext()).touchControls = this;
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
    private static String layoutKey(int style) { return style == STYLE_FLEXIBLE ? "touch_layout_flexible" : "touch_layout_classic"; }
    private static String presetStyle(int style) { return style == STYLE_FLEXIBLE ? TouchPresets.FLEXIBLE : TouchPresets.CLASSIC; }
    private boolean flexible() { return style == STYLE_FLEXIBLE; }

    private TouchLayout.Placement placement(Control c) {
        TouchLayout.Placement p = layout.get(key(c));
        return p != null ? p : new TouchLayout.Placement(c.fx, c.fy, c.fr);
    }
    private void place(Control c, int w, int h, float s) {
        TouchLayout.Placement p=layout.get(key(c));
        if (p==null && !editor.editing()) {
            c.cx=c.fx*w; c.cy=c.fy*h; c.r=c.fr*s; // unchanged default path
            c.shape=TouchLayout.SHAPE_CIRCLE; c.opacity=TouchLayout.OPACITY_DEFAULT; c.filled=true;
        } else {
            if (p==null) p=new TouchLayout.Placement(c.fx,c.fy,c.fr);
            p=TouchLayout.fit(p,w,h,editor.editing() && editorBar!=null ? editorBar.getBottom():0);
            c.cx=p.x*w; c.cy=p.y*h; c.r=p.radius*s;
            c.shape=p.shape; c.opacity=p.opacity; c.filled=p.filled;
        }
        if (c.stick != null) c.stick.home(c.cx, c.cy, c.r);
    }
    private void placeControls() {
        for (Control c:controls) place(c,getWidth(),getHeight(),Math.min(getWidth(),getHeight()));
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (editor.editing() && grid) drawGrid(canvas);
        if (!editor.editing() && contextPack.startupTouch()) {
            if (contextPack.startupScene() == ContextPack.STARTUP_TITLE && !settingsOpen) {
                text.setColor(0xE6F8F4E8);
                text.setTextSize(getResources().getDisplayMetrics().scaledDensity * 16);
                canvas.drawText(getContext().getString(R.string.touch_tap_to_start), getWidth() / 2f, getHeight() * .94f, text);
            }
            return;
        }
        if (editor.editing() || active())
            for (Control c : controls) {
                if (!editor.editing() && !visible(c)) continue;
                // floating sticks: only under a thumb, and only when they are shown
                if (c.stick != null && flexible() && !editor.editing() && (!showSticks || !c.stick.held())) continue;
                drawControl(canvas, c);
            }
        if (!editor.editing() && buttonsVisible && !settingsOpen) drawControl(canvas, toggle);
        if (!editor.editing() && rings.playing(SystemClock.uptimeMillis())) postInvalidateOnAnimation();
    }

    private static int alpha(int a, int opacity) { return Math.round(a * opacity / 100f) << 24 | 0xFFFFFF; }
    private void drawControl(Canvas canvas, Control c) {
        final boolean editing = editor.editing();
        final String k = key(c);
        final int b = c.kind == KIND_TOGGLE ? ContextPack.UNCHANGED : ContextPack.buttonFor(k);
        final boolean on = (editing && c==selected) || c.pressed || (c.kind == KIND_TOGGLE && active())
                || (!editing && contextPack.held(k));
        final float x = c.stick != null ? c.stick.cx : c.cx, y = c.stick != null ? c.stick.cy : c.cy;
        final int op = c.kind == KIND_TOGGLE ? TouchLayout.OPACITY_DEFAULT : c.opacity;
        // the golden rings: the button pops a little and gold rings spread from it
        final float flash = editing || b < 0 ? -1 : rings.progress(b, SystemClock.uptimeMillis());
        final float r = c.r * GoldenRings.pop(flash);
        fill.setColor(alpha(on ? 0x90 : c.filled ? 0x38 : 0, op));
        edge.setColor(alpha(0x80, op));
        drawShape(canvas, c.kind == KIND_TOGGLE ? TouchLayout.SHAPE_CIRCLE : c.shape, x, y, r);
        if (flash >= 0) drawRings(canvas, x, y, c.r, flash);
        if (c.stick != null) {
            fill.setColor(alpha(c.pressed ? 0xB0 : 0x70, op));
            canvas.drawCircle(x + c.stick.sx * c.r * 0.6f, y + c.stick.sy * c.r * 0.6f, c.r * 0.42f, fill);
            return;
        }
        final String letter = b >= 0 ? ContextPack.letter(b, c.label, contextPack.itemNamesA()) : c.label;
        final int look = editing ? ContextPack.KIND_PLAIN : contextPack.kindFor(k);
        // Link in the pose of the action (P8), the word under him; the Jump button (P6) as the game's Jump
        if (!editing && c.kind == KIND_VIRTUAL && c.code == VIRT_JUMP) {
            final Bitmap jump = contextPack.linkPoses() ? pose(ContextAssets.JUMP_ACTION) : null;
            if (jump != null) { drawPose(canvas, jump, c.label, x, y, r, op); return; }
        }
        final Bitmap posed = look == ContextPack.KIND_ACTION && contextPack.linkPoses() ? pose(contextPack.labelFor(k)) : null;
        if (ContextAssets.showPose(contextPack.linkPoses(), look, posed != null)) {
            final String word = ContextPack.word(contextPack.labelFor(k), words);
            drawPose(canvas, posed, word != null ? word : "", x, y, r, op);
            drawBadge(canvas, letter, x, y, r, op);
            return;
        }
        boolean drawn = false;
        if (look == ContextPack.KIND_ACTION) drawn = drawWord(canvas, ContextPack.word(contextPack.labelFor(k), words), x, y, r, op);
        else if (look == ContextPack.KIND_FAST_FORWARD) drawn = drawWord(canvas, "▶▶", x, y, r, op);
        else if (look == ContextPack.KIND_ITEM) drawn = drawIcon(canvas, contextPack.labelFor(k), x, y, r, op);
        if (drawn) { drawBadge(canvas, letter, x, y, r, op); return; }
        text.setColor(alpha(0xE0, op));
        text.setTextSize(r * (letter.length() > 2 ? 0.5f : 0.9f));  // "Jump", "Moon": smaller
        canvas.drawText(letter, x, y - (text.descent() + text.ascent()) / 2, text);
    }
    // an action's word (or ▶▶) inside the button: as large as a letter, smaller to fit, on two lines when
    // one would be too small; false: no word (the letter shows)
    private boolean drawWord(Canvas canvas, String word, float x, float y, float r, int op) {
        if (word == null || word.isEmpty() || r <= 0) return false;
        final float maxWidth = r * 1.7f, minSize = r * 0.28f, readable = r * 0.42f;
        text.setColor(alpha(0xF0, op));
        text.setTextSize(1f);
        float size = ContextPack.fitTextSize(text.measureText(word), r, maxWidth, minSize);
        final String[] two = size < readable ? ContextAssets.twoLines(word) : null;
        if (two == null) {
            text.setTextSize(size);
            canvas.drawText(word, x, y - (text.descent() + text.ascent()) / 2, text);
            return true;
        }
        text.setTextSize(1f);
        final float wide = Math.max(text.measureText(two[0]), text.measureText(two[1]));
        size = Math.min(r * 0.55f, ContextPack.fitTextSize(wide, r, r * 1.6f, minSize));
        text.setTextSize(size);
        final float line = text.descent() - text.ascent();
        canvas.drawText(two[0], x, y - line * 0.5f - (text.descent() + text.ascent()) / 2, text);
        canvas.drawText(two[1], x, y + line * 0.5f - (text.descent() + text.ascent()) / 2, text);
        return true;
    }
    // an item's icon (the game's, from the cache); false: not there (the letter shows)
    private boolean drawIcon(Canvas canvas, int item, float x, float y, float r, int op) {
        final Bitmap bm = icon(item);
        if (bm == null) return false;
        final float h = r * 0.68f;
        iconRect.set(x - h, y - h, x + h, y + h);
        iconPaint.setAlpha(Math.round(255 * op / 100f));
        canvas.drawBitmap(bm, null, iconRect, iconPaint);
        return true;
    }
    // Link's pose (P8) in the upper part of the button and the action's word small under him
    private void drawPose(Canvas canvas, Bitmap bm, String word, float x, float y, float r, int op) {
        final float h = r * ContextAssets.POSE_HALF, cy = y + r * ContextAssets.POSE_DY;
        iconRect.set(x - h, cy - h, x + h, cy + h);
        iconPaint.setAlpha(Math.round(255 * op / 100f));
        canvas.drawBitmap(bm, null, iconRect, iconPaint);
        if (word == null || word.isEmpty()) return;
        text.setColor(alpha(0xF0, op));
        text.setTextSize(1f);
        text.setTextSize(Math.min(r * ContextAssets.POSE_WORD_SIZE, r * ContextAssets.POSE_WORD_WIDTH / Math.max(1e-3f, text.measureText(word))));
        canvas.drawText(word, x + r * ContextAssets.POSE_WORD_DX, y + r * ContextAssets.POSE_WORD_DY, text);
    }
    // the button's own name beside a word or icon: a small disc on its lower right edge
    private void drawBadge(Canvas canvas, String letter, float x, float y, float r, int op) {
        final float br = r * 0.3f, bx = x + r * 0.74f, by = y + r * 0.74f;
        final int fillWas = fill.getColor();
        fill.setColor(Math.round(0xC0 * op / 100f) << 24 | 0x101820);
        canvas.drawCircle(bx, by, br, fill);
        fill.setColor(fillWas);
        text.setColor(alpha(0xF0, op));
        text.setTextSize(br * (letter.length() > 1 ? 1.0f : 1.3f));
        canvas.drawText(letter, bx, by - (text.descent() + text.ascent()) / 2, text);
    }
    private void drawRings(Canvas canvas, float x, float y, float r, float f) {
        ringPaint.setStyle(Paint.Style.STROKE);
        for (int k = 0; k < GoldenRings.RINGS; k++) {
            final float t = GoldenRings.ringT(k, f);
            if (t < 0) continue;
            ringPaint.setColor(GoldenRings.GOLD);
            ringPaint.setAlpha(GoldenRings.alpha(t));
            ringPaint.setStrokeWidth(r * GoldenRings.stroke(t));
            canvas.drawCircle(x, y, r * GoldenRings.radius(t), ringPaint);
        }
    }
    private void drawShape(Canvas canvas, int shape, float x, float y, float r) {
        if (shape == TouchLayout.SHAPE_CIRCLE) {
            canvas.drawCircle(x, y, r, fill);
            canvas.drawCircle(x, y, r, edge);
            return;
        }
        shapePath.reset();
        if (shape == TouchLayout.SHAPE_ROUNDED || shape == TouchLayout.SHAPE_SQUARE) {
            final float h = r * 0.89f, corner = shape == TouchLayout.SHAPE_ROUNDED ? r * 0.35f : r * 0.08f;
            shapeRect.set(x - h, y - h, x + h, y + h);
            shapePath.addRoundRect(shapeRect, corner, corner, Path.Direction.CW);
        } else {
            final int n = shape == TouchLayout.SHAPE_DIAMOND ? 4 : shape == TouchLayout.SHAPE_HEXAGON ? 6 : 8;
            final double start = shape == TouchLayout.SHAPE_OCTAGON ? Math.PI / 8 : shape == TouchLayout.SHAPE_HEXAGON ? 0 : -Math.PI / 2;
            final float rr = shape == TouchLayout.SHAPE_DIAMOND ? r * 1.1f : r;
            for (int i = 0; i < n; i++) {
                double a = start + i * 2 * Math.PI / n;
                float px = x + rr * (float) Math.cos(a), py = y + rr * (float) Math.sin(a);
                if (i == 0) shapePath.moveTo(px, py); else shapePath.lineTo(px, py);
            }
            shapePath.close();
        }
        canvas.drawPath(shapePath, fill);
        canvas.drawPath(shapePath, edge);
    }
    // the editor's mirrored grid: lines through the screen's centre, so places mirror left / right
    private float gridCell() { return Math.min(getWidth(), getHeight()) / 14f; }
    private void drawGrid(Canvas canvas) {
        final float g = gridCell(), w = getWidth(), h = getHeight();
        if (g < 4) return;
        gridPaint.setStrokeWidth(Math.max(1f, getResources().getDisplayMetrics().density * 0.75f));
        final int nx = (int) Math.ceil(w / 2 / g), ny = (int) Math.ceil(h / 2 / g);
        for (int i = -nx; i <= nx; i++) {
            gridPaint.setColor(i == 0 ? 0x60FFFFFF : (i & 1) == 0 ? 0x38FFFFFF : 0x18FFFFFF);
            canvas.drawLine(w / 2 + i * g, 0, w / 2 + i * g, h, gridPaint);
        }
        for (int i = -ny; i <= ny; i++) {
            gridPaint.setColor(i == 0 ? 0x60FFFFFF : (i & 1) == 0 ? 0x38FFFFFF : 0x18FFFFFF);
            canvas.drawLine(0, h / 2 + i * g, w, h / 2 + i * g, gridPaint);
        }
    }

    private Control hit(float x, float y) {
        if (buttonsVisible && !settingsOpen && near(toggle, x, y, 1.3f)) return toggle;
        if (!active()) return null;
        Control best = null;
        float bestD = Float.MAX_VALUE;
        for (Control c : controls) {
            if (!visible(c)) continue;
            if (c.stick != null && flexible()) continue;  // floating: no fixed place to hit
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

    // a finger on no control: the game's, a floating stick, the swipe camera or the talk (touch_style.h; in
    // Classic native code answers the game or the talk)
    private Control freeTouch(float x, float y) {
        if (!active()) return null;
        int free;
        try {
            free = nativeFreeTouch(getWidth() > 1 ? x / (getWidth() - 1) : 0.5f, getHeight() > 1 ? y / (getHeight() - 1) : 0.5f);
        } catch (UnsatisfiedLinkError e) { return null; }
        switch (free) {
            case FREE_LEFT: return leftStick.stick.held() ? dead : leftStick;  // one thumb per stick, others are ignored
            case FREE_RIGHT: return rightStick.stick.held() ? dead : rightStick;
            case FREE_SWIPE: return swipePointer >= 0 ? dead : swipe;
            case FREE_TALK: return talkPointer >= 0 ? dead : talk;
            default: return null;
        }
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
        if (settingsOpen) { startup.cancel(); editor.pointers(0);return false; }
        if (action == MotionEvent.ACTION_DOWN) syncContext(SystemClock.uptimeMillis());
        if (startup.tracking()) {
            final int i = event.getActionIndex(), id = event.getPointerId(i);
            final float slop = android.view.ViewConfiguration.get(getContext()).getScaledTouchSlop();
            if (action == MotionEvent.ACTION_MOVE) {
                for (int k = 0; k < event.getPointerCount(); k++)
                    startup.move(event.getPointerId(k), event.getX(k), event.getY(k), slop);
            } else if (action == MotionEvent.ACTION_UP) {
                if (startup.up(contextPack.startupScene(), id, event.getX(i), event.getY(i), slop))
                    try { nativeStartupTitleTap(); } catch (UnsatisfiedLinkError ignored) { }
            } else if (action == MotionEvent.ACTION_CANCEL || action == MotionEvent.ACTION_POINTER_DOWN || action == MotionEvent.ACTION_POINTER_UP) {
                startup.cancel();
            }
            editor.pointers(0); return true;
        }
        if (contextPack.startupTouch()) {
            editor.pointers(0);
            if (action == MotionEvent.ACTION_DOWN && contextPack.startupScene() == ContextPack.STARTUP_TITLE) {
                startup.down(event.getPointerId(0), event.getX(), event.getY());
                return true;
            }
            return false;  // SDLSurface -> real GamePad touchscreen, including native text entry.
        }
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
                // a new gesture: no finger is down, so anything still held lost its up event
                if (action == MotionEvent.ACTION_DOWN && owner.size() > 0) releaseFingers();
                if (action==MotionEvent.ACTION_POINTER_DOWN) removeCallbacks(editLongPress);
                final int id = event.getPointerId(index);
                final float x = event.getX(index), y = event.getY(index);
                Control c = hit(x, y);
                final boolean free = c == null;
                if (c == null) c = freeTouch(x, y);
                if (free && c == talk && talkPointer >= 0 && startModPinch(event, id, x, y)) c = pinch;
                else if (free && c != null && c != talk && startPinch(event, id, x, y)) c = pinch;
                else if (free && c != null && c != talk) freeDown.put(id, SystemClock.uptimeMillis());
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
                } else if (c == swipe) {
                    swipePointer = id; swipeX = x; swipeY = y;
                } else if (c == talk) {
                    talkPointer = id;
                    talkFinger(0, x, y);
                } else if (c == pinch || c == dead) {
                    // (a pinch's finger, or one more thumb on a half that already has its stick)
                } else if (c != dead) {
                    c.pressed = true;
                    if (c.stick != null) c.stick.press(x, y, flexible());
                    c.downX = x; c.downY = y;
                    itemRingFinger(c, true, x, y);
                }
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < event.getPointerCount(); i++) {
                    final int id = event.getPointerId(i);
                    if (owner.indexOfKey(id) < 0) continue;
                    Control c = owner.get(id);
                    final float x = event.getX(i), y = event.getY(i);
                    if (c == null) forward(event, i, MotionEvent.ACTION_MOVE);
                    else if (c == talk) { if (modPinch) pinchMove(event, 1); else talkFinger(1, x, y); }
                    else if (c == pinch) pinchMove(event, 1);
                    else if (c == swipe) {
                        if (x != swipeX || y != swipeY) {
                            try { nativeSwipe(x - swipeX, y - swipeY, screenDpi); } catch (UnsatisfiedLinkError ignored) {}
                            swipeX = x; swipeY = y;
                        }
                    }
                    else if (c.stick != null) c.stick.move(x, y, follow && c == leftStick);
                    else if (itemButton(c) >= 0 && c.pressed) itemRingFinger(c, true, x, y);
                    else if (c == toggle && !near(c,x,y,1.3f)) removeCallbacks(editLongPress);
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
                    if (c == swipe) { swipePointer = -1; continue; }
                    freeDown.remove(id);
                    if (c == talk) {
                        if (modPinch) endPinch(event);
                        talkPointer = -1;
                        talkFinger(all ? 3 : 2, event.getX(i), event.getY(i));
                        continue;
                    }
                    if (c == pinch) { endPinch(event); continue; }
                    c.pressed = false;
                    if (c.stick != null) c.stick.release();
                    itemRingFinger(c, false, c.downX, c.downY);
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

    // the item buttons: the drawn X and Y, and R (-1: another control)
    private static int itemButton(Control c) {
        if (c.kind == KIND_FACE && c.code == FACE_X) return 0;
        if (c.kind == KIND_FACE && c.code == FACE_Y) return 1;
        if (c.kind == KIND_BUTTON && c.code == RIGHT_SHOULDER) return 2;
        return -1;
    }

    private static void itemRingFinger(Control c, boolean down, float x, float y) {
        final int b = itemButton(c);
        if (b < 0 || c.r <= 0) return;
        try { nativeItemRingFinger(b, down, (x - c.downX) / c.r, (y - c.downY) / c.r); }
        catch (UnsatisfiedLinkError ignored) { /* not loaded yet */ }
    }

    // every control lets go: buttons up, sticks back to rest, no swipe (the pad itself is not touched)
    private void resetControls() {
        for (Control c : controls) { c.pressed = false; if (c.stick != null) c.stick.release(); }
        swipePointer = -1;
        if (talkPointer >= 0) { talkPointer = -1; talkFinger(3, 0, 0); }
        if (pinchA >= 0) {
            try { if (modPinch) nativeModPinch(2, 1, 0.5f, 0.5f); else nativePinch(2, pinchD); } catch (UnsatisfiedLinkError ignored) {}
            pinchA = pinchB = -1;
            modPinch = false;
        }
        freeDown.clear();
    }
    // a second free finger soon after the first: both become a pinch (the first one's stick lets go)
    private float pinchD;
    private boolean startPinch(MotionEvent event, int id, float x, float y) {
        if (!flexible() || pinchA >= 0 || freeDown.size() != 1) return false;
        final int other = freeDown.keyAt(0);
        if (SystemClock.uptimeMillis() - freeDown.valueAt(0) > PINCH_MS) return false;
        final int oi = event.findPointerIndex(other);
        if (oi < 0) return false;
        final float d = (float) Math.hypot(event.getX(oi) - x, event.getY(oi) - y);
        try { if (nativePinch(0, d) == 0) return false; } catch (UnsatisfiedLinkError e) { return false; }
        final Control first = owner.get(other);
        if (first != null && first.stick != null) { first.pressed = false; first.stick.release(); }
        if (first == swipe) swipePointer = -1;
        owner.put(other, pinch);
        freeDown.clear();
        pinchA = other; pinchB = id; pinchD = d;
        return true;
    }
    private void pinchMove(MotionEvent event, int phase) {
        final int a = event.findPointerIndex(pinchA), b = event.findPointerIndex(pinchB);
        if (a < 0 || b < 0) return;
        pinchD = (float) Math.hypot(event.getX(a) - event.getX(b), event.getY(a) - event.getY(b));
        try {
            if (modPinch) nativeModPinch(phase, pinchD / pinchD0, midX(event, a, b), midY(event, a, b));
            else nativePinch(phase, pinchD);
        } catch (UnsatisfiedLinkError ignored) {}
    }
    // a second finger while a code mod has the talk finger (its claim): a pinch for that mod; the talk finger stays the
    // talk's (its moves pause during the pinch, its lift ends the pinch and then goes to the mod)
    private boolean modPinch;
    private float pinchD0 = 1;
    private boolean startModPinch(MotionEvent event, int id, float x, float y) {
        if (pinchA >= 0) return false;
        final int ti = event.findPointerIndex(talkPointer);
        if (ti < 0) return false;
        final float d = Math.max(1, (float) Math.hypot(event.getX(ti) - x, event.getY(ti) - y));
        final float mx = (event.getX(ti) + x) * 0.5f, my = (event.getY(ti) + y) * 0.5f;
        try {
            if (!nativeModPinch(0, 1, getWidth() > 1 ? mx / (getWidth() - 1) : 0.5f, getHeight() > 1 ? my / (getHeight() - 1) : 0.5f)) return false;
        } catch (UnsatisfiedLinkError e) { return false; }
        pinchA = talkPointer; pinchB = id; pinchD = pinchD0 = d;
        modPinch = true;
        return true;
    }
    private float midX(MotionEvent e, int a, int b) { return getWidth() > 1 ? (e.getX(a) + e.getX(b)) * 0.5f / (getWidth() - 1) : 0.5f; }
    private float midY(MotionEvent e, int a, int b) { return getHeight() > 1 ? (e.getY(a) + e.getY(b)) * 0.5f / (getHeight() - 1) : 0.5f; }
    // either finger lifted: the pinch ends there, the other finger does nothing until it lifts
    private void endPinch(MotionEvent event) {
        if (pinchA < 0) return;
        pinchMove(event, 2);
        if (modPinch) {  // (the talk finger stays the talk's; the second does nothing until it lifts)
            if (owner.indexOfKey(pinchB) >= 0) owner.put(pinchB, dead);
            pinchA = pinchB = -1;
            modPinch = false;
            return;
        }
        final int rest = owner.indexOfKey(pinchA) >= 0 && owner.get(pinchA) == pinch ? pinchA : pinchB;
        if (owner.indexOfKey(rest) >= 0) owner.put(rest, dead);
        pinchA = pinchB = -1;
    }
    private void talkFinger(int action, float x, float y) {
        try {
            nativeTalkFinger(action, getWidth() > 1 ? x / (getWidth() - 1) : 0.5f, getHeight() > 1 ? y / (getHeight() - 1) : 0.5f);
        } catch (UnsatisfiedLinkError ignored) { /* not loaded yet */ }
    }
    // fingers that never got their up event: the controls let go, the game's touches are cancelled
    private void releaseFingers() {
        startup.cancel();
        removeCallbacks(editLongPress);
        try { nativeItemRingFinger(-1, false, 0, 0); } catch (UnsatisfiedLinkError ignored) {}
        resetControls();
        toggle.pressed = false;
        for (int i=0;i<owner.size();++i) if (owner.valueAt(i)==null)
            try { SDLActivity.onNativeTouch(lastTouchDevice,owner.keyAt(i),MotionEvent.ACTION_CANCEL,0,0,0); }
            catch (UnsatisfiedLinkError ignored) {}
        owner.clear();
    }
    private void releaseInputs() {
        releaseFingers();
        try { nativeDetachPad(); nativeSetVirtual(0); } catch (UnsatisfiedLinkError ignored) {}
        lastButtons=-1; lastVirtual=-1;
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
        editorStatus=new TextView(getContext());
        editorStatus.setTextColor(0xFFFFE08A);editorStatus.setGravity(android.view.Gravity.CENTER);
        editorBar.addView(editorStatus);
        LinearLayout look=new LinearLayout(getContext());
        editorButton(look,R.string.touch_layout_smaller,()->resizeSelected(-1));
        editorButton(look,R.string.touch_layout_larger,()->resizeSelected(1));
        editorButton(look,R.string.touch_layout_shape,()->restyleSelected(p->p.look((p.shape+1)%TouchLayout.SHAPES,p.opacity,p.filled)));
        editorButton(look,R.string.touch_layout_opacity,()->restyleSelected(p->p.look(p.shape,TouchLayout.nextOpacity(p.opacity),p.filled)));
        editorButton(look,R.string.touch_layout_fill,()->restyleSelected(p->p.look(p.shape,p.opacity,!p.filled)));
        editorBar.addView(look);
        LinearLayout actions=new LinearLayout(getContext());
        editorButton(actions,R.string.touch_layout_grid,()->{ grid=!grid;invalidate(); });
        editorButton(actions,R.string.touch_layout_presets,this::layoutsMenu);
        editorButton(actions,R.string.touch_layout_reset,()->{ layout.reset();selected=null;saveLayout();placeControls();showStatus(); });
        editorButton(actions,R.string.touch_layout_done,this::endEditing);
        editorBar.addView(actions);
        RelativeLayout.LayoutParams lp=new RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.addRule(RelativeLayout.ALIGN_PARENT_TOP);
        ((RelativeLayout)getParent()).addView(editorBar,lp);
        editorBar.post(this::placeControls);
        placeControls();showStatus();
        announceForAccessibility(getContext().getString(R.string.touch_layout_hint));
    }
    private void editorButton(LinearLayout bar,int label,Runnable action) {
        Button b=new Button(getContext());b.setText(label);b.setOnClickListener(v->action.run());
        b.setMinHeight((int)(48*getResources().getDisplayMetrics().density));
        b.setAllCaps(false);
        bar.addView(b,new LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.WRAP_CONTENT,1));
    }
    // the editor's second line: the style's layout, or the selected control's size and look
    private void showStatus() {
        if (editorStatus==null) return;
        if (selected==null) {
            editorStatus.setText(flexible() ? R.string.touch_layout_flexible : R.string.touch_layout_classic);
            return;
        }
        TouchLayout.Placement p=placement(selected);
        String name=selected==leftStick ? getContext().getString(R.string.touch_layout_left_stick)
                : selected==rightStick ? getContext().getString(R.string.touch_layout_right_stick)
                : ContextPack.letter(ContextPack.buttonFor(key(selected)),selected.label,contextPack.itemNamesA());
        String[] shapes=getResources().getStringArray(R.array.touch_layout_shapes);
        String status=getContext().getString(R.string.touch_layout_status,name,
                TouchLayout.sizePercent(p.radius,selected.fr),shapes[Math.max(0,Math.min(shapes.length-1,p.shape))],p.opacity,
                getContext().getString(p.filled ? R.string.touch_layout_filled : R.string.touch_layout_outline));
        // with context buttons on, say when this control shows in play
        final int b=ContextPack.buttonFor(key(selected));
        if (contextPack.enabled() && b!=ContextPack.UNCHANGED)
            status+="\n"+getContext().getString(b==ContextPack.HIDDEN ? R.string.touch_layout_context_hidden
                    : R.string.touch_layout_context_shows);
        editorStatus.setText(status);
    }
    private void endEditing() {
        if (!editor.editing()) return;
        saveLayout();editor.setEditing(false,this::releaseInputs);selected=null;dragPointer=-1;
        if (editorBar!=null && editorBar.getParent() instanceof ViewGroup)
            ((ViewGroup)editorBar.getParent()).removeView(editorBar);
        editorBar=null;editorStatus=null;placeControls();
    }
    private void saveLayout() { prefs.edit().putString(layoutKey(style),layout.encode()).apply(); }
    private void savePresets() { prefs.edit().putString("touch_layout_presets",presets.toJson(null)).apply(); }
    private void moveSelected(float x,float y,float radius) {
        int w=getWidth(),h=getHeight();if (selected==null || w<=0 || h<=0) return;
        TouchLayout.Placement p=TouchLayout.fit(placement(selected).at(x/w,y/h,radius/Math.min(w,h)),
                w,h,editorBar!=null ? editorBar.getBottom():0);
        if (layout.put(key(selected),p)) placeControls();
    }
    private void resizeSelected(int steps) {
        if (selected==null) return;
        TouchLayout.Placement p=placement(selected);
        layout.put(key(selected),p.at(p.x,p.y,TouchLayout.resize(p.radius,selected.fr,steps)));
        saveLayout();placeControls();showStatus();
    }
    private interface Restyle { TouchLayout.Placement apply(TouchLayout.Placement p); }
    private void restyleSelected(Restyle change) {
        if (selected==null) return;
        layout.put(key(selected),change.apply(placement(selected)));
        saveLayout();placeControls();showStatus();
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
            showStatus();
        } else if (action==MotionEvent.ACTION_MOVE && selected!=null && dragPointer>=0) {
            int i=e.findPointerIndex(dragPointer);
            if (i>=0) {
                float x=e.getX(i)-dragOffsetX,y=e.getY(i)-dragOffsetY;
                if (grid) { x=TouchLayout.snap(x,getWidth()/2f,gridCell());y=TouchLayout.snap(y,getHeight()/2f,gridCell()); }
                moveSelected(x,y,selected.r);
            }
        } else if (action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_CANCEL ||
                (action==MotionEvent.ACTION_POINTER_UP && e.getPointerId(e.getActionIndex())==dragPointer)) {
            dragPointer=-1;saveLayout();
        }
        invalidate();return true;
    }
    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) return;
        if (!dialogOpen) endEditing();  // the layouts dialog over the editor keeps it open
        releaseInputs();editor.pointers(0);
    }

    // ---- saved layouts, the Arc layout, export and import
    private void applyLayout(String text) {
        if (text==null) layout.reset(); else layout.decode(text);
        selected=null;saveLayout();placeControls();showStatus();
    }
    private void dialog(AlertDialog.Builder b) {
        dialogOpen=true;
        AlertDialog d=b.create();
        d.setOnDismissListener(x->dialogOpen=false);
        d.show();
    }
    private void layoutsMenu() {
        final String st=presetStyle(style);
        final List<String> names=presets.names(st);
        final List<String> items=new ArrayList<>();
        items.add(getContext().getString(R.string.touch_layout_default));
        items.add(getContext().getString(R.string.touch_layout_arc));
        items.addAll(names);
        items.add(getContext().getString(R.string.touch_layout_save));
        if (!names.isEmpty()) items.add(getContext().getString(R.string.touch_layout_delete));
        items.add(getContext().getString(R.string.touch_layout_export));
        items.add(getContext().getString(R.string.touch_layout_import));
        dialog(new AlertDialog.Builder(getContext())
                .setTitle(flexible() ? R.string.touch_layout_flexible_title : R.string.touch_layout_classic)
                .setItems(items.toArray(new String[0]),(d,which)->{
                    if (which==0) applyLayout(null);
                    else if (which==1) applyLayout(TouchPresets.arc(getWidth(),getHeight(),baseRadius));
                    else if (which<2+names.size()) applyLayout(presets.get(st,names.get(which-2)));
                    else {
                        String item=items.get(which);
                        if (item.equals(getContext().getString(R.string.touch_layout_save))) saveAsDialog(st);
                        else if (item.equals(getContext().getString(R.string.touch_layout_delete))) deleteDialog(st,names);
                        else if (item.equals(getContext().getString(R.string.touch_layout_export))) pickFile(true);
                        else pickFile(false);
                    }
                }));
    }
    private void saveAsDialog(String st) {
        final EditText name=new EditText(getContext());
        name.setSingleLine(true);
        name.setFilters(new android.text.InputFilter[] {new android.text.InputFilter.LengthFilter(TouchPresets.MAX_NAME)});
        dialog(new AlertDialog.Builder(getContext()).setTitle(R.string.touch_layout_name).setView(name)
                .setPositiveButton(android.R.string.ok,(d,w)->{
                    boolean ok=presets.save(st,name.getText().toString(),layout.encode());
                    if (ok) savePresets();
                    Toast.makeText(getContext(),ok ? R.string.touch_layout_saved : R.string.touch_layout_save_failed,Toast.LENGTH_SHORT).show();
                })
                .setNegativeButton(android.R.string.cancel,null));
    }
    private void deleteDialog(String st,List<String> names) {
        dialog(new AlertDialog.Builder(getContext()).setTitle(R.string.touch_layout_delete)
                .setItems(names.toArray(new String[0]),(d,which)->{ presets.remove(st,names.get(which));savePresets(); }));
    }
    private void pickFile(boolean export) {
        if (!(getContext() instanceof Activity)) return;
        Intent i=new Intent(export ? Intent.ACTION_CREATE_DOCUMENT : Intent.ACTION_OPEN_DOCUMENT)
                .addCategory(Intent.CATEGORY_OPENABLE).setType(export ? "application/json" : "*/*");
        if (export) i.putExtra(Intent.EXTRA_TITLE,"wwhd-touch-layouts.json");
        try { ((Activity)getContext()).startActivityForResult(i,export ? EXPORT_REQUEST : IMPORT_REQUEST); }
        catch (android.content.ActivityNotFoundException e) {
            Toast.makeText(getContext(),R.string.touch_layout_file_failed,Toast.LENGTH_SHORT).show();
        }
    }
    /** WwhdActivity.onActivityResult: the file the player picked (null: none) */
    void layoutFile(int request,Uri uri) {
        if (uri==null) return;
        try {
            if (request==EXPORT_REQUEST) {
                saveLayout();
                Map<String,String> current=new LinkedHashMap<>();
                current.put(TouchPresets.CLASSIC,prefs.getString(layoutKey(STYLE_CLASSIC),"2;"));
                current.put(TouchPresets.FLEXIBLE,prefs.getString(layoutKey(STYLE_FLEXIBLE),"2;"));
                try (OutputStream out=getContext().getContentResolver().openOutputStream(uri,"wt")) {
                    if (out==null) throw new java.io.IOException("no output");
                    out.write(presets.toJson(current).getBytes(StandardCharsets.UTF_8));
                }
                Toast.makeText(getContext(),R.string.touch_layout_exported,Toast.LENGTH_SHORT).show();
                return;
            }
            ByteArrayOutputStream bytes=new ByteArrayOutputStream();
            try (InputStream in=getContext().getContentResolver().openInputStream(uri)) {
                if (in==null) throw new java.io.IOException("no input");
                byte[] buf=new byte[8192];int n;
                while ((n=in.read(buf))>0) {
                    bytes.write(buf,0,n);
                    if (bytes.size()>TouchPresets.MAX_FILE) throw new java.io.IOException("too large");
                }
            }
            Map<String,String> current=new HashMap<>();
            TouchPresets incoming=new TouchPresets();
            int taken=incoming.fromJson(new String(bytes.toByteArray(),StandardCharsets.UTF_8),current);
            if (taken<0) { Toast.makeText(getContext(),R.string.touch_layout_import_failed,Toast.LENGTH_LONG).show();return; }
            presets.fromJson(incoming.toJson(null),null);
            // the file's current layouts replace ours; ours stay as a saved layout "Before import"
            SharedPreferences.Editor ed=prefs.edit();
            for (int s : new int[] {STYLE_CLASSIC,STYLE_FLEXIBLE}) {
                String text=current.get(presetStyle(s));
                if (text==null) continue;
                String mine=prefs.getString(layoutKey(s),"");
                if (TouchLayout.valid(mine) && !mine.equals(text))
                    presets.save(presetStyle(s),getContext().getString(R.string.touch_layout_before_import),mine);
                ed.putString(layoutKey(s),text);
            }
            ed.apply();
            savePresets();
            layout.decode(prefs.getString(layoutKey(style),""));
            selected=null;placeControls();showStatus();
            Toast.makeText(getContext(),getContext().getString(R.string.touch_layout_imported,taken),Toast.LENGTH_SHORT).show();
        } catch (Exception e) {
            Toast.makeText(getContext(),R.string.touch_layout_file_failed,Toast.LENGTH_LONG).show();
        }
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
        resetControls();
        for (int i = owner.size() - 1; i >= 0; i--) if (owner.valueAt(i) != null && owner.valueAt(i) != toggle) owner.removeAt(i);
        nativeShowControls(on);
        if (!on) { nativeDetachPad(); nativeSetVirtual(0); lastButtons = -1; lastVirtual = -1; }
    }

    // shown, and not hidden for a connected controller
    private boolean active() { return !editor.editing() && shown && buttonsVisible && !settingsOpen; }

    // the touch options (touch_style.h pack): a style change swaps the layout and lets every finger go
    private void pollOptions() {
        try {
            if (nativeTouchOptions(options) < 1) return;
        } catch (UnsatisfiedLinkError e) { return; }  // a game library from before the touch options: as seen last
        final int newStyle = options[1] == STYLE_FLEXIBLE ? STYLE_FLEXIBLE : STYLE_CLASSIC;
        showSticks = options[2] != 0;
        follow = options[3] != 0;
        final int newRight = options[4] == RIGHT_STICK ? RIGHT_STICK : RIGHT_SWIPE;
        if (newStyle == style && newRight == rightMode) return;
        if (newStyle != style && editor.editing()) endEditing();
        releaseFingers();
        try { nativeDetachPad(); nativeSetVirtual(0); } catch (UnsatisfiedLinkError ignored) {}
        lastButtons = -1; lastVirtual = -1;
        if (newStyle != style) {
            style = newStyle;
            prefs.edit().putInt("touch_style_seen", style).apply();  // the next start draws the right layout at once
            layout.decode(prefs.getString(layoutKey(style), ""));
            placeControls();
        }
        rightMode = newRight;
    }

    private void syncTouchState() {
        try {
            android.util.DisplayMetrics metrics = getResources().getDisplayMetrics();
            float dpi = Math.min(metrics.xdpi, metrics.ydpi);
            if (!Float.isFinite(dpi) || dpi < 72 || dpi > 1000) dpi = metrics.densityDpi;
            screenDpi = dpi;
            nativeScreenDpi(dpi);
            nativeControllerConnected(controller);
            pollOptions();
            final int state = nativeTouchState();
            final boolean visible = (state & 1) != 0, controlsShown = (state & 2) != 0, open = (state & 4) != 0;
            if (buttonsVisible == visible && shown == controlsShown && settingsOpen == open) return;
            buttonsVisible = visible; shown = controlsShown; settingsOpen = open;
            if (open && editor.editing()) endEditing();
            resetControls();
            for (int i = owner.size() - 1; i >= 0; --i) {
                if (owner.valueAt(i) != null) owner.removeAt(i);
                else if (settingsOpen) {
                    SDLActivity.onNativeTouch(lastTouchDevice, owner.keyAt(i), MotionEvent.ACTION_CANCEL, 0, 0, 0);
                    owner.removeAt(i);
                }
            }
            nativeDetachPad(); nativeSetVirtual(0); lastButtons = -1; lastVirtual = -1;
            if (active()) update();
        } catch (UnsatisfiedLinkError ignored) { /* SDL has not loaded the native library yet. */ }
    }

    // a Jump / Moon jump button shows only while its mod is on and it does something (native rules)
    // ... and a context button only while it does something (ContextPack: native rules)
    private boolean visible(Control c) {
        if (c.kind == KIND_VIRTUAL) return (jumpShown & c.code) != 0;
        if (c.pressed && ringOpen && itemButton(c) >= 0) return true;  // its finger picks in the item ring
        return c.kind == KIND_TOGGLE || contextPack.visible(key(c));
    }

    // the context buttons (P3): poll the pack, play the golden rings, let go of a control that hid under a
    // finger, and load the game's words and icons once native code has exported them
    private void syncContext(long now) {
        int version;
        try { version = nativeContextButtons(contextRaw); }
        catch (UnsatisfiedLinkError e) { return; }  // not loaded yet, or a game library from before P3
        boolean changed = contextPack.set(contextRaw, version);
        if (contextPack.on()) {
            boolean open;
            try { open = nativeItemRingOpen(); } catch (UnsatisfiedLinkError e) { open = false; }
            if (open != ringOpen) { ringOpen = open; changed = true; }
        } else if (ringOpen) { ringOpen = false; changed = true; }
        if (!contextPack.valid()) rings.forget();
        else for (int b = 0; b < ContextPack.COUNT; b++)
            if (rings.note(b, contextPack.ring(b), active() && contextPack.on() && contextPack.shown(b), now)) invalidate();
        if (contextPack.enabled()) syncAssets(now);
        if (!changed) return;
        boolean released = false;
        for (Control c : controls) {
            if (visible(c)) continue;
            rings.stop(ContextPack.buttonFor(key(c)));
            if (!c.pressed) continue;
            c.pressed = false;  // as the up event would, and its finger now does nothing
            itemRingFinger(c, false, c.downX, c.downY);
            for (int i = 0; i < owner.size(); i++) if (owner.valueAt(i) == c) owner.setValueAt(i, dead);
            released = true;
        }
        if (released) update();
        invalidate();
    }

    private void syncAssets(long now) {
        final int generation = contextPack.assetsGeneration();
        if (!assets.shouldAsk(now, generation)) return;
        if (assetDir == null) assetDir = new File(getContext().getCacheDir(), "context");
        int r;
        try { r = nativeContextAssets(assetDir.getPath()); }
        catch (UnsatisfiedLinkError e) { r = -1; }
        if (assets.result(r, now, generation)) loadAssets();
    }
    // a new generation of the exported files: the words now, the icons when first drawn
    private void loadAssets() {
        icons.clear();
        poses.clear();
        Map<Integer, String> loaded = new HashMap<>();
        File f = new File(assetDir, ContextAssets.ACTIONS);
        if (f.isFile() && f.length() <= ContextAssets.MAX_TEXT) {
            try (InputStream in = new FileInputStream(f)) {
                loaded = ContextAssets.parseActions(new String(in.readAllBytes(), StandardCharsets.UTF_8));
            } catch (java.io.IOException ignored) { /* English words */ }
        }
        words = loaded;
        invalidate();
    }
    private Bitmap icon(int item) {
        if (assetDir == null || assets.generation() <= 0) return null;
        if (icons.containsKey(item)) return icons.get(item);
        File f = new File(assetDir, ContextAssets.itemFile(item));
        Bitmap bm = null;
        try { if (f.isFile()) bm = BitmapFactory.decodeFile(f.getPath()); }
        catch (RuntimeException | OutOfMemoryError ignored) { /* the letter shows */ }
        icons.put(item, bm);
        return bm;
    }

    // Link's pose for an action code (P8), from the exported pose_HH.png; null: none (the word shows)
    private Bitmap pose(int action) {
        if (assetDir == null || assets.generation() <= 0) return null;
        if (poses.containsKey(action)) return poses.get(action);
        File f = new File(assetDir, ContextAssets.poseFile(action));
        Bitmap bm = null;
        try { if (f.isFile()) bm = BitmapFactory.decodeFile(f.getPath()); }
        catch (RuntimeException | OutOfMemoryError ignored) { /* the word shows */ }
        poses.put(action, bm);
        return bm;
    }

    private void syncJumpButtons() {
        int shown;
        try { shown = nativeJumpButtons(); } catch (UnsatisfiedLinkError e) { return; }
        if (shown == jumpShown) return;
        jumpShown = shown;
        boolean released = false;
        for (Control c : controls)
            if (c.kind == KIND_VIRTUAL && !visible(c) && c.pressed) { c.pressed = false; released = true; }
        if (released) update();
    }

    // each real game controller once per session: native code offers it the setup on its first press
    private void announceControllers() {
        if (inputs == null) return;
        for (int id : inputs.getInputDeviceIds()) {
            InputDevice d = InputDevice.getDevice(id);
            if (d != null && hintPolicy.claim(d.getDescriptor(), isController(d)))
                try { nativeControllerSetupOffer(); } catch (UnsatisfiedLinkError ignored) { return; }
        }
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
        resetControls();
        for (int i = owner.size() - 1; i >= 0; i--) if (owner.valueAt(i) != null && owner.valueAt(i) != toggle) owner.removeAt(i);
        if (shown) { nativeDetachPad(); nativeSetVirtual(0); lastButtons = -1; lastVirtual = -1; }
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
        int buttons = 0, face = 0, virt = 0;
        float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
        for (Control c : controls) {
            if (c.kind == KIND_VIRTUAL && c.pressed && visible(c)) virt |= c.code;
            else if (c.kind == KIND_BUTTON && c.pressed) buttons |= 1 << c.code;
            else if (c.kind == KIND_FACE && c.pressed) face |= 1 << c.code;
            else if (c.kind == KIND_TRIGGER && c.pressed) { if (c.code == 0) lt = 1; else rt = 1; }
        }
        if (virt != lastVirtual) {
            try { nativeSetVirtual(virt); lastVirtual = virt; } catch (UnsatisfiedLinkError e) { lastVirtual = -1; }
        }
        lx = leftStick.stick.sx; ly = leftStick.stick.sy;
        rx = rightStick.stick.sx; ry = rightStick.stick.sy;
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
