"""Execute the actual PPC heap consumer through the public translated-module ABI.

No RPX or game data is used. The existing synthetic callbacks are dispatch mocks,
not engine allocations. Memory and stack addresses are genuinely 32-bit guest EA.
"""
import os
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
sys.path.insert(0,str(REPO/'tools/guestmod'))
import build_guest_mod as builder
import guestmod
from seed_package import FLAGS as CORE_PPC_FLAGS


def ppc_compiler():
    requested=os.environ.get('WWHD_PPC_CLANG')
    candidates=[requested] if requested else ['clang','/opt/homebrew/opt/llvm/bin/clang']
    for candidate in candidates:
        if not candidate or not shutil.which(candidate):
            continue
        result=subprocess.run([candidate,'--target=powerpc-unknown-eabi','-mstackrealign','-x','c','-c','-o',os.devnull,'-'],input='int probe;',text=True,capture_output=True)
        if not result.returncode:
            return candidate
    return None


class PermanentResourceTranslated(unittest.TestCase):
    def test_actual_module_guest_stack_and_dispatch(self):
        compiler=ppc_compiler()
        if not compiler:
            self.skipTest('PowerPC-capable clang unavailable')
        header=(HERE/'permanent_resource_heap.h').read_text()
        fixture=(HERE/'permanent_resource_heap_test.c').read_text()
        # Reuse the independent callback expectations, not a second implementation
        # of the consumer. Host-sized memory and direct C invocation are discarded.
        state=fixture[fixture.index('static const u32 self='):fixture.index('int main(void)')]
        main=fixture[fixture.index('int main(void)'):]
        main=main.replace('memset(memory,0,sizeof memory);','reset_guest();')
        main=main.replace('randomizer_permanent_resource_prepare(self);','invoke_heap(0x20010000+(mode&64?8:0));')
        main=main.replace('mode<64','mode<128')
        main=main.replace('invoke_heap(0x20010000+(mode&64?8:0));','invoke_heap(0x20010000+(mode&64?8:0));invoke_stack_args();')
        main=main.replace('int main(void){','int main(int argc,char**argv){load_module(argc,argv);')
        dispatch=[]
        for match in re.finditer(r'WWHD_GAME_FUNC\(0x([0-9A-F]+),(\w+),(\w+),\((.*?)\)\);',header):
            address,result,name,arguments=match.groups()
            count=0 if arguments=='void' else len(arguments.split(','))
            call=f'{name}('+','.join(f'c->r[{3+i}]' for i in range(count))+')'
            dispatch.append(f'case 0x{address}: '+('result='+call+';' if result!='void' else call+';')+'break;')
        virtual='''case 0xAA01:result=call3(c->pc,c->r[3],c->r[4],c->r[5]);break;
case 0xAA04:result=call1(c->pc,c->r[3]);break;
case 0xAA03:result=call2(c->pc,c->r[3],c->r[4]);break;'''
        prefix=r'''
#undef NDEBUG
#include "wwhd_guest_abi.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/mman.h>
#endif
typedef uint32_t u32;typedef uint8_t u8;
template<class T> struct GuestValue {
 uint32_t address;
 operator T()const {if constexpr(sizeof(T)==1)return (T)ld8(address);else return(T)ld32(address);}
 GuestValue& operator=(uint32_t value){if constexpr(sizeof(T)==1)st8(address,(uint8_t)value);else st32(address,value);return *this;}
};
#define R_PERM_HEAP(t,a) (GuestValue<t>{(uint32_t)(a)})
static const WWHDGuestModuleV1* module;
static uint32_t guest_sp;
static void dispatch(Cpu*);
static void map_guest(uint32_t address,size_t size){
#ifdef _WIN32
 assert(VirtualAlloc(ppc_ptr(address),size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==ppc_ptr(address));
#else
 assert(mmap(ppc_ptr(address),size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==ppc_ptr(address));
#endif
}
static void load_module(int argc,char**argv){
 assert(argc==2);
#ifdef _WIN32
 HMODULE library=LoadLibraryA(argv[1]);assert(library);
 WWHDGuestInitV1 init=(WWHDGuestInitV1)GetProcAddress(library,WWHD_GUEST_INIT_SYMBOL);
#else
 void*library=dlopen(argv[1],RTLD_NOW);assert(library);
 WWHDGuestInitV1 init=(WWHDGuestInitV1)dlsym(library,WWHD_GUEST_INIT_SYMBOL);
#endif
 assert(init);static volatile int preempt[3]={0};static WWHDGuestHostV1 host={};
 host.size=sizeof(host);host.abi_version=WWHD_GUEST_ABI_VERSION;host.core_preempt=preempt;host.dispatch=dispatch;
 module=init(&host);assert(module&&module->hook_count==2&&module->hooks[0].target==0x0203EE2C);
 map_guest(0,0x100000);map_guest(0x10000000,0x500000);map_guest(0x20000000,0x20000);map_guest(module->mem_base,(module->mem_size+0xFFFF)&~0xFFFFu);
 memcpy(ppc_ptr(module->mem_base),module->image,module->image_size);
}
static void reset_guest(){memset(ppc_ptr(0),0,0x100000);memset(ppc_ptr(0x10000000),0,0x500000);memset(ppc_ptr(0x20000000),0xA5,0x20000);}
static void invoke_heap(uint32_t sp){guest_sp=sp;Cpu c={};c.r[1]=guest_sp;c.r[3]=0x80000;c.lr=0xDEADC0DE;
 for(unsigned i=14;i<32;i++)c.r[i]=0xBEEF0000+i;
 module->hooks[0].fn(&c);
 assert(c.r[1]==guest_sp&&c.lr==0xDEADC0DE);
 for(unsigned i=14;i<32;i++)assert(c.r[i]==0xBEEF0000+i);
 for(unsigned i=0;i<64;i++){assert(ld8(guest_sp-0x1000+i)==0xA5);assert(ld8(guest_sp+0x100+i)==0xA5);}
}
static void invoke_stack_args(){
 Cpu c={};c.r[1]=guest_sp;c.lr=0xFEEDCAFE;
 for(unsigned i=0;i<8;i++)c.r[3+i]=0x100+i;
 const uint32_t byref=guest_sp+0x80;
 st32(guest_sp+8,byref);st32(guest_sp+12,0x222);st32(guest_sp+16,0x333);st32(guest_sp+20,0x444);
 st32(byref,0);st32(byref+4,0);
 assert(module->hooks[1].target==0x02000000);module->hooks[1].fn(&c);
 assert(c.r[1]==guest_sp&&c.lr==0xFEEDCAFE);
 assert((ld32(byref)&31)==0&&ld32(byref)>=guest_sp-0x1000&&ld32(byref)<guest_sp);
 assert(ld32(byref+4)==(0x100^0x101^0x102^0x103^0x104^0x105^0x106^0x107^0x222^0x333^0x444));
 assert(c.r[3]==((0x100+0x101+0x102+0x103+0x104+0x105+0x106+0x107)^(0x100+0x101+0x102+0x103+0x104+0x105+0x106+0x107+7)^0x222^0x333^0x444));
 for(unsigned i=0;i<64;i++){assert(ld8(guest_sp-0x1000+i)==0xA5);assert(ld8(guest_sp+0x100+i)==0xA5);}
}
'''
        tail='''static void dispatch(Cpu*c){
 for(uint32_t i=0;i<module->func_count;i++)if(module->funcs[i].addr==c->pc){module->funcs[i].fn(c);return;}
 uint32_t result=0xFEFEFEFE;switch(c->pc){'''+''.join(dispatch)+virtual+'''default:fprintf(stderr,"unexpected dispatch %08X\\n",c->pc);assert(false);}
 /* Legal caller-save clobbers expose bad translated register capture. */
 c->r[0]=0xCC000000;for(unsigned i=3;i<13;i++)c->r[i]=0xCC000000+i;c->r[3]=result;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            directory=Path(directory)
            source=directory/'module.c';source.write_text('''#include "wwhd_guest.h"
