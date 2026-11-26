"""find_dry_run.py: which dry run a tag may publish (offline fixtures; WWHD_LIVE_GH=1 also asks the real API).

python3 tools/release/test_find_dry_run.py              # offline
WWHD_LIVE_GH=1 python3 tools/release/test_find_dry_run.py  # + the v0.5.2 runs on GitHub (read-only, gh CLI)
"""
import datetime
import hashlib
import io
import json
import os
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import find_dry_run as f  # noqa: E402

REPO = "ZeldaWWHDRecomp/ZeldaWWHDRecomp"
SHA = "87834c8646e1a606a2d5fb5c9bd5993319f6c8b6"
NOW = datetime.datetime(2026, 11, 26, 13, 0, tzinfo=datetime.timezone.utc)
LIVE = os.environ.get("WWHD_LIVE_GH") == "1"


def good_run(rid=100, created="2026-11-26T09:58:55Z", sha=SHA):
    return {"id": rid, "path": f.WORKFLOW, "event": "workflow_dispatch", "status": "completed",
            "conclusion": "success", "head_sha": sha, "created_at": created, "html_url": "u/%d" % rid,
            "repository": {"full_name": REPO}, "head_repository": {"full_name": REPO}}


def good_jobs():
    jobs = [{"id": i, "name": n, "conclusion": "success", "steps": []} for i, n in enumerate(f.BUILD_JOBS)]
    jobs[-1]["steps"] = [{"name": n, "conclusion": c} for n, c in f.PUBLISH_STEPS.items()]
    jobs.append({"id": 99, "name": "android-compiler-cache", "conclusion": "skipped", "steps": []})
    return jobs


def good_artifacts():
    arts = [{"name": n, "expired": False, "expires_at": "2027-02-24T09:58:57Z"} for n in f.ARTIFACTS]
    return arts + [{"name": "release-all", "expired": False, "expires_at": "2027-02-24T09:58:57Z"}]


LOG = "2026-11-26T10:16:35.4498366Z \x1b[36;1mecho \"version: $(grep version= \"$GITHUB_OUTPUT\"), publish: $publish\"\n" \
      "2026-11-26T10:16:35.4882340Z version: version=v0.6.0, publish: false\n"


class FakeApi:
    def __init__(self, runs):
        self.runs = {r["id"]: r for r in runs}
        self.jobs, self.arts, self.logs = {}, {}, {}

    def __call__(self, path, raw=False):
        if raw:
            return self.logs[int(path.split("/")[-2])]
        if "/workflows/release.yml/runs" in path:
            return {"workflow_runs": [r for r in self.runs.values() if r["head_sha"] in path]}
        rid = int(path.split("/runs/")[1].split("/")[0].split("?")[0])
        if "/jobs" in path:
            return {"jobs": self.jobs[rid]}
        if "/artifacts" in path:
            return {"artifacts": self.arts[rid]}
        return self.runs[rid]

    def add(self, run, jobs=None, arts=None, log=LOG):
        self.runs[run["id"]] = run
        self.jobs[run["id"]] = jobs if jobs is not None else good_jobs()
        for j in self.jobs[run["id"]]:
            if j["name"] == "version":
                j["id"] = run["id"] * 1000
                self.logs[j["id"]] = log
        self.arts[run["id"]] = arts if arts is not None else good_artifacts()


def problems(run=None, jobs=None, arts=None, log=LOG, tag="v0.6.0", sha=SHA):
    return f.run_problems(run or good_run(), good_jobs() if jobs is None else jobs,
                          good_artifacts() if arts is None else arts, log, REPO, tag, sha, NOW)


