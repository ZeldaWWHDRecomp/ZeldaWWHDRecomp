# GitLab CI (dormant backup)

`.gitlab-ci.yml` is a disaster-recovery copy of the GitHub Actions setup. If the GitHub project ever goes away,
a private GitLab mirror of this repository can build and publish releases with it.

It never runs by itself. The mirror pushes every branch and tag to GitLab, but pushes and merge requests never
start a pipeline. A pipeline starts only when you start one by hand, with a trigger token, or from a schedule or
API call that sets `WWHD_GITLAB_CI=1`.

The GitLab jobs don't copy the build steps. Each job runs the steps of the matching GitHub job
(`ci/gitlab/gh_steps.py`), so changes to the GitHub steps reach GitLab by themselves. When a GitHub job or a
`uses:` action is added or removed, update `.gitlab-ci.yml` as well. A test run fails on anything it doesn't know.

## Run a pipeline

Go to **Build > Pipelines > Run pipeline**, pick the branch or tag, and add variables if you need them:

| Variable | Effect |
|---|---|
| none | CI pipeline: every job waits for you to press its play button |
| `WWHD_CI_AUTO=1` | CI jobs start at once (Mac jobs too, if `WWHD_MACOS_RUNNER=1`) |
| `WWHD_RELEASE=1` on a tag `vX.Y.Z` | builds everything `release.yml` builds and publishes a GitLab release |
| `WWHD_RELEASE=1` + `WWHD_VERSION=vX.Y.Z` on a branch | the same; the tag must already exist and point at that commit |
| `WWHD_RELEASE=1` + `WWHD_DRY_RUN=1` | builds and checks the release but doesn't publish it |
| `WWHD_RELEASE=1` + `WWHD_DEVEL_BUILD=1` | development build `<last tag>-devel.<commit>`, kept as job artifacts for 30 days |
| `WWHD_SKIP_MACOS=1` | release without the macOS build (no Mac runner) |
| `WWHD_WINDOWS_ARM64=1` | release also builds Windows arm64 (needs a self-hosted runner tagged `wwhd-windows-arm64`) |
| `WWHD_WINDOWS_TAG=wwhd-windows` | Windows jobs use your own Windows runner instead of GitLab's |
| `WWHD_ANDROID_EMULATOR=1` | emulator tests on a self-hosted Linux runner with KVM (tag `wwhd-linux-kvm`) |

A published release puts its files in **Deploy > Package registry** (package `wwhd`). The release page links to
them. Development builds aren't published as a rolling pre-release like `devel-build` on GitHub: that tag comes
from GitHub through the mirror.

## CI/CD variables (Settings > CI/CD > Variables)

Add these yourself. They use the same names as the GitHub secrets. Leave a variable out and the build still
works as it does on a GitHub fork: an unsigned (debug-key) APK, no shader template, no VirusTotal scan.

| Variable | Type / flags | Notes |
|---|---|---|
| `ANDROID_KEYSTORE_B64` | Variable, masked, protected | base64 keystore, one line |
| `ANDROID_KEY_ALIAS` | Variable, protected | |
| `ANDROID_KEYSTORE_PASSWORD` | Variable, masked if GitLab accepts its characters, protected | |
| `SHADER_TEMPLATE_DEPLOY_KEY` | **File**, protected | private half of a read-only deploy key on the shader-template project (below). A masked one-line base64 copy of the key works too |
| `VT_API_KEY` | Variable, masked, protected | VirusTotal scan of published releases |

Notes:
- Instead of the deploy key, you can set `WWHD_SHADER_TEMPLATE_JOB_TOKEN=1`. You then also need to add this
  project to the job token allowlist of the shader-template project (**Settings > CI/CD > Job token permissions**).
- The shader template comes from a private `shader-template` project in the same group/namespace as this
  project. `SHADER_TEMPLATE_REPOSITORY` (an SSH URL) overrides it.
- Protected variables only reach pipelines on protected branches and tags. Protect the tags `v*` (and `devel` if
  you want signed development builds) under **Settings > Repository**. The mirror's deploy key must stay allowed to
  push to them, or the mirror stops updating those refs.
