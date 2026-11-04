"""Complete play-delete callback order, live reloads, resource loops and BGM seam."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest


class StageUnloadBgm(unittest.TestCase):
    def test_complete_delete(self):
        header = Path(__file__).with_name('stage_unload_bgm.h')
        declarations = re.findall(r'WWHD_GAME_FUNC\(0x[0-9A-F]+,(\w+),(unload_\w+),\(([^)]*)\)\);', header.read_text())
        # Each native endpoint records its exact arguments. No destructor is
        # silently omitted by a generic stub; the expected full order is below.
        stubs = []
        for index, (result, name, args) in enumerate(declarations):
            params = [] if args == 'void' else [part.strip().split()[-1] for part in args.split(',')]
            body = f'record({index+1},' + (params[0] if params else '0') + ',' + (params[1] if len(params)>1 else '0') + ');'
            if name == 'unload_release_resources':
                body += 'R_UNLOAD(u32,UNLOAD_DATA(0x1018F47C))=sys2;'
            if name == 'unload_stop_bgm':
                body += 'current_info=info2;'
            if name == 'unload_finalize_game':
                body += 'R_UNLOAD(u8,UNLOAD_DATA(0x101F8350))=changed_keep;'
            if name == 'unload_resource':
                body += 'if(resource_mutation)R_UNLOAD(u32,names+4)=0x5678;'
            if result != 'void':
                body += 'return ' + ('heap;' if name == 'unload_heap' else '(u32)x+0x10;')
            stubs.append(f'{result} {name}({args})'+'{'+body+'}')
        ids = {name: i+1 for i, (_, name, _) in enumerate(declarations)}
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef int8_t s8;typedef int32_t s32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) unsigned char memory[0xC0000];
#define R_UNLOAD_u32(a) (*(uint32_t*)(memory+(a)))
#define R_UNLOAD_s32(a) (*(int32_t*)(memory+(a)))
#define R_UNLOAD_u8(a) (*(uint8_t*)(memory+(a)))
#define R_UNLOAD(t,a) R_UNLOAD_##t(a)
#define UNLOAD_DATA(a) (0x80000u+((a)&0x1FFFFu))
static const u32 self=0x10000,info=0x20000,info2=0x30000,sys=0x40000,sys2=0x40100,heap=0x41000,renderer=0x50000,views=0x52000,view=0x60000,names=0x70000;
static u32 current_info;static unsigned changed_keep,resource_mutation,assure_count;
static unsigned ids[128],n;static unsigned assure_mode;static u32 *literal_stack;static u32 arg1[128],arg2[128];
static void record(unsigned id,u32 a,u32 b){assert(n<128);ids[n]=id;arg1[n]=a;arg2[n++]=b;}
static u32 wwhd_dComIfG_getGameInfo(void){record(100,current_info,0);return current_info;}
static void assure(u32 *s){assert(s[1]==UNLOAD_DATA(0x10052EEC));assure_count++;
 if(assure_mode&&assure_count==1)literal_stack=s;
 if(assure_count==3&&assure_mode==1)literal_stack[0]=0x71000;
 if(assure_count==3&&assure_mode==2){literal_stack[0]=0x71000;s[0]=0x72000;}
}
#define UNLOAD_ASSURE(a) assure(a)
#define UNLOAD_BACKGROUND_DESTROY(i) record(101,(i)+0x12A0,0)
#include "stage_unload_bgm.h"
STUBS
static void reset(void){
 memset(memory,0xA5,sizeof memory);n=assure_count=assure_mode=0;current_info=info;changed_keep=resource_mutation=0;
 R_UNLOAD(u32,UNLOAD_DATA(0x1018F47C))=0;
 R_UNLOAD(u32,sys+0x18)=0;R_UNLOAD(u32,sys2+0x1C)=heap;
 R_UNLOAD(u32,UNLOAD_DATA(0x101F95D0))=renderer;
 R_UNLOAD(u32,renderer+0x1020)=1;R_UNLOAD(u32,renderer+0x1024)=views;
 R_UNLOAD(u32,views)=view;R_UNLOAD(u32,views+4)=view+0x7000;
 R_UNLOAD(u8,UNLOAD_DATA(0x101EACB4))=255;
 R_UNLOAD(u32,self+0xB64)=4;
 R_UNLOAD(u32,info+0x5AB0)=0x1234;R_UNLOAD(u32,info+0x5C24)=0x2345;
 R_UNLOAD(u8,UNLOAD_DATA(0x1047B560))=255;
 // All literal/stage strings mismatch, hence four three-callback comparisons.
 memory[info+0x5134]='z';
}
static unsigned find(unsigned id){for(unsigned i=0;i<n;i++)if(ids[i]==id)return i;assert(0);return 0;}
int main(void){unsigned cases=0;
 for(unsigned display=0;display<2;display++)for(unsigned state=0;state<6;state++)for(unsigned mode=0;mode<5;mode++)for(unsigned keep=0;keep<2;keep++)for(unsigned count=0;count<4;count++)for(unsigned windows=0;windows<3;windows++){
  reset();R_UNLOAD(u32,heap+0x80)=state;R_UNLOAD(u32,self+0xB64)=mode;changed_keep=keep;resource_mutation=1;
  R_UNLOAD(u32,renderer+0x1020)=windows;
  if(display)R_UNLOAD(u32,UNLOAD_DATA(0x1018F47C))=sys;
  if(count){R_UNLOAD(u8,UNLOAD_DATA(0x101EACB4))=2;const u32 entry=UNLOAD_DATA(0x100530D8)+24;R_UNLOAD(u32,entry+4)=names;R_UNLOAD(u8,entry+8)=count;for(unsigned i=0;i<count;i++)R_UNLOAD(u32,names+4*i)=0x1000+i;}
  assert(randomizer_stage_delete(self)==1);assert(assure_count==12);
  unsigned expected[128],k=0;
  #define E(name) expected[k++]=ID_##name
  #define G() expected[k++]=100
  if(display){E(release_resources);if(state==3||state==4){E(heap);E(release_heap);}}
  G();E(attention);G();E(vibration);G();expected[k++]=101;G();E(collision);E(snapshot);E(stage);G();E(event);G();E(particle);E(demo);E(environment);G();E(message_heap);
  G();G();G();G();G();E(magma);G();E(grass);G();E(tree);G();E(wood);G();E(flower);G();E(hio);E(ocean);G();
  for(unsigned i=0;i<count;i++)E(resource);
  E(stop_bgm);G();E(initialize);
  if(mode==0){E(finalize_title);E(destroy_title);}else if(mode==1){E(finalize_game);if(!keep)E(destroy_game);}else if(mode==2){E(finalize_ending);E(destroy_ending);E(ending_resources);}else if(mode==3){E(finalize_sea);E(destroy_sea);}
  E(wind);E(scene);E(base);assert(n==k&&!memcmp(ids,expected,k*sizeof *ids));
  assert(arg1[find(ID_initialize)]==info2+0x12A0);
  assert(arg1[find(ID_attention)]==info+0x5804&&arg2[find(ID_attention)]==2);
  assert(arg1[find(ID_hio)]==(u32)(s32)-1);
  assert(R_UNLOAD(u32,info+0x5B4C)==0&&R_UNLOAD(u8,info+0x5BB0)==0&&R_UNLOAD(u8,info+0x5AC9)==0);
  assert(R_UNLOAD(u8,info2+0x5AC9)==0xA5);
  const u32 active=view+(windows>1?0x7000:0);assert(R_UNLOAD(u32,active+0x6BCC)==0&&R_UNLOAD(u32,active+0x6BD0)==0);
  if(count>1){unsigned at=find(ID_resource);assert(arg1[at]==UNLOAD_DATA(0x1047B448)&&arg2[at]==0x1000);assert(arg1[at+1]==UNLOAD_DATA(0x1047B448)+8&&arg2[at+1]==0x5678);}
  cases++;
 }
 reset();R_UNLOAD(u32,UNLOAD_DATA(0x1018F47C))=sys;R_UNLOAD(u32,sys+0x18)=1;
 unsigned char before[sizeof memory];memcpy(before,memory,sizeof memory);
 assert(randomizer_stage_delete(self)==0&&!n&&!memcmp(before,memory,sizeof memory));
 // Actual captured-pointer fastpath precedes the mutated literal fallback.
 reset();assure_mode=1;assert(unload_stage_is(info+0x5134)==1);
 assert(assure_count==3&&n==1);
 reset();assure_mode=2;memory[0x71000]='q';memory[0x71001]=0;memory[0x72000]='q';memory[0x72001]=0;
 assert(unload_stage_is(UNLOAD_DATA(0x10053228))==1&&assure_count==3);
 reset();memory[UNLOAD_DATA(0x10053228)]='z';memory[UNLOAD_DATA(0x10053228)+1]=0;memory[info+0x5134+1]=0;
 assert(randomizer_stage_delete(self)==1&&assure_count==3);
 assert(find(ID_recollection)<find(ID_magma));
 printf("stage unload: %u complete callback-order cases passed\n",cases);return 0;
}
'''.replace('STUBS', '\n'.join(stubs))
        source = '\n'.join(f'#define ID_{name.removeprefix("unload_")} {value}' for name,value in ids.items())+'\n'+source
        with tempfile.TemporaryDirectory() as directory:
            c, exe = Path(directory)/'test.c', Path(directory)/'test'
            c.write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(header.parent),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
