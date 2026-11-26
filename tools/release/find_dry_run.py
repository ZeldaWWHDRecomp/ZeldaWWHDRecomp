#!/usr/bin/env python3
"""Publish a release from a green dry run instead of rebuilding it (release.yml, job "version" and "reuse").

usage: find_dry_run.py find  --repo OWNER/NAME --tag vX.Y.Z --sha COMMIT
       find_dry_run.py check --repo OWNER/NAME --tag vX.Y.Z --sha COMMIT --run RUN_ID
       find_dry_run.py files --artifact release-PLATFORM --version vX.Y.Z --template-ref FILE DIR

find: the newest run of release.yml that may be published as TAG (all checks below pass); prints its id on
stdout, or nothing when there is none (each rejected run and why goes to stderr). Exit status 0 either way;
1 only when the GitHub API cannot be asked (the caller then builds as before).
check: the same checks for one run (the from_run input); exit status 1 with the reasons when it fails.
files: an artifact downloaded from that run holds exactly the release files of TAG for its platform (names,
the zip's single top folder, sdk/manifest.json version and platform, the pinned shader template).

A run qualifies only if: it is release.yml of this repository (not a fork), started by workflow_dispatch,
completed with success, on exactly the tag's commit; every build job and the publish job succeeded, the
guard and checksum steps ran and both publish steps were skipped (a dry run: nothing was published, no
development build); its version job built version TAG (the "version" input of a dry run on main, or a dry
run on the tag itself); and all release artifacts are there and stay downloadable for at least an hour.
The GitHub API is reached with the gh CLI (GH_TOKEN), read-only.
"""
import argparse
import datetime
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import zipfile

WORKFLOW = ".github/workflows/release.yml"
# artifact name -> the platform part of its file names; the files each holds (release.yml's uploads)
ARTIFACTS = {
    "release-macos-arm64": ("macos-arm64", (".zip",)),
    "release-linux-x86_64": ("linux-x86_64", (".zip", ".AppImage")),
    "release-linux-aarch64": ("linux-aarch64", (".zip", ".AppImage")),
    "release-windows-x86_64": ("windows-x86_64", (".zip",)),
    "release-windows-arm64": ("windows-arm64", (".zip",)),
    "release-android-arm64": ("android-arm64", (".apk",)),
    "release-android-x86_64": ("android-x86_64", (".apk",)),
}
BUILD_JOBS = ("version", "macos", "linux-x86_64", "linux-aarch64", "windows-x86_64", "windows-arm64",
              "android-arm64", "android-x86_64", "publish")
PUBLISH_STEPS = {"Guard all artifacts again": "success", "Checksums and release notes": "success",
                 "Publish GitHub Release": "skipped", "Publish development pre-release": "skipped"}
# the version job's own log line (release.yml): "version: version=v0.6.0, publish: false"
VERSION_LINE = re.compile(r"^\S*\s*version: version=(\S+), publish: (true|false)\s*$", re.M)
MIN_LEFT = datetime.timedelta(hours=1)
TEMPLATE = "tools/shadercache/template.bin"


def gh_api(path, raw=False):
    out = subprocess.run(["gh", "api", path], check=True, stdout=subprocess.PIPE).stdout
    return out.decode("utf-8", "replace") if raw else json.loads(out)


def parse_time(s):
    return datetime.datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)


def meta_problems(run, repo, sha):
    """The checks on the run itself (no further API calls)."""
    p = []
    if run.get("path") != WORKFLOW:
        p.append("not %s (%s)" % (WORKFLOW, run.get("path")))
    for key in ("repository", "head_repository"):
        name = (run.get(key) or {}).get("full_name")
        if name != repo:
            p.append("%s is %s, not %s" % (key, name, repo))
    if run.get("event") != "workflow_dispatch":
        p.append("started by %s, not workflow_dispatch (not a dry run)" % run.get("event"))
    if run.get("status") != "completed" or run.get("conclusion") != "success":
        p.append("%s/%s, not completed/success" % (run.get("status"), run.get("conclusion")))
    if run.get("head_sha") != sha:
        p.append("commit %s, not the tag's %s" % (run.get("head_sha"), sha))
    return p


