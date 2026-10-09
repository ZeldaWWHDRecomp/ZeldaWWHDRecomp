import org.wwhdrecomp.wwhd.SetupRecoveryPolicy;

public final class SetupRecoveryPolicyTest {
    public static void main(String[] args) {
        for (String state : new String[] {null,"","selected","failed","complete","running",
                "importing","preparing","activating","paused","unknown"}) {
            for (boolean manual : new boolean[] {false,true})
                for (boolean update : new boolean[] {false,true})
                    for (boolean changed : new boolean[] {false,true}) {
                        boolean interrupted = java.util.Arrays.asList("running","importing",
                            "preparing","activating","paused").contains(state);
                        boolean expected = !manual && (interrupted ||
                            ("complete".equals(state) && update && changed));
                        if (SetupRecoveryPolicy.shouldStart(state,manual,update,changed) != expected)
                            throw new AssertionError("Recovery decision: " + state);
                    }
        }
        System.out.println("Setup recovery policy checks passed (88 combinations)");
    }
}
