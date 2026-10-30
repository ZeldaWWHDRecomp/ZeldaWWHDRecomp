// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

public final class WindwakeTouchRedrawTest {
    public static void main(String[] args) {
        WindwakeTouchRedraw p = new WindwakeTouchRedraw();
        assert p.request(100) == 0;
        assert p.request(101) == WindwakeTouchRedraw.NONE; // latest state rides the outstanding draw
        assert !p.drawn(105);
        assert p.request(105) == 11; // VSync/onDraw latency belongs inside the 16 ms period
        assert p.request(110) == WindwakeTouchRedraw.NONE; // one callback for a motion burst
        assert p.callback(115) == 1;
        assert p.callback(116) == 0;
        assert p.callback(116) == WindwakeTouchRedraw.NONE;
        assert p.request(117) == WindwakeTouchRedraw.NONE;
        assert !p.drawn(120);
        assert p.request(131) == 1;
        assert p.drawn(131); // an unrelated framework draw supersedes the queued callback
        assert p.callback(132) == WindwakeTouchRedraw.NONE;
        assert p.request(131) == 16;
        p.cancel(); // release/detach: no stale callback and a fresh first frame after attach
        assert p.callback(1000) == WindwakeTouchRedraw.NONE;
        assert p.request(1000) == 0;
        assert !p.drawn(1000);
        assert p.request(1016) == 0; // exact boundary, no extra interval of latency
        p.drawn(1016);
        assert p.request(1) == 0; // a fresh/reset time domain cannot stall the view
        p.cancel();
        int draws = 0;
        for (long now = 0; now < 1000; now++) {
            long callback = p.callback(now);
            if (callback == 0) { draws++; p.drawn(now); }
            if (now % 8 == 0 && p.request(now) == 0) { draws++; p.drawn(now); } // about 120 Hz motion
        }
        assert draws <= 63 : draws;
        // Unlike immediate-draw tests, this models invalidate waiting for display VSync,
        // plus time from that VSync to onDraw. Anchoring each period to onDraw fails
        // at high refresh rates: VSync/traversal time must not be added to each period.
        for (int hz : new int[] {60, 90, 120}) {
            for (int phaseUs : new int[] {500, 2000, 4000}) {
                int visualFrames = simulatedFrames(hz, phaseUs);
                assert visualFrames >= (hz == 60 ? 237 : 246) && visualFrames <= (hz == 60 ? 240 : 253)
                        : hz + " Hz, onDraw phase " + phaseUs + " us: " + visualFrames + " frames / 4 s";
            }
        }
        // Traversal can itself exceed the 16 ms budget after VSync quantization.
        // Under this load we coalesce obsolete visuals rather than promise 60 Hz
        // or enqueue a catch-up burst; input delivery is outside this scheduler.
        int loadedFrames = simulatedFrames(90, 7000);
        assert loadedFrames >= 200 && loadedFrames <= 253 : loadedFrames;
        // A stalled UI skips obsolete slots instead of queueing a callback per missed interval.
        p.cancel();
        assert p.request(0) == 0;
        p.drawn(1);
        assert p.request(8) == 8;
        assert p.callback(250) == 0;
        p.drawn(255);
        assert p.request(256) == 0;
        p.drawn(260);
        assert p.request(261) == 11;
        System.out.println("Touch redraw: motion coalescing, 16 ms deadlines, 60/90/120 Hz VSync, stalls and detach/release cancellation PASS");
    }

    /** Model only the integration boundary: Handler deadlines, invalidate and display traversal. */
    private static int simulatedFrames(int refreshHz, int drawPhaseUs) {
        WindwakeTouchRedraw p = new WindwakeTouchRedraw();
        long callbackMs = -1, drawUs = -1, vsyncIndex = 1;
        boolean invalidated = false;
        int frames = 0;
        for (long us = 0; us < 4_000_000; us += 100) {
            long now = us / 1000;
            if (callbackMs >= 0 && now >= callbackMs) {
                long delay = p.callback(now);
                callbackMs = delay > 0 ? now + delay : -1;
                if (delay == 0) invalidated = true;
            }
            if (us >= vsyncIndex * 1_000_000L / refreshHz) {
                vsyncIndex++;
                if (invalidated && drawUs < 0) {
                    drawUs = us + drawPhaseUs;
                    invalidated = false;
                }
            }
            if (drawUs >= 0 && us >= drawUs) {
                if (p.drawn(now)) callbackMs = -1;
                frames++;
                drawUs = -1;
            }
            if (us % 8000 == 0) { // sustained motion, roughly 120 Hz
                long delay = p.request(now);
                if (delay == 0) invalidated = true;
                else if (delay > 0) callbackMs = now + delay;
            }
        }
        return frames;
    }
}
