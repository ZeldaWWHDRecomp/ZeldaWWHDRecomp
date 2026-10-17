package org.wwhdrecomp.wwhd;

import java.io.File;
import java.io.IOException;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;
import java.nio.charset.StandardCharsets;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Removes setup jobs nobody uses any more: a failed or abandoned import after a different game was chosen,
 *  and such jobs left behind by an earlier run. Kept are the selected job, every job the active or previous
 *  game build points into, and a job the setup service holds (it is running or paused there). Plain Java,
 *  so the rules are testable without Android. */
public final class SetupJobCleanup {
    private SetupJobCleanup() {}

    /** Held by the setup service while it owns a job; cleanup skips a job it cannot lock. */
    public static final String LOCK = "running.lock";
    /** Job creation and cleanup in the setup screen's process never overlap. */
    public static final Object CREATION = new Object();
    private static final Pattern ID = Pattern.compile("[0-9a-f]{32}");
    private static final long RECORD_LIMIT = 1024 * 1024;

    public static final class Report {
        public final List<String> removed = new ArrayList<>();
        public final List<String> running = new ArrayList<>();
        public final List<String> failed = new ArrayList<>();
        public long bytes;
        /** Why nothing was removed, when the records could not be read. */
        public String skipped;
        public String summary() {
            if (skipped != null) return "Old setup jobs kept: " + skipped;
            String text = removed.isEmpty() ? "No old setup jobs to remove" :
                "Removed " + removed.size() + " old setup job" + (removed.size() == 1 ? "" : "s") + " " + removed +
                ", freed " + (bytes / (1024 * 1024)) + " MiB";
            if (!running.isEmpty()) text += "; kept running " + running;
            return failed.isEmpty() ? text : text + "; could not remove " + failed;
        }
    }

    /** The service's hold on its job: an exclusive lock on the job's lock file. */
    public static final class Hold implements AutoCloseable {
        public final File job;
        private final FileChannel channel;
        private final FileLock lock;
        private Hold(File job, FileChannel channel, FileLock lock) {
            this.job = job;
            this.channel = channel;
            this.lock = lock;
        }
        @Override public void close() throws IOException {
            try { lock.release(); }
            finally { channel.close(); }
        }
    }

    /** Locks a job for its owner; null when someone else holds it. */
    public static Hold hold(File job) throws IOException {
        if (!job.isDirectory() && !job.mkdirs()) throw new IOException("Cannot create setup job folder");
        FileChannel channel = FileChannel.open(new File(job, LOCK).toPath(), StandardOpenOption.CREATE, StandardOpenOption.WRITE);
        try {
            FileLock lock = channel.tryLock();
            if (lock != null) return new Hold(job, channel, lock);
        } catch (OverlappingFileLockException held) { /* held in this process */ }
        catch (IOException | RuntimeException failure) { channel.close(); throw failure; }
        channel.close();
        return null;
    }

    /** Every job id named by the records in the setup storage's top folder (selection, active and previous
     *  build, and their atomic-write leftovers); null when a record cannot be read. */
    static Set<String> referenced(File storage) {
        Set<String> ids = new HashSet<>();
        File[] records = storage.listFiles();
        if (records == null) return ids;
        for (File record : records) {
            if (!record.isFile() || record.getName().endsWith(".lock")) continue;
            try {
                if (record.length() > RECORD_LIMIT) return null;
                Matcher found = ID.matcher(new String(Files.readAllBytes(record.toPath()), StandardCharsets.UTF_8));
                while (found.find()) ids.add(found.group());
            } catch (IOException unreadable) { return null; }
        }
        return ids;
    }

    /** Removes the unused jobs under {@code storage}/jobs. The choice of jobs (and their locks) is made while
     *  no job is being created; the deleting itself runs after that. */
    public static Report clean(File storage) {
        Report report = new Report();
        List<Hold> unused = new ArrayList<>();
        synchronized (CREATION) {
            File[] jobs = new File(storage, "jobs").listFiles();
            if (jobs == null) return report;
            Set<String> keep = referenced(storage);
            if (keep == null) { report.skipped = "a setup record cannot be read"; return report; }
            for (File job : jobs) {
                String id = job.getName();
                if (!ID.matcher(id).matches() || !job.isDirectory() || Files.isSymbolicLink(job.toPath()) || keep.contains(id)) continue;
                try {
                    Hold hold = hold(job);
                    if (hold == null) report.running.add(id);
                    else unused.add(hold);
                } catch (IOException failure) { report.failed.add(id + " (" + failure.getMessage() + ")"); }
            }
        }
        for (Hold hold : unused) {
            String id = hold.job.getName();
            try (Hold held = hold) {
                report.bytes += remove(held.job.toPath());
                report.removed.add(id);
            } catch (IOException failure) { report.failed.add(id + " (" + failure.getMessage() + ")"); }
        }
        return report;
    }

    /** Deletes a folder tree without following links; returns the bytes freed. */
    static long remove(Path root) throws IOException {
        long[] bytes = {0};
        Files.walkFileTree(root, new SimpleFileVisitor<Path>() {
            @Override public FileVisitResult preVisitDirectory(Path directory, BasicFileAttributes attributes) {
                directory.toFile().setWritable(true, true);  // finished builds are read-only
                return FileVisitResult.CONTINUE;
            }
            @Override public FileVisitResult visitFile(Path file, BasicFileAttributes attributes) throws IOException {
                if (attributes.isRegularFile()) bytes[0] += attributes.size();
                Files.delete(file);
                return FileVisitResult.CONTINUE;
            }
            @Override public FileVisitResult postVisitDirectory(Path directory, IOException failure) throws IOException {
                if (failure != null) throw failure;
                Files.delete(directory);
                return FileVisitResult.CONTINUE;
            }
        });
        return bytes[0];
    }
}
