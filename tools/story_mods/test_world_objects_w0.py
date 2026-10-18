"""Original render assets: reproducibility and visible deck dimensions, no game data."""
import importlib.util
from pathlib import Path
import unittest
import actor_model
ROOT=Path(__file__).resolve().parents[2]
ASSETS=ROOT/'examples/guest-mods/world-objects-w0/assets'
spec=importlib.util.spec_from_file_location('w0_fixtures',ASSETS/'make_fixtures.py')
fixtures=importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
class Fixtures(unittest.TestCase):
    def test_reproducible_and_valid(self):
        for name,triangles in [('platform',24),('buoy',12),('raft',12)]:
            with self.subTest(name=name):
                self.assertEqual((ASSETS/(name+'.glb')).read_bytes(),fixtures.make(name))
                info=actor_model.inspect(str(ASSETS/(name+'.glb')))
                self.assertEqual(info['triangles'],triangles)
                self.assertEqual((info['texture_width'],info['texture_height']),(64,16))
    def test_visible_deck_dimensions(self):
        expected={'platform':((-300,-170,-900),(300,0,300)),'buoy':((-70,-100,-70),(70,100,70)),'raft':((-250,-50,-350),(250,0,350))}
        for name,bounds in expected.items():
            vertices=[p for prism in fixtures.geometry(name) for p in prism]
            self.assertEqual(tuple(min(p[i] for p in vertices) for i in range(3)),bounds[0])
            self.assertEqual(tuple(max(p[i] for p in vertices) for i in range(3)),bounds[1])
if __name__=='__main__':unittest.main()
