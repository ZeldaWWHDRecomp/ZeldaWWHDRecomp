package org.wwhdrecomp.wwhd;

import java.util.HashMap;
import java.util.Map;

/** The context buttons' Java side (controls plan P3): reading the native pack (context_touch.h), which
 *  controls show and how, the golden rings' timing, and the exported words / icons bookkeeping. */
public final class ContextButtonsTest {
    static void check(boolean ok, String what) { if (!ok) throw new AssertionError(what); }
    static boolean near(float a, float b) { return Math.abs(a - b) < 1e-4f; }

    // a pack as context_touch.h's pack() lays it out
    static int[] pack(int flags, int generation) {
        int[] v = new int[ContextPack.LEN];
        v[0] = ContextPack.VERSION; v[1] = flags; v[2] = generation;
        return v;
    }
    static void button(int[] v, int b, boolean shown, int kind, boolean held, int label, int ring) {
        v[4 + 3 * b] = (shown ? ContextPack.SHOWN : 0) | kind << ContextPack.KIND_SHIFT | (held ? ContextPack.HELD : 0);
        v[5 + 3 * b] = label;
        v[6 + 3 * b] = ring;
    }

    static void packLayout() {
        // the last per-button slot (LEFT_RIGHT's ring) is the pack's last int: 4 + 3 * 11 + 3 = 40
        check(4 + 3 * ContextPack.COUNT + 3 == ContextPack.LEN, "length matches context_touch.h kPackLen");
        int[] v = pack(ContextPack.ENABLED | ContextPack.ACTIVE, 3);
        button(v, ContextPack.A, true, ContextPack.KIND_ACTION, false, 0x04, 7);
        button(v, ContextPack.X, true, ContextPack.KIND_ITEM, true, 0x27, 0);
        button(v, ContextPack.LEFT_RIGHT, true, ContextPack.KIND_PLAIN, false, 0, 9);
        ContextPack p = new ContextPack();
        check(p.set(v, 1) && p.valid() && p.on() && p.assetsGeneration() == 3, "valid pack");
        check(!p.set(v, 1), "same pack: no change");
        check(p.shown(ContextPack.A) && p.kind(ContextPack.A) == ContextPack.KIND_ACTION && p.label(ContextPack.A) == 0x04
                && p.ring(ContextPack.A) == 7, "A");
        check(p.kind(ContextPack.X) == ContextPack.KIND_ITEM && p.label(ContextPack.X) == 0x27, "X item");
        check(p.ring(ContextPack.LEFT_RIGHT) == 9 && p.shown(ContextPack.LEFT_RIGHT), "last slot");
        check(!p.shown(-1) && !p.shown(ContextPack.COUNT) && p.label(99) == 0, "out of range");
        // unknown versions and short arrays are no pack: everything as without context buttons
        int[] v2 = v.clone(); v2[0] = 2;
        check(p.set(v2, 2) && !p.valid() && !p.on() && p.visible(TouchLayout.KEY_L), "unknown version");
        check(!p.set(new int[10], 1) && !p.valid(), "short array (already invalid: no change)");
        p.set(v, 1);
        check(p.set(v, 2) && !p.valid(), "return value differs from out[0]");
        p.set(v, 1);
        p.clear();
        check(!p.valid() && !p.enabled() && p.assetsGeneration() == 0, "clear");
    }

