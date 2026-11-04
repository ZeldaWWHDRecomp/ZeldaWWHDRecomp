"""Synthetic unpacked-release worker, explicit compiler and failure boundaries."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import core_randomizer_resources as resources


class CoreWorker(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo, self.release = self.root/'public', self.root/'release'
        core = self.repo/resources.CORE
        core.mkdir(parents=True)
        # Synthetic public source API probes orchestrator's contract. Real
        # preparation/package safety remains covered by test_seed_package.
        (core/'mod.c').write_text('int fixture(void){return 1;}')
        (core/'LICENSE-TWWHD-Randomizer.txt').write_text('synthetic licence')
        (core/'seed_prepare.py').write_text('''from pathlib import Path

def prepare_seed(original, randomized, output, include_content=False):
    assert include_content
    if (original/'fail-prepare').exists(): raise ValueError('fixture preparation failure')
    output.mkdir();(output/'ready').write_text('prepared')
    if (original/'change-resource').exists():Path(__file__).with_name('mod.c').write_text('mutated during preparation')
''')
        (core/'seed_package.py').write_text('''from pathlib import Path
import hashlib,json,subprocess
FLAGS=['--target=powerpc-unknown-eabi','-mcpu=750']
def _read(path,maximum):
    data=path.read_bytes()
    if len(data)>maximum:raise ValueError('oversized')
    return data

def build_package(prepared,base,seed,output,compiler,**kw):
    assert (prepared/'ready').is_file()
    assert kw['source_dir'].is_dir() and kw['sdk_include'].is_dir()
    assert kw['provenance']['release_version']=='fixture-release'
    assert len(kw['expected_source_sha256'])==len(kw['expected_sdk_sha256'])==64
    assert len(compiler)==3 and compiler[1:] == ['--fixture','literal;no-shell']
    if (base/'fail-build').exists():raise ValueError('fixture build failure')
    if (base/'change-resource').exists():
        raise ValueError('fixture changed inputs')
    output.mkdir();(output/'mod.elf').write_bytes(b'fixture')
    if (base/'interrupt-copy').exists():raise OSError('fixture copy failure')
    (output/'manifest.json').write_text(json.dumps({'id':'twwhd-randomizer-core'}))
    return {'seed_identity':'a'*64,'elf_sha256':hashlib.sha256(b'fixture').hexdigest()}
''')
        names=sorted(p.name for p in core.iterdir())
        (core/'public-files.json').write_text(json.dumps(names))
        for name in resources.DEPENDENCIES:
            target=self.release/name
            target.parent.mkdir(parents=True,exist_ok=True)
            actual=Path(__file__).parent/Path(name).name
            target.write_bytes(actual.read_bytes() if actual.exists() else b'# synthetic public dependency\n')
        (self.release/'sdk/guest/include').mkdir(parents=True)
        (self.release/'sdk/guest/include/wwhd_guest.h').write_text('/* fixture sdk */')
        resources.bundle(self.repo,self.release,'fixture-release')
        self.original,self.seed,self.output=self.root/'original',self.root/'seed',self.root/'output'
        self.original.mkdir();self.seed.mkdir()
        (self.original/'owned').write_text('original unchanged')
        (self.seed/'owned').write_text('seed unchanged')
        self.compiler=self.root/'compiler fixture.py'
        self.compiler.write_text('''import sys
from pathlib import Path
if '--fixture' not in sys.argv:sys.exit(9)
out=Path(sys.argv[sys.argv.index('-o')+1])
data=bytearray(20);data[:6]=b'\\x7fELF\\x01\\x02';data[18:20]=b'\\x00\\x14';out.write_bytes(data)
''')
        self.command=[sys.executable,str(self.release/'tools/core_randomizer.py'),
                      '--resources',str(self.release/resources.DESCRIPTOR),'--original',str(self.original),
                      '--randomized',str(self.seed),'--output',str(self.output),'--compiler-json',
                      json.dumps([sys.executable,str(self.compiler),'--fixture','literal;no-shell'])]
        # Adapter receives structured argv including executable and script.
        package=self.release/resources.CORE/'seed_package.py'
        text=package.read_text().replace("len(compiler)==3 and compiler[1:]", "len(compiler)==4 and compiler[2:]")
        package.write_text(text)
        self.refresh()

    def refresh(self):
        descriptor=self.release/resources.DESCRIPTOR
        d=json.loads(descriptor.read_text())
        d['files']=resources.inventory(self.release,d['files'])
        d['provenance']['source_sha256']=resources.tree_digest(d['files'],resources.CORE,True)
        descriptor.write_text(json.dumps(d))

    def run_worker(self,success):
        result=subprocess.run(self.command,capture_output=True,text=True,
                              env={'PATH':'',**({'SYSTEMROOT':__import__('os').environ['SYSTEMROOT']} if 'SYSTEMROOT' in __import__('os').environ else {})})
        self.assertEqual(result.returncode==0,success,result.stderr)
        self.assertEqual((self.original/'owned').read_text(),'original unchanged')
        self.assertEqual((self.seed/'owned').read_text(),'seed unchanged')
        self.assertFalse(list(self.root.glob('.wwhd-core-build-*')))
        return result

    def test_real_public_release_import_closure(self):
        repo=Path(__file__).resolve().parents[1]
        release=self.root/'actual-public-release';release.mkdir()
        for name in resources.DEPENDENCIES:
            target=release/name;target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(repo/name,target)
        shutil.copytree(repo/'runtime/guest/include',release/'sdk/guest/include')
        for path in (repo/'tools/recomp').glob('hooks*.txt'):
            shutil.copyfile(path,release/'tools/recomp'/path.name)
        shutil.copytree(repo/'tools/recomp/builds',release/'tools/recomp/builds')
        descriptor=resources.bundle(repo,release,'public-fixture')
        self.assertEqual(resources.load(release/resources.DESCRIPTOR)[1],descriptor)
        script="import sys;sys.path[:0]=sys.argv[1:];import seed_prepare,seed_package,randomizer_compat,source_core_compat,build_guest_mod;assert seed_package.REPO.name=='actual-public-release'"
        result=subprocess.run([sys.executable,'-B','-c',script,str(release/resources.CORE),
                               str(release/'tools/recomp'),str(release/'tools'),str(release/'tools/guestmod')],
                              capture_output=True,text=True,env={'PATH':''})
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse((release/'.git').exists())
        # Player-owned headers and arbitrary untracked source files are absent.
        for name in resources.FORBIDDEN:self.assertFalse((release/resources.CORE/name).exists())
        self.assertFalse(any(p.name.startswith('test_') for p in (release/resources.CORE).iterdir()))

    def test_release_without_git_path_or_installed_game(self):
        receipt=json.loads(self.run_worker(True).stdout)
        self.assertEqual(receipt['manager_action'],'import')
        self.assertTrue(receipt['restart_required'])
        self.assertTrue((self.output/'manifest.json').exists())
        self.assertFalse((self.release/'.git').exists())

    def test_progress_and_cancel_before_build(self):
        progress=self.root/'progress.json';cancel=self.root/'cancel'
        self.command += ['--progress-file',str(progress),'--cancel-file',str(cancel)]
        cancel.touch()
        self.run_worker(False);self.assertFalse(self.output.exists());self.assertFalse(progress.exists())
        cancel.unlink()
        self.run_worker(True)
        self.assertEqual(json.loads(progress.read_text())['phase'],'Ready to import')

    def test_progress_cannot_overwrite_game_input(self):
        self.command += ['--progress-file',str(self.original/'owned')]
        self.run_worker(False);self.assertFalse(self.output.exists())

    def test_failures_no_completion_manifest(self):
        for marker in ('fail-prepare','fail-build','change-resource','interrupt-copy'):
            with self.subTest(marker=marker):
                (self.original/marker).touch()
                self.run_worker(False)
                self.assertFalse((self.output/'manifest.json').exists())
                (self.original/marker).unlink()
                (self.release/resources.CORE/'mod.c').write_text('int fixture(void){return 1;}')
                self.refresh()
                if self.output.exists():shutil.rmtree(self.output)

    def test_existing_output_and_compiler_refusal(self):
        self.output.mkdir();(self.output/'owned').write_text('keep')
        self.run_worker(False);self.assertEqual((self.output/'owned').read_text(),'keep')
        shutil.rmtree(self.output)
        self.compiler.write_text('raise SystemExit(3)')
        self.run_worker(False);self.assertFalse(self.output.exists())

    def test_resources_changed_and_private_sources_not_bundled(self):
        (self.release/resources.CORE/'mod.c').write_text('changed')
        self.run_worker(False);self.assertFalse(self.output.exists())
        self.assertFalse((self.release/resources.CORE/'seed_init_data.h').exists())
        self.assertFalse((self.release/resources.CORE/'content').exists())


if __name__=='__main__':unittest.main()
