#!/usr/bin/env python3
"""Run the game-free core randomizer suite with single-executable host compilers.

Separate from ci_guestmods.py: its setup toolchain may set CC='zig cc', whereas
core host fixtures pass CC as one executable. No downloads or game files here.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
CORE = REPO / 'examples/guest-mods/twwhd-randomizer-core'


def executable(value, label):
    found = shutil.which(value)
    if not found:
        raise RuntimeError(f'{label} executable unavailable: {value}')
    # Forward slashes remain one argv filename for fixtures and also survive
    # the SDK builder's shlex parsing on Windows.
    return Path(found).absolute().as_posix()


def child_environment(environ):
    env = dict(environ)
    host = env.get('WWHD_CORE_HOST_CC') or ('/usr/bin/clang' if sys.platform == 'darwin' else 'clang')
    cc = executable(host, 'native clang')
    # Core fixtures use [CC,...], but the SDK builder parses CC with shlex.
    # Known matrix compiler locations contain no spaces; refuse ambiguity.
    if any(character.isspace() for character in cc):
        raise RuntimeError('core CI native compiler path must contain no whitespace')
    cxx_name = str(Path(cc).with_name('clang++.exe' if cc.lower().endswith('.exe') else 'clang++'))
    cxx = executable(env.get('WWHD_CORE_HOST_CXX') or cxx_name, 'native clang++')
    ppc = env.get('WWHD_PPC_CLANG')
    if sys.platform == 'darwin' and not ppc:
        for package, variable, binary in (('llvm', 'WWHD_PPC_CLANG', 'clang'),):
            if not env.get(variable):
                prefix = subprocess.check_output(['brew', '--prefix', package], text=True).strip()
                env[variable] = str(Path(prefix)/'bin'/binary)
        ppc = env['WWHD_PPC_CLANG']
    env['CC'], env['CXX'] = cc, cxx
    env['WWHD_PPC_CLANG'] = executable(ppc or 'clang', 'PowerPC clang')
    env['PATH'] = str(Path(cc).parent) + os.pathsep + env.get('PATH', '')
    env['PYTHONUNBUFFERED'] = '1'
    return env


def probe_toolchains(env):
    # Fail before unittest discovery can turn a missing PPC backend into a skip.
    subprocess.run([env['WWHD_PPC_CLANG'], '--target=powerpc-unknown-eabi', '-mcpu=750', '-mstackrealign',
                    '-x', 'c', '-c', '-o', os.devnull, '-'], input='int probe;', text=True, env=env, check=True)
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        for variable, suffix, source, options in (
            ('CC', '.c', '#include <stdint.h>\nint main(void){return sizeof(uintptr_t)!=8;}\n', ['-std=c11']),
            ('CXX', '.cpp', '#include <string>\nint main(){std::string s="core";return s.size()!=4;}\n', ['-std=c++17']),
        ):
            path = root/('probe'+suffix)
            output = root/(variable+'.exe' if os.name == 'nt' else variable)
            path.write_text(source)
            subprocess.run([env[variable], *options, str(path), '-o', str(output)], env=env, check=True)
            subprocess.run([str(output)], env=env, check=True)


def run_suite():
    suite = unittest.defaultTestLoader.discover(str(CORE), pattern='test_*.py')
    if not suite.countTestCases():
        raise RuntimeError('core randomizer suite unexpectedly contains no tests')
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if result.skipped:
        print('Core CI refuses skipped tests:', result.skipped, file=sys.stderr)
    return 0 if result.wasSuccessful() and not result.skipped else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-suite', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--check-only', action='store_true', help='probe required toolchains without running the suite')
    args = parser.parse_args()
    if args.run_suite:
        return run_suite()
    env = child_environment(os.environ)
    probe_toolchains(env)
    print('Core native CC:', env['CC'], flush=True)
    print('Core native CXX:', env['CXX'], flush=True)
    print('Core PowerPC clang:', env['WWHD_PPC_CLANG'], flush=True)
    if not args.check_only:
        subprocess.run([sys.executable, str(Path(__file__).resolve()), '--run-suite'],
                       cwd=REPO, env=env, check=True)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(f'Core randomizer CI failed: {error}') from error