    static void visibility() {
        ContextPack p = new ContextPack();
        String[] all = {TouchLayout.KEY_A, TouchLayout.KEY_B, TouchLayout.KEY_X, TouchLayout.KEY_Y, TouchLayout.KEY_R,
                TouchLayout.KEY_ZR, TouchLayout.KEY_ZL, TouchLayout.KEY_L, TouchLayout.KEY_PLUS, TouchLayout.KEY_MINUS,
                TouchLayout.KEY_UP, TouchLayout.KEY_DOWN, TouchLayout.KEY_LEFT, TouchLayout.KEY_RIGHT,
                TouchLayout.KEY_LEFT_STICK, TouchLayout.KEY_RIGHT_STICK, "5:1", "5:2", "3:0"};
        // no pack (an older game library): every control
        for (String k : all) check(p.visible(k) && !p.held(k) && p.kindFor(k) == ContextPack.KIND_PLAIN, "no pack " + k);
        // enabled but not active (a menu, the title screen): every control, but held still draws
        int[] v = pack(ContextPack.ENABLED, 0);
        button(v, ContextPack.ZL, false, 0, true, 0, 0);
        p.set(v, 1);
        for (String k : all) check(p.visible(k), "inactive " + k);
        check(p.held(TouchLayout.KEY_ZL), "held while inactive");
        // active, nothing shown: only the controls the rules leave alone
        p.set(pack(ContextPack.ENABLED | ContextPack.ACTIVE, 0), 1);
        for (String k : all) {
            int b = ContextPack.buttonFor(k);
            check(p.visible(k) == (b == ContextPack.UNCHANGED), "active, none shown " + k);
        }
        check(ContextPack.buttonFor(TouchLayout.KEY_MINUS) == ContextPack.UNCHANGED, "− unchanged");
        check(ContextPack.buttonFor(TouchLayout.KEY_L) == ContextPack.HIDDEN
                && ContextPack.buttonFor(TouchLayout.KEY_DOWN) == ContextPack.HIDDEN, "L, D-pad down hidden");
        // shown buttons: both D-pad arrows from one slot; held from the state or the ZL latch flag
        v = pack(ContextPack.ENABLED | ContextPack.ACTIVE | ContextPack.ZL_LATCHED, 0);
        button(v, ContextPack.LEFT_RIGHT, true, 0, false, 0, 0);
        button(v, ContextPack.ZR, true, ContextPack.KIND_FAST_FORWARD, false, 0, 0);
        button(v, ContextPack.Y, true, ContextPack.KIND_ITEM, true, 0x34, 0);
        p.set(v, 1);
        check(p.visible(TouchLayout.KEY_LEFT) && p.visible(TouchLayout.KEY_RIGHT) && !p.visible(TouchLayout.KEY_UP), "D-pad");
        check(!p.visible(TouchLayout.KEY_ZL) && p.held(TouchLayout.KEY_ZL), "ZL latched (drawn held when shown)");
        check(p.held(TouchLayout.KEY_Y) && !p.held(TouchLayout.KEY_X) && !p.held(TouchLayout.KEY_MINUS), "held Y");
        check(p.kindFor(TouchLayout.KEY_ZR) == ContextPack.KIND_FAST_FORWARD, "ZR fast forward");
        check(p.kindFor(TouchLayout.KEY_Y) == ContextPack.KIND_ITEM && p.labelFor(TouchLayout.KEY_Y) == 0x34, "Y icon");
        check(p.kindFor(TouchLayout.KEY_X) == ContextPack.KIND_PLAIN, "hidden X: plain");
        // disabled option: all shown, nothing held
        p.set(pack(ContextPack.ACTIVE | ContextPack.ZL_LATCHED, 0), 1);
        check(p.visible(TouchLayout.KEY_L) && !p.held(TouchLayout.KEY_ZL), "option off");
    }

    static void labels() {
        Map<Integer, String> words = new HashMap<>();
        words.put(0x04, "Soulever");
        check(ContextPack.word(0x04, words).equals("Soulever"), "the game's word first");
        check(ContextPack.word(0x08, words).equals("Put Away"), "English fallback");
        check(ContextPack.word(0x08, null).equals("Put Away"), "no words");
        check(ContextPack.word(0x44, null) == null && ContextPack.word(0x00, words) == null, "no word: the letter");
        check(ContextPack.letter(ContextPack.X, "X", true).equals("A1") && ContextPack.letter(ContextPack.Y, "Y", true).equals("A2")
                && ContextPack.letter(ContextPack.R, "R", true).equals("A3"), "A1 A2 A3");
        check(ContextPack.letter(ContextPack.X, "X", false).equals("X") && ContextPack.letter(ContextPack.A, "A", true).equals("A"), "letters");
        ContextPack p = new ContextPack();
        int[] v = pack(ContextPack.ITEM_NAMES_A, 0);
        p.set(v, 1);
        check(p.itemNamesA(), "names flag");
        check(!p.linkPoses(), "poses flag off");
        p.set(pack(ContextPack.LINK_POSES, 0), 1);
        check(p.linkPoses() && !p.itemNamesA(), "poses flag (P8)");
        // text fitting: a letter-sized word stays 0.9 r; a long one shrinks to the width; never below min
        check(near(ContextPack.fitTextSize(1f, 50, 85, 10), 45), "short word");
        check(near(ContextPack.fitTextSize(4f, 50, 85, 10), 85 / 4f), "long word fits");
        check(near(ContextPack.fitTextSize(40f, 50, 85, 10), 10), "min size");
        check(near(ContextPack.fitTextSize(0f, 50, 85, 10), 45), "empty width");
        String[] two = ContextAssets.twoLines("Put Away");
        check(two != null && two[0].equals("Put") && two[1].equals("Away"), "two lines");
        two = ContextAssets.twoLines("Take a Photo");
        check(two != null && two[0].equals("Take a") && two[1].equals("Photo") || two != null && two[0].equals("Take")
                && two[1].equals("a Photo"), "middle space");
        check(ContextAssets.twoLines("Attack") == null && ContextAssets.twoLines(" X") == null && ContextAssets.twoLines(null) == null,
                "no split");
    }

