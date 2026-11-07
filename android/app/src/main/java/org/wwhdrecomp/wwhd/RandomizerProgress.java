// SPDX-License-Identifier: MPL-2.0
package org.wwhdrecomp.wwhd;

/**
 * What the game's Settings > Randomizer shows while the phone's setup makes or imports a randomized game the page
 * asked for (WwhdActivity.randomizerStatus): the setup job's records read into one state. No Android classes, so the
 * rules are tested on the desktop (tools/android/RandomizerProgressTest.java).
 */
public final class RandomizerProgress {
    /** "picking", "building", "ready", "failed" or "cancelled", and what goes with it. */
    public final String state, stage, error;
    public final int done, total;

    private RandomizerProgress(String state, String stage, String error, int done, int total) {
        this.state = state;
        this.stage = stage;
        this.error = error;
        this.done = done;
        this.total = total;
    }

    static RandomizerProgress building(String stage) { return new RandomizerProgress("building", stage, "", 0, 0); }

    /** What the page asked for and the job it started: the request's own state before (or instead of) a job. */
    public static final class Request {
        public boolean starting;     // a new game's settings and job are being made
        public boolean picking;      // the folder picker is open (Import seed...)
        public boolean cancelled;    // it was closed without a folder
        public String refusal;       // the folder or the job was refused before setup started ("" none)
        public String job;           // the setup job's id ("" none yet)
        public boolean importing;
        public long since;           // when the job was made (System.currentTimeMillis)
    }

    /** The job's records: host.json (state, reason, error, updated), state.json's last_event (stage, compiled, reused,
     *  total), and whether its randomized game is the one the next start plays (ready.json's files are active). */
    public static final class Job {
        public boolean selected;     // still the setup's current job
        public String host = "", reason = "", hostError = "";
        public long updated;
        public String event = "";
        public int compiled, reused, eventTotal;
        public boolean active;
    }

    public static RandomizerProgress of(Request request, Job job) {
        if (request.picking) return new RandomizerProgress("picking", "Choose the official randomizer's output folder", "", 0, 0);
        if (request.starting) return building("Starting setup");
        if (request.cancelled) return new RandomizerProgress("cancelled", "", "", 0, 0);
        if (request.refusal != null && !request.refusal.isEmpty())
            return new RandomizerProgress("failed", "", request.refusal, 0, 0);
        if (request.job == null || request.job.isEmpty()) return new RandomizerProgress("cancelled", "", "", 0, 0);
        if (job.active) return new RandomizerProgress("ready", "", "", 0, 0);
        if (!job.selected)
            return new RandomizerProgress("failed", "", "Another setup was started in the setup screen; this randomized game was not made", 0, 0);
        // a record from before this job's start is not its own (a reused job folder never is: every request makes one)
        String host = job.updated < request.since ? "" : job.host;
        switch (host) {
            case "failed":
                return new RandomizerProgress("failed", "", job.hostError.isEmpty() ? "Setup failed; open the setup screen for details" : job.hostError, 0, 0);
            case "complete":
                return new RandomizerProgress("failed", "", "Setup finished without the randomized game; open the setup screen for details", 0, 0);
            case "paused":
                return building("Setup paused" + (job.reason.isEmpty() ? "" : " (" + job.reason + ")") + ": open the setup screen to continue");
            case "importing":
                return building(request.importing ? "Copying the seed's files" : "Starting setup");
            case "activating":
            case "warming shaders":
                return building("Getting it ready");
            case "running":
                break;
            default:
                return building("Starting setup");
        }
        switch (job.event) {
            case "randomize": return building(request.importing ? "Checking the seed" : "Making the randomized game (TWWHD Randomizer)");
            case "translate": return building("Translating the game code");
            case "compile": return new RandomizerProgress("building", "Compiling the game code", "", job.compiled + job.reused, job.eventTotal);
            case "link": return building("Linking the game code");
            case "shaders": return building(SetupPresentation.READING_SHADERS);
            case "activate": return building("Getting it ready");
            default: return building("Setup is running");
        }
    }
}
