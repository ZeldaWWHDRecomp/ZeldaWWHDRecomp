package android.app;
public final class ApplicationExitInfo {
    private final long time;
    private final int reason;
    private final String process;
    public ApplicationExitInfo(long time, int reason, String process) { this.time = time; this.reason = reason; this.process = process; }
    public long getTimestamp() { return time; }
    public int getPid() { return 77; }
    public int getReason() { return reason; }
    public int getStatus() { return 6; }
    public int getImportance() { return 100; }
    public long getPss() { return 1234; }
    public long getRss() { return 5678; }
    public String getProcessName() { return process; }
    public String getDescription() { throw new AssertionError("Private description must never be read"); }
    public java.io.InputStream getTraceInputStream() { throw new AssertionError("Private trace must never be read"); }
    public byte[] getProcessStateSummary() { throw new AssertionError("Private state must never be read"); }
}
