"""Daily mail eligibility and unchanged daily register/flag work."""
from pathlib import Path
import subprocess,tempfile,unittest
class DailyMail(unittest.TestCase):
 def test_daily_work(self):
  source=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef uint16_t u16;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u8 saves[2][0x1400],regs[65536],flags[65536];static u32 saveptr;static unsigned active,mutation;
static unsigned boss_first,boss_second,boss_calls,tingle,day,reserve,bag,stocks[2],stockcount,deliveries[5],deliverycount,temp_count,friday_count;
static void* data(u32 a){assert(a==1);return&saveptr;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static void changed(void){if(mutation){active^=1;saveptr=(u32)saves[active];}}
static void event_pointer(u32 e){assert(e==saveptr+0x644);}
static u8 wwhd_dSv_event_c_getEventReg(u32 e,u32 r){event_pointer(e);u8 v=regs[r];changed();return v;}
static void wwhd_dSv_event_c_setEventReg(u32 e,u32 r,u32 v){event_pointer(e);assert(v<256);regs[r]=v;changed();}
static u32 wwhd_dSv_event_c_isEventBit(u32 e,u32 f){event_pointer(e);assert(f!=0x1820);u32 v=f==0xB80?tingle:flags[f];changed();return v;}
static void wwhd_dSv_event_c_onEventBit(u32 e,u32 f){event_pointer(e);flags[f]=1;changed();}
static void wwhd_dSv_event_c_offEventBit(u32 e,u32 f){
 if(e==saveptr+0x1178)temp_count++;else{event_pointer(e);if(f>=0x2000&&f!=0x2680)friday_count++;}flags[f]=0;changed();
}
static u8 wwhd_dSv_player_bag_item_c_checkReserveItem(u32 b,u32 item){assert(b==saveptr+0x96&&item==0x9B);changed();return bag;}
static int wwhd_dKy_get_dayofweek(void){changed();return day;}
#include "daily_mail.h"
u32 randomizer_daily_boss(u32 stage){assert(stage==2);unsigned v=boss_calls++?boss_second:boss_first;changed();return v;}
void randomizer_daily_stock(u32 letter){assert(stockcount<2);stocks[stockcount++]=letter;changed();}
void randomizer_daily_delivery(u32 letter){assert(deliverycount<5);deliveries[deliverycount++]=letter;changed();}
u32 randomizer_daily_reserve_owned(u32 b,u32 slot){assert(b==saveptr+0xB0&&slot==15);changed();return reserve;}
int main(void){const unsigned letters[]={0xAC03,0xAE03,0xB003,0xAF03,0x9D03};
 for(unsigned mask=0;mask<256;mask++)for(unsigned v=0;v<256;v++){
  boss_first=mask&1;boss_second=(mask>>1)&1;tingle=(mask>>2)&1;day=(mask&8)?5:4;reserve=(mask>>4)&1;bag=(mask>>5)&1;mutation=(mask>>6)&1;
  memset(regs,0,sizeof regs);memset(flags,0,sizeof flags);active=0;saveptr=(u32)saves[0];boss_calls=stockcount=deliverycount=temp_count=friday_count=0;
  regs[0xCF03]=v;regs[0xAB03]=v;regs[0xCCFF]=v;regs[0xC103]=v;regs[0xBB07]=v;regs[0xC407]=v;regs[0xB907]=v;regs[0xA60F]=v;
  flags[0x1F10]=flags[0x2A20]=flags[0x2F01]=(mask>>7)&1;
  randomizer_daily_mail();assert(boss_calls==2);unsigned n=0;if(boss_first)assert(stocks[n++]==0x8B03);if(boss_second&&tingle)assert(stocks[n++]==0xB203);assert(stockcount==n);
  assert(deliverycount==5&&!memcmp(deliveries,letters,sizeof letters));assert(temp_count==15&&friday_count==(day==5?16:0));
  assert(regs[0xCF03]==(v>=3?3:v+1)&&regs[0xAB03]==(v>=3?3:v+1));
  assert(regs[0xCCFF]==(reserve&&!bag?(v>=2?2:v+1):v));assert(regs[0xC103]==(v==1?2:v));
  assert(regs[0xC407]==(v==6?7:v));assert(regs[0xB907]==((v&1)?(u8)(v+1):v));
  assert(regs[0xBB07]==((mask&128)&&v!=7?(v>=7?7:v+1):v));assert(flags[0x1F08]==((mask&128)&&v==7));
  assert(regs[0xA60F]==((mask&128)?(v>=3?3:v+1):v));assert(flags[0x3080]==!!(mask&128));
 }return 0;}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(source)
   subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
