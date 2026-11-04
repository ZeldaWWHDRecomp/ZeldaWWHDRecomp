from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class DamageMultiplier(unittest.TestCase):
    def test_modes_health_flags_and_shield_path(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef int16_t s16;typedef int32_t s32;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static unsigned char save[0x1300],actor[0x6A00],play[0x6000],before[0x1300];
static u32 save_pointer=(u32)save;
#define WWHD_GAME_DATA(a,t) (*(t*)&save_pointer)
static f32 init_damage_multiplier,last_amount;static unsigned hero,rupees,order;
u32 wwhd_dComIfG_getGameInfo(void){assert(order==2);order=3;return (u32)play;}
#include "damage_multiplier.h"
u32 randomizer_damage_mode_manager(u32 info){assert(info==(u32)save+0x12C0&&order==0);order=1;return 77;}
s32 randomizer_damage_hero_mode(u32 mode){assert(mode==77&&order==1);order=2;return hero;}
void randomizer_damage_rupees(u32 self,f32 amount){assert(self==(u32)actor&&order==2);order=4;rupees++;last_amount=amount;}
int main(void){
 const float scales[]={0,0.5f,1,2,3.25f,80};
 const float amounts[]={-16,-1,-0.25f,-0.0f,0,0.25f,1,16};
 for(unsigned m=0;m<6;m++)for(hero=0;hero<2;hero++)for(unsigned a=0;a<8;a++)
 for(unsigned inv=0;inv<2;inv++)for(unsigned timer=0;timer<2;timer++)for(unsigned sword=0;sword<256;sword++){
  memset(save,0xA5,sizeof(save));memcpy(before,save,sizeof(save));
  memset(actor,0,sizeof(actor));memset(play,0,sizeof(play));save[0x2E]=sword;before[0x2E]=sword;
  *(u32*)(actor+0x3BC)=0x208000|inv;*(s16*)(actor+0x699E)=timer;
  *(float*)(play+0x5B44)=7;init_damage_multiplier=scales[m];order=rupees=0;
  const float delta=hero?amounts[a]*scales[m]:amounts[a];
  const s32 result=randomizer_damage_point((u32)actor,amounts[a]);
  if(inv||timer){assert(!result&&rupees==1&&last_amount==delta&&order==4);assert(*(float*)(play+0x5B44)==7);}
  else{assert(result&&rupees==0&&order==3);assert(*(float*)(play+0x5B44)==7+delta);}
  uint32_t flags=0x208000|inv;
  if(!inv&&!timer&&delta<0){flags&=~0x8000u;if(sword!=0x3E)flags&=~0x200000u;}
  assert(*(u32*)(actor+0x3BC)==flags);assert(!memcmp(save,before,sizeof(save)));
 }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'test.c').write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(Path(__file__).resolve().parent), str(root / 'test.c'),
                            '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