    static void rings() {
        GoldenRings g = new GoldenRings(ContextPack.COUNT);
        // the first counter is a baseline: no rings at the start
        check(!g.note(ContextPack.A, 5, true, 1000) && g.progress(ContextPack.A, 1000) < 0, "baseline");
        check(!g.note(ContextPack.A, 5, true, 1100), "same counter");
        check(g.note(ContextPack.A, 6, true, 2000), "a new action plays");
        check(near(g.progress(ContextPack.A, 2000), 0) && near(g.progress(ContextPack.A, 2400), 0.5f) && g.playing(2400), "progress");
        check(g.progress(ContextPack.A, 2000 + GoldenRings.DURATION_MS) < 0 && !g.playing(2000 + GoldenRings.DURATION_MS), "ends");
        check(g.progress(ContextPack.A, 1999) < 0, "not before its start");
        // a change while hidden moves the baseline only
        check(!g.note(ContextPack.A, 7, false, 3000) && g.progress(ContextPack.A, 3000) < 0, "hidden");
        check(!g.note(ContextPack.A, 7, true, 3100), "shown again, same counter: nothing");
        // one change per play, even when several come close together: the latest restarts it
        check(g.note(ContextPack.A, 8, true, 4000) && g.note(ContextPack.A, 9, true, 4100)
                && near(g.progress(ContextPack.A, 4100), 0), "restart");
        g.stop(ContextPack.A);
        check(g.progress(ContextPack.A, 4200) < 0, "stopped");
        g.forget();
        check(!g.note(ContextPack.A, 1, true, 5000), "after forget: baseline again");
        check(!g.note(-1, 1, true, 0) && !g.note(ContextPack.COUNT, 1, true, 0) && g.progress(99, 0) < 0, "range");
        // ring timing: ring 0 from the start for RING_MS, ring 1 STAGGER_MS later to the end
        check(near(GoldenRings.ringT(0, 0), 0) && GoldenRings.ringT(1, 0) < 0, "ring 1 later");
        float f1 = GoldenRings.STAGGER_MS / (float) GoldenRings.DURATION_MS;
        check(near(GoldenRings.ringT(1, f1), 0), "ring 1 starts");
        check(GoldenRings.ringT(0, GoldenRings.RING_MS / (float) GoldenRings.DURATION_MS) < 0, "ring 0 ends");
        check(GoldenRings.ringT(1, 0.999f) > 0.99f, "ring 1 ends with the animation");
        check(GoldenRings.ringT(2, 0.5f) < 0 && GoldenRings.ringT(0, -1) < 0, "no other rings");
        check(near(GoldenRings.radius(0), 1) && near(GoldenRings.radius(1), 1 + GoldenRings.SPREAD), "radius");
        check(GoldenRings.alpha(0) == 230 && GoldenRings.alpha(1) == 0 && GoldenRings.alpha(0.5f) < 230 / 2, "fading");
        check(GoldenRings.stroke(1) < GoldenRings.stroke(0), "thinner");
        check(near(GoldenRings.pop(-1), 1) && near(GoldenRings.pop(0), 1) && near(GoldenRings.pop(GoldenRings.POP_PART / 2), 1 + GoldenRings.POP)
                && near(GoldenRings.pop(0.5f), 1), "pop");
    }