class RunChecks(unittest.TestCase):
    def test_green_dry_run(self):
        self.assertEqual(problems(), [])

    def test_wrong_commit(self):
        self.assertIn("not the tag's", " ".join(problems(sha="1" * 40)))

    def test_failed_or_running(self):
        for status, conclusion in (("completed", "failure"), ("completed", "cancelled"), ("in_progress", None)):
            r = good_run()
            r.update(status=status, conclusion=conclusion)
            self.assertTrue(problems(run=r), (status, conclusion))

    def test_not_dispatch(self):
        for event in ("push", "schedule", "pull_request"):
            r = good_run()
            r["event"] = event
            self.assertIn("not workflow_dispatch", " ".join(problems(run=r)))

    def test_other_workflow_or_fork(self):
        r = good_run()
        r["path"] = ".github/workflows/linux.yml"
        self.assertTrue(problems(run=r))
        r = good_run()
        r["head_repository"] = {"full_name": "someone/fork"}
        self.assertIn("head_repository", " ".join(problems(run=r)))

    def test_published_or_devel_build(self):
        for step in ("Publish GitHub Release", "Publish development pre-release"):
            jobs = good_jobs()
            for s in jobs[-2]["steps"]:  # [-1] is android-compiler-cache
                if s["name"] == step:
                    s["conclusion"] = "success"
            self.assertIn("not a dry run", " ".join(problems(jobs=jobs)), step)

    def test_guard_not_run(self):
        jobs = good_jobs()
        jobs[-2]["steps"] = [s for s in jobs[-2]["steps"] if s["name"] != "Guard all artifacts again"]
        self.assertIn("Guard all artifacts again", " ".join(problems(jobs=jobs)))

    def test_build_job_skipped_or_missing(self):
        jobs = [j for j in good_jobs() if j["name"] != "windows-arm64"]
        self.assertIn("windows-arm64: missing", " ".join(problems(jobs=jobs)))
        jobs = good_jobs()
        jobs[1]["conclusion"] = "skipped"  # e.g. a run that itself reused a dry run
        self.assertIn("macos: skipped", " ".join(problems(jobs=jobs)))

    def test_version(self):
        self.assertIn("built as version dryrun-87834c86",
                      " ".join(problems(log="T version: version=dryrun-87834c86, publish: false\n")))
        self.assertIn("publish: true", " ".join(problems(log="T version: version=v0.6.0, publish: true\n")))
        self.assertIn("no single", " ".join(problems(log=None)))
        self.assertIn("not v0.6.1", " ".join(problems(tag="v0.6.1")))

    def test_artifacts(self):
        arts = [a for a in good_artifacts() if a["name"] != "release-android-x86_64"]
        self.assertIn("release-android-x86_64: 0 found", " ".join(problems(arts=arts)))
        arts = good_artifacts()
        arts[0]["expired"] = True
        self.assertIn("expired", " ".join(problems(arts=arts)))
        arts = good_artifacts()
        arts[0]["expires_at"] = "2026-11-26T13:30:00Z"
        self.assertIn("expires at", " ".join(problems(arts=arts)))


class Find(unittest.TestCase):
    def test_newest_green_wins_and_bad_ones_are_skipped(self):
        api = FakeApi([])
        api.add(good_run(1, "2026-11-26T08:00:00Z"))
        newer_failed = good_run(2, "2026-11-26T09:00:00Z")
        newer_failed["conclusion"] = "failure"
        api.add(newer_failed)
        api.add(good_run(3, "2026-11-26T10:00:00Z"), log="T version: version=dryrun-87834c86, publish: false\n")
        log = io.StringIO()
        self.assertEqual(f.find(api, REPO, "v0.6.0", SHA, NOW, log=log), 1)
        self.assertIn("run 3: not usable: built as version dryrun", log.getvalue())
        self.assertIn("run 2: not usable", log.getvalue())
        api.add(good_run(4, "2026-11-26T11:00:00Z"))
        self.assertEqual(f.find(api, REPO, "v0.6.0", SHA, NOW, log=io.StringIO()), 4)

    def test_none(self):
        api = FakeApi([])
        self.assertIsNone(f.find(api, REPO, "v0.6.0", SHA, NOW, log=io.StringIO()))

    def test_cli(self):
        api = FakeApi([])
        api.add(good_run(7))
        out = io.StringIO()
        sys.stdout, saved = out, sys.stdout
        try:
            self.assertEqual(f.main(["find", "--repo", REPO, "--tag", "v0.6.0", "--sha", SHA], api=api, now=NOW), 0)
            self.assertEqual(f.main(["check", "--repo", REPO, "--tag", "v0.6.0", "--sha", SHA, "--run", "7"],
                                    api=api, now=NOW), 0)
            self.assertEqual(f.main(["check", "--repo", REPO, "--tag", "v0.6.1", "--sha", SHA, "--run", "7"],
                                    api=api, now=NOW), 1)
            self.assertEqual(f.main(["check", "--repo", REPO, "--tag", "v0.6.0", "--sha", SHA, "--run", "7; x"],
                                    api=api, now=NOW), 1)
            self.assertEqual(f.main(["find", "--repo", REPO, "--tag", "v0.6.0", "--sha", "abc"], api=api, now=NOW), 1)
        finally:
            sys.stdout = saved
        self.assertTrue(out.getvalue().startswith("7\n"))


PIN = hashlib.sha256(b"template").hexdigest()


def make_zip(path, base, version, platform, template=b"template", extra=None):
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(base + "/CHANGELOG.md", "x")
        z.writestr(base + "/sdk/manifest.json", json.dumps({"version": version, "platform": platform}))
        if template is not None:
            z.writestr(base + "/" + f.TEMPLATE, template)
        if extra:
            z.writestr(extra, "x")


def make_apk(path, template=b"template"):
    inner = io.BytesIO()
    with zipfile.ZipFile(inner, "w") as z:
        z.writestr("tools/installer/setup.py", "x")
        if template is not None:
            z.writestr(f.TEMPLATE, template)
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("assets/python-source.zip", inner.getvalue())


