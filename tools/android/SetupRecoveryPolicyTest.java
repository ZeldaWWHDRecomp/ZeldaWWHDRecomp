import org.wwhdrecomp.wwhd.SetupRecoveryPolicy;

public final class SetupRecoveryPolicyTest {
    public static void main(String[] args) {
        for (String state : new String[] {null,"","selected","failed","complete","running",
                "importing","preparing","activating","paused","preparing models","unknown"}) {
            for (boolean manual : new boolean[] {false,true})
                for (boolean update : new boolean[] {false,true})
                    for (boolean changed : new boolean[] {false,true}) {
                        boolean interrupted = java.util.Arrays.asList("running","importing",
                            "preparing","activating","paused","preparing models").contains(state);
                        boolean expected = !manual && (interrupted ||
                            ("complete".equals(state) && update && changed));
                        if (SetupRecoveryPolicy.shouldStart(state,manual,update,changed) != expected)
                            throw new AssertionError("Recovery decision: " + state);
                    }
        }
        // A start without an action on a finished setup with nothing to rebuild is idle; explicit starts
        // (Resume: the player's own start) always run, as does any start while a run is active.
        int idle = 0;
        for (String action : new String[] {null, "org.wwhdrecomp.wwhd.SETUP_RESUME", "org.wwhdrecomp.wwhd.SETUP_RUN_MODELS"})
            for (boolean running : new boolean[] {false,true})
                for (String state : new String[] {null,"complete","failed","running","preparing models"})
                    for (boolean changed : new boolean[] {false,true}) {
                        boolean expected = action == null && !running && "complete".equals(state) && !changed;
                        if (SetupRecoveryPolicy.automaticStartIsIdle(action,running,state,changed) != expected)
                            throw new AssertionError("Idle start: " + action + " " + running + " " + state + " " + changed);
                        idle++;
                    }
        System.out.println("Setup recovery policy checks passed (96 combinations, " + idle + " start cases)");
    }
}
