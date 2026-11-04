import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import permanent_resource_policy as policy
from contextlib import contextmanager

SYNTHETIC_BODY=bytes(i%251 for i in range(policy.END-policy.START))

@contextmanager
def synthetic_permanent_hash():
    with patch.object(policy, 'EXPECTED', hashlib.sha256(SYNTHETIC_BODY).hexdigest()):
        yield


class PermanentResourceHeap(unittest.TestCase):
    def test_complete_policy_hash(self):
        self.assertGreaterEqual(policy.END-policy.START,64)
        body=bytes(i%251 for i in range(policy.END-policy.START))
        with patch.object(policy,'EXPECTED',hashlib.sha256(body).hexdigest()):
            policy.validate_permanent_resource_policy(lambda a,n:body)
            for offset in (0,len(body)//2,len(body)-1):
                bad=bytearray(body);bad[offset]^=1
                with self.assertRaisesRegex(ValueError,'unsupported'):
                    policy.validate_permanent_resource_policy(lambda a,n:bad)
            for bad in (None,b'',body[:-1],body+b'!'):
                with self.assertRaisesRegex(ValueError,'truncated'):
                    policy.validate_permanent_resource_policy(lambda a,n:bad)

    def test_complete_initialization_callbacks(self):
        source=Path(__file__).with_name('permanent_resource_heap_test.c')
        with tempfile.TemporaryDirectory() as directory:
            exe=Path(directory)/'test'
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