def run_problems(run, jobs, artifacts, version_log, repo, tag, sha, now):
    """Why `run` (the API's workflow run) must not be published as `tag` at commit `sha`; [] = it may.
    jobs: the run's latest jobs; artifacts: its artifacts; version_log: its version job's log (or None)."""
    p = meta_problems(run, repo, sha)
    if p:
        return p
    by_name = {}
    for j in jobs:
        by_name.setdefault(j.get("name"), []).append(j)
    for name in BUILD_JOBS:
        got = by_name.get(name, [])
        if len(got) != 1 or got[0].get("conclusion") != "success":
            p.append("job %s: %s" % (name, ", ".join(str(j.get("conclusion")) for j in got) or "missing"))
    publish = by_name.get("publish", [{}])[0]
    steps = {s.get("name"): s.get("conclusion") for s in publish.get("steps") or []}
    for step, want in PUBLISH_STEPS.items():
        if steps.get(step) != want:
            p.append("publish step %r: %s, not %s%s" % (step, steps.get(step), want,
                     " (this run published something: not a dry run)" if want == "skipped" else ""))
    m = VERSION_LINE.findall(version_log or "")
    if len(m) != 1:
        p.append("version job log: no single 'version: version=..., publish: ...' line")
    else:
        version, publish_flag = m[0]
        if version != tag:
            p.append("built as version %s, not %s (dry run on main without the version input?)" % (version, tag))
        if publish_flag != "false":
            p.append("version job says publish: %s (not a dry run)" % publish_flag)
    art = {}
    for a in artifacts:
        art.setdefault(a.get("name"), []).append(a)
    for name in ARTIFACTS:
        got = art.get(name, [])
        if len(got) != 1:
            p.append("artifact %s: %d found, want 1" % (name, len(got)))
            continue
        a = got[0]
        if a.get("expired") or not a.get("expires_at"):
            p.append("artifact %s expired" % name)
        elif parse_time(a["expires_at"]) - now < MIN_LEFT:
            p.append("artifact %s expires at %s" % (name, a["expires_at"]))
    return p


def details(api, repo, run):
    rid = run["id"]
    jobs = api("repos/%s/actions/runs/%s/jobs?filter=latest&per_page=100" % (repo, rid))["jobs"]
    artifacts = api("repos/%s/actions/runs/%s/artifacts?per_page=100" % (repo, rid))["artifacts"]
    log = None
    version_jobs = [j for j in jobs if j.get("name") == "version"]
    if len(version_jobs) == 1:
        try:
            log = api("repos/%s/actions/jobs/%s/logs" % (repo, version_jobs[0]["id"]), raw=True)
        except subprocess.CalledProcessError:
            log = None
    return jobs, artifacts, log


def check_run(api, repo, tag, sha, run, now):
    cheap = meta_problems(run, repo, sha)
    if cheap:
        return cheap  # the jobs, artifacts and log need more API calls
    return run_problems(run, *details(api, repo, run), repo, tag, sha, now)


def candidates(api, repo, sha):
    """release.yml runs started by workflow_dispatch on `sha`, newest first."""
    runs = api("repos/%s/actions/workflows/release.yml/runs?event=workflow_dispatch&head_sha=%s&per_page=100"
               % (repo, sha))["workflow_runs"]
    return sorted(runs, key=lambda r: (r.get("created_at") or "", r["id"]), reverse=True)


def find(api, repo, tag, sha, now, log=sys.stderr):
    for run in candidates(api, repo, sha):
        problems = check_run(api, repo, tag, sha, run, now)
        if not problems:
            print("run %s (%s): a green dry run of %s as %s" % (run["id"], run.get("html_url"), sha[:8], tag), file=log)
            return run["id"]
        print("run %s: not usable: %s" % (run["id"], "; ".join(problems)), file=log)
    return None


def file_problems(artifact, version, directory, template_sha256):
    """Why the downloaded `artifact` in `directory` is not exactly the release files of `version`."""
    if artifact not in ARTIFACTS:
        return ["unknown artifact %s" % artifact]
    platform, suffixes = ARTIFACTS[artifact]
    base = "WindWakerHD-%s-%s" % (version, platform)
    want = {base + s for s in suffixes}
    have = set(os.listdir(directory))
    p = []
    if have != want:
        p.append("files %s, want %s" % (sorted(have), sorted(want)))
    for name in sorted(want & have):
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            p.append("%s is not a file" % name)
            continue
        if name.endswith(".zip"):
            p += zip_problems(path, base, version, platform, template_sha256)
        elif name.endswith(".apk"):
            p += apk_problems(path, template_sha256)
    return p