class Files(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def p(self, artifact, version="v0.6.0"):
        return f.file_problems(artifact, version, self.dir, PIN)

    def test_zip_and_appimage(self):
        base = "WindWakerHD-v0.6.0-linux-x86_64"
        make_zip(os.path.join(self.dir, base + ".zip"), base, "v0.6.0", "linux-x86_64")
        self.assertIn("want", " ".join(self.p("release-linux-x86_64")))  # the AppImage is missing
        open(os.path.join(self.dir, base + ".AppImage"), "wb").close()
        self.assertEqual(self.p("release-linux-x86_64"), [])
        self.assertTrue(self.p("release-linux-x86_64", "v0.6.1"))
        self.assertTrue(self.p("release-linux-aarch64"))
        open(os.path.join(self.dir, "stray.txt"), "wb").close()
        self.assertTrue(self.p("release-linux-x86_64"))

    def test_zip_contents(self):
        base = "WindWakerHD-v0.6.0-macos-arm64"
        z = os.path.join(self.dir, base + ".zip")
        for kwargs, want in ((dict(version="dryrun-1"), "manifest"), (dict(template=None), "template.bin (a release carries"),
                             (dict(template=b"other"), "not the pinned"), (dict(extra="evil/x"), "outside")):
            args = dict(version="v0.6.0", template=b"template")
            args.update(kwargs)
            make_zip(z, base, args.pop("version"), "macos-arm64", **args)
            self.assertIn(want, " ".join(self.p("release-macos-arm64")), kwargs)
        make_zip(z, base, "v0.6.0", "macos-arm64")
        self.assertEqual(self.p("release-macos-arm64"), [])

    def test_apk(self):
        apk = os.path.join(self.dir, "WindWakerHD-v0.6.0-android-arm64.apk")
        make_apk(apk, template=None)
        self.assertIn("no tools/shadercache", " ".join(self.p("release-android-arm64")))
        make_apk(apk)
        self.assertEqual(self.p("release-android-arm64"), [])
        self.assertTrue(self.p("release-android-x86_64"))

    def test_unknown(self):
        self.assertTrue(self.p("release-all"))


@unittest.skipUnless(LIVE, "set WWHD_LIVE_GH=1 to ask the GitHub API (read-only, gh CLI)")
class LiveV052(unittest.TestCase):
    """v0.5.2: dry run 45796994531 on main @ 87834c86, tag push run 45817133536 on the same commit. That dry run
    was built as "dryrun-87834c86" (before the version input existed), so it passes every check but the version."""
    DRY, TAG_RUN = 45796994531, 45817133536

    def run_of(self, rid):
        return f.gh_api("repos/%s/actions/runs/%d" % (REPO, rid))

    def test_dry_run_passes_all_but_the_version(self):
        now = datetime.datetime.now(datetime.timezone.utc)
        got = f.check_run(f.gh_api, REPO, "v0.5.2", SHA, self.run_of(self.DRY), now)
        self.assertEqual(len(got), 1, got)
        self.assertIn("built as version dryrun-87834c86, not v0.5.2", got[0])
        # as if it had been built with version=dryrun-87834c86 as the tag name: chosen
        self.assertEqual(f.find(f.gh_api, REPO, "dryrun-87834c86", SHA, now, log=io.StringIO()), self.DRY)
        self.assertIsNone(f.find(f.gh_api, REPO, "v0.5.2", SHA, now, log=io.StringIO()))

    def test_wrong_sha_and_non_dry_runs(self):
        now = datetime.datetime.now(datetime.timezone.utc)
        self.assertIn("not the tag's", " ".join(
            f.check_run(f.gh_api, REPO, "dryrun-87834c86", "b4f93546" + "0" * 32, self.run_of(self.DRY), now)))
        self.assertIn("not workflow_dispatch", " ".join(
            f.check_run(f.gh_api, REPO, "v0.5.2", SHA, self.run_of(self.TAG_RUN), now)))
        runs = f.gh_api("repos/%s/actions/workflows/release.yml/runs?event=workflow_dispatch&per_page=100" % REPO)
        runs = runs["workflow_runs"]
        failed = next(r for r in runs if r["conclusion"] == "failure")
        self.assertIn("not completed/success", " ".join(
            f.check_run(f.gh_api, REPO, "v0.5.2", failed["head_sha"], failed, now)))
        # a green development build (published the devel-build pre-release): not a dry run
        devel = next(r for r in runs if r["conclusion"] == "success" and r["head_branch"] == "devel"
                     and f.parse_time(r["created_at"]) > now - datetime.timedelta(days=80))
        got = " ".join(f.check_run(f.gh_api, REPO, "v0.5.2", devel["head_sha"], devel, now))
        self.assertIn("Publish development pre-release", got)
        self.assertIn("not a dry run", got)


if __name__ == "__main__":
    unittest.main()
