"""Compile and execute the public save-flag adapter through the real guest-mod translator.

Uses synthetic save bytes and native-method stand-ins with the pinned methods' byte semantics.
No game files or PowerPC linker are required. Exercise both USA and EU relocation maps.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import build_guest_mod as builder
import guestmod
from builds import by_name

CLANG = os.environ.get('WWHD_PPC_CLANG') or shutil.which('clang')
FLAGS = ['--target=powerpc-unknown-eabi', '-mcpu=750', '-O2', '-ffreestanding', '-fno-builtin',
         '-fno-jump-tables', '-ffunction-sections', '-fdata-sections', '-nostdlib',
         '-I', str(ROOT/'runtime/guest/include')]


def ppc_available():
    if not CLANG or not shutil.which(CLANG):
        return False
    return subprocess.run([CLANG, *FLAGS, '-x', 'c', '-c', '-o', os.devnull, '-'],
                          input='int fixture;', text=True, capture_output=True).returncode == 0


SOURCE = r'''
#include "wwhd/save_flags.h"
/* Synthetic hook targets, only called by this test's driver. */
WWHD_REPLACE(0x02000020, s32, read_field, (u32 field)) { return wwhd_save_event_read(field); }
WWHD_REPLACE(0x020000B4, s32, write_field, (u32 field, u32 bits)) { return wwhd_save_event_write(field,bits); }
WWHD_REPLACE(0x02000108, s32, test_flag, (u32 field)) { return wwhd_save_event_test(field); }
WWHD_REPLACE(0x02000110, s32, set_flag, (u32 field, s32 on)) { return wwhd_save_event_set(field,on); }
'''

DRIVER = r'''
#undef NDEBUG
#include "wwhd_guest_abi.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/mman.h>
#endif
static unsigned calls;
static const WWHDGuestModuleV1* module;
static uint32_t targets[4];
static uint32_t functions[5];
static uint32_t pointer;
static void allocate(uint32_t at, size_t n) {
#ifdef _WIN32
    assert(VirtualAlloc(ppc_ptr(at),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
    assert(mmap(ppc_ptr(at),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
static void dispatch(Cpu* c) {
    ++calls;
    assert(c->r[3]==0x20000644);
    assert(c->r[4]<=0xFFFF);
    const uint32_t at=c->r[3]+(c->r[4]>>8), mask=c->r[4]&255;
    const uint8_t old=ld8(at);
    if(c->pc==functions[0]) c->r[3]=old&mask;                   /* getEventReg */
    else if(c->pc==functions[1]) st8(at,(old&~mask)|c->r[5]); /* setEventReg */
    else if(c->pc==functions[2]) c->r[3]=(old&mask)!=0;        /* isEventBit */
    else if(c->pc==functions[3]) st8(at,old|mask);            /* onEventBit */
    else if(c->pc==functions[4]) st8(at,old&~mask);           /* offEventBit */
    else assert(!"unmapped native call");
}
static void unimplemented(Cpu* c,uint32_t a,uint32_t i) { (void)c;(void)a;(void)i;abort(); }
static int32_t call(unsigned index,uint32_t field,uint32_t value) {
    Cpu c={0};c.r[1]=0x20008000;c.r[3]=field;c.r[4]=value;
    for(unsigned i=0;i<module->hook_count;i++) if(module->hooks[i].target==targets[index]) {
        module->hooks[i].fn(&c);return (int32_t)c.r[3];
    }
    assert(!"missing adapter hook");return 0;
}
int main(int argc,char** argv) {
    assert(argc==12);
    pointer=(uint32_t)strtoul(argv[2],0,16);
    for(unsigned i=0;i<4;i++) targets[i]=(uint32_t)strtoul(argv[3+i],0,16);
    for(unsigned i=0;i<5;i++) functions[i]=(uint32_t)strtoul(argv[7+i],0,16);
#ifdef _WIN32
    HMODULE library=LoadLibraryA(argv[1]);assert(library);
    WWHDGuestInitV1 init=(WWHDGuestInitV1)GetProcAddress(library,WWHD_GUEST_INIT_SYMBOL);
#else
    void* library=dlopen(argv[1],RTLD_NOW);assert(library);
    WWHDGuestInitV1 init=(WWHDGuestInitV1)dlsym(library,WWHD_GUEST_INIT_SYMBOL);
#endif
    volatile int preempt[3]={0};WWHDGuestHostV1 host={0};
    host.size=sizeof(host);host.abi_version=WWHD_GUEST_ABI_VERSION;
    host.dispatch=dispatch;host.unimplemented=unimplemented;host.core_preempt=preempt;
    module=init(&host);assert(module&&module->hook_count==4);
    allocate(module->mem_base,module->mem_size);memcpy(ppc_ptr(module->mem_base),module->image,module->image_size);
    allocate(pointer&~0xFFFFu,0x10000);allocate(0x20000000,0x10000);

    /* No allocation yet: no native calls. Invalid selectors take precedence over readiness. */
    assert(call(0,0xC407,0)==-2);assert(call(1,0xC407,2)==-2);
    assert(call(2,0xC401,0)==-2);assert(call(3,0xC401,1)==-2);assert(!calls);
    assert(call(0,0x10000,0)==-1);assert(call(0,0xC400,0)==-1);
    assert(call(2,0xC403,0)==-1);assert(call(3,0xC403,1)==-1);assert(!calls);
    st32(pointer,0xFFFFFFFC);assert(call(0,0xC407,0)==-2);
    st32(pointer,0x20000001);assert(call(0,0xC407,0)==-2);assert(!calls);
    st32(pointer,0x20000000);memset(ppc_ptr(0x20000644),0xA5,256);

    assert(call(0,0xC407,0)==5);
    assert(call(1,0xC407,2)==0);assert(ld8(0x20000708)==0xA2);
    unsigned before=calls;
    assert(call(1,0xC407,8)==-1);assert(call(1,0xC407,0x100)==-1);
    assert(call(1,0x10007,0)==-1);assert(calls==before&&ld8(0x20000708)==0xA2);
    assert(call(1,0xC4F0,0x30)==0);assert(ld8(0x20000708)==0x32);
    assert(call(0,0xC4F0,0)==0x30); /* raw aligned bits, no implicit shift */
    assert(call(2,0xC401,0)==0);assert(call(3,0xC401,1)==0);
    assert(call(2,0xC401,0)==1);assert(ld8(0x20000708)==0x33);
    assert(call(3,0xC401,0)==0);assert(call(2,0xC401,0)==0);
    assert(ld8(0x20000708)==0x32);
    assert(call(1,0x00FF,0x96)==0);assert(ld8(0x20000644)==0x96);
    assert(ld8(0x20000643)==0); /* guard before the event object */
    assert(call(1,0xFFFF,0x5A)==0);assert(ld8(0x20000743)==0x5A);
    assert(ld8(0x20000744)==0); /* guard after the 256-byte event object */
    return 0;
}
'''


@unittest.skipUnless(ppc_available(), 'no PowerPC-capable clang')
class SaveFlags(unittest.TestCase):
    def test_c_and_cpp_headers(self):
        for language, standard in (('c', 'gnu11'), ('c++', 'gnu++17')):
            with self.subTest(language=language):
                subprocess.run([CLANG, *FLAGS, '-x', language, '-std='+standard, '-fsyntax-only', '-'],
                               input=SOURCE, text=True, check=True, capture_output=True)

    def test_translated_adapter_boundaries_and_native_abi(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp);source=tmp/'mod.c';source.write_text(SOURCE)
            elf=tmp/'mod.elf'
            subprocess.run([CLANG, *FLAGS, '-c', str(source), '-o', str(elf)], check=True, capture_output=True)
            driver=tmp/'driver.c';driver.write_text(DRIVER)
            exe=tmp/('driver.exe' if os.name=='nt' else 'driver')
            command=builder.default_cc()+['-O2', '-I', str(ROOT/'runtime/include'), str(driver), '-o', str(exe)]
            if sys.platform.startswith('linux'): command.append('-ldl')
            subprocess.run(command, check=True, capture_output=True)
            for name in ('USA','EU'):
                with self.subTest(build=name):
                    build=by_name(name)
                    translator=guestmod.Translator(guestmod.Elf(elf.read_bytes()),0x7F000000,build)
                    self.assertEqual(translator.services, [])
                    generated=tmp/(name+'.c');generated.write_text(translator.emit('save-flags-test'))
                    library=tmp/(name+('.dll' if os.name=='nt' else '.dylib' if sys.platform=='darwin' else '.so'))
                    result=subprocess.run(builder.default_cc()+['-O2', '-shared', '-fPIC', '-I', str(ROOT/'runtime/include'),
                                                               str(generated), '-o', str(library)], capture_output=True,text=True)
                    self.assertEqual(result.returncode,0,result.stderr)
                    targets=[build.code(a) for a in (0x02000020,0x020000B4,0x02000108,0x02000110)]
                    functions=[build.code(a) for a in (0x025B8BB0,0x025B8AF4,0x025B8B94,0x025B8B68,0x025B8B7C)]
                    subprocess.run([str(exe), str(library), '%X'%build.data(0x101F84DC),
                                    *['%X'%a for a in targets+functions]], check=True,capture_output=True)


if __name__ == '__main__':
    unittest.main()