def zip_problems(path, base, version, platform, template_sha256):
    p = []
    name = os.path.basename(path)
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
        outside = [n for n in names if not n.startswith(base + "/")]
        if outside:
            p.append("%s: %d entries outside %s/ (%s)" % (name, len(outside), base, outside[0]))
        try:
            m = json.loads(z.read(base + "/sdk/manifest.json"))
            if m.get("version") != version or m.get("platform") != platform:
                p.append("%s: sdk/manifest.json is %s %s, not %s %s" % (name, m.get("version"), m.get("platform"),
                                                                        version, platform))
        except (KeyError, ValueError) as e:
            p.append("%s: sdk/manifest.json unreadable (%s)" % (name, e))
        p += template_problems(z, base + "/" + TEMPLATE, name, template_sha256)
    return p


def apk_problems(path, template_sha256):
    name = os.path.basename(path)
    with zipfile.ZipFile(path) as apk:
        try:
            source = zipfile.ZipFile(io.BytesIO(apk.read("assets/python-source.zip")))
        except (KeyError, zipfile.BadZipFile) as e:
            return ["%s: assets/python-source.zip unreadable (%s)" % (name, e)]
        with source:
            return template_problems(source, TEMPLATE, name + " assets/python-source.zip", template_sha256)


def template_problems(z, member, where, template_sha256):
    try:
        got = hashlib.sha256(z.read(member)).hexdigest()
    except KeyError:
        return ["%s: no %s (a release carries the pinned shader template)" % (where, member)]
    return [] if got == template_sha256 else ["%s: %s is %s, not the pinned %s" % (where, member, got, template_sha256)]


def main(argv=None, api=gh_api, now=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for cmd in ("find", "check"):
        s = sub.add_parser(cmd)
        s.add_argument("--repo", required=True)
        s.add_argument("--tag", required=True)
        s.add_argument("--sha", required=True)
        if cmd == "check":
            s.add_argument("--run", required=True)
    s = sub.add_parser("files")
    s.add_argument("--artifact", required=True)
    s.add_argument("--version", required=True)
    s.add_argument("--template-ref", required=True)
    s.add_argument("dir")
    a = ap.parse_args(argv)
    now = now or datetime.datetime.now(datetime.timezone.utc)
    if a.cmd == "files":
        with open(a.template_ref) as f:
            pin = json.load(f)["sha256"]
        problems = file_problems(a.artifact, a.version, a.dir, pin)
        for line in problems:
            print("::error::%s: %s" % (a.artifact, line))
        if not problems:
            print("%s: the release files of %s (%s)" % (a.artifact, a.version, ", ".join(sorted(os.listdir(a.dir)))))
        return 1 if problems else 0
    if not re.fullmatch(r"[0-9a-f]{40}", a.sha):
        print("::error::--sha must be a full commit id (got %r)" % a.sha)
        return 1
    if a.cmd == "find":
        try:
            rid = find(api, a.repo, a.tag, a.sha, now)
        except (subprocess.CalledProcessError, KeyError, ValueError) as e:
            print("::warning::cannot look for a dry run (%s)" % e, file=sys.stderr)
            return 1
        if rid:
            print(rid)
        return 0
    if not re.fullmatch(r"[0-9]+", a.run):
        print("::error::from_run must be a run id (got %r)" % a.run)
        return 1
    try:
        run = api("repos/%s/actions/runs/%s" % (a.repo, a.run))
        problems = check_run(api, a.repo, a.tag, a.sha, run, now)
    except (subprocess.CalledProcessError, KeyError, ValueError) as e:
        problems = ["cannot read run %s from the GitHub API (%s)" % (a.run, e)]
    for line in problems:
        print("::error::run %s: %s" % (a.run, line))
    if not problems:
        print("run %s (%s): a green dry run of %s as %s" % (a.run, run.get("html_url"), a.sha[:8], a.tag))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
