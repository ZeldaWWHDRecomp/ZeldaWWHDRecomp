package android.app;
import java.util.List;
public final class ActivityManager {
    public List<ApplicationExitInfo> history;
    public int queries;
    public boolean fail;
    public List<ApplicationExitInfo> getHistoricalProcessExitReasons(String name, int pid, int limit) {
        if (!name.equals("synthetic.test") || pid != 0 || limit != 32) throw new AssertionError();
        queries++;
        if (fail) throw new SecurityException("synthetic unavailable history");
        return history;
    }
}
