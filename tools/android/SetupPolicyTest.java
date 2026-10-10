import org.wwhdrecomp.wwhd.SetupPolicy;

public final class SetupPolicyTest {
    private static void expect(String expected, String actual) {
        if (!java.util.Objects.equals(expected, actual))
            throw new AssertionError("Expected " + expected + ", got " + actual);
    }
    public static void main(String[] args) {
        SetupPolicy policy = new SetupPolicy();
        expect(null, policy.update(0, 300, 80, false, false));
        expect("heat", policy.update(3, 300, 80, true, false));
        expect("heat", policy.update(2, 300, 80, true, false));
        expect(null, policy.update(1, 390, 80, true, false));
        expect("heat", policy.update(0, 420, 80, true, false));
        expect("heat", policy.update(0, 400, 80, true, false));
        expect("battery", policy.update(0, 390, 20, false, false));
        expect("battery", policy.update(0, 300, 29, false, false));
        expect(null, policy.update(0, 300, 30, false, false));
        expect("manual", policy.update(3, 450, 10, false, true));
        expect("heat", policy.update(3, 450, 10, true, false));
        expect(null, policy.update(0, 300, 10, true, false));
        expect("battery_unknown", policy.update(0, -1, -1, false, false));
        expect(null, policy.update(0, -1, -1, true, false));
        // compile workers: 8 cores and 10 GB free -> 4; memory and heat limit it
        if (SetupPolicy.compileJobs(8, 10L << 30, false, 0) != 4) throw new AssertionError("8 cores, 10 GB");
        if (SetupPolicy.compileJobs(8, (1L << 30) + (1100L << 20), false, 0) != 2) throw new AssertionError("2.1 GB free");
        if (SetupPolicy.compileJobs(8, 512L << 20, false, 0) != 1) throw new AssertionError("little memory");
        if (SetupPolicy.compileJobs(4, 10L << 30, false, 0) != 2) throw new AssertionError("4 cores");
        if (SetupPolicy.compileJobs(8, 10L << 30, true, 0) != 1) throw new AssertionError("low-RAM phone");
        if (SetupPolicy.compileJobs(8, 10L << 30, false, 2) != 1) throw new AssertionError("warm phone");
        expect("Compiling: 30/80 · about 12 min left", SetupPolicy.compileProgress("compiled", 22, 8, 80, 700));
        expect("Compiling: 3/80", SetupPolicy.compileProgress("compiled", 3, 0, 80, -1));
        expect("Compiling: 79/80 · about a minute left", SetupPolicy.compileProgress("compiled", 79, 0, 80, 20));
        expect("Compiling: done (80/80)", SetupPolicy.compileProgress("complete", 80, 0, 80, -1));
        expect(null, SetupPolicy.compileProgress("compiled", 1, 0, 0, -1));
        System.out.println("Setup policy checks passed");
    }
}
