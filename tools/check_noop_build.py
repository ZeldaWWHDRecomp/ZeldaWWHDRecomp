"""A build with nothing changed does nothing: builds a configured Ninja build directory once (to settle it),
then again with `ninja -d explain`, and fails when the second build ran anything but the steps that run
on every build (the glob check and the build info, cmake/BuildInfo.cmake). It prints ninja's reason for
each extra step, the file that made it dirty.

    python3 tools/check_noop_build.py build/rel [--recomp game/code/cking.rpx build/gen --mod-hooks]

With --recomp the game code is generated again between the builds first (as the build steps in the
README do): an unchanged game and recompiler must not rebuild it either."""
import os
import re
import subprocess
import sys

ALWAYS = ("Re-checking globbed directories", "Build info (version, commit)")


def build(directory, explain):
    command = ["cmake", "--build", directory, "-j4"] + (["--", "-d", "explain"] if explain else [])
    run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if run.returncode:
        sys.exit("build failed:\n" + run.stdout[-4000:])
    return run.stdout.splitlines()


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    directory, recomp = args[0], None
    if len(args) > 2 and args[1] == "--recomp":
        recomp = args[2:]
    build(directory, False)
    if recomp:
        here = os.path.dirname(os.path.abspath(__file__))
        subprocess.run([sys.executable, os.path.join(here, "recomp", "recomp.py")] + recomp,
                       check=True, stdout=subprocess.DEVNULL)
    lines = build(directory, True)
    steps = [m.group(1) for m in (re.match(r"\[\d+/\d+\] (.*)", l) for l in lines) if m]
    extra = [s for s in steps if not s.startswith(ALWAYS)]
    reasons = [l for l in lines if l.startswith("ninja explain:") and "VerifyGlobs.cmake_force" not in l
               and "wwhd_build_info" not in l]
    if extra:
        print("\n".join(reasons[:40]))
        sys.exit("%d steps ran in a build with nothing changed, first: %s" % (len(extra), extra[0]))
    print("no-op build: only %s" % ", ".join(steps) if steps else "no-op build")


if __name__ == "__main__":
    main()
