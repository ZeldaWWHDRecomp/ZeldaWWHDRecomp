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
        (source / 'tools/prepare.py').write_text("import pathlib,sys,time\ntime.sleep(.05)\nif pathlib.Path(sys.argv[3]).exists():\n print('synthetic deliberate failure');sys.exit(7)\npathlib.Path(sys.argv[1],'result.bin').write_text('synthetic maps, no game data')\n")
    if mode == 'choice':
        manifest['options'] = [dict(id='consent', name='Consent', type='bool', default=False),
                               dict(id='colour', name='Colour', type='enum', default='green', choices=['green', 'blue'])]
        manifest['setup'][:0] = [dict(id='consent', type='confirm', title='Prepare the heart ticker', option='consent'),
                                dict(id='colour', type='choice', title='Choose a colour', option='colour', choices=['green', 'blue'])]
    (source / 'manifest.json').write_text(json.dumps(manifest))
    (source / 'mod.elf').write_bytes(b'\x7fELF\x01\x02')
    archive = root / 'package.zip'
    with zipfile.ZipFile(archive, 'w') as package:
        for item in source.rglob('*'):
            if item.is_file():
                package.write(item, item.relative_to(source))
    entry = dict(id=ident, name=manifest['name'], description='Authored synthetic setup fixture', version='1.0.0', kind='guest',
                 authors=['Fixture'], licences=['MPL-2.0'], port_versions=dict(minimum='0.2.10'), setup=manifest['setup'],
                 downloads=dict(all=dict(url='package.zip', size=archive.stat().st_size, sha256=hashlib.sha256(archive.read_bytes()).hexdigest())))
    (root / 'index.json').write_text(json.dumps(dict(format_version=1, mods=[entry])))
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
    for key in ('WWHD_CODE_MODS', 'WWHD_TEST_TRUST_NATIVE_MODS'):
        environment.pop(key, None)
    result = subprocess.run([str(binary), str(root), mode], env=environment, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
    print(result.stdout, end='')
    if result.returncode:
        raise RuntimeError(f'{ident}/{mode} failed: {result.returncode}')
    if mode == 'missing':
        assert not (root / 'manager/profiles.json').exists(), 'Blocked setup must not record trust or a run'
        return
    deadline = time.monotonic() + 25
    while not (root / 'flow-result.json').exists() and time.monotonic() < deadline:
        time.sleep(.02)
    final = json.loads((root / 'flow-result.json').read_text())
    assert final['enabled'] is (mode not in ('missing', 'cancel', 'cancel-rebuild'))
    if mode == 'restart':
        assert final['resumed'] and final['confirmations'] == 0
    assert (root / 'consent-count').read_text() == '1'
    database = json.loads((root / 'manager/profiles.json').read_text())
    profile = database['profiles']['Default']
    assert profile.get('enabled', {}).get(ident, False) is (mode not in ('missing', 'cancel', 'cancel-rebuild'))
    assert not profile.get('setup_pending', {})
    if mode == 'choice':
        assert profile['config'][ident] == dict(consent=True, colour='blue')
    if mode != 'missing':
        assert database['native_trust'][ident]
        if ident == 'gc-sea-minimap':
            assert database['setup_receipts'][ident]['prepare']

scenarios = [('gc-sea-minimap', 'normal'), ('heart-ticker', 'normal'), ('gc-sea-minimap', 'restart'),
             ('heart-ticker', 'restart'), ('gc-sea-minimap', 'broken'), ('gc-sea-minimap', 'missing'),
             ('gc-sea-minimap', 'checkbox'), ('heart-ticker', 'checkbox'), ('gc-sea-minimap', 'cancel'), ('heart-ticker', 'choice'),
             ('gc-sea-minimap', 'catalogue'), ('heart-ticker', 'catalogue'), ('gc-sea-minimap', 'cancel-rebuild')]
if retained:
    for ident, mode in scenarios:
        run(retained / (ident + '-' + mode), ident, mode)
else:
    with tempfile.TemporaryDirectory(prefix='wwhd-oneclick-flow-') as temporary:
        for ident, mode in scenarios:
            run(Path(temporary) / (ident + '-' + mode), ident, mode)
