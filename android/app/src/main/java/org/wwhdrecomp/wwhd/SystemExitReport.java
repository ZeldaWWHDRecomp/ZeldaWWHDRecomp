package org.wwhdrecomp.wwhd;

import java.util.List;

/** Scalar-only exit history policy, independent of Android for synthetic tests. */
final class SystemExitReport {
    static final class Exit {
        final long timestamp, pssKb, rssKb;
        final int pid, reason, status, importance;
        final boolean mainProcess;
        Exit(long timestamp, int pid, int reason, int status, int importance,
             long pssKb, long rssKb, boolean mainProcess) {
            this.timestamp = timestamp; this.pid = pid; this.reason = reason;
            this.status = status; this.importance = importance;
            this.pssKb = pssKb; this.rssKb = rssKb; this.mainProcess = mainProcess;
        }
    }
    // ApplicationExitInfo reason codes (API 30). Never emit vendor descriptions,
    // process names, traces, process-state summaries or logcat: these can contain user data.
    static String reason(int code) {
        switch (code) {
            case 2: return "terminated by signal (may include a driver abort or low-memory kill)";
            case 3: return "low-memory kill";
            case 4: return "Java crash";
            case 5: return "native crash (may include a driver abort)";
            case 6: return "not responding (ANR)";
            case 7: return "initialization failure";
            case 9: return "excessive resource usage";
            default: return null;
        }
    }
    static boolean unexpected(Exit e, int expectedPid, long expectedAt) {
        if (!e.mainProcess || reason(e.reason) == null) return false;
        // Save import/export deliberately stops the game from the tools process.
        if (e.reason == 2 && e.pid == expectedPid && e.timestamp >= expectedAt
                && e.timestamp - expectedAt <= 30_000) return false;
        // Background cache reclamation is normal, but crashes and ANRs remain useful.
        return (e.reason != 2 && e.reason != 3 && e.reason != 9) || e.importance <= 230;
    }
    static Exit latest(List<Exit> exits, long seen, int expectedPid, long expectedAt) {
        Exit result = null;
        for (Exit e : exits)
            if (e.timestamp > seen && unexpected(e, expectedPid, expectedAt)
                    && (result == null || e.timestamp > result.timestamp)) result = e;
        return result;
    }
    static long watermark(List<Exit> exits, long seen) {
        for (Exit e : exits) if (e.mainProcess) seen = Math.max(seen, e.timestamp);
        return seen;
    }
    static String text(Exit e) {
        String label = reason(e.reason);
        if (label == null) throw new IllegalArgumentException("Not an unexpected exit");
        return "Android system process-exit report\n"
            + "Reason: " + label + "\nReason code: " + e.reason
            + "\nExit timestamp (Unix ms): " + e.timestamp + "\nStatus/signal: " + e.status
            + "\nProcess importance: " + e.importance + "\nPSS (KiB): " + Math.max(0, e.pssKb)
            + "\nRSS (KiB): " + Math.max(0, e.rssKb)
            + "\nMemory figures are Android's last sample, not peak measurements."
            + "\nPrivacy: system descriptions, traces, logs and process-state bytes are omitted.\n";
    }
}