    static void assets() {
        ContextAssets a = new ContextAssets();
        // not ready: ask again after RETRY_MS
        check(a.shouldAsk(0, 0), "first ask");
        check(!a.result(0, 0, 0) && !a.shouldAsk(500, 0) && a.shouldAsk(ContextAssets.RETRY_MS, 0), "retry");
        // ready: load once
        check(a.result(4, 1000, 0) && a.generation() == 4, "load");
        check(!a.shouldAsk(99999, 4) && !a.shouldAsk(99999, 0), "loaded: no more asks");
        // the game pack changed: native code re-exported, a new generation shows in the pack
        check(a.shouldAsk(99999, 5) && a.result(5, 99999, 5) && !a.shouldAsk(99999, 5), "new generation");
        check(!a.result(5, 99999, 5), "same generation: nothing to load");
        // failed: letters and English; asked again only when the pack's generation changes
        ContextAssets f = new ContextAssets();
        check(!f.result(-1, 0, 0) && f.failed() && !f.shouldAsk(99999, 0), "failed");
        check(f.shouldAsk(99999, 2) && f.result(2, 99999, 2) && !f.failed(), "recovered");
        check(!f.result(-1, 0, 2) && !f.shouldAsk(99999, 2) && f.shouldAsk(99999, 3), "failed at a generation");
        check(ContextAssets.itemFile(0x27).equals("item_27.png") && ContextAssets.itemFile(0x0A).equals("item_0A.png")
                && ContextAssets.itemFile(-1).equals("item_FF.png"), "item file names");
        // Link's poses (P8): pose_HH.png as native code writes it (link_poses.h file_for); shown only for an
        // action with the option on and its picture loaded, else the word as before
        check(ContextAssets.poseFile(0x35).equals("pose_35.png") && ContextAssets.poseFile(0x0A).equals("pose_0A.png"), "pose file names");
        check(ContextAssets.showPose(true, ContextPack.KIND_ACTION, true), "pose shown");
        check(!ContextAssets.showPose(false, ContextPack.KIND_ACTION, true), "option off: the word");
        check(!ContextAssets.showPose(true, ContextPack.KIND_ACTION, false), "no picture (not exported, or the model unreadable): the word");
        check(!ContextAssets.showPose(true, ContextPack.KIND_ITEM, true) && !ContextAssets.showPose(true, ContextPack.KIND_FAST_FORWARD, true)
                && !ContextAssets.showPose(true, ContextPack.KIND_PLAIN, true), "items and plain buttons: no pose");
        // the picture and its word stay inside the button (radius 1)
        check(-ContextAssets.POSE_DY + ContextAssets.POSE_HALF < 1 && ContextAssets.POSE_DY - ContextAssets.POSE_HALF > -1
                && ContextAssets.POSE_WORD_DY + 0.2f < 1, "pose layout inside the button");
        // the word ends left of the letter badge (centre 0.74 r, radius 0.3 r)
        check(ContextAssets.POSE_WORD_DX + ContextAssets.POSE_WORD_WIDTH / 2 < 0.74f - 0.3f + 0.05f, "word clear of the badge");
        Map<Integer, String> w = ContextAssets.parseActions("04\tLift\r\n0B\t Open \n1b\tPick Up\nZZ\tbad\n\tnone\n123\tlong code\n"
                + "05\t\n04\tagain\n08\tPut\u0007Away\n0C\t" + "x".repeat(50) + "\n");
        check(w.get(0x04).equals("Lift") && w.get(0x0B).equals("Open") && w.get(0x1B).equals("Pick Up"), "parse");
        check(!w.containsKey(0x05) && !w.containsKey(0x123) && w.size() == 5, "bad lines: " + w);
        check(w.get(0x08).equals("PutAway") && w.get(0x0C).length() == ContextAssets.MAX_WORD, "sanitised");
        check(ContextAssets.parseActions(null).isEmpty() && ContextAssets.parseActions("x".repeat(ContextAssets.MAX_TEXT + 1)).isEmpty(),
                "empty / too large");
    }

    public static void main(String[] args) {
        packLayout();
        visibility();
        labels();
        rings();
        assets();
        System.out.println("ContextButtonsTest: ok");
    }
}
