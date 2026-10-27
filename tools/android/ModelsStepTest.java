import org.wwhdrecomp.wwhd.ModelsStep;

/** ModelsStep: the setup service's reading of the models step's end, the setup screen's attempt starts, the bar. */
public final class ModelsStepTest {
    private static void expect(Object expected, Object actual) {
        if (!java.util.Objects.equals(expected, actual))
            throw new AssertionError("Expected " + expected + ", got " + actual);
    }

    public static void main(String[] args) {
        // the step's end (tools/android/android_models.py models-result.json)
        expect(ModelsStep.Outcome.DONE, ModelsStep.outcome("done", ""));
        expect(ModelsStep.Outcome.FAILED, ModelsStep.outcome("failed", ""));
        expect(ModelsStep.Outcome.FAILED, ModelsStep.outcome("", ""));  // no result: Python could not run
        expect(ModelsStep.Outcome.CANCELLED, ModelsStep.outcome("paused", "cancel"));
        expect(ModelsStep.Outcome.WAIT_FOR_SCREEN, ModelsStep.outcome("paused", "screen"));  // not started: setup not in front
        expect(ModelsStep.Outcome.WAIT_FOR_SCREEN, ModelsStep.outcome("paused", "left"));    // the player left the screen
        expect(ModelsStep.Outcome.PAUSED, ModelsStep.outcome("paused", "pause"));            // heat, battery, Pause
        expect(ModelsStep.Outcome.PAUSED, ModelsStep.outcome("paused", ""));

        // on a phone the step is recommended: ticked until the player unticks it (the choice is remembered)
        expect(true, ModelsStep.RECOMMENDED);
        expect(true, ModelsStep.chosen(null));
        expect(false, ModelsStep.chosen(false));
        expect(true, ModelsStep.chosen(true));

        // the chosen step without prepared shaders: setup says why it was skipped
        expect("Prepare model shaders skipped: it needs the shader template and the prepared shaders (Shader template unavailable; shaders will compile during play).",
            ModelsStep.skipped("Shader template unavailable; shaders will compile during play"));
        expect("Prepare model shaders skipped: it needs the shader template and the prepared shaders.", ModelsStep.skipped(null));

        // the setup screen starts each requested attempt once
        String id = "0123456789abcdef0123456789abcdef";
        expect(true, ModelsStep.launch(id, null));
        expect(true, ModelsStep.launch(id, "fedcba9876543210fedcba9876543210"));
        expect(false, ModelsStep.launch(id, id));
        expect(false, ModelsStep.launch(null, null));
        expect(false, ModelsStep.launch("../../x", null));
        expect(false, ModelsStep.validId("0123456789ABCDEF0123456789abcdef"));

        // the bar and the time left (as the compile step shows it)
        expect(-1, ModelsStep.bar(Double.NaN));
        expect(0, ModelsStep.bar(0));
        expect(425, ModelsStep.bar(0.4254));
        expect(1000, ModelsStep.bar(1.2));
        expect(-1L, ModelsStep.minutesLeft(-1));
        expect(0L, ModelsStep.minutesLeft(89));
        expect(2L, ModelsStep.minutesLeft(90));
        expect(41L, ModelsStep.minutesLeft(2401));

        // the phases of runtime/src/dsweep.cpp
        expect(ModelsStep.Phase.OBJECTS, ModelsStep.phase("objects"));
        expect(ModelsStep.Phase.VISITS, ModelsStep.phase("visits"));
        expect(ModelsStep.Phase.FILE_SELECT, ModelsStep.phase("file-select"));
        expect(ModelsStep.Phase.STARTING, ModelsStep.phase(""));
        expect(ModelsStep.Phase.STARTING, ModelsStep.phase(null));

        // an attempt's files stay in the setup job; only the game's own options reach it
        String job = "/data/user/0/org.wwhdrecomp.wwhd/no_backup/ondevice/jobs/" + id;
        expect(true, ModelsStep.inside(job + "/model-scratch/wwhd-model-sweep-x/output.log", job));
        expect(false, ModelsStep.inside(job + "/../" + id + "x/output.log", job));
        expect(false, ModelsStep.inside(job + "x/output.log", job));
        expect(false, ModelsStep.inside(job, job));
        expect(true, ModelsStep.gameOption("WWHD_DSWEEP_ARCHIVES"));
        expect(false, ModelsStep.gameOption("LD_PRELOAD"));
        expect(false, ModelsStep.gameOption("WWHD_x"));
        System.out.println("Models step checks passed");
    }
}
