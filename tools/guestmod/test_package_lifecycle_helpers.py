"""Game-free tests of the real lifecycle driver's metadata/receipt checks."""
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from test_package_lifecycle_e2e import local_catalogue, required_setup_receipts


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
