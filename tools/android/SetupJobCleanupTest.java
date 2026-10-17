package org.wwhdrecomp.wwhd;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

/** Old-job cleanup: failed and abandoned jobs go; the selected job, the installed builds and a job the setup
 *  service holds stay. */
public final class SetupJobCleanupTest {
    private static final String CURRENT = id('1'), ACTIVE = id('2'), PREVIOUS = id('3'), FAILED = id('4'),
        ABANDONED = id('5'), RUNNING = id('6'), ORPHAN = id('7');

    private static String id(char digit) { return new String(new char[32]).replace('\0', digit); }

    private static void write(File file, String text) throws Exception {
        file.getParentFile().mkdirs();
        Files.write(file.toPath(), text.getBytes(StandardCharsets.UTF_8));
    }

    private static File job(File storage, String id, String state, int bytes) throws Exception {
        File job = new File(storage, "jobs/" + id);
        write(new File(job, "host.json"), "{\"schema\":1,\"state\":\"" + state + "\"}");
        write(new File(job, "source.json"), "{\"schema\":1,\"kind\":\"folder\",\"uri\":\"content://x\"}");
        File copy = new File(job, "input/game/content/data.bin");
        copy.getParentFile().mkdirs();
        Files.write(copy.toPath(), new byte[bytes]);
        return job;
    }

    private static void expect(boolean condition, String what) { if (!condition) throw new AssertionError(what); }

    public static void main(String[] args) throws Exception {
        Path temporary = Files.createTempDirectory("wwhd-job-cleanup");
        try {
            File storage = temporary.toFile();
            job(storage, CURRENT, "failed", 10);                // the selected job, even failed, stays
            File active = job(storage, ACTIVE, "complete", 20);  // the installed build
            job(storage, PREVIOUS, "complete", 30);              // the build a rollback returns to
            job(storage, FAILED, "failed", 1000);                // a failed earlier choice
            job(storage, ABANDONED, "paused", 2000);             // an earlier choice never finished
            File running = job(storage, RUNNING, "running", 40); // a job the setup service still holds
            job(storage, ORPHAN, "importing", 3000);             // left behind by a killed import
            new File(storage, "jobs/not-a-job").mkdirs();         // unknown names are left alone
            write(new File(storage, "setup.json"), "{\"schema\":1,\"job\":\"" + CURRENT + "\"}");
            // org.json writes slashes escaped; the ids are found either way
            write(new File(storage, "active.json"), "{\"library\":\"jobs\\/" + ACTIVE + "\\/link\\/libmain.so\",\"game\":\"jobs\\/" + ACTIVE + "\\/game\"}");
            write(new File(storage, "previous.json"), "{\"library\":\"jobs/" + PREVIOUS + "/link/libmain.so\"}");
            // a finished build's files are read-only
            File library = new File(active, "link/libmain.so");
            write(library, "so");
            library.setWritable(false);

            SetupJobCleanup.Hold service = SetupJobCleanup.hold(running);
            expect(service != null, "The service could not hold its job");
            expect(SetupJobCleanup.hold(running) == null, "A held job could be held twice");
            SetupJobCleanup.Report report = SetupJobCleanup.clean(storage);
            expect(report.skipped == null, "Cleanup skipped: " + report.skipped);
            expect(report.removed.size() == 3 && report.removed.contains(FAILED) && report.removed.contains(ABANDONED) &&
                report.removed.contains(ORPHAN), "Wrong jobs removed: " + report.removed);
            expect(report.running.equals(java.util.Collections.singletonList(RUNNING)), "Running job not reported: " + report.running);
            expect(report.bytes >= 6000, "Freed bytes not counted: " + report.bytes);
            expect(report.summary().contains("freed"), "Summary: " + report.summary());
            for (String kept : new String[] {CURRENT, ACTIVE, PREVIOUS, RUNNING})
                expect(new File(storage, "jobs/" + kept + "/input/game/content/data.bin").isFile(), "Kept job lost files: " + kept);
            expect(library.isFile(), "Active library removed");
            for (String gone : new String[] {FAILED, ABANDONED, ORPHAN})
                expect(!new File(storage, "jobs/" + gone).exists(), "Removed job still on disk: " + gone);
            expect(new File(storage, "jobs/not-a-job").isDirectory(), "Unknown folder removed");

            // once the service lets go (and the job is no longer chosen), the job is removed
            service.close();
            write(new File(storage, "setup.json"), "{\"schema\":1,\"job\":\"" + CURRENT + "\"}");
            report = SetupJobCleanup.clean(storage);
            expect(report.removed.equals(java.util.Collections.singletonList(RUNNING)), "Released job not removed: " + report.removed);

            // choosing a different game: the earlier selection becomes removable, the new one stays
            String chosen = id('8');
            job(storage, chosen, "selected", 5);
            write(new File(storage, "setup.json"), "{\"schema\":1,\"job\":\"" + chosen + "\"}");
            report = SetupJobCleanup.clean(storage);
            expect(report.removed.equals(java.util.Collections.singletonList(CURRENT)), "Earlier choice not removed: " + report.removed);
            expect(new File(storage, "jobs/" + chosen).isDirectory(), "New choice removed");

            // an unreadable record keeps everything
            String spare = id('9');
            job(storage, spare, "failed", 5);
            File broken = new File(storage, "previous.json");
            broken.setReadable(false);
            boolean unreadable = !Files.isReadable(broken.toPath());
            report = SetupJobCleanup.clean(storage);
            broken.setReadable(true);
            if (unreadable) {  // (a superuser can read it anyway)
                expect(report.skipped != null && report.removed.isEmpty(), "Unreadable record did not stop cleanup");
                expect(new File(storage, "jobs/" + spare).isDirectory(), "Job removed despite an unreadable record");
            }

            // no jobs folder at all
            File empty = Files.createTempDirectory(temporary, "empty").toFile();
            expect(SetupJobCleanup.clean(empty).removed.isEmpty(), "Empty storage");
            System.out.println("SetupJobCleanupTest passed");
        } finally {
            Files.walk(temporary).sorted(java.util.Comparator.reverseOrder()).forEach(path -> {
                path.toFile().setWritable(true);
                path.toFile().delete();
            });
        }
    }
}
