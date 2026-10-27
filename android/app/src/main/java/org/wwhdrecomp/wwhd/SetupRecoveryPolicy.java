package org.wwhdrecomp.wwhd;

/** Automatic recovery never starts a merely selected dump or retries a failed job. */
public final class SetupRecoveryPolicy {
    private SetupRecoveryPolicy() {}

    public static boolean shouldStart(String state, boolean manualPause,
            boolean checkUpdate, boolean inputsChanged) {
        if (manualPause) return false;
        if ("running".equals(state) || "importing".equals(state) ||
                "preparing".equals(state) || "activating".equals(state) || "paused".equals(state) ||
                "preparing models".equals(state))  // setup's models step (ModelsStep.STATE)
            return true;
        return "complete".equals(state) && checkUpdate && inputsChanged;
    }

    /** A start without an action (the setup screen's recovery check, an update check, the boot receiver) that
     *  arrives while no run is active, on a complete setup with nothing to rebuild, starts nothing. Explicit starts
     *  (Resume, Pause, Run models, the continue-anyway actions) always reach the service's normal handling. */
    public static boolean automaticStartIsIdle(String action, boolean running, String state, boolean inputsChanged) {
        return action == null && !running && "complete".equals(state) && !inputsChanged;
    }
}
