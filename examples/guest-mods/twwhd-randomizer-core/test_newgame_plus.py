"""New-game+ preserves carry-over settings without marking pictographs done."""
from pathlib import Path
import subprocess,tempfile,unittest
class NewGamePlus(unittest.TestCase):
 def test_carry_over(self):
  code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef uint16_t u16;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u8 save[0x1400],replacement[0x1400],progress[4],picture[0x3C0310],play[0x6000];
static u16 held[17],bits[5];static u32 saveptr;static unsigned gets,sets,ons,pcs,prs,sw;
static unsigned redirect;static unsigned values[17],last89,picturecount;
static void* mem(u32 a){if(a==1)return&saveptr;if(a==0x101EADA8)return held;assert(a==0x101EADCC);return bits;}
#define WWHD_GAME_DATA(a,t) (*(t*)mem(a))
static u8 wwhd_dSv_event_c_getEventReg(u32 ev,u32 reg){assert(ev==(u32)save+0x644);if(gets<17){assert(reg==held[gets]);return (u8)values[gets++];}assert(gets++==17&&reg==0x89FF);return 213;}
static void wwhd_dSv_event_c_setEventReg(u32 ev,u32 reg,u32 value){assert(ev==saveptr+0x644);if(sets<17){assert(reg==held[sets]&&value==values[sets]);sets++;}else{assert(sets++==17&&reg==0x89FF);last89=value;}}
static void wwhd_dSv_event_c_onEventBit(u32 ev,u32 flag){assert(ev==saveptr+0x644);assert(ons<5&&flag==bits[ons++]);}
static u32 wwhd_dComIfG_getGameInfo(void){return(u32)play;}
#include "newgame_plus.h"
u32 randomizer_ng_progress(u32 hd){assert(hd==saveptr+0x12C0);prs++;return(u32)progress;}
u32 randomizer_ng_picture(u32 hd){assert(hd==saveptr+0x12C0);pcs++;return(u32)picture;}
void randomizer_ng_picture_count(u32 p,u32 count){assert(p==(u32)picture);picturecount=count;}
void randomizer_ng_switch(u32 m,u32 bit){assert(m==saveptr+0x3A0);assert(bit==(sw++?0x5E:0x47));}
void randomizer_ng_init(u32 info){assert(info==(u32)save);assert(gets==18&&prs==2&&pcs==1);memset(save,0xA5,sizeof save);memset(replacement,0xA5,sizeof replacement);if(redirect)saveptr=(u32)replacement;memset(progress,0xA5,sizeof progress);picture[0x3C030C]=0;}
int main(void){
 for(redirect=0;redirect<2;redirect++)for(unsigned first=0;first<256;first++)for(unsigned second=0;second<4;second++){
  memset(save,0x33,sizeof save);memset(play,0,sizeof play);progress[0]=first;progress[3]=second;picture[0x3C030C]=first;
  saveptr=(u32)save;save[0x1C0]=first;save[0x1C5]=second;save[0x1C7]=first^0xFF;
  gets=sets=ons=pcs=prs=sw=0;last89=picturecount=0;
  for(unsigned i=0;i<17;i++){held[i]=0x4000+i;values[i]=(first+i)&255;}for(unsigned i=0;i<5;i++)bits[i]=0x5000+i;
  randomizer_ng_reinit((u32)save);
  assert(gets==18&&sets==18&&ons==5&&pcs==2&&prs==4&&sw==2);
  u8* active=redirect?replacement:save;
  assert(active[0x1C0]==first&&active[0x1C5]==second&&active[0x1C7]==(first^255)&&active[0x1C1]==3);
  if(redirect)assert(save[0x1C0]==0xA5&&save[0x1C5]==0xA5&&save[0x1C7]==0xA5&&save[0x1C1]==0xA5);
  assert(progress[0]==(first<2?first:0xA5)&&progress[3]==(second<2?second:0xA5));
  assert(picturecount==first&&last89==213);assert(save[0x64]==0xA5&&save[0x71]==0xA5&&save[0x52C]==0xA5);
  assert(play[0x5BC8]==0x26&&play[0x5BC7]==8);
 }
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(code)
   subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
