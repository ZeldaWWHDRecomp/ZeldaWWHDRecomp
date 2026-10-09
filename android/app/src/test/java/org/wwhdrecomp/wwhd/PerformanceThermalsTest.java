package org.wwhdrecomp.wwhd;
// No emulator/GPU required: run with java -ea and android.jar on the classpath.
public final class PerformanceThermalsTest {
    public static void main(String[] args) {
        String[] names = {"none", "light", "moderate", "severe", "critical", "emergency", "shutdown"};
        for (int i = 0; i < names.length; ++i) assert names[i].equals(PerformanceThermals.statusName(i));
        assert "n/a".equals(PerformanceThermals.statusName(-1));
        assert "n/a".equals(PerformanceThermals.statusName(7));
        assert "n/a".equals(PerformanceThermals.batteryTemperature(Integer.MIN_VALUE));
        assert "0.0 C".equals(PerformanceThermals.batteryTemperature(0));
        assert "25.3 C".equals(PerformanceThermals.batteryTemperature(253));
        assert "-0.5 C".equals(PerformanceThermals.batteryTemperature(-5));
        System.out.println("PerformanceThermals: all public statuses, missing readings and Celsius formatting PASS");
    }
}
