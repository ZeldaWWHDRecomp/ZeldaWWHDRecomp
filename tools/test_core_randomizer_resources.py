from pathlib import Path
import tempfile
import unittest

from core_randomizer_resources import CORE, validate_guest_includes


class GuestHeaderClosure(unittest.TestCase):
    def test_transitive_dependency_must_be_in_shipped_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/CORE;source.mkdir(parents=True)
            (source/'mod.c').write_text('#include "outer.h"\n')
            (source/'outer.h').write_text('#include "inner.h"\n')
            (source/'inner.h').write_text('/* present in checkout, absent from release list */')
            files={CORE+'/mod.c':'',CORE+'/outer.h':''}
            with self.assertRaisesRegex(ValueError,'inner.h'):
                validate_guest_includes(root,files)
            files[CORE+'/inner.h']=''
            validate_guest_includes(root,files)

    def test_sdk_relative_and_private_generated_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/CORE;source.mkdir(parents=True)
            sdk=root/'sdk/guest/include';(sdk/'wwhd').mkdir(parents=True)
            (source/'mod.c').write_text('#include "seed_init_data.h"\n#include "wwhd/save.h"\n#include <stdint.h>\n')
            (sdk/'wwhd/save.h').write_text('#include "../wwhd_guest.h"\n')
            (sdk/'wwhd_guest.h').write_text('/* public SDK */')
            files={CORE+'/mod.c':'','sdk/guest/include/wwhd/save.h':'','sdk/guest/include/wwhd_guest.h':''}
            validate_guest_includes(root,files)
            del files['sdk/guest/include/wwhd_guest.h']
            with self.assertRaisesRegex(ValueError,'wwhd_guest.h'):
                validate_guest_includes(root,files)
