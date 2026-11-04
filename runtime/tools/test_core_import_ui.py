"""Click the production import UI with authored packages; never run guest code."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='wwhd-core-ui-') as temporary:
    for mode in ('core-import','core-cancel','core-profile'):
        root=Path(temporary)/mode;root.mkdir()
        release=root/'release';(release/'tools/guestmod').mkdir(parents=True)
        (release/'sdk/include').mkdir(parents=True)
        (release/'tools/guestmod/build_guest_mod.py').write_text('# fixture build bridge')
        (release/'core-randomizer-resources.json').write_text('{}')
        for game in ('original','seed'):
            (root/game/'code').mkdir(parents=True);(root/game/'content').mkdir()
            (root/game/'code/cking.rpx').write_bytes(b'authored fixture, not game code')
        (root/'save').mkdir();(root/'save/slot.bin').write_text('authored save fixture')
        (root/'sources.json').write_text('{}')
        worker=release/'tools/core_randomizer.py'
        worker.write_text('''import argparse,json,pathlib,time,sys
p=argparse.ArgumentParser()
for field in ['resources','original','randomized','output','compiler-json','progress-file','cancel-file']:p.add_argument('--'+field,required=True)
a=p.parse_args();out=pathlib.Path(a.output);root=pathlib.Path(a.original).parent
assert pathlib.Path(a.original)==root/'original' and pathlib.Path(a.randomized)==root/'seed'
assert json.loads(a.compiler_json)==[sys.executable]
pathlib.Path(a.progress_file).write_text(json.dumps({'phase':'Building private core package'}))
(root/'worker-ready').touch()
mode=root.name
if mode!='core-import':
    deadline=time.time()+10
    gate=pathlib.Path(a.cancel_file) if mode=='core-cancel' else root/'profile-changed'
    while not gate.exists() and time.time()<deadline:time.sleep(.005)
    assert gate.exists()
else:time.sleep(.1)
out.mkdir();(out/'mod.elf').write_bytes(b'\\x7fELF\\x01\\x02')
manifest=dict(format_version=1,id='twwhd-randomizer-core',name='Authored randomizer UI fixture',version='0.0.1',game_id='wwhd',kind='guest',experimental=True,minimum_manager_version='1.7.0',supported_game_builds=['USA'],guest=dict(api_version=1,elf='mod.elf',heap_size=0),save_profile=dict(version=1,provider='twwhd-randomizer',identity='a'*64))
(out/'manifest.json').write_text(json.dumps(manifest))
(root/'worker-finished').touch()
# Return success even when cancelled; the UI must independently suppress import.
print(json.dumps(dict(package=str(out),manager_action='import')))
''')
        config=dict(format_version=2,python=[sys.executable],compiler=['unused-bare-setup-command'],
                    builder=str(release/'tools/guestmod/build_guest_mod.py'),include=str(release/'sdk/include'))
        (root/'guest-sdk.json').write_text(json.dumps(config))
        env={**os.environ,'WWHD_NO_AUDIO':'1','WWHD_NO_HOST_INPUT':'1','WWHD_MOD_MANAGER_DIR':str(root/'manager'),
             'WWHD_TEST_MOD_SETUP':'twwhd-randomizer-core','WWHD_TEST_GAME_SOURCES':str(root/'sources.json'),
             'WWHD_GUEST_BUILD_CONFIG':str(root/'guest-sdk.json'),'WWHD_UI_COMPILER':sys.executable}
        env.pop('WWHD_TEST_TRUST_NATIVE_MODS',None)
        result=subprocess.run([str(binary),str(root),mode],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=25)
        print(result.stdout,end='')
        if result.returncode:raise RuntimeError(f'{mode} failed: {result.returncode}')
        assert (root/'save/slot.bin').read_text()=='authored save fixture'
