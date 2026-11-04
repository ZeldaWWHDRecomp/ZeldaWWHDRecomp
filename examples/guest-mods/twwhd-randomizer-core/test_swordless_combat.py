"""Swordless damage eligibility, native-tail effects, and call-scope isolation."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

class SwordlessCombat(unittest.TestCase):
    def test_damage_tail(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;
typedef int32_t s32;typedef int16_t s16;typedef int8_t s8;typedef float f32;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) unsigned char memory[0x20000];
#define R_SW(type,address) (*(type*)(memory+(address)-0x10000))
static u32 arena,actor,info,hit,play,player;static unsigned init_swordless,attacks,particles,sounds,stops,powers,returns,sword_tail;
static u32 attack_source=0x7755;static u8 damage,dead,result;static s8 resulting_health;
static u32 native_hit;
u32 wwhd_dComIfG_getGameInfo(void){return play;}
u32 wwhd_attackCheck(u32 a,u32 i){assert(a==actor&&i==info);++attacks;R_SW(u8,i+8)=damage;R_SW(u8,i+9)=dead;R_SW(u8,i+10)=result;R_SW(s8,a+0x3A1)=resulting_health;return attack_source;}
u32 wwhd_dComIfGp_getReverb(u32 room){assert((s32)room==-1);return 9;}
s16 wwhd_fopAcM_searchActorAngleY(u32 a,u32 p){assert(a==actor&&p==player);return -123;}
#include "swordless_combat.h"
u32 randomizer_swordless_particle(u32 c,u32 g,u32 id,u32 p,u32 a,u32 s,u32 alpha,u32 cb,s32 room,u32 color,u32 primary,u32 environment){assert(c==0x456&&g==0&&alpha==255&&cb==0&&room==-1&&!color&&!primary&&!environment);if(id==0x10)assert(p==actor+0x37C&&!a&&!s);else assert(id==0xD&&p==actor+0xBF8&&a&&s);++particles;return 0;}
void randomizer_swordless_monster_sound(u32 id,u32 pos,u32 process,u32 arg,s32 reverb){assert(id==(resulting_health<=0?0x4943:0x4942)&&pos==actor+0x37C&&process==99&&!arg&&reverb==9);++sounds;}
void randomizer_swordless_stop_bgm(void){++stops;}
u32 randomizer_native_phantom_power(u32 i){assert(i==info);++powers;R_SW(u32,i)=native_hit;return 0x1234;}
void randomizer_native_phantom_damage(u32 a){assert(a==actor);u32 ret=randomizer_phantom_power(info);assert(ret==0x1234);if(R_SW(u32,info))++sword_tail;++returns;}
static void reset(void){memset(memory,0,sizeof memory);actor=arena;info=arena+0x10000;hit=arena+0x11000;play=arena+0x12000;player=arena+0x18000;R_SW(u32,play+0x5B2C)=player;R_SW(u32,play+0x5AB0)=0x456;R_SW(u32,actor+4)=99;R_SW(s8,actor+0x326)=-1;R_SW(u32,info+0x14)=actor+0xBF8;attacks=particles=sounds=stops=powers=returns=sword_tail=0;damage=4;dead=result=0;resulting_health=7;native_hit=hit;}
int main(void){
 arena=0x10000;
 for(unsigned option=0;option<2;option++)for(unsigned variant=0;variant<3;variant++)for(unsigned action=7;action<=10;action++)for(unsigned mask=0;mask<4;mask++)for(unsigned die=0;die<2;die++){
  reset();init_swordless=option;R_SW(u8,actor+0x3D8)=variant;R_SW(s16,actor+0x5BA)=action;R_SW(s16,actor+0x5BC)=51;R_SW(s16,actor+0x5E2)=6;R_SW(u8,actor+0x8C4)=6;R_SW(u32,hit+0x10)=(mask&1?2:0)|(mask&2?0x10000:0);dead=die;resulting_health=die?0:7;
  randomizer_phantom_damage(actor);assert(powers==1&&returns==1&&!randomizer_phantom_actor);
  if(!option||action==9){assert(!attacks&&!particles&&!sounds&&!stops&&sword_tail==1);continue;}
  assert(!sword_tail);if(!(mask&2)){assert(!attacks&&!particles&&!sounds&&!stops&&R_SW(s16,actor+0x5BC)==51);continue;}
  assert(attacks==1&&sounds==1&&particles==1+die);
  if(action==7||action==10){assert(R_SW(s16,actor+0x5BA)==8&&R_SW(s16,actor+0x5BC)==0&&!stops);}
  else if(variant==0){if(die)assert(R_SW(s16,actor+0x5BA)==21&&R_SW(s16,actor+0x5E2)==1000&&stops==1);else assert(R_SW(u8,actor+0x8C4)==2&&R_SW(s16,actor+0x5BC)==10&&!stops);}
  else if(variant==1){assert(R_SW(s8,actor+0x3A1)==0&&R_SW(s16,actor+0x5BA)==3&&stops==1);}
  else assert(R_SW(s16,actor+0x5BA)==action&&R_SW(s16,actor+0x5BC)==51&&!stops);
 }
 reset();init_swordless=1;R_SW(s16,actor+0x5BA)=8;R_SW(s16,actor+0x5E2)=6;native_hit=0;randomizer_phantom_damage(actor);assert(!attacks&&!sword_tail);
 reset();init_swordless=1;R_SW(u32,hit+0x10)=0x10000;R_SW(s16,actor+0x5BA)=8;R_SW(s16,actor+0x5E2)=6;R_SW(u8,actor+0x8C4)=2;result=1;R_SW(u8,player+0x69E8)=1;randomizer_phantom_damage(actor);assert(R_SW(s16,actor+0x5E2)==2&&R_SW(s16,actor+0x5BA)==2&&R_SW(u8,actor+0x8C4)==10);
 for(unsigned wrong=0;wrong<2;wrong++){reset();init_swordless=1;R_SW(s16,actor+0x5BA)=8;R_SW(s16,actor+0x5E2)=wrong?6:5;if(wrong)R_SW(u32,info+0x14)=actor+0xBF4;R_SW(u32,hit+0x10)=0x10000;randomizer_phantom_damage(actor);assert(!attacks&&sword_tail==1);}
 reset();randomizer_phantom_power(info);assert(R_SW(u32,info)==hit&&!attacks);return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/"test.c";exe=Path(tmp)/"test"
            src.write_text(source)
            subprocess.run([os.environ.get("CC","cc"),"-std=c11","-D_DARWIN_C_SOURCE","-Wno-int-to-pointer-cast","-Wno-pointer-to-int-cast","-I",str(Path(__file__).parent),str(src),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__ == "__main__":
    unittest.main()
