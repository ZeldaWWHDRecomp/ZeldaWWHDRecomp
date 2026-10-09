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
        System.out.println("Setup policy checks passed");
    }
}