#include "permanent_resource_heap.h"
WWHD_REPLACE(0x02000000,u32,stack_arguments,(u32 a0,u32 a1,u32 a2,u32 a3,u32 a4,u32 a5,u32 a6,u32 a7,u32 pointer,u32 a9,u32 a10,u32 a11)){
 volatile u32 aligned[8] __attribute__((aligned(32)));
 for(u32 i=0;i<8;i++)aligned[i]=a0+a1+a2+a3+a4+a5+a6+a7+i;
 volatile u32* output=(volatile u32*)pointer;output[0]=(u32)aligned;
 output[1]=a0^a1^a2^a3^a4^a5^a6^a7^a9^a10^a11;
 return aligned[0]^aligned[7]^a9^a10^a11;
}
''')
            elf=directory/'module.elf'
            subprocess.run([compiler,*CORE_PPC_FLAGS,'-I',str(REPO/'runtime/guest/include'),'-I',str(HERE),'-c',str(source),'-o',str(elf)],check=True)
            translator=guestmod.Translator(guestmod.Elf(elf.read_bytes()),0x7F000000)
            self.assertEqual([(kind,target) for kind,target,_,_ in translator.hooks],[(1,0x0203EE2C),(1,0x02000000)])
            native_source=directory/'module-native.c';native_source.write_text(translator.emit('heap-abi'))
            library=directory/('heap'+builder.module_ext())
            subprocess.run(builder.default_cc()+builder.CFLAGS+['-I',str(REPO/'runtime/include'),str(native_source),'-o',str(library)],check=True)
            driver=directory/'driver.cpp';driver.write_text(prefix+state+tail+main)
            executable=directory/('driver.exe' if os.name=='nt' else 'driver')
            command=builder.default_cc()+['-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',str(REPO/'runtime/include'),str(driver),'-o',str(executable)]
            if sys.platform.startswith('linux'):command.append('-ldl')
            subprocess.run(command,check=True)
            subprocess.run([str(executable),str(library)],check=True)
            print(json.dumps({'translated_heap_abi': 'passed', 'cases': 128,
                              'heap_requests_per_case': 8,
                              'stack_argument_byref_cases': 128,
                              'entry_stack_mod_16': [0, 8],
                              'required_ppc_flag': '-mstackrealign',
                              'elf_sha256': hashlib.sha256(elf.read_bytes()).hexdigest(),
                              'module_sha256': hashlib.sha256(library.read_bytes()).hexdigest(),
                              'guest_abi_version': 2,
                              'stack_and_nonvolatile_register_guards': True,
                              'game_or_rpx_required': False}, sort_keys=True))
