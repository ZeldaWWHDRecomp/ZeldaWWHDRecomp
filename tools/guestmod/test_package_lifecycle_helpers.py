"""Game-free tests of the real lifecycle driver's metadata/receipt checks."""
import json
import hashlib
from pathlib import Path
import tempfile
import unittest
import zipfile
from test_package_lifecycle_e2e import local_catalogue, required_setup_receipts, selected_build, read_database


class LifecycleHelpers(unittest.TestCase):
    def test_actual_package_metadata_is_preserved_and_hashed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            package=root/'source.zip'
            with zipfile.ZipFile(package,'w') as archive:
                archive.writestr('manifest.json',json.dumps({'id':'example','version':'1.0','kind':'guest'}))
            entry={'id':'example','version':'1.0','setup':[{'id':'build','type':'build_guest_mod'}]}
            manifest,actual=local_catalogue({'mods':[entry]},package,root/'catalogue')
            self.assertEqual(manifest['id'],'example')
            self.assertEqual(actual['setup'],entry['setup'])
            self.assertEqual(actual['downloads']['all']['url'],'example.zip')
            self.assertEqual(actual['downloads']['all']['size'],package.stat().st_size)
            with self.assertRaises(ValueError):
                local_catalogue({'mods':[]},package,root/'bad')

    def test_selected_build_requires_actual_mode_binary_and_hash_provenance(self):
        with tempfile.TemporaryDirectory() as temporary:
            data=Path(temporary);cache=data/'code-builds'/'synthetic-fingerprint';(cache/'bin').mkdir(parents=True)
            executable=cache/'bin'/'wwhd';executable.write_bytes(b'authored synthetic executable bytes')
            ready={'fingerprint':'synthetic-fingerprint','hooks':False,
                   'sha256':hashlib.sha256(executable.read_bytes()).hexdigest(),
                   'generated':{'table.c':'authored-hash'},'gamecode_objects':{'game.o':'authored-hash'}}
            active={'state':'ready','fingerprint':'synthetic-fingerprint','hooks':False,
                    'exe':str(executable),'cached':True,'seconds':0.1}
            (cache/'ready.json').write_text(json.dumps(ready))
            (data/'code-mods-active.json').write_text(json.dumps(active))
            self.assertEqual(selected_build(data,False)['rebuild_kind'],'cached')
            with self.assertRaises(ValueError):
                selected_build(data,True) # an environment flag cannot change compiled mode
            ready['sha256']='wrong';(cache/'ready.json').write_text(json.dumps(ready))
            with self.assertRaises(ValueError):
                selected_build(data,False)
            ready['sha256']=hashlib.sha256(executable.read_bytes()).hexdigest();ready['generated']={}
            (cache/'ready.json').write_text(json.dumps(ready))
            with self.assertRaises(ValueError):
                selected_build(data,False)

    def test_only_initial_missing_profile_uses_native_defaults(self):
        with tempfile.TemporaryDirectory() as temporary:
            manager = Path(temporary)
            self.assertEqual(read_database(manager, True)['profiles']['Default']['enabled'], {})
            with self.assertRaises(FileNotFoundError):
                read_database(manager)
            (manager / 'profiles.json').write_text('invalid')
            with self.assertRaises(json.JSONDecodeError):
                read_database(manager, True)

    def test_every_required_receipt_and_option_is_checked(self):
        setup=[{'id':'source','type':'game_path','game':'gc_wind_waker'},
               {'id':'maps','type':'run_tool'}, {'id':'build','type':'build_guest_mod'},
               {'id':'optional','type':'run_tool','optional':True}]
        database={}
        self.assertEqual(required_setup_receipts(database,'example',setup),['source','maps','build'])
        database={'game_sources':{'gc_wind_waker':'private'},'setup_receipts':{'example':{'maps':'hash'}},
                  'guest_prepared':{'example':{'module':'private.module'}}}
        self.assertEqual(required_setup_receipts(database,'example',setup),[])


if __name__=='__main__':
    unittest.main()
