package org.wwhdrecomp.wwhd;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;

public final class SystemExitReportTest {
    private static void check(boolean result) { if (!result) throw new AssertionError(); }
    private static SystemExitReport.Exit exit(long time, int reason, int importance, boolean main) {
        return new SystemExitReport.Exit(time, 77, reason, 6, importance, 1234, 5678, main);
    }
    private static File log(File folder, String name, long time, String text) throws Exception {
        File file = new File(folder, name);
        Files.write(file.toPath(), text.getBytes(StandardCharsets.UTF_8));
        check(file.setLastModified(time));
        return file;
    }
    public static void main(String[] args) throws Exception {
        // Unordered histories; newer helper exits must neither win nor advance the main watermark.
        SystemExitReport.Exit crash = exit(1000, 5, 400, true);
        SystemExitReport.Exit memory = exit(2000, 3, 100, true);
        java.util.List<SystemExitReport.Exit> history = Arrays.asList(memory, exit(9000, 5, 100, false), crash);
        check(SystemExitReport.latest(history, 0, -1, 0) == memory);
        check(SystemExitReport.latest(history, 2000, -1, 0) == null);
        check(SystemExitReport.watermark(history, 0) == 2000);
        check(SystemExitReport.watermark(history, 3000) == 3000);
        for (int reason : new int[]{0, 1, 8, 10, 11, 12, 13, 15, 16, 999})
            check(!SystemExitReport.unexpected(exit(1000, reason, 100, true), -1, 0));
        for (int reason : new int[]{2, 3, 9}) {
            check(SystemExitReport.unexpected(exit(1000, reason, 230, true), -1, 0));
            check(!SystemExitReport.unexpected(exit(1000, reason, 400, true), -1, 0));
        }
        for (int reason : new int[]{4, 5, 6, 7})
            check(SystemExitReport.unexpected(exit(1000, reason, 400, true), -1, 0));
        check(!SystemExitReport.unexpected(exit(1000, 2, 100, true), 77, 999));
        check(SystemExitReport.unexpected(exit(1000, 2, 100, true), 78, 999));
        check(SystemExitReport.unexpected(exit(40_000, 2, 100, true), 77, 999));
        check(SystemExitReport.unexpected(exit(998, 2, 100, true), 77, 999));
        check(SystemExitReport.unexpected(crash, 77, 999)); // never suppress an actual crash
        String report = SystemExitReport.text(memory);
        check(report.contains("low-memory kill") && report.contains("PSS (KiB): 1234") && report.contains("RSS (KiB): 5678"));
        check(report.contains("Unix ms): 2000") && report.contains("omitted"));
        check(!SystemExitReport.text(exit(1000, 6, 100, true)).contains("123-private-save-name"));
        check(SystemExitReport.text(exit(1000, 2, 100, true)).contains("may include")); // do not assert GPU blame
        File dir = Files.createTempDirectory("exit-reports-synthetic").toFile();
        try {
            check(CrashReportFiles.latest(dir) == null);
            File nativeLog = log(dir, "crash-native.log", 1_000_000, "synthetic native diagnostic\n");
            log(dir, "crash-system-incomplete.log.tmp", 2_000_000, "incomplete");
            log(dir, "other.log", 3_000_000, "irrelevant");
            check(CrashReportFiles.latest(dir).primary.equals(nativeLog));
            File systemLog = log(dir, "crash-system-1-77.log", 1_000_010, report);
            CrashReportFiles pair = CrashReportFiles.latest(dir);
            check(pair.primary.equals(systemLog) && pair.nearby.equals(nativeLog));
            String identity = pair.identity();
            File copy = new File(dir, "shared.txt");
            pair.copyTo(copy);
            String shared = new String(Files.readAllBytes(copy.toPath()), StandardCharsets.UTF_8);
            check(shared.contains("low-memory kill") && shared.contains("synthetic native diagnostic"));
            check(!shared.contains("irrelevant") && !shared.contains("incomplete"));
            check(systemLog.setLastModified(2_000_000));
            check(CrashReportFiles.latest(dir).nearby == null);
            check(!CrashReportFiles.latest(dir).identity().equals(identity));
            check(nativeLog.setLastModified(3_000_000));
            check(CrashReportFiles.latest(dir).primary.equals(nativeLog));
        } finally {
            for (File file : dir.listFiles()) check(file.delete());
            check(dir.delete());
        }
        System.out.println("System-exit selection, scalar reports, intentional-stop filtering and share pairing passed");
    }
}
