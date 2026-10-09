#!/usr/bin/env python3
"""Tests of the guest mod translator (Mod SDK v2 prototype).

Builds the example mods with a PowerPC-capable clang (WWHD_PPC_CLANG / WWHD_PPC_LLD, or clang and
ld.lld on PATH; skipped without one), translates them and compiles the modules with the host
compiler. No game files are needed.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import guestmod  # noqa: E402
import build_guest_mod as builder  # noqa: E402

CLANG = os.environ.get("WWHD_PPC_CLANG") or shutil.which("clang")
LLD = os.environ.get("WWHD_PPC_LLD") or shutil.which("ld.lld")
FLAGS = ["--target=powerpc-unknown-eabi", "-mcpu=750", "-O2", "-ffreestanding", "-fno-builtin", "-nostdlib",
         "-fno-jump-tables", "-ffunction-sections", "-fdata-sections",
         "-I", os.path.join(REPO, "runtime", "guest", "include")]


def ppc_ok():
    if not CLANG or not LLD or not shutil.which(CLANG) or not shutil.which(LLD):
        return False
    p = subprocess.run([CLANG, "--target=powerpc-unknown-eabi", "-x", "c", "-c", "-o", os.devnull, "-"],
                       input="int x;", capture_output=True, text=True)
    if p.returncode:
        print("PowerPC compiler probe failed:", p.stderr, file=sys.stderr)
    return p.returncode == 0


@unittest.skipUnless(ppc_ok(), "no clang with the PowerPC target / ld.lld")
class GuestModTest(unittest.TestCase):
    def test_european_hook_calls_function_pointer_and_data(self):
        from builds import by_name
        src = r'''
#include "wwhd_guest.h"
WWHD_GAME_FUNC(0x0240EBB0, void, link_execute, (void*));
WWHD_GAME_ORIGINAL(0x0240EBB0, void, original, (void*));
void (*volatile function_pointer)(void*) = link_execute;
WWHD_HOOK(0x0240EBB0, mapped, (void* actor)) {
    WWHD_GAME_DATA(0x101F84DC, u32) = 123;
    WWHD_GAME_DATA(0x1048D0CC, f32) = 1.0f;
    link_execute(actor); original(actor);
}
'''
        with tempfile.TemporaryDirectory() as d:
            elf = Path(self.build_elf(src, d)).read_bytes()
            eu = by_name("EU")
            usa = guestmod.Translator(guestmod.Elf(elf), 0x7F000000)
            mapped = guestmod.Translator(guestmod.Elf(elf), 0x7F000000, eu)
            self.assertEqual(mapped.hooks[0][1], eu.code(0x0240EBB0))
            self.assertEqual({value for kind, value in mapped.imports.values() if kind != "svc"},
                             {eu.code(0x0240EBB0)})
            self.assertIn(eu.code(0x0240EBB0).to_bytes(4, "big"), mapped.image)
            self.assertNotEqual(usa.image, mapped.image)
            # lis/addi materializes the mapped data address, including its adjusted high half.
            address = eu.data(0x101F84DC)
            halves = [int.from_bytes(mapped.image[i:i+2], "big") for i in range(0, len(mapped.image), 2)]
            self.assertIn((address + 0x8000) >> 16, halves)
            self.assertIn(address & 0xFFFF, halves)
            self.assertIn(eu.data(0x1048D0CC) & 0xFFFF, halves)
            include = str(Path(REPO, "runtime/include"))
            self.assertNotEqual(builder.cache_key(elf, "mapped", 0x7F000000, builder.default_cc(), include),
                                builder.cache_key(elf, "mapped", 0x7F000000, builder.default_cc(), include, eu))

    def test_european_changed_and_unmapped_functions_refused(self):
        from builds import by_name
        eu = by_name("EU")
        for address in (eu.differing[0][0], 0x01000000):
            with self.subTest(address=address), tempfile.TemporaryDirectory() as d:
                for use in (f'WWHD_HOOK(0x{address:08X}, hook, (void)) {{ }}',
                            f'WWHD_GAME_FUNC(0x{address:08X}, void, game, (void)); void mod(void) {{ game(); }}'):
                    elf = Path(self.build_elf('#include "wwhd_guest.h"\n' + use, d)).read_bytes()
                    with self.assertRaisesRegex(guestmod.ModError, f'0x{address:08X} on EU'):
                        builder.translator_for(elf, 0x7F000000, eu)

    def test_european_data_addend_maps_before_shift(self):
        from builds import Build
        eu = Build({"name": "EU", "title_id": "test", "rpx_sha256": "test",
                    "code_bounds": ["02000000", "03000000"],
                    "data_bounds": ["10000000", "11000000"],
                    "data_steps": [["10000000", 4], ["10000004", 16]]})
        src = '#include "wwhd_guest.h"\n __asm__(".section .data,\\\"aw\\\"\\n.long __wwhd_gdata_0x10000000 + 8\\n");'
        with tempfile.TemporaryDirectory() as d:
            elf = Path(self.build_elf(src, d)).read_bytes()
            t = builder.translator_for(elf, 0x7F000000, eu)
            self.assertIn((0x10000018).to_bytes(4, "big"), t.image)

    def test_european_old_sdk_package_refused(self):
        from builds import by_name
        with tempfile.TemporaryDirectory() as d:
            elf = Path(self.build_elf('int old_mod(void) { return *(volatile int*)0x101F84DC; }', d)).read_bytes()
            builder.translator_for(elf, 0x7F000000)  # USA retains compatibility
            with self.assertRaisesRegex(guestmod.ModError, 'older SDK'):
                builder.translator_for(elf, 0x7F000000, by_name("EU"))

    def test_public_headers_c_and_cpp(self):
        headers = ("bindings", "actor", "link", "camera", "items", "save", "messages", "data")
        source = "".join('#include "wwhd/%s.h"\n' % name for name in headers)
        for language, standard in (("c", "c11"), ("c++", "c++17")):
            with self.subTest(language=language):
                subprocess.run([CLANG] + FLAGS + ["-x", language, "-std=" + standard,
                               "-fsyntax-only", "-"], input=source, text=True, check=True)

    def build_elf(self, src_text, d):
        src = os.path.join(d, "mod.c")
        with open(src, "w") as f:
            f.write(src_text)
        subprocess.run([CLANG] + FLAGS + ["-c", src, "-o", os.path.join(d, "mod.o")], check=True)
        subprocess.run([LLD, "-m", "elf32ppc", "-r", os.path.join(d, "mod.o"), "-o", os.path.join(d, "mod.elf")], check=True)
        return os.path.join(d, "mod.elf")

    def test_examples_build(self):
        for mod in ("heart-ticker", "addcalc-replace"):
            with tempfile.TemporaryDirectory() as d:
                pkg = os.path.join(d, "pkg")
                os.makedirs(pkg)
                shutil.copy(os.path.join(REPO, "examples", "guest-mods", mod, "manifest.json"), pkg)
                with open(os.path.join(REPO, "examples", "guest-mods", mod, "mod.c")) as f:
                    self.build_elf(f.read(), pkg)
                out = subprocess.run([sys.executable, os.path.join(HERE, "build_guest_mod.py"), pkg, "--out",
                                      os.path.join(d, "cache"), "--json"], capture_output=True, text=True)
                r = json.loads(out.stdout.strip().splitlines()[-1])
                self.assertTrue(r["ok"], r)
                self.assertTrue(os.path.isfile(r["module"]))
                again = subprocess.run([sys.executable, os.path.join(HERE, "build_guest_mod.py"), pkg, "--out",
                                        os.path.join(d, "cache"), "--json"], capture_output=True, text=True)
                self.assertTrue(json.loads(again.stdout.strip().splitlines()[-1])["cached"])

    def test_inspect_without_compiler(self):
        with tempfile.TemporaryDirectory() as d:
            Path(d, "manifest.json").write_text(json.dumps({"id": "inspect-test", "kind": "guest",
                                                          "guest": {"api_version": 1}}))
            self.build_elf("int value = 3; int f(void) { return value; }", d)
            with mock.patch.object(builder.subprocess, "run", side_effect=AssertionError("compiler invoked")):
                result = builder.inspect_package(d, 0x7F100000)
            self.assertEqual(result["base"], 0x7F100000)
            self.assertGreater(result["memory_size"], 0)
            self.assertEqual(result["allocation_size"] % 65536, 0)
            self.assertEqual(len(result["elf_sha256"]), 64)

    def test_relocations_and_imports(self):
        src = r'''
#include "wwhd_guest.h"
WWHD_GAME_FUNC(0x02001234, int, game_fn, (int));
WWHD_GAME_ORIGINAL(0x02005678, void, orig_fn, (void));
static int table[4] = {1, 2, 3, 4};
int (*volatile ptr)(int) = 0;
__attribute__((noinline)) static int helper(int x) { return table[x & 3] + game_fn(x); }
WWHD_REPLACE(0x02005678, void, repl, (void)) { ptr = helper; orig_fn(); wwhd_log_int("v", ptr(2)); }
'''
        with tempfile.TemporaryDirectory() as d:
            t = guestmod.Translator(guestmod.Elf(Path(self.build_elf(src, d)).read_bytes()), 0x7F200000)
            c = t.emit("test")
            self.assertEqual([(k, tg) for k, tg, _, _ in t.hooks], [(1, 0x02005678)])
            self.assertIn("c->pc = 0x02001234u; ppc_dispatch(c);", c)
            self.assertIn("g_host->call_original(c, 0x02005678u);", c)
            self.assertEqual(t.services, ["wwhd_log_int"])
            self.assertTrue(all(0x7F200000 <= e < t.end for e in t.entries))
            self.assertGreaterEqual(len(t.entries), 2)  # helper is address-taken: its own function

    def test_host_services_compile(self):
        src = r'''
#include "wwhd_guest.h"
WWHD_HOOK(0x02000000, all_services, (void)) {
    char* p = wwhd_malloc(64);
    wwhd_input_state pad;
    wwhd_input_read(&pad);
    wwhd_config_string("choice", p, 64);
    if (wwhd_config_bool("on", 0)) wwhd_log_float("dt", wwhd_logic_dt());
    wwhd_log_float("amount", wwhd_config_float("amount", 1.5));
    wwhd_log_int("step", (int)wwhd_logic_step());
    wwhd_file_write("progress.bin", p, 64);
    wwhd_file_read("progress.bin", p, 64);
    wwhd_free(p);
}
'''
        with tempfile.TemporaryDirectory() as d:
            t = guestmod.Translator(guestmod.Elf(Path(self.build_elf(src, d)).read_bytes()), 0x7F000000)
            translated = t.emit("services")
            for address, (kind, name) in t.imports.items():
                if kind == "svc":
                    self.assertIn("c->pc = 0x%08Xu;" % address, translated, name)
            self.assertIn("wwhd_file_write", t.services)
            Path(d, "manifest.json").write_text(json.dumps({"kind": "guest", "id": "services", "guest": {"api_version": 1}}))
            result = builder.build(d, str(Path(d, "cache")), 0x7F000000, builder.default_cc(), str(Path(REPO, "runtime/include")))
            self.assertTrue(result["ok"])

    def test_register_pair_module_executes(self):
        # Leaf guest functions need no guest RAM: this executes the actual translated module.
        src = r'''
#include "wwhd/bindings.h"
WWHD_REPLACE(0x02000000, wwhd_gpr_pair, pair_result, (void)) {
    return 0x1122334455667788ULL;
}
WWHD_REPLACE(0x02000004, u32, pair_low, (wwhd_gpr_pair value)) {
    return WWHD_RESULT_R4(value);
}
'''
        driver = r'''
#include "wwhd_guest_abi.h"
#include <assert.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
int main(int argc, char** argv) {
    assert(argc == 2);
#ifdef _WIN32
    HMODULE library = LoadLibraryA(argv[1]); assert(library);
    WWHDGuestInitV1 init = (WWHDGuestInitV1)GetProcAddress(library, WWHD_GUEST_INIT_SYMBOL);
#else
    void* library = dlopen(argv[1], RTLD_NOW); assert(library);
    WWHDGuestInitV1 init = (WWHDGuestInitV1)dlsym(library, WWHD_GUEST_INIT_SYMBOL);
#endif
    assert(init);
    volatile int preempt[3] = {0};
    WWHDGuestHostV1 host = {0}; host.size = sizeof(host);
    host.abi_version = WWHD_GUEST_ABI_VERSION; host.core_preempt = preempt;
    const WWHDGuestModuleV1* module = init(&host); assert(module && module->hook_count == 2);
    Cpu cpu = {0}; int pairs = 0, lows = 0;
    for (uint32_t i = 0; i < module->hook_count; ++i) if (module->hooks[i].target == 0x02000000) {
        ++pairs; module->hooks[i].fn(&cpu);
        assert(cpu.r[3] == 0x11223344 && cpu.r[4] == 0x55667788);
    }
    cpu.r[3] = 0xABCDEF01; cpu.r[4] = 0x12345678;
    for (uint32_t i = 0; i < module->hook_count; ++i) if (module->hooks[i].target == 0x02000004) {
        ++lows; module->hooks[i].fn(&cpu); assert(cpu.r[3] == 0x12345678);
    }
    assert(pairs == 1 && lows == 1);
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            self.build_elf(src, d)
            Path(d, 'manifest.json').write_text(json.dumps({'kind': 'guest', 'id': 'register-pair', 'guest': {'api_version': 1}}))
            result = builder.build(d, str(Path(d, 'cache')), 0x7F000000, builder.default_cc(), str(Path(REPO, 'runtime/include')))
            source = Path(d, 'driver.c'); source.write_text(driver)
            executable = Path(d, 'driver.exe' if os.name == 'nt' else 'driver')
            command = builder.default_cc() + ['-O2', '-I', str(Path(REPO, 'runtime/include')), str(source), '-o', str(executable)]
            if sys.platform.startswith('linux'):
                command.append('-ldl')
            subprocess.run(command, check=True)
            subprocess.run([str(executable), result['module']], check=True)

    def test_errors(self):
        with self.assertRaises(guestmod.ModError):
            guestmod.Elf(b"not an elf at all" * 4)


class BuildInterfaceTest(unittest.TestCase):
    def test_source_and_release_include_roots(self):
        with tempfile.TemporaryDirectory() as d, mock.patch.object(builder, "REPO", d):
            self.assertEqual(builder.default_include(), os.path.join(d, "runtime", "include"))
            installed = Path(d, "sdk", "include")
            installed.mkdir(parents=True)
            (installed / "wwhd_guest_abi.h").write_text("/* ABI fixture */")
            self.assertEqual(builder.default_include(), str(installed))

    def test_package_paths_and_ids(self):
        with tempfile.TemporaryDirectory() as d:
            pkg = Path(d, "pkg"); pkg.mkdir()
            (pkg / "mod.elf").write_bytes(b"test-elf")
            man = {"id": "valid-id", "kind": "guest", "guest": {"api_version": 1}}
            def write():
                (pkg / "manifest.json").write_text(json.dumps(man))
            write()
            self.assertEqual(builder.package_elf(pkg)[1], b"test-elf")
            for name in ("../mod.elf", "/mod.elf", "C:/mod.elf", "a\\b", "a//b", "./mod.elf"):
                man["guest"]["elf"] = name; write()
                with self.subTest(path=name), self.assertRaises(guestmod.ModError):
                    builder.package_elf(pkg)
            man["guest"]["elf"] = "mod.elf"
            for mod_id in ("../bad", "", ".bad", "bad/id", 1):
                man["id"] = mod_id; write()
                with self.subTest(id=mod_id), self.assertRaises(guestmod.ModError):
                    builder.package_elf(pkg)
            man["id"] = "valid-id"; write()
            (pkg / "mod.elf").unlink()
            Path(d, "outside.elf").write_bytes(b"outside")
            try:
                (pkg / "mod.elf").symlink_to(Path(d, "outside.elf"))
            except OSError:
                return # Windows runners without symlink privileges still exercise path validation.
            with self.assertRaises(guestmod.ModError):
                builder.package_elf(pkg)

    def test_cache_invalidation(self):
        with tempfile.TemporaryDirectory() as d:
            for name in ("ppc.h", "wwhd_guest_abi.h"):
                shutil.copy(Path(REPO, "runtime", "include", name), d)
            with mock.patch.object(builder.subprocess, "run", return_value=mock.Mock(returncode=0, stdout="clang test")):
                def key(elf=b"elf", mod_id="mod", base=0x7F000000, cc=None):
                    return builder.cache_key(elf, mod_id, base, cc or ["clang"], d)
                initial = key()
                self.assertEqual(initial, key())
                self.assertNotEqual(initial, key(elf=b"changed"))
                self.assertNotEqual(initial, key(mod_id="other"))
                self.assertNotEqual(initial, key(base=0x7F100000))
                self.assertNotEqual(initial, key(cc=["zig", "cc"]))
                for name in ("ppc.h", "wwhd_guest_abi.h"):
                    p = Path(d, name); before = p.read_bytes()
                    p.write_bytes(before + b"\n/* changed ABI source */\n")
                    self.assertNotEqual(initial, key())
                    p.write_bytes(before)
                with mock.patch.object(guestmod, "TRANSLATOR_VERSION", "new-version"):
                    self.assertNotEqual(initial, key())
                read_bytes = Path.read_bytes
                def changed_source(path):
                    data = read_bytes(path)
                    return data + b"changed" if path.name == "ppc2c.py" else data
                with mock.patch.object(Path, "read_bytes", changed_source):
                    self.assertNotEqual(initial, key())
                with mock.patch.object(builder.subprocess, "run", return_value=mock.Mock(returncode=0, stdout="clang newer")):
                    self.assertNotEqual(initial, key())

    def test_truncated_elf_errors(self):
        for length in range(52):
            data = (b"\x7fELF\x01\x02" + bytes(52))[:length]
            with self.subTest(length=length), self.assertRaises(guestmod.ModError):
                builder.translator_for(data, 0x7F000000)

    def test_invalid_base(self):
        for base in (0, 0x7F000001, 0x80000000, -1):
            with self.subTest(base=base), self.assertRaises(guestmod.ModError):
                builder.translator_for(b"unused", base)


if __name__ == "__main__":
    unittest.main()
