#!/usr/bin/env python3
"""Opt-in real-game code-mod toggle test. Uses an installed local release and copies saves.

This is deliberately outside unit-test discovery: it needs the player's extracted
USA or EU game, a compatible regional slot1 state, a built/installed release and clang/lld.
"""
import argparse
import datetime
import contextlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from types import SimpleNamespace

REPO = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--max-game-sessions', type=int, choices=range(1, 5), default=1,
                        help='Concurrent functional game limit; these runs do not measure performance')
    parser.add_argument('--catalogue', action='store_true', help='Install through a generated local catalogue and its normal overlay worker')
    parser.add_argument('--release', type=Path, required=True)
    parser.add_argument('--data-dir', type=Path)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--save', type=Path, required=True)
    parser.add_argument('--state-dir', type=Path)
    parser.add_argument('--boot', action='store_true', help='Boot from copied normal saves without loading a full state')
    parser.add_argument('--renderer', choices=('metal', 'vulkan'), default='metal')
    parser.add_argument('--ppc-clang', default=os.environ.get('WWHD_PPC_CLANG', 'clang'))
    parser.add_argument('--ppc-lld', default=os.environ.get('WWHD_PPC_LLD', 'ld.lld'))
    parser.add_argument('--out', type=Path, default=REPO / 'build' / ('code-mods-e2e-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S')))
    args = parser.parse_args()
    if not args.boot and args.state_dir is None:
        parser.error('--state-dir is required unless --boot is used')
    release, out = args.release.resolve(), args.out.resolve()
    data = (args.data_dir or release / 'data').resolve()
    out.mkdir(parents=True, exist_ok=False)  # never overwrite another run's evidence
    states = args.state_dir.resolve() if args.state_dir else out / 'states'
    states.mkdir(parents=True, exist_ok=True)
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

    catalogue = out / 'catalogue'
    if args.catalogue:
        subprocess.run([sys.executable, str(REPO / 'tools/catalogue/make_pilots.py'),
                        '--out', str(catalogue), '--heart-elf', str(package / 'mod.elf')], check=True)

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

    # Reuse save isolation and owned-PID cleanup in process. This is a functional
    # lifecycle check, so it must not appear as an active benchmark to other jobs.
    spec = importlib.util.spec_from_file_location('functional_game_runner', REPO / 'tools/bench/run_bench.py')
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)

    def run(name, mode, extra=None):
        env = {'WWHD_CODE_MODS': str(mode), 'WWHD_MOD_MANAGER_DIR': str(manager),
               'WWHD_TEST_TRUST_NATIVE_MODS': 'heart-ticker'}
        env.update(extra or {})
        if args.boot:
            env['WWHD_STATE_LOAD_AT'] = ''
            if 'WWHD_TEST_OVERLAY' in env:
                env['WWHD_TEST_OVERLAY'] = 'open:mods@3000'
            env['WWHD_DUMP_PRESENT'] = '1'
            env['WWHD_DUMP_FRAMES'] = '3001,3010,3030'
        deadline = time.monotonic() + 600
        while True:
            listing = subprocess.run(['ps', '-Ao', 'pid=,comm='], check=True, capture_output=True, text=True).stdout
            games = [line for line in listing.splitlines()
                     if line.strip() and Path(line.strip().split(None, 1)[1]).name.startswith('wwhd')]
            if len(games) < args.max_game_sessions:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError('Concurrent functional game limit did not become available')
            time.sleep(2)
        options = SimpleNamespace(
            out=str(out), binary=str(binary), variant_binaries={}, game=str(args.game.resolve()),
            save=str(args.save.resolve()), state_dir=str(states), renderer=args.renderer,
            scene='still', slot=1, fps='30', seconds=12, timeout=240 if args.boot else 120,
            min_free_gb=15, load_frame=3000 if args.boot else 450,
            origin=2950 if args.boot else 650, cache_dir=None, press_from=120, press_every=30,
            display_hz=None, uncapped=False, visible=False, max_load=0,
            wait_for_others=args.max_game_sessions == 1,
            watch_others=args.max_game_sessions == 1, quiet_load_max=None,
            exclusive_bench=False, exclusive_work=False, gate=None, skip_windows=2)
        with (out / (name + '-driver.log')).open('w') as log:
            with contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
                runner.run_once(options, name, env, 1, str(out / name))
        result = json.loads((out / name / (name + '_01') / 'result.json').read_text())['result']
        log_text = (out / name / (name + '_01') / 'log').read_text(errors='replace')
        expected = 'no state load' if args.boot else 'ok'
        assert result['status'] == expected, (name, result['status'])
        assert (out / name / (name + '_01') / 'test_done').is_file(), name
        if args.boot:
            assert 'Loaded slot' not in log_text, 'Boot checks must not restore full states'
        return log_text

    off, original = rebuild(0, 'prepare-off')
    on, _ = rebuild(1, 'prepare-on')  # warm both caches; fresh compilation is recorded separately
    install = {'WWHD_TEST_MOD_ENABLE': 'heart-ticker', 'WWHD_TEST_CODE_MOD_REBUILD': '1',
               'WWHD_TEST_OVERLAY': 'open:mods@700'}
    if args.catalogue:
        install.update(WWHD_MOD_CATALOGUE=str(catalogue / 'index.json'), WWHD_TEST_CATALOGUE_INSTALL='heart-ticker')
    else:
        install['WWHD_TEST_MOD_INSTALL'] = str(package)
    log = run('install', 0, install)
    if args.catalogue:
        assert '[catalogue] refreshed ' in log and '[catalogue] installed heart-ticker disabled' in log
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
    assert '[guestmods] loaded ' not in run('disabled', 0, {'WWHD_TEST_MOD_DISABLE': 'heart-ticker',
                                                        'WWHD_TEST_OVERLAY': 'open:mods@700'})
    removed = run('removed', 0, {'WWHD_TEST_MOD_REMOVE': 'heart-ticker', 'WWHD_TEST_OVERLAY': 'open:mods@700'})
    assert '[mods] removed heart-ticker' in removed
    assert not (manager / 'Mods' / 'heart-ticker').exists()
    result = {'removed': True, 'catalogue_install': args.catalogue, 'install_offer_rebuild': True, 'disabled_until_restart': True,
              'heart_ticker_active_after_restart': True, 'off_again_cached': True,
              'off_code_identical': True, 'prepare_off_seconds': off['seconds'],
              'prepare_on_seconds': on['seconds'], 'cached_off_seconds': final['seconds']}
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
