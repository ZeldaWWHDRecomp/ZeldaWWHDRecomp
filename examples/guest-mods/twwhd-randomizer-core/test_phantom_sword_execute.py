"""Full weapon update: maze sword policy, native callbacks and fire lifecycle."""
from pathlib import Path
import subprocess, tempfile, unittest

class PhantomSwordExecute(unittest.TestCase):
 def test_full_consumer(self):
  code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef int8_t s8;typedef int16_t s16;typedef int32_t s32;typedef float f32;
static unsigned char arena[0x20000];static const u32 actor=0x30000000,game=0x30004000,save=0x30010000,hitobj=0x30012000,stageinfo=0x30013000;
static f32 zero,one=1,dim=.65217394f,power=150;static u32 saveptr;
static void* mem(u32 a){if(a>=actor&&a<actor+sizeof(arena))return arena+a-actor;return(void*)a;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_BOKO(t,a) W(t,a)
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static void* data(u32 a){if(a==1)return &saveptr;if(a==0x1000A4B8)return &zero;if(a==0x1000A48C)return &one;if(a==0x1000A4F0)return &dim;if(a==0x1000A4F4)return &power;assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static void proc(u32 fn,u32 self);static u32 stage_call(u32 fn,u32 self);
#define RANDOMIZER_BOKO_PROC(fn,self) proc(fn,self)
#define RANDOMIZER_BOKO_STAGE_CALL(fn,self) stage_call(fn,self)
#include "phantom_sword_execute.h"
static unsigned hits,has_hit,flags,water,kind,queries,anims,deletes,flames,particles,ended,resets,models,tops,centers,collisions,masses,chains,procs,stages,gamecalls,sounds,mutate,proc_mode;static s32 event_value;static f32 water_height;static unsigned mass_value;
static void proc(u32 fn,u32 self){assert(fn==0x12345678&&self==actor+(proc_mode==1?4:-4));procs++;W(u32,actor+0x314)=0x7F812345;}
static u32 stage_call(u32 fn,u32 self){assert(fn==0x87654321&&self==game+0x5150);stages++;return stageinfo;}
s32 randomizer_boko_hit(u32 s){assert(s==actor+0x700);return hits;}
u32 randomizer_boko_hit_object(u32 s){assert(s==actor+0x700);return has_hit?hitobj:0;}
void randomizer_boko_model(u32 m){assert(m==0x99887766);models++;}
void randomizer_boko_top(u32 a,s32 m){assert(a==actor&&!m);tops++;W(u32,a+0x44C)=0x7FC12345;W(f32,a+0x450)=10;W(f32,a+0x454)=-3;}
s32 randomizer_boko_reverb(s32 room){assert(room==-2);return -7;}
void randomizer_boko_sound(u32 id,u32 pos,u32 f,s32 r){assert((id==0x6902||id==0x6904)&&pos==actor+0x44C&&!f&&r==-7);sounds++;}
s32 randomizer_boko_water(u32 pos,u32 height){assert(pos==actor+0x44C);*(f32*)height=water_height;if(mutate==1)W(s16,actor+0x43C)=0;return water;}
u32 randomizer_boko_game(void){gamecalls++;return game;}
void randomizer_boko_particle(u32 p,u32 g,u32 id,u32 pos,u32 angle,u32 scale,u32 alpha,u32 cb,s32 room,u32 c,u32 e,u32 x){assert(p==0xABCDEF&&!g&&id==0x35A&&pos==actor+0x44C&&!angle&&!scale&&alpha==255&&!cb&&room==-1&&!c&&!e&&!x);particles++;}
void randomizer_boko_flame(u32 a){assert(a==actor);flames++;if(mutate==2){W(s16,a+0x43C)=0;W(u32,a+0xB0)=0;}}
void randomizer_boko_end_flame(u32 cb){assert(cb==actor+0x4AC);ended++;}
void randomizer_boko_reset_hit(u32 s){assert(s==actor+0x700);resets++;}
void randomizer_boko_center(u32 s,u32 p){assert(s==actor+0x818&&p==actor+0x44C);centers++;}
void randomizer_boko_collision(u32 cc,u32 s){assert(cc==game+0x26A4&&s==actor+0x700);collisions++;if(mutate==3)W(u8,actor+0x430)=3;}
void randomizer_boko_mass(u32 mgr,u32 s,u32 mass){assert(mgr==game+0x4EF8&&s==actor+0x700);masses++;mass_value=mass;}
void randomizer_boko_chain(u32 a){assert(a==actor);chains++;if(mutate==4)W(u32,a+0xB0)=5;}
s32 randomizer_boko_event(u32 e,u32 f){assert(e==saveptr+0x644&&f==0x3A08);queries++;assert(models==1&&tops==1);if(mutate==5)strcpy(mem(game+0x5134),"GanonJ");return event_value;}
void randomizer_boko_animation(u32 a){assert(a==actor+0x3B8);anims++;if(mutate==6)W(u8,actor+0x3C7)=1;}
void randomizer_boko_delete(u32 a){assert(a==actor);deletes++;}
static void reset(void){memset(arena,0,sizeof arena);saveptr=save;memset(mem(actor+0x388),0xA7,8);W(u32,actor+0x3B4)=0x99887766;W(s8,actor+0x326)=-2;W(u32,game+0x5AB0)=0xABCDEF;W(u32,game+0x5150)=game+0x6000;W(u32,game+0x6000+0x15C)=0x87654321;W(u32,actor+0x314)=0x7F812345;W(u32,actor+0x318)=0x80000000;W(u32,actor+0x31C)=0x7FC98765;W(f32,actor+0x3B8)=1;W(f32,actor+0x964)=2;W(u32,actor+0x4B0)=1;strcpy(mem(game+0x5134),"sea");hits=has_hit=flags=water=kind=queries=anims=deletes=flames=particles=ended=resets=models=tops=centers=collisions=masses=chains=procs=stages=gamecalls=sounds=mutate=proc_mode=0;event_value=0;water_height=0;}
int main(void){unsigned cases=0;
for(unsigned delay=1;delay<65536;delay++){reset();W(s16,actor+0x446)=(s16)delay;assert(randomizer_phantom_sword_execute(actor)==1&&W(s16,actor+0x446)==(s16)(delay-1)&&!models&&!gamecalls);cases++;}
const char* names[]={"GanonJ","Ganon","GanonJx","sea","","GanonK"};
for(unsigned n=0;n<6;n++)for(unsigned ev=0;ev<4;ev++)for(unsigned state=0;state<256;state++)for(unsigned rate=0;rate<3;rate++){
reset();W(u32,actor+0xB0)=5;strcpy(mem(game+0x5134),names[n]);event_value=ev==0?0:ev==1?1:ev==2?-1:0x100;W(u8,actor+0x3C7)=state;W(f32,actor+0x3B8)=rate==0?0:rate==1?1:NAN;assert(randomizer_phantom_sword_execute(actor)==1);unsigned disappear=ev&&n!=0;assert(queries==1&&anims==disappear&&deletes==(disappear&&((state&1)||rate==0)));assert(gamecalls==(ev?1u:0u));assert(W(u32,actor+0x37C)==0x7F812345&&W(u32,actor+0x394)==0x80000000&&W(u32,actor+0x398)==0x7FC98765);assert(W(u32,actor+0x388)==0xA7A7A7A7&&W(u32,actor+0x38C)==0xA7A7A7A7);cases++;}
for(unsigned timer=0;timer<5;timer++)for(unsigned mode=0;mode<4;mode++)for(unsigned contact=0;contact<4;contact++)for(unsigned wet=0;wet<4;wet++)for(unsigned k=0;k<8;k++){
reset();const s16 ts[]={0,1,2,-1,32767};W(s16,actor+0x43C)=ts[timer];W(u8,actor+0x430)=mode;hits=has_hit=contact!=0;flags=contact==1?0x100:contact==2?0x200:0x20000;W(u32,hitobj+0x10)=flags;water=wet!=0;water_height=wet==1?5:wet==2?10:NAN;W(u32,stageinfo+12)=k<<16;assert(randomizer_phantom_sword_execute(actor)==1);assert(models==1&&tops==1&&centers==1&&collisions==1&&masses==1&&mass_value==(mode==3?1u:4u));if(ts[timer]==0&&contact<2){assert(resets==1&&!stages);}else{assert(W(u32,actor+0x47C)==0x7FC12345&&W(u32,actor+0x484)==(uint32_t)0xC0400000);assert(stages==2);assert(W(f32,actor+0x490)==(150*(k==1||k==4?dim:1))*W(f32,actor+0x964));}cases++;}
for(proc_mode=1;proc_mode<3;proc_mode++){unsigned pm=proc_mode;reset();proc_mode=pm;W(s16,actor+0xA2E)=pm==1?-1:2;W(s16,actor+0xA2C)=pm==1?4:-4;W(u32,actor+0xA30)=0x12345678;if(pm==2){W(s16,actor+0xA32)=0x100;W(u32,actor-4+0x100)=game+0x7000;W(u32,game+0x7000+20)=0x12345678;}randomizer_phantom_sword_execute(actor);assert(procs==1);cases++;}
for(unsigned m=1;m<=6;m++){reset();mutate=m;W(u32,actor+0xB0)=m==5||m==6?5:0;W(s16,actor+0x43C)=m==1||m==2?2:0;W(u32,actor+0xA28)=m==4;event_value=1;randomizer_phantom_sword_execute(actor);if(m==1)assert(W(s16,actor+0x43C)==0&&ended==1);if(m==2)assert(flames==1&&W(s16,actor+0x43C)==0&&!ended);if(m==3)assert(mass_value==1);if(m==4)assert(chains==1&&queries==1);if(m==5)assert(queries==1&&!anims);if(m==6)assert(anims==1&&deletes==1);cases++;}
printf("Phantom sword Execute: %u cases passed\n",cases);}
'''
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'test.c').write_text(code)
   subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(root/'test.c'),'-o',str(root/'test')],check=True)
   subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
