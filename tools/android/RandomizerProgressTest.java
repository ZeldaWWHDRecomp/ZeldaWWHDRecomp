// SPDX-License-Identifier: MPL-2.0
// What the game's Settings > Randomizer shows while the phone's setup makes or imports a randomized game the page asked
// for (RandomizerProgress.of over the setup job's records).
import org.wwhdrecomp.wwhd.RandomizerProgress;

public final class RandomizerProgressTest {
    static void check(boolean ok, String what) { if (!ok) throw new AssertionError(what); }

    static RandomizerProgress.Request asked(String job, boolean importing) {
        RandomizerProgress.Request r = new RandomizerProgress.Request();
        r.job = job;
        r.importing = importing;
        r.since = 1000;
        return r;
    }

    static RandomizerProgress.Job job(String host, long updated, String event) {
        RandomizerProgress.Job j = new RandomizerProgress.Job();
        j.selected = true;
        j.host = host;
        j.updated = updated;
        j.event = event;
        return j;
    }

    public static void main(String[] args) {
        RandomizerProgress.Request picking = new RandomizerProgress.Request();
        picking.picking = true;
        RandomizerProgress p = RandomizerProgress.of(picking, new RandomizerProgress.Job());
        check(p.state.equals("picking") && p.stage.contains("output folder"), "the folder picker is open");

        RandomizerProgress.Request starting = new RandomizerProgress.Request();
        starting.starting = true;
        p = RandomizerProgress.of(starting, new RandomizerProgress.Job());
        check(p.state.equals("building") && p.stage.equals("Starting setup"), "the job is being made");

        RandomizerProgress.Request closed = new RandomizerProgress.Request();
        closed.cancelled = true;
        check(RandomizerProgress.of(closed, new RandomizerProgress.Job()).state.equals("cancelled"), "picker closed");

        RandomizerProgress.Request refused = new RandomizerProgress.Request();
        refused.refusal = "Choose the folder with code, content and meta";
        p = RandomizerProgress.of(refused, new RandomizerProgress.Job());
        check(p.state.equals("failed") && p.error.equals(refused.refusal), "a refused folder says why");

        // the job's records, before and while it runs
        check(RandomizerProgress.of(asked("j", false), job("selected", 0, "")).stage.equals("Starting setup"), "new job");
        // an older record (before the request) is not this job's
        check(RandomizerProgress.of(asked("j", false), job("failed", 10, "")).state.equals("building"), "stale failure");
        check(RandomizerProgress.of(asked("j", true), job("importing", 2000, "")).stage.equals("Copying the seed's files"), "import copy");
        check(RandomizerProgress.of(asked("j", false), job("running", 2000, "randomize")).stage.contains("TWWHD Randomizer"), "randomize");
        check(RandomizerProgress.of(asked("j", true), job("running", 2000, "randomize")).stage.equals("Checking the seed"), "import check");
        check(RandomizerProgress.of(asked("j", false), job("running", 2000, "translate")).stage.equals("Translating the game code"), "translate");
        RandomizerProgress.Job compiling = job("running", 2000, "compile");
        compiling.compiled = 30;
        compiling.reused = 10;
        compiling.eventTotal = 400;
        p = RandomizerProgress.of(asked("j", false), compiling);
        check(p.state.equals("building") && p.done == 40 && p.total == 400 && p.stage.equals("Compiling the game code"), "compile progress");
        RandomizerProgress.Job paused = job("paused", 2000, "compile");
        paused.reason = "heat";
        p = RandomizerProgress.of(asked("j", false), paused);
        check(p.state.equals("building") && p.stage.contains("paused (heat)") && p.stage.contains("setup screen"), "paused");
        check(RandomizerProgress.of(asked("j", false), job("warming shaders", 2000, "activate")).state.equals("building"), "warming");

        // the end: active is ready (even while setup still warms shaders); failures say why
        RandomizerProgress.Job active = job("warming shaders", 2000, "activate");
        active.active = true;
        check(RandomizerProgress.of(asked("j", false), active).state.equals("ready"), "active is ready");
        RandomizerProgress.Job failed = job("failed", 2000, "compile");
        failed.hostError = "Not enough free space";
        p = RandomizerProgress.of(asked("j", false), failed);
        check(p.state.equals("failed") && p.error.equals("Not enough free space"), "failure message");
        check(!RandomizerProgress.of(asked("j", false), job("failed", 2000, "")).error.isEmpty(), "failure without message");
        check(RandomizerProgress.of(asked("j", false), job("complete", 2000, "")).state.equals("failed"), "complete, not active");
        RandomizerProgress.Job replaced = job("running", 2000, "compile");
        replaced.selected = false;
        check(RandomizerProgress.of(asked("j", false), replaced).state.equals("failed"), "another job replaced it");
        System.out.println("RandomizerProgressTest: ok");
    }
}
