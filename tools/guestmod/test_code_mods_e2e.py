#!/usr/bin/env python3
"""Opt-in real-game code-mod toggle test. Uses an installed local release and copies saves.

This is deliberately outside unit-test discovery: it needs the player's extracted
USA game, a compatible slot1 state, a built/installed release and clang/lld.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

REPO = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release', type=Path, required=True)
    parser.add_argument('--data-dir', type=Path)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--save', type=Path, required=True)
    parser.add_argument('--state-dir', type=Path, required=True)
    parser.add_argument('--renderer', choices=('metal', 'vulkan'), default='metal')
    parser.add_argument('--ppc-clang', default=os.environ.get('WWHD_PPC_CLANG', 'clang'))
    parser.add_argument('--ppc-lld', default=os.environ.get('WWHD_PPC_LLD', 'ld.lld'))
    parser.add_argument('--out', type=Path, default=REPO / 'build' / ('code-mods-e2e-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S')))
    args = parser.parse_args()
    release, out = args.release.resolve(), args.out.resolve()
    data = (args.data_dir or release / 'data').resolve()
    out.mkdir(parents=True, exist_ok=False)  # never overwrite another run's evidence
    manifest = json.loads((release / 'sdk/manifest.json').read_text())
    binary = data / 'bin' / manifest['exe']
    manager, package = out / 'manager', out / 'heart-ticker'
    package.mkdir()
    shutil.copy2(REPO / 'examples/guest-mods/heart-ticker/manifest.json', package / 'manifest.json')
    subprocess.run([args.ppc_clang, '--target=powerpc-unknown-eabi', '-mcpu=750', '-O2', '-ffreestanding',
                    '-fno-builtin', '-nostdlib', '-fno-jump-tables', '-ffunction-sections', '-fdata-sections',
                    '-I' + str(REPO / 'runtime/guest/include'), '-c', str(REPO / 'examples/guest-mods/heart-ticker/mod.c'),
                    '-o', str(package / 'mod.o')], check=True)
    subprocess.run([args.ppc_lld, '-m', 'elf32ppc', '-r', str(package / 'mod.o'), '-o', str(package / 'mod.elf')], check=True)

    def rebuild(mode, name):
        status = out / (name + '.json')
        with (out / (name + '.log')).open('w') as log:
            subprocess.run([sys.executable, str(release / 'tools/installer/setup.py'), '--yes', '--data-dir',
                            str(data), '--rebuild-code-mods', '--code-mods', str(mode), '--jobs', '4',
                            '--code-mods-status', str(status)], stdout=log, stderr=subprocess.STDOUT, check=True)
        result = json.loads(status.read_text())
        ready = json.loads((Path(result['exe']).parents[1] / 'ready.json').read_text())
        assert result['state'] == 'ready' and result['hooks'] == bool(mode)
        return result, ready

    def run(name, mode, extra=None):
        env = {'WWHD_CODE_MODS': str(mode), 'WWHD_MOD_MANAGER_DIR': str(manager),
               'WWHD_TEST_TRUST_NATIVE_MODS': 'heart-ticker'}
        env.update(extra or {})
        with (out / (name + '-driver.log')).open('w') as log:
            subprocess.run([sys.executable, str(REPO / 'tools/bench/run_bench.py'), '--binary', str(binary),
                            '--game', str(args.game.resolve()), '--save', str(args.save.resolve()),
                            '--state-dir', str(args.state_dir.resolve()), '--renderer', args.renderer,
                            '--scene', 'still', '--slot', '1', '--fps', '30', '--seconds', '12',
                            '--timeout', '120', '--min-free-gb', '15', '--out', str(out / name),
                            '--variant', name + ':' + ','.join(k + '=' + v for k, v in env.items())],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        return (out / name / (name + '_01') / 'log').read_text(errors='replace')

    off, original = rebuild(0, 'prepare-off')
    on, _ = rebuild(1, 'prepare-on')  # warm both caches; fresh compilation is recorded separately
    log = run('install', 0, {'WWHD_TEST_MOD_INSTALL': str(package), 'WWHD_TEST_MOD_ENABLE': 'heart-ticker',
                           'WWHD_TEST_CODE_MOD_REBUILD': '1', 'WWHD_TEST_OVERLAY': 'open:mods@700'})
    assert '[code mods] rebuild offer: support on for heart-ticker' in log
    assert '[code mods] rebuild ready; restart required' in log
    profiles = json.loads((manager / 'profiles.json').read_text())
    profile = profiles['profiles'][profiles['active']]
    assert not profile.get('enabled', {}).get('heart-ticker', False)
    assert 'heart-ticker' in profile['code_mod_pending']
    log = run('active', 1)  # the original launcher must route to the selected hook-enabled executable
    assert '[guestmods] loaded ' in log and 'heart-ticker: first return hook' in log
    assert 'heart-ticker: life (quarter hearts)' in log
    profiles = json.loads((manager / 'profiles.json').read_text())
    profile = profiles['profiles'][profiles['active']]
    assert profile['enabled']['heart-ticker'] and not profile.get('code_mod_pending', {})
    final, ready = rebuild(0, 'final-off')
    assert final['cached'] and original['gamecode_objects']
    assert ready['gamecode_objects'] == original['gamecode_objects']
    assert ready['generated'] == original['generated']
    assert '[guestmods] loaded ' not in run('disabled', 0)
    result = {'install_offer_rebuild': True, 'disabled_until_restart': True,
              'heart_ticker_active_after_restart': True, 'off_again_cached': True,
              'off_code_identical': True, 'prepare_off_seconds': off['seconds'],
              'prepare_on_seconds': on['seconds'], 'cached_off_seconds': final['seconds']}
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