- Published releases and development builds need the shader template, as on GitHub. Without it they stop early
  with a clear message.
- APK version codes on GitLab are 100000 + the pipeline number, so a GitLab APK installs over a GitHub one.
  `WWHD_RUN_NUMBER` overrides this.

## macOS runner (only when needed)

GitLab Free has no hosted macOS runners. The macOS jobs (`release:macos`, `decomp:macos`, `guestmods:modules-macos`)
run on the Mac, and only after you register it. Register it only when you need it:

```sh
brew install gitlab-runner
# Settings > CI/CD > Runners > New project runner: tag wwhd-macos, "Run untagged jobs" off; copy the token
gitlab-runner register --non-interactive --url https://gitlab.com --token <runner token> \
  --executor shell --description wwhd-mac
gitlab-runner run        # in a terminal while the jobs run; Ctrl+C stops it
```

The Mac needs the GitHub `macos-15` tools: Xcode 16 at `/Applications/Xcode_16.4.app` (a symlink to your Xcode
is fine), Homebrew, and Python 3 with PyYAML. The jobs run as your user and install `ninja` (and
`llvm`/`lld` for guestmods) with Homebrew. To remove the runner afterwards, run
`gitlab-runner unregister --all-runners` and delete it in the GitLab UI.

## Windows

GitLab Free includes hosted Windows runners (`saas-windows-medium-amd64`: 2 vCPU, Windows Server 2022, cost
factor 1). They are still in **beta**: each job waits about 5 minutes for a new VM, and GitLab warns that jobs may
queue and that breaking changes can come. `ci/gitlab/windows_job.ps1` installs the pinned Python 3.14.8, MSYS2 and
CMake first. GitLab has no hosted Windows on Arm, so the `windows-arm64` release file needs your own runner
(`WWHD_WINDOWS_ARM64=1`). If the hosted runner doesn't work out, register a Windows machine with the tag
`wwhd-windows` and run with `WWHD_WINDOWS_TAG=wwhd-windows`.

## Minute budget

GitLab Free gives 400 compute minutes a month. Linux medium (4 vCPU, the size GitHub uses) counts double, and
every hosted job stops after 3 hours. Estimates from the GitHub run times:

| Run | Compute minutes |
|---|---|
| `linux:build`, warm compiler cache | 20–60 |
| `linux:build`, first run (cold cache) | up to 360; may hit the 3-hour limit |
| `android:apk` | 30–60 |
| `windows:build` | 20–60 |
| Full release, warm caches (no Mac) | about 450 |
| Full release, first time (builds the Android compiler twice) | 700–900 |

A release doesn't fit into the free minutes. Buy extra minutes (GitLab sells them in packs) or register your own
Linux runner for the heaviest jobs. The macOS jobs on your own Mac cost no minutes. Usage is under
**Settings > Usage quotas**.

## First test run

1. **Run pipeline** on `devel` without variables.
2. Start `android:apk` (play button), then `linux:build`. Start `linux:build` alone, since its first run can use
   most of the month's minutes.
3. Check the job logs. Each GitHub step is a collapsible section, and anything GitLab can't do yet is named in
   the log.

## Not mapped

- `reopen.yml`: GitHub issue automation (`/reopen`), GitLab issues don't need it.
- `release.yml` nightly schedule and `android-compiler-cache`: GitLab doesn't run on its own, and the release
  jobs build and cache the Android compiler themselves.
- Emulator tests (`android-display-smoke`, the emulator steps of `android-toolchain`): GitLab's hosted runners
  have no KVM. They run only on your own Linux runner with `/dev/kvm` passed to Docker (`WWHD_ANDROID_EMULATOR=1`).
- The builds still download pinned third-party sources from github.com (SDL, Vulkan headers, glslang,
  llvm-mingw, MoltenVK, python-build-standalone, LLVM, the public decomp). They work as long as github.com is
  reachable. Point `WWHD_MIRROR_ZELDAWWHDDECOMP_WWHD` at a mirror of the decomp if needed.
