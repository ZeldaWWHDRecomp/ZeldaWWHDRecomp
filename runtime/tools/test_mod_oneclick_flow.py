#!/usr/bin/env python3
"""Game-free production setup-loop tests; all paths and rebuilds are synthetic."""
import hashlib
import json
import os
import shutil
import time
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

binary = Path(sys.argv[1]).resolve()
retained = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None

def run(root, ident, mode):
    root.mkdir(parents=True)
    source = root / 'source'
    (source / 'tools').mkdir(parents=True)
    manifest = dict(format_version=1, id=ident, name='Sea minimap' if ident == 'gc-sea-minimap' else 'Heart ticker',
                    version='1.0.0', game_id='wwhd-usa', kind='guest', guest=dict(api_version=1, heap_size=0),
                    setup=[dict(id='build', type='build_guest_mod', title='Build the minimap' if ident == 'gc-sea-minimap' else 'Build the heart ticker')])
    if ident == 'gc-sea-minimap':
        manifest['setup'][:0] = [dict(id='source', type='game_path', title='GameCube game', game='gc_usa'),
            dict(id='prepare', type='run_tool', title='Prepare island maps from your GameCube game',
                 tool='tools/prepare.py', arguments=['{data}', '{game:gc_usa}', str(root / 'fail-tool')], outputs=['result.bin'])]
        (source / 'tools/fixture_helper.py').write_text("VALUE = 1\n")
        # imports a module of its own, like real tools do: Python must not leave __pycache__ in the package
        (source / 'tools/prepare.py').write_text("import pathlib,sys,time\nimport fixture_helper\ntime.sleep(.05)\nif pathlib.Path(sys.argv[3]).exists():\n print('synthetic deliberate failure');sys.exit(7)\npathlib.Path(sys.argv[1],'result.bin').write_text('synthetic maps, no game data')\n")
    if mode == 'choice':
        manifest['options'] = [dict(id='consent', name='Consent', type='bool', default=False),
                               dict(id='colour', name='Colour', type='enum', default='green', choices=['green', 'blue'])]
        manifest['setup'][:0] = [dict(id='consent', type='confirm', title='Prepare the heart ticker', option='consent'),
                                dict(id='colour', type='choice', title='Choose a colour', option='colour', choices=['green', 'blue'])]
    if mode in ('update-on', 'exp-update-on'):  # a mod that applies live (a settings preset): updated while it is on
        manifest = dict(format_version=1, id=ident, name='Quick doors preset', version='1.0.0', game_id='wwhd-usa',
                        kind='settings', settings={'quick-doors': True})
    experimental = mode.startswith('exp-')  # an experimental mod: switching it on backs up the saves first
    if experimental:
        manifest['experimental'] = True
    (source / 'mod.elf').write_bytes(b'\x7fELF\x01\x02')

    def package(version, name):
        manifest['version'] = version
        (source / 'manifest.json').write_text(json.dumps(manifest))
        archive = root / name
        with zipfile.ZipFile(archive, 'w') as package:
            for item in source.rglob('*'):
                if item.is_file() and (manifest['kind'] == 'guest' or item.name == 'manifest.json'):
                    package.write(item, item.relative_to(source))
        return dict(id=ident, name=manifest['name'], description='Authored synthetic setup fixture', version=version,
                    kind=manifest['kind'], authors=['Fixture'], licences=['MPL-2.0'], port_versions=dict(minimum='0.2.10'),
                    setup=manifest.get('setup', []),
                    experimental=experimental,
                    downloads=dict(all=dict(url=name, size=archive.stat().st_size, sha256=hashlib.sha256(archive.read_bytes()).hexdigest())))
    if mode.startswith('update') or mode in ('ids', 'tabs', 'exp-update-on'):
        # the installed version; the catalogue offers 1.1.0
        (root / 'installed-index.json').write_text(json.dumps(dict(format_version=1, mods=[package('1.0.0', 'package-1.0.0.zip')])))
        entry = package('1.1.0', 'package.zip')
    else:
        entry = package('1.0.0', 'package.zip')
    listed = [entry]
    if mode in ('ids', 'tabs'):  # one more catalogue mod that is not downloaded (its page and review are checked too)
        listed.append(dict(entry, id='available-fixture', name='Available fixture'))
    (root / 'index.json').write_text(json.dumps(dict(format_version=1, mods=listed)))
    header = bytearray(32); header[:6] = b'GZLE01'; header[28:32] = b'\xc2\x33\x9f\x3d'
    (root / 'synthetic.iso').write_bytes(header)
    (root / 'sources.json').write_text(json.dumps(dict(gc_usa=str(root / 'synthetic.iso'))))
    if mode == 'broken':
        (root / 'fail-tool').touch()
    cache = root / 'code-builds' / 'synthetic'
    (cache / 'bin').mkdir(parents=True)
    executable = cache / 'bin' / binary.name
    if mode in ('restart', 'cancel-rebuild'):
        shutil.copy2(binary, executable)
        if os.name == 'nt':
            for library in binary.parent.glob('*.dll'):
                shutil.copy2(library, cache / 'bin' / library.name)
    (cache / 'ready.json').write_text(json.dumps(dict(fingerprint='synthetic', hooks=True)))
    setup = root / 'rebuild.py'
    setup.write_text("""import argparse,json,pathlib,os,time
p=argparse.ArgumentParser()
p.add_argument('--data-dir');p.add_argument('--rebuild-code-mods',action='store_true');p.add_argument('--code-mods')
p.add_argument('--code-mods-status');p.add_argument('--code-mods-cancel');p.add_argument('--jobs')
a=p.parse_args();assert a.jobs=='4' and a.code_mods=='1';time.sleep(.05)
# cancel-rebuild: the rebuild lasts until the player's Cancel arrives (no race with a slow test machine)
if os.environ.get('WWHD_TEST_SLOW_REBUILD'):
    deadline=time.time()+20
    while not pathlib.Path(a.code_mods_cancel).exists() and time.time()<deadline:time.sleep(.01)
    raise SystemExit(1)
root=pathlib.Path(a.data_dir)
(root/'code-mods-active.json').write_text(json.dumps(dict(state='ready',hooks=True,fingerprint='synthetic',exe=str(root/'code-builds/synthetic/bin'/{binary_name}))))
path=pathlib.Path(a.code_mods_status);temporary=path.with_suffix('.tmp');temporary.write_text(json.dumps(dict(state='ready',exe=str(root/'code-builds/synthetic/bin'/{binary_name}))));os.replace(temporary,path)
""".replace("{binary_name}", repr(binary.name)))
    config = dict(format_version=2, python=[sys.executable], compiler=['unused'], builder='unused', include='unused',
                  setup=str(setup), data_dir=str(root))
    (root / 'guest-sdk.json').write_text(json.dumps(config))
    environment = {**os.environ, 'WWHD_NO_AUDIO': '1', 'WWHD_NO_HOST_INPUT': '1', 'WWHD_MOD_MANAGER_DIR': str(root / 'manager'),
                   'WWHD_TEST_CATALOGUE_INSTALL': ident, 'WWHD_TEST_MOD_SETUP': ident, 'WWHD_TEST_GAME_SOURCES': str(root / 'sources.json'),
                   'WWHD_GUEST_BUILD_CONFIG': str(root / 'guest-sdk.json'), 'WWHD_INSTALL_DIR': str(root)}
    if mode == 'cancel-rebuild':
        environment['WWHD_TEST_SLOW_REBUILD'] = '1'
    for key in ('WWHD_CODE_MODS', 'WWHD_TEST_TRUST_NATIVE_MODS'):
        environment.pop(key, None)
    result = subprocess.run([str(binary), str(root), mode], env=environment, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    print(result.stdout, end='')
    if result.returncode:
        raise RuntimeError(f'{ident}/{mode} failed: {result.returncode}')
    if mode in ('ids', 'tabs'):  # these checks report by their exit code
        return
    if mode == 'missing':
        assert not (root / 'manager/profiles.json').exists(), 'Blocked setup must not record trust or a run'
        return
    deadline = time.monotonic() + 25
    while not (root / 'flow-result.json').exists() and time.monotonic() < deadline:
        time.sleep(.02)
    final = json.loads((root / 'flow-result.json').read_text())
    backup_fails = mode in ('exp-backup-fails', 'exp-backup-nospace')
    switched_on = mode not in ('missing', 'cancel', 'cancel-rebuild', 'update-off') and not backup_fails
    assert final['enabled'] is switched_on
    if mode == 'restart':
        assert final['resumed'] and final['confirmations'] == 0
    assert (root / 'consent-count').read_text() == '1'
    database = json.loads((root / 'manager/profiles.json').read_text())
    profile = database['profiles']['Default']
    assert profile.get('enabled', {}).get(ident, False) is switched_on
    assert not profile.get('setup_pending', {})
    if mode == 'choice':
        assert profile['config'][ident] == dict(consent=True, colour='blue')
    if experimental:  # exactly one backup before it was switched on (none when the backup could not be made)
        backups = [p for p in (root / 'save-backups').glob('*') if (p / 'backup.json').is_file()] if (root / 'save-backups').is_dir() else []
        assert len(backups) == (0 if backup_fails else 1), backups
    # a settings preset runs no code: nothing to trust; a failed backup stops before trust is recorded
    if mode not in ('missing', 'update-on', 'exp-update-on') and not backup_fails:
        assert database['native_trust'][ident]
        if ident == 'gc-sea-minimap':
            assert database['setup_receipts'][ident]['prepare']

scenarios = [('gc-sea-minimap', 'normal'), ('heart-ticker', 'normal'), ('gc-sea-minimap', 'restart'),
             ('heart-ticker', 'restart'), ('gc-sea-minimap', 'broken'), ('gc-sea-minimap', 'missing'),
             ('gc-sea-minimap', 'checkbox'), ('heart-ticker', 'checkbox'), ('gc-sea-minimap', 'cancel'), ('heart-ticker', 'choice'),
             ('gc-sea-minimap', 'catalogue'), ('heart-ticker', 'catalogue'), ('gc-sea-minimap', 'cancel-rebuild'),
             ('gc-sea-minimap', 'update-off'), ('quick-preset', 'update-on'), ('gc-sea-minimap', 'ids'),
             ('gc-sea-minimap', 'exp-catalogue'), ('heart-ticker', 'exp-switch-on'), ('quick-preset', 'exp-update-on'),
             ('gc-sea-minimap', 'exp-backup-fails'), ('gc-sea-minimap', 'exp-backup-nospace'),
             ('gc-sea-minimap', 'tabs')]
if retained:
    for ident, mode in scenarios:
        run(retained / (ident + '-' + mode), ident, mode)
else:
    with tempfile.TemporaryDirectory(prefix='wwhd-oneclick-flow-') as temporary:
        for ident, mode in scenarios:
            run(Path(temporary) / (ident + '-' + mode), ident, mode)
