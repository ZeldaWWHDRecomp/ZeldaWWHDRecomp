package org.wwhdrecomp.wwhd;

// javac -d "$OUT" android/app/src/main/java/org/wwhdrecomp/wwhd/SurfaceRegrow.java \
//   android/app/src/test/java/org/wwhdrecomp/wwhd/SurfaceRegrowTest.java && java -ea -cp "$OUT" org.wwhdrecomp.wwhd.SurfaceRegrowTest
public final class SurfaceRegrowTest {
    static void expect(boolean got, boolean want, String what) {
        if (got != want) throw new AssertionError(what + ": expected " + want + " got " + got);
    }
    public static void main(String[] args) {
        SurfaceRegrow r = new SurfaceRegrow();

        // #349's fresh start on the Retroid Pocket Nova: made at 1280x912 (the bar's rows reserved), then 1280x864
        // (bars shown at the top and bottom), then full screen 1280x960
        r.created();
        expect(r.changed(1280, 912), false, "first size: the size the surface is made at");
        expect(r.changed(1280, 864), false, "smaller than made: nothing due");
        expect(r.changed(1280, 960), true, "grew past the made size: a new surface is due");
        expect(r.due(), true, "still due until it is asked for");
        if (!r.describe().equals("made at 1280x912, now 1280x960")) throw new AssertionError(r.describe());
        // the new surface is asked for once ...
        r.renewing();
        expect(r.due(), false, "asked for: not due again");
        // ... the old one goes and the new one is made at the full size: nothing more (no loop)
        r.destroyed();
        expect(r.due(), false, "destroyed: nothing due");
        r.created();
        expect(r.changed(1280, 960), false, "the new surface's first size");
        expect(r.changed(1280, 960), false, "the same size again (another surfaceChanged)");

        // the resumed start (the reporter's workaround): the surface made at the full size needs nothing
        r = new SurfaceRegrow();
        r.created();
        expect(r.changed(1280, 960), false, "made at full size");
        expect(r.changed(1280, 912), false, "full screen turned off: smaller, nothing due");
        expect(r.changed(1280, 960), false, "back to the made size: nothing due");

        // a window grown step by step (multi-window): due while larger, the last size decides when it settles
        r = new SurfaceRegrow();
        r.created();
        r.changed(800, 600);
        expect(r.changed(900, 600), true, "wider");
        expect(r.changed(800, 700), true, "taller");
        expect(r.changed(800, 600), false, "back to the made size before it settled: nothing due");
        expect(r.changed(700, 500), false, "smaller");

        // growing in one direction while shrinking in the other still shows rows the surface was not made with
        r = new SurfaceRegrow();
        r.created();
        r.changed(1280, 912);
        expect(r.changed(1200, 960), true, "taller but narrower");

        // a surface destroyed while a new one was due (the app went to the background): the system makes a new one
        r.destroyed();
        expect(r.due(), false, "destroyed while due");

        // sizes not known yet are ignored
        r = new SurfaceRegrow();
        r.created();
        expect(r.changed(0, 0), false, "no size yet");
        expect(r.changed(1280, 912), false, "the first real size is the made size");
        expect(r.changed(-1, 960), false, "a bad size changes nothing");
        expect(r.changed(1280, 960), true, "then grew");
        expect(r.changed(0, 0), true, "a bad size keeps what was due");

        System.out.println("SurfaceRegrowTest PASS");
    }
}
