package org.wwhdrecomp.wwhd;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

/** Select the newest event and preserve a nearby native report alongside its system summary. */
final class CrashReportFiles {
    final File primary, nearby;
    private CrashReportFiles(File primary, File nearby) { this.primary = primary; this.nearby = nearby; }
    static CrashReportFiles latest(File captures) {
        File[] files = captures.listFiles((dir, name) -> name.startsWith("crash-") && name.endsWith(".log"));
        if (files == null) return null;
        File nativeLog = null, systemLog = null;
        for (File file : files) {
            if (!file.isFile()) continue;
            if (file.getName().startsWith("crash-system-")) {
                if (systemLog == null || file.lastModified() > systemLog.lastModified()) systemLog = file;
            } else if (nativeLog == null || file.lastModified() > nativeLog.lastModified()) nativeLog = file;
        }
        if (nativeLog == null && systemLog == null) return null;
        File primary = systemLog == null || (nativeLog != null && nativeLog.lastModified() >= systemLog.lastModified())
            ? nativeLog : systemLog;
        File other = primary == nativeLog ? systemLog : nativeLog;
        File nearby = other != null && Math.abs(primary.lastModified() - other.lastModified()) <= 120_000 ? other : null;
        return new CrashReportFiles(primary, nearby);
    }
    private static String identity(File file) { return file.getName() + ":" + file.lastModified() + ":" + file.length(); }
    String identity() { return identity(primary) + (nearby == null ? "" : ":" + identity(nearby)); }
    void copyTo(File copy) throws IOException {
        Files.copy(primary.toPath(), copy.toPath(), StandardCopyOption.REPLACE_EXISTING);
        if (nearby != null) {
            try (FileOutputStream out = new FileOutputStream(copy, true)) {
                out.write("\n--- Nearby process-exit/native report (timestamps may differ) ---\n".getBytes(StandardCharsets.UTF_8));
                Files.copy(nearby.toPath(), out);
            }
        }
    }
}
