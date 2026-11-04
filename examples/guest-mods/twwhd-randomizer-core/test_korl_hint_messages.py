import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class KorlHintMessages(unittest.TestCase):
    def test_range_progression_and_native_delegation(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(uintptr_t) unsigned char actor[0x700];
static u32 init_last_korl_hint_message_number,current,last;static unsigned calls;static s32 result;
#include "korl_hint_messages.h"
s32 randomizer_native_ship_next_message(u32 a,u32 message){assert(a==(u32)actor&&message==0xDEADBEEFu);calls++;*(uint32_t*)(actor+0x648)=0x12345678;return result;}
static void check(u32 value){current=value;memset(actor,0xA5,sizeof(actor));*(uint32_t*)(actor+0x648)=(uint32_t)current;calls=0;const s32 got=randomizer_ship_next_hint_message((u32)actor,0xDEADBEEFu);if((int32_t)current>=0xD73&&(int32_t)current<=(int32_t)last){assert(!calls&&got==(current==last));assert(*(uint32_t*)(actor+0x648)==(current==last?current:current+1));}else assert(calls==1&&got==result&&*(uint32_t*)(actor+0x648)==0x12345678);for(unsigned i=0;i<sizeof(actor);i++)if(i<0x648||i>=0x64C)assert(actor[i]==0xA5);}
int main(void){const u32 ends[]={0xD73,0xD74,0xD80,0x7FFFFFFF};for(unsigned n=0;n<4;n++){last=init_last_korl_hint_message_number=ends[n];for(result=-1;result<=1;result++)for(u32 value=0;value<65536;value++)check(value);check(0x7FFFFFFE);check(0x7FFFFFFF);check(0x80000000u);check(0xFFFFFFFFu);}}
'''
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'korl_hint_messages.h').write_text(Path(__file__).with_name('korl_hint_messages.h').read_text().replace('volatile u32*','volatile uint32_t*'))
            (root/'test.c').write_text(source)
            subprocess.run([os.environ.get("CC", "cc"),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'test.c'),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
