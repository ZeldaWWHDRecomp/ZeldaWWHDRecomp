"""Saved shortcut byte, native initializer callbacks and save-pointer reload."""
import os
from pathlib import Path
import subprocess,tempfile,unittest
class DrcShortcut(unittest.TestCase):
 def test_initializer(self):
  code=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef int32_t s32;
static unsigned char saves[3][0x800];static u32 saveptr;static const uint32_t descriptor[2]={0x0000FFFF,0x021E38B8};
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) ((a)==1?saveptr:(a)==0x10015B28?descriptor[0]:descriptor[1])
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#include "drc_shortcut.h"
static s32 event_result;static unsigned calls,actions,mutate;
s32 randomizer_shortcut_event(u32 self,u32 flag){assert(self==saveptr+0x644&&flag==(calls?0x1580u:0x2E04u));calls++;if(mutate)saveptr=(u32)saves[1];return event_result;}
void randomizer_shortcut_action(u32 actor,u32 p,u32 parameter){assert(actor==123&&!parameter&&calls==2);const u32* d=(const u32*)p;assert(d[0]==descriptor[0]&&d[1]==descriptor[1]);actions++;if(mutate)saveptr=(u32)saves[2];}
int main(void){unsigned cases=0;for(unsigned byte=0;byte<256;byte++)for(unsigned current=0;current<256;current++)for(unsigned ev=0;ev<4;ev++)for(mutate=0;mutate<2;mutate++){
 memset(saves,0,sizeof saves);for(unsigned i=0;i<3;i++){saves[i][0x53C]=byte;saves[i][0x7A8]=current;}saveptr=(u32)saves[0];calls=actions=0;event_result=ev==0?0:ev==1?1:ev==2?-1:0x100;assert(randomizer_drc_shortcut(123)==(ev!=0));assert(calls==(ev?2u:1u)&&actions==(ev!=0));unsigned selected=mutate?(ev?2:1):0;for(unsigned i=0;i<3;i++){assert(saves[i][0x7A8]==(u8)(current|((i==selected&&(byte&0x80))?0x40:0)));assert(saves[i][0x53C]==byte);}cases++;}
printf("DRC shortcut: %u cases passed\n",cases);}
'''
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'test.c').write_text(code)
   subprocess.run([os.environ.get("CC", "cc"),'-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(root/'test.c'),'-o',str(root/'test')],check=True)
   subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
