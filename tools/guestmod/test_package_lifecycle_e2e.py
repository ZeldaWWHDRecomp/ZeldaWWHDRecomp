#!/usr/bin/env python3
"""Opt-in catalogue/setup/restart lifecycle for an actual reviewed guest ZIP.

Uses private normal-save copies, the real overlay workers and local catalogue
fixtures. It does not automate a native file-picker dialog or prove HTTPS hosting.
No game-derived output is written to a source checkout.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import zipfile

REPO = Path(__file__).resolve().parents[2]


def local_catalogue(metadata, package, output):
    """Keep real metadata; replace only transport with a bounded local fixture."""
    with zipfile.ZipFile(package) as archive:
        manifest = json.loads(archive.read('manifest.json'))
    ident = manifest['id']
    entries = [entry for entry in metadata['mods'] if entry['id'] == ident]
    if len(entries) != 1 or entries[0]['version'] != manifest['version'] or manifest['kind'] != 'guest':
        raise ValueError('ZIP does not match one declared guest catalogue entry')
    entry = json.loads(json.dumps(entries[0]))
    payload = package.read_bytes()
    output.mkdir()
    target = output / (ident + '.zip')
    target.write_bytes(payload)
    # Deliberate local-fixture exception. Published catalogues still require HTTPS.
    entry['downloads'] = {'all': {'url': target.name, 'size': len(payload),
                                 'sha256': hashlib.sha256(payload).hexdigest()}}
    (output / 'index.json').write_text(json.dumps({'format_version': 1, 'mods': [entry]}, indent=2) + '\n')
    return manifest, entry


def required_setup_receipts(database, ident, setup):
    missing = []
    for step in setup:
        if step.get('optional'):
            continue
        kind = step['type']
        if kind == 'game_path':
            ready = bool(database.get('game_sources', {}).get(step['game']))
        elif kind == 'run_tool':
            ready = bool(database.get('setup_receipts', {}).get(ident, {}).get(step['id']))
        elif kind == 'build_guest_mod':
            ready = bool(database.get('guest_prepared', {}).get(ident, {}).get('module'))
        else:
            profile = database.get('profiles', {}).get(database.get('active'), {})
            value = profile.get('config', {}).get(ident, {}).get(step['option'])
            ready = value is True if kind == 'confirm' else value in step['choices']
        if not ready:
            missing.append(step['id'])
    return missing


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('release', 'game', 'save', 'package', 'catalogue-source', 'game-sources', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--data-dir', type=Path)
    parser.add_argument('--guest-build-config', type=Path)
    parser.add_argument('--renderer', choices=('metal', 'vulkan'), required=True)
    parser.add_argument('--region', choices=('USA', 'EU'), required=True)
    parser.add_argument('--mode', choices=('30', 'interp60', 'true60'), default='30')
    parser.add_argument('--max-game-sessions', type=int, choices=range(1, 5), default=4)
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--min-free-gib', type=int, default=15, help='Explicit local disk floor; 0 disables the gate when authorized')
    args = parser.parse_args()
    if args.min_free_gib < 0:
        parser.error('Disk floor must be nonnegative')
    args.out = args.out.resolve()
    if args.out.is_relative_to(REPO):
        parser.error('Private lifecycle output must be outside the source checkout')
    if not args.game_sources.is_file() or args.game_sources.is_symlink():
        parser.error('Explicit private game-source JSON is required ({} for mods without game_path)')
    if shutil.disk_usage(args.out.parent).free < args.min_free_gib * 1024**3:
        parser.error('Free disk is below the configured floor')
    release = args.release.resolve()
    data = (args.data_dir or release / 'data').resolve()
    release_manifest = json.loads((release / 'sdk/manifest.json').read_text())
    binary = data / 'bin' / release_manifest['exe']
    build_config = (args.guest_build_config or data / 'guest-sdk.json').resolve()
    if not binary.is_file() or not build_config.is_file():
        parser.error('Installed executable and guest build configuration are required')
    args.out.mkdir(exist_ok=False)
    manifest, entry = local_catalogue(json.loads(args.catalogue_source.read_text()), args.package, args.out / 'catalogue')
    ident = manifest['id']
    manager = args.out / 'manager'
    sources = args.out / 'game-sources.json'
    shutil.copy2(args.game_sources, sources)
    spec = importlib.util.spec_from_file_location('lifecycle_gates', REPO / 'tools/bench/run_bench.py')
    gates = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gates)
    phases = {}

    def database():
        return json.loads((manager / 'profiles.json').read_text())

    def profile():
        value = database()
        return value['profiles'][value['active']]

    def run(name, support, extra=None, overlay=True, done_marker=None, want_enabled=None):
        if len(gates.other_games()) >= args.max_game_sessions or gates.other_benchmarks():
            raise RuntimeError('Functional game limit reached or a benchmark is active')
        phase = args.out / name
        phase.mkdir()
        shutil.copytree(args.save, phase / 'save')
        env = {key: value for key, value in os.environ.items() if not key.startswith('WWHD_')}
        env.update(WWHD_CODE_MODS=str(support), WWHD_NO_AUDIO='1', WWHD_NO_HOST_INPUT='1',
                   WWHD_HIDDEN_WINDOWS='1', WWHD_UNCAPPED='1', WWHD_RENDERER_RUNTIME=args.renderer,
                   WWHD_MOD_MANAGER_DIR=str(manager), WWHD_TEST_TRUST_NATIVE_MODS=ident,
                   WWHD_GUEST_BUILD_CONFIG=str(build_config), WWHD_SETTINGS=str(args.out / 'settings.ini'),
                   WWHD_DISPLAY_SETTINGS=str(args.out / 'display.plist'), WWHD_CONTROLS=str(args.out / 'controls.json'),
                   WWHD_SHADER_CACHE=str(args.out / 'shaders.bin'), WWHD_VK_SHADER_CACHE=str(args.out / 'vkshaders'),
                   WWHD_VK_PIPELINE_CACHE=str(args.out / 'vkpipelines.bin'), WWHD_STATE_DIR=str(phase / 'states'),
                   WWHD_TEST_ORIGIN='2950', WWHD_TEST_END='10', WWHD_DUMP_FRAMES='3000,3002,3004',
                   WWHD_DUMP_PRESENT='1', WWHD_SIM_SCREEN='1280x720',
                   WWHD_PRESS=','.join('%d-%d:8000' % (frame, frame + 8) for frame in range(150, 2800, 30)),
                   WWHD_TRUE60='1' if args.mode == 'true60' else '0', WWHD_INTERP='0',
                   XDG_CONFIG_HOME=str(args.out / 'config'))
        if args.mode == 'interp60':
            env['WWHD_INTERP_AT_STEP'] = '2810'
        if overlay:
            env['WWHD_TEST_OVERLAY'] = 'open:mods@700'
        env.update(extra or {})
        with (phase / 'runtime.log').open('w') as log:
            process = subprocess.Popen([str(binary), '--game', str(args.game.resolve()), '--save', str(phase / 'save')],
                                       cwd=phase, env=env, stdout=log, stderr=subprocess.STDOUT)
            print(name, 'owned game PID', process.pid, flush=True)
            deadline = time.monotonic() + args.timeout
            try:
                def completed():
                    if not (phase / 'test_done').exists():
                        return False
                    if done_marker and done_marker not in (phase / 'runtime.log').read_text(errors='replace'):
                        return False
                    if want_enabled is not None and profile().get('enabled', {}).get(ident, False) != want_enabled:
                        return False
                    return True
                while process.poll() is None and not completed():
                    current_log = (phase / 'runtime.log').read_text(errors='replace')
                    if '[setup test] failed ' in current_log:
                        raise RuntimeError(name + ' setup diagnostic failed; inspect private runtime log')
                    if time.monotonic() > deadline:
                        raise RuntimeError(name + ' timed out')
                    if shutil.disk_usage(phase).free < args.min_free_gib * 1024**3:
                        raise RuntimeError('Free disk fell below the configured floor')
                    if len(gates.other_games(process.pid)) >= args.max_game_sessions or gates.other_benchmarks():
                        raise RuntimeError('Functional game limit reached or benchmark started')
                    time.sleep(1)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
        text = (phase / 'runtime.log').read_text(errors='replace')
        if not (phase / 'test_done').exists():
            raise RuntimeError(name + ' exited before scenario completion')
        if 'Loaded slot' in text:
            raise RuntimeError('Lifecycle checks must boot normal saves without full-state restore')
        phases[name] = {'test_done': True, 'guest_loaded': '[guestmods] loaded ' in text}
        return text

    installed = run('install-support-off', 0, {'WWHD_MOD_CATALOGUE': str(args.out / 'catalogue/index.json'),
                    'WWHD_TEST_CATALOGUE_INSTALL': ident, 'WWHD_TEST_CODE_MOD_REBUILD': '1'},
                    done_marker='[code mods] rebuild ready; restart required')
    assert '[catalogue] refreshed ' in installed and '[catalogue] installed ' + ident + ' disabled' in installed
    assert '[code mods] rebuild ready; restart required' in installed
    assert not profile().get('enabled', {}).get(ident, False)
    assert not phases['install-support-off']['guest_loaded']
    setup = run('setup-enable-support-on', 1, {'WWHD_TEST_MOD_SETUP': ident,
                'WWHD_TEST_GAME_SOURCES': str(sources), 'WWHD_TEST_MOD_ENABLE': ident},
                done_marker='[setup test] ready ' + ident, want_enabled=True)
    assert '[setup test] ready ' + ident in setup and '[setup test] failed ' not in setup
    assert not required_setup_receipts(database(), ident, entry.get('setup', []))
    assert profile()['enabled'][ident] and not phases['setup-enable-support-on']['guest_loaded']
    prepared = database()['guest_prepared'][ident]
    assert prepared['build'] == args.region and Path(prepared['module']).is_file()
    region = database()['guest_regions'][ident]
    assert prepared['base'] == region['base'] and prepared['size'] == region['size']
    for step in entry.get('setup', []):
        if step['type'] in ('run_tool', 'build_guest_mod') and not step.get('optional'):
            assert '[setup test] worker %s %s ready' % (ident, step['id']) in setup
        if step['type'] == 'run_tool' and not step.get('optional'):
            assert '[setup test] worker %s %s ready' % (ident, step['id']) in setup
            assert all((manager / 'Data' / ident / name).is_file() for name in step['outputs'])
    active = run('active-after-restart', 1, overlay=False)
    assert phases['active-after-restart']['guest_loaded']
    assert ident + '.dylib' in active or ident + '.so' in active or ident + '.dll' in active
    run('disable-until-restart', 1, {'WWHD_TEST_MOD_DISABLE': ident})
    assert not profile()['enabled'][ident]
    assert phases['disable-until-restart']['guest_loaded']
    removed = run('disabled-remove-after-restart', 1, {'WWHD_TEST_MOD_REMOVE': ident})
    assert not phases['disabled-remove-after-restart']['guest_loaded']
    assert '[mods] removed ' + ident in removed and not (manager / 'Mods' / ident).exists()
    result = {'min_free_gib': args.min_free_gib, 'mod': ident, 'region': args.region, 'renderer': args.renderer, 'mode': args.mode, 'phases': phases,
              'setup_receipts_verified': True, 'disabled_until_restart': True,
              'package_sha256': hashlib.sha256(args.package.read_bytes()).hexdigest(),
              'launcher_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'limitations': 'Local catalogue fixture transport only; native file-picker UI and trust acceptance are not automated. Existing explicit test trust is used. Captures require visual review for mod behavior.'}
    (args.out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
