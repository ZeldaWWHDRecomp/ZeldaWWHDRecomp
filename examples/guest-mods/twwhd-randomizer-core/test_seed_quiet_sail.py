"""Full-function policy hashes reject partial or unrelated executable changes."""
import hashlib
import json
from pathlib import Path
from unittest.mock import patch
import unittest
import seed_quiet_sail as policy
from seed_headers import render_quiet_sail

class QuietPolicy(unittest.TestCase):
    def test_source_only_review_extension(self):
        repo=Path(__file__).resolve().parents[3]
        base=json.loads((repo/'tools/recomp/randomizer-usa.json').read_text())
        source=json.loads((repo/'tools/recomp/source-core-usa.json').read_text())
        native=next(f for f in base['functions'] if f['start']==policy.START)
        extension=next(f for f in source['functions'] if f['start']==policy.START)
        self.assertEqual(native['end'],policy.END)
        self.assertEqual(extension['end'],policy.END)
        self.assertIn(policy.DISABLED,native['sha256'])
        self.assertNotIn(policy.ENABLED,native['sha256'])
        self.assertEqual(extension['sha256'],[policy.ENABLED])

    def test_variants_and_unknown(self):
        size=policy.END-policy.START
        self.assertGreaterEqual(size,64)
        self.assertEqual(size,1684)
        off=bytes((i*17)&255 for i in range(size))
        on=bytearray(off)
        for site in (0x0247E9AC,0x0247EA20,0x0247EA2C):
            n=site-policy.START;on[n:n+4]=b'TEST'
        with patch.object(policy,'DISABLED',hashlib.sha256(off).hexdigest()),patch.object(policy,'ENABLED',hashlib.sha256(on).hexdigest()):
            for value,data in ((0,off),(1,on)):
                self.assertEqual(policy.extract_quiet_sail(lambda a,n:data),value)
                self.assertEqual(render_quiet_sail(value),f'#define WWHD_RANDOMIZER_QUIET_SWIFT_SAIL {value}\n')
                for offset in range(size):
                    bad=bytearray(data);bad[offset]^=1
                    with self.assertRaisesRegex(ValueError,'unsupported'):
                        policy.extract_quiet_sail(lambda a,n:bad)
            # Mixed three-site combinations are not either reviewed whole body.
            for mask in range(1,7):
                mixed=bytearray(off)
                for index,site in enumerate((0x0247E9AC,0x0247EA20,0x0247EA2C)):
                    if mask&(1<<index):n=site-policy.START;mixed[n:n+4]=b'TEST'
                with self.assertRaisesRegex(ValueError,'unsupported'):
                    policy.extract_quiet_sail(lambda a,n:mixed)
        for wrong in (None,b'',off[:-1],off+b'x'):
            with self.assertRaisesRegex(ValueError,'truncated'):
                policy.extract_quiet_sail(lambda a,n:wrong)
        for invalid in (True,False,-1,2,'1',None):
            with self.assertRaises(ValueError):render_quiet_sail(invalid)

if __name__=='__main__':unittest.main()
