// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

/** Visual invalidation only. Input delivery and native context polling never wait for this clock. */
final class WindwakeTouchRedraw {
    // Approximately 60 visual updates/s; the display quantizes this 62.5 Hz target.
    static final long INTERVAL_MS = 16;
    static final long NONE = -1;
    private long lastRedrawAt = Long.MIN_VALUE, invalidatedAt, due;
    private boolean pending, invalidated;

    /** Zero invalidates now, positive schedules the sole callback, NONE coalesces into existing work. */
    long request(long now) {
        if (pending || invalidated) return NONE;
        if (lastRedrawAt == Long.MIN_VALUE || now < lastRedrawAt || now - lastRedrawAt >= INTERVAL_MS) {
            invalidated = true;
            invalidatedAt = now;
            return 0;
        }
        due = lastRedrawAt + INTERVAL_MS;
        pending = true;
        return due - now;
    }

    /** An early callback is requeued; a cancelled/stale callback cannot resurrect a redraw. */
    long callback(long now) {
        if (!pending) return NONE;
        if (now < due) return due - now;
        pending = false;
        invalidated = true;
        // Keep the scheduled deadline: Handler delay and the following VSync belong
        // inside this period, rather than extending every subsequent period.
        invalidatedAt = due;
        return 0;
    }

    /** A framework/layout draw already displays the latest model and replaces any pending redraw. */
    boolean drawn(long now) {
        boolean cancelCallback = pending;
        // A paced draw advances from its invalidation deadline, not its onDraw time.
        // Restarting at onDraw would add traversal and the following VSync to every
        // period, systematically reducing cadence. A framework draw starts afresh.
        lastRedrawAt = invalidated ? invalidatedAt : now;
        pending = invalidated = false;
        return cancelCallback;
    }

    void cancel() {
        pending = invalidated = false;
        lastRedrawAt = Long.MIN_VALUE;
        invalidatedAt = due = 0;
    }
}
