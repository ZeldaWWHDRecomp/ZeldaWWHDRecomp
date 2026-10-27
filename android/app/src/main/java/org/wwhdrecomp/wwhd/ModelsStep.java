package org.wwhdrecomp.wwhd;

/**
 * Setup's optional models step ("Preparing shaders: models", tools/android/android_models.py): the decisions the
 * setup service, the setup screen and ModelPrepareActivity share. No Android types, so tools/android/ModelsStepTest
 * checks it on a computer.
 *
 * The service runs the step's Python (model_sweep.run, as on a computer); each attempt of the game is a start of
 * ModelPrepareActivity in its own process, which the setup screen makes (only an app in front may start an
 * activity). The files below live in the setup job.
 */
public final class ModelsStep {
    private ModelsStep() {}

    /** host.json state while the step runs. */
    public static final String STATE = "preparing models";
    /** The step is due for this job (written after the warm, or when the player ticks the step later). */
    public static final String STEP = "models-step.json";
    /** The attempt the Python asks for: id, the game's WWHD_ options, arguments, folder, output file. */
    public static final String ATTEMPT = "model-attempt.json";
    /** The setup screen started that attempt (id), so it is started once. */
    public static final String LAUNCHED = "model-attempt-launched.json";
    /** ModelPrepareActivity: id, pid, state ("running", "left"). */
    public static final String STATUS = "model-attempt-status.json";
    /** Stop that attempt in order (id). */
    public static final String STOP = "model-attempt-stop.json";
    /** For the progress screens: done, total, phase, place, fraction, eta_seconds. */
    public static final String PROGRESS = "models-progress.json";
    /** The player's Cancel. */
    public static final String CANCEL = "models-cancel.json";
    /** The step's end: status done|failed|paused, reason. */
    public static final String RESULT = "models-result.json";
    /** The step waits for the setup screen (the player left it, or it was not in front). */
    public static final String SCREEN = "models-screen.json";
    /** host.json / pause.json reason while the step waits for the setup screen. */
    public static final String SCREEN_REASON = "screen";

    /** On a phone the step is recommended: ticked unless the player unticked it (the choice is remembered).
     *  Measured on a Xiaomi 14: about 33 pipeline waits on a 25-place route with the step, about 1000 without. */
    public static final boolean RECOMMENDED = true;

    /** The player's remembered choice (models-choice.json "enabled"), or the recommendation when there is none. */
    public static boolean chosen(Boolean remembered) { return remembered == null ? RECOMMENDED : remembered; }

    /** Setup's message when the chosen step cannot run: it needs the prepared shaders (the shader template and the
     *  warm). why: shader preparation's own message (for example the missing template), or null. */
    public static String skipped(String why) {
        return "Prepare model shaders skipped: it needs the shader template and the prepared shaders" +
            (why == null || why.isEmpty() ? "." : " (" + why + ").");
    }

    public enum Outcome { DONE, FAILED, CANCELLED, WAIT_FOR_SCREEN, PAUSED }

    /** What the service does with models-result.json. */
    public static Outcome outcome(String status, String reason) {
        if ("done".equals(status)) return Outcome.DONE;
        if ("paused".equals(status)) {
            if ("cancel".equals(reason)) return Outcome.CANCELLED;
            if ("screen".equals(reason) || "left".equals(reason)) return Outcome.WAIT_FOR_SCREEN;
            return Outcome.PAUSED;  // heat, battery, Pause, the service stopping: the service's own pause
        }
        return Outcome.FAILED;
    }

    /** An attempt id as android_models.py makes them (uuid4 hex). */
    public static boolean validId(String id) { return id != null && id.matches("[0-9a-f]{32}"); }

    /** Whether the setup screen starts the requested attempt now (not one it started already). */
    public static boolean launch(String requested, String launched) {
        return validId(requested) && !requested.equals(launched);
    }

    /** The bar (0..1000) for a fraction of the step; -1: not known yet (moving bar). */
    public static int bar(double fraction) {
        if (Double.isNaN(fraction) || fraction < 0) return -1;
        return (int) Math.round(1000 * Math.min(1.0, fraction));
    }

    /** -1: no estimate yet; 0: about a minute; else minutes left (as the compile step shows them). */
    public static long minutesLeft(long etaSeconds) {
        if (etaSeconds < 0) return -1;
        return etaSeconds < 90 ? 0 : (etaSeconds + 59) / 60;
    }

    public enum Phase { STARTING, OBJECTS, ROOMS, ENVIRONMENT, ACTORS, PLANTS, PARTICLES, VISITS, FILE_SELECT }

    /** The sweep's phase (progress.txt "phase <name>", runtime/src/dsweep.cpp phase_time). */
    public static Phase phase(String name) {
        if (name == null) return Phase.STARTING;
        switch (name) {
            case "objects": return Phase.OBJECTS;
            case "rooms": return Phase.ROOMS;
            case "environment": return Phase.ENVIRONMENT;
            case "actors": return Phase.ACTORS;
            case "plants": return Phase.PLANTS;
            case "particles": return Phase.PARTICLES;
            case "visits": return Phase.VISITS;
            case "file-select": case "end": return Phase.FILE_SELECT;
            default: return Phase.STARTING;
        }
    }

    /** A request's path (the attempt's folder or output) must lie inside the setup job. */
    public static boolean inside(String path, String job) {
        if (path == null || job == null || path.isEmpty() || job.isEmpty() || path.contains("/../") || path.endsWith("/.."))
            return false;
        String root = job.endsWith("/") ? job : job + "/";
        return path.startsWith(root);
    }

    /** Only the game's own options reach the game process. */
    public static boolean gameOption(String name) { return name != null && name.matches("WWHD_[A-Z0-9_]+"); }
}
