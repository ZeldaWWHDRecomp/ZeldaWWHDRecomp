"""Complete Moblin execution: lifecycle branches and native callback mutations."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

class MoblinExecute(unittest.TestCase):
    def test_execution(self):
        root=Path(__file__).parent
        declarations=[]
        for name in ('moblin_create.h','moblin_execute_effects.h','moblin_execute_damage.h','moblin_execute.h'):
            declarations.extend(re.findall(r'WWHD_GAME_FUNC\([^\n]+', (root/name).read_text()))
        prefix=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint16_t u16;typedef int8_t s8;typedef uint8_t u8;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
'''
        weak=[]
        seen=set()
        for d in declarations:
            m=re.fullmatch(r'WWHD_GAME_FUNC\([^,]+,([^,]+),([^,]+),\((.*)\)\);',d)
            self.assertIsNotNone(m,d)
            result,name,args=m.groups()
            if name in seen:continue
            seen.add(name)
            weak.append(f'__attribute__((weak)) {result} {name}({args}){{assert(0&&"unexpected {name}");'+('return 0;' if result!='void' else '')+'}')
        code=prefix+'\n'.join(declarations)+r'''
static unsigned char arena[0x40000],data[0x500000];
static const u32 actor=0x30000000,morf=0x30008000,model=0x30009000,game=0x30010000,player=0x30020000,save=0x30030000,object=0x30031000,emitter=0x30032000,weapon=0x30033000;
static void* mem(u32 a){if(a>=actor&&a<actor+sizeof arena)return arena+a-actor;if(a>=0x10000000&&a<0x10500000)return data+a-0x10000000;if(a>0xFFFFFFFFu)return(void*)a;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_MO2(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) W(t,a)
static u32 randomizer_daily_save(void){return save;}
#include "moblin_execute.h"
static unsigned ice_on,trigger,hold,spawn_on,reaction,hit,attack_type,dead,cut,health,mutate;
static unsigned plays,calcs,moves,collisions,damagecalls,texturecalls,particlecalls,voices,deletes,firecalls,stages,orders,stops;
static unsigned groundcalls,angles,ground_valid,creates,judges,camsets,shockcalls,smokes,smokeends,precise;static f32 atan_x[2];static unsigned atan_calls,random_precise,random_calls;static f64 random_values[4];
static unsigned switchons,actorons,sequence[256],seq;
static void trace(unsigned x){assert(seq<256);sequence[seq++]=x;}
u32 randomizer_mo2_game(void){return game;}
s32 randomizer_mo2_ice(u32 a){assert(a==actor+0x2E54);trace(1);return ice_on;}
s32 randomizer_mo2_morf_play(u32 m,u32 p,u32 sound,s32 reverb){assert(m==morf&&p==actor+0x37C&&!sound&&!reverb);plays++;trace(2);return 0;}
void randomizer_mo2_morf_calc(u32 m){assert(m==morf);calcs++;trace(3);}
u32 randomizer_mo2_light(void){return 123;}
void randomizer_mo2_tev(u32 l,s32 type,u32 p,u32 tev){assert(l==123&&!type&&p==actor+0x314&&tev==actor+0x110);trace(4);}
void randomizer_mo2_fire(u32 f){assert(f==actor+0x320C);firecalls++;trace(5);}
void randomizer_mo2_next_stage(u32 name,s32 point,s32 room,s32 layer,f32 speed,u32 mode,s32 wipe,s32 arg){assert(name==0x10015118&&!point&&!room&&layer==-1&&!speed&&!mode&&wipe==1&&!arg);stages++;}
void randomizer_mo2_sound_stop(u32 id,s32 f){assert(id==0x834&&f==30);stops++;}
void randomizer_mo2_order_event(u32 a,u32 flag,u32 hind,u32 arg){assert(a==actor&&flag==2&&hind==0xFFFF&&!arg);orders++;}
s32 randomizer_mo2_trigger_b(s32 p){assert(!p);return trigger;}
s32 randomizer_mo2_hold_y(s32 p){assert(!p);return hold;}
s32 randomizer_mo2_switch(u32 s,s32 i,s32 room){assert(s==save&&i==6&&room==-3);return spawn_on;}
void randomizer_mo2_move(u32 a){assert(a==actor);moves++;trace(6);if(mutate)W(u8,0x101BA692)=1;}
s32 randomizer_mo2_damage_reaction(u32 d){assert(d==actor+0x8BC);damagecalls++;trace(7);return reaction;}
void randomizer_mo2_matrix_copy(u32 a,u32 b){assert(a==0x1048D0CC||a==0x30035000+0x16*0x30);assert(b==model+0xC8||b==0x1048D0CC);trace(8);}
void randomizer_mo2_scale(f32 x,f32 y,f32 z,u32 c){assert(x==1&&y==1&&z==1&&c==1);trace(9);}
void randomizer_mo2_rotate_y(u32 m,s32 a){assert(m==0x1048D0CC);(void)a;}
void randomizer_mo2_rotate_y_set(u32 m,s32 a){assert(m==0x1048D0CC);(void)a;}
void randomizer_mo2_translate(f32 x,f32 y,f32 z,u32 c){(void)x;(void)y;(void)z;assert(!c);}
void randomizer_mo2_transform(u32 src,u32 dst){f32* p=mem(src);W(f32,dst)=p[0];W(f32,dst+4)=p[1];W(f32,dst+8)=p[2];}
void randomizer_mo2_cylinder_center(u32 c,u32 p){assert(c==actor+0x25AC||c==actor+0x26DC);(void)p;}
void randomizer_mo2_sphere_center(u32 c,u32 p){assert(c==actor+0x2938||c==actor+0x280C);(void)p;}
void randomizer_mo2_sphere_radius(u32 c,f32 r){assert(c==actor+0x2938&&r==-200);}
void randomizer_mo2_collision_add(u32 m,u32 o){assert(m==game+0x26A4);assert(o==actor+0x2494||o==actor+0x25C4||o==actor+0x26F4||o==actor+0x2820);collisions++;}
void randomizer_mo2_mass(u32 m,u32 o,u32 weight){assert(m==game+0x4EF8&&o==actor+0x2494&&weight==3);}
void randomizer_mo2_status_move(u32 s){assert(s==actor+0x1030);}
s32 randomizer_mo2_target_hit(u32 o){if(o==actor+0x2820)return hit==3;if(o==actor+0x26F4)return hit==1;if(o==actor+0x25C4)return hit==2;assert(0);return 0;}
u32 randomizer_mo2_hit_object(u32 o){assert(o==actor+0x2820||o==actor+0x26F4||o==actor+0x25C4);return object;}
void randomizer_mo2_attack_power(u32 info){struct randomizer_mo2_attack_info* p=mem(info);assert(p->object==object&&!p->damage&&!p->cut);p->type=attack_type;p->dead=dead;p->damage=3;p->angle[1]=123;p->cut=cut;}
u32 randomizer_mo2_attack_check(u32 a,u32 info){struct randomizer_mo2_attack_info* p=mem(info);assert(a==actor&&p->type==attack_type);assert(W(s8,actor+0x3A1)==((attack_type==10||attack_type==14)?20:health));W(s8,actor+0x3A1)=0;return player;}
void randomizer_mo2_defense_sound(u32 a,u32 o,u32 material){assert(a==actor&&o==object&&material==0x41);}
void randomizer_mo2_sound_influence(u32 p,s32 r,u32 id,s32 t){assert(r==100&&id==1234&&t==5);assert(W(f32,p)==W(f32,actor+0x314));}
s32 randomizer_mo2_reverb(s32 room){assert(room==-3);return 17;}
void randomizer_mo2_monster_sound(u32 id,u32 p,u32 process,u32 param,s32 reverb){assert(id==0x4809||id==0x480A||id==0x480E);assert(p==actor+0x37C&&process==1234&&!param&&reverb==17);voices++;}
void randomizer_mo2_texture(u32 a,u32 i){assert(a==actor&&i>=1&&i<=4);texturecalls++;}
void randomizer_mo2_animation(u32 a,s32 idx,f32 m,u32 loop,f32 speed,s32 sound){assert(a==actor&&(idx==0x6A||idx==0x6B)&&m==2&&!loop&&speed==1&&sound==(idx==0x6A?0x31:0x32));}
void randomizer_mo2_wait(u32 a){assert(a==actor);}
void randomizer_mo2_path_check(u32 a){assert(a==actor);}
f64 randomizer_mo2_random(f64 range){if(random_precise){assert(random_calls<4);return random_values[random_calls++];}return range*.25;}
f64 randomizer_mo2_random_centered(f64 range){return -range*.5;}
s32 randomizer_mo2_atan(f32 x,f32 z){if(precise){assert(atan_calls<2);atan_x[atan_calls++]=x;}(void)z;return 0x100;}
void randomizer_mo2_camera_get(void){}
u32 randomizer_mo2_camera_body(void){return game+0x7000;}
void randomizer_mo2_camera_trim(u32 c,s32 s){assert(c==game+0x7000&&(s==0||s==1));}
void randomizer_mo2_camera_type(u32 c,u32 type,u32 a){assert(c==game+0x7000&&type==0x10014EF4&&a==actor);}
void randomizer_mo2_camera_trim_type(u32 c,s32 t){assert(c==game+0x7000&&t==1);}
void randomizer_mo2_camera_stop(u32 c){assert(c==game+0x7000);}
void randomizer_mo2_camera_start(u32 c){assert(c==game+0x7000);}
void randomizer_mo2_camera_set(u32 c,u32 center,u32 eye,f32 fov,s32 a){assert(c==game+0x7000&&!a);assert(isfinite(fov));(void)center;(void)eye;camsets++;}
void randomizer_mo2_meter_show(void){}
void randomizer_mo2_calculate(u32 v,f32 t,f32 s,f32 max){(void)s;(void)max;W(f32,v)=t;}
void randomizer_mo2_shock(u32 v,s32 strength,s32 flags,u32 dir){assert(v==game+0x599C&&strength==4&&flags==-33);assert(W(f32,dir)==0&&W(f32,dir+4)==1&&W(f32,dir+8)==0);shockcalls++;}
void randomizer_mo2_sound(u32 id,u32 p,u32 parameter,s32 rev){assert(id==0x905&&!p&&!parameter&&rev==17);}
void randomizer_mo2_smoke(u32 a,f32 scale){assert(a==actor&&scale>0);smokes++;}
void randomizer_mo2_smoke_end(u32 c){assert(c==actor+0x89C);smokeends++;}
void randomizer_mo2_fire_remove(u32 f){assert(f==actor+0x320C);}
u32 randomizer_mo2_particle(u32 c,u32 grp,u32 id,u32 p,u32 a,u32 scale,u32 alpha,u32 cb,s32 setup,u32 color,u32 env,u32 scale2d){assert(c==0x12340000&&!grp&&(id==0x2B||id==0xE)&&!scale&&alpha==255&&!cb&&setup==-1&&!scale2d);if(id==0x2B)assert(p==actor+0x2D30&&color==actor+0x1A8&&env==color);else assert(p==actor+0x2CD0&&!color&&!env);(void)a;particlecalls++;return emitter;}
void randomizer_mo2_ground_construct(u32 p){memset(mem(p),0,0x54);}
void randomizer_mo2_ground_destroy(u32 p,s32 f){unsigned int* g=mem(p);assert(!f&&g[0x20/4]==0x10014DEC&&g[0x40/4]==0x10014E0C&&g[0x4C/4]==0x10014DCC);}
f64 randomizer_mo2_ground_cross(u32 m,u32 p){unsigned int* g=mem(p);assert(m==game+0x12A0&&g[0x10/4]==0x10014DDC&&g[0x20/4]==0x10014DEC&&g[0x40/4]==0x10014E0C&&g[0x4C/4]==0x10014DFC&&g[0x50/4]==1);groundcalls++;if(precise)return groundcalls==1?1.00000006:1.00000008;return ground_valid?10.0f:-1e9f;}
void randomizer_mo2_angle_calculate(u32 p,s32 target,s32 ratio,s32 max){assert((p==actor+0xD7C||p==actor+0xD80)&&target>=-0x2000&&target<=0x2000&&(ratio==1||ratio==8)&&(max==0x100||max==0x400));angles++;}
s32 randomizer_mo2_delete(u32 a){assert(a==actor||a==weapon);deletes++;return 1;}
void randomizer_mo2_switch_on(u32 s,s32 i,s32 room){assert(s==save&&i==6&&room==-3);switchons++;}
void randomizer_mo2_actor_on(u32 s,s32 i,s32 room){assert(s==save&&i==12&&room==-4);actorons++;}
u32 randomizer_mo2_judge(u32 cb,u32 key){assert(cb==0x025E1234&&*(u32*)key==555);judges++;return weapon;}
u32 randomizer_mo2_spawn(s32 name,u32 param,u32 p,s32 room,u32 angle,u32 scale,s32 subtype,u32 cb){assert(name==0x1CF&&param==4&&room==-3&&!angle&&!scale&&subtype==-1&&!cb);(void)p;creates++;return 555;}
static void reset(void){memset(arena,0,sizeof arena);memset(data,0,sizeof data);ice_on=trigger=hold=spawn_on=reaction=hit=attack_type=dead=cut=mutate=atan_calls=random_precise=random_calls=0;health=10;plays=calcs=moves=collisions=damagecalls=texturecalls=particlecalls=voices=deletes=firecalls=stages=orders=stops=groundcalls=angles=ground_valid=precise=creates=judges=camsets=shockcalls=smokes=smokeends=switchons=actorons=seq=0;W(u32,actor+4)=1234;W(u32,actor+0x3E0)=morf;W(u32,morf+0x90)=model;W(u32,model+0x2C)=0x30034000;W(u32,0x30034010)=0x30035000;W(u32,game+0x5B2C)=player;W(u32,game+0x5AB0)=0x12340000;W(u32,game+0x5AF8)=game+0x6000;W(u32,0x1018C7B0)=0x1048D0CC;W(u8,0x104654E2)=1;W(f32,0x104654EC)=1;W(f32,0x1046561C)=300;W(u8,0x101BA692)=1;W(s8,actor+0x326)=-3;W(s8,actor+0x2FE)=-4;W(u16,actor+0x2D8)=12;W(s16,actor+0x45C)=2;W(s16,actor+0x2D4C)=-1;W(s8,actor+0x3A1)=10;W(s16,actor+0x8C2)=4;W(f32,player+0x3CC)=1;}
int main(void){unsigned cases=0;const unsigned reactions[]={0,1,2,5,10,20,21,30,31,0xFFFFFFFFu};
for(unsigned active=0;active<2;active++)for(unsigned mode=0;mode<256;mode++)for(unsigned r=0;r<10;r++)for(unsigned ground=0;ground<3;ground++){
reset();W(u8,0x104654E2)=!active;W(u8,actor+0x3D8)=mode;reaction=reactions[r];if(ground)W(u32,actor+0xE34)=0x20;ground_valid=ground==2;assert(randomizer_moblin_execute(actor)==1);assert(moves==active&&damagecalls==1);assert(W(u8,0x101BA692)==!active);assert(groundcalls==(ground==2?3:ground==1?1:0));assert(angles==(ground==2?2:0));assert(W(f32,actor+0x3B4)==300);cases++;}
for(unsigned count=0;count<3;count++)for(unsigned spawned=0;spawned<2;spawned++){reset();W(u8,0x104654E2)=0;W(u8,actor+0x3DD)=7;spawn_on=spawned;W(s16,actor+0x848)=count;assert(randomizer_moblin_execute(actor)==1);assert(W(s16,actor+0x848)==(count?count-1:0));assert(moves==spawned&&damagecalls==spawned);assert(W(u8,0x101BA692)==0&&W(u32,actor+0x39C)==(spawned?4:0));cases++;}
for(unsigned t=0;t<256;t++)for(unsigned part=1;part<4;part++)for(unsigned d=0;d<2;d++)for(unsigned md=0;md<2;md++){
reset();hit=part;attack_type=t;dead=d;cut=0x8001;W(u8,actor+0x3D8)=md;const u8 damage=randomizer_mo2_damage(actor);assert(damage==(part==3?0:t==10?1:3));if(part==3){assert(particlecalls==1&&W(s32,emitter+0x5C)==1&&W(f32,emitter+0x34)==10);}else {assert(W(u16,actor+0x2D68)==0x8001&&W(s8,actor+0x3A1)==(t==10||t==14?10:0));assert(voices==1);}cases++;}
for(unsigned attack=0;attack<3;attack++){reset();hit=1;W(u32,object+0x10)=attack==0?0x80000:attack==1?0x100000:0x200;assert(randomizer_mo2_damage(actor)==0||attack==2);if(attack<2)assert(W(u8,actor+0x2E0D)==1);cases++;}
for(unsigned mode=0;mode<256;mode++)for(unsigned timer=0;timer<3;timer++){reset();W(s8,actor+0x2E21)=mode;W(u16,actor+0xF8)=timer==0?0:2;W(s16,actor+0x2E22)=timer==2?150:0;randomizer_mo2_demo_camera(actor);if(mode==1||mode==2)assert(shockcalls==1&&W(s8,actor+0x2E21)==3);if(mode==0x32&&timer==0)assert(orders==1&&W(u16,actor+0xFA)==2);if(mode==0x33&&timer==2)assert(deletes==1&&(W(u16,game+0x52B8)&8));cases++;}
for(unsigned kind=0;kind<256;kind++)for(unsigned timer=0;timer<4;timer++){reset();W(u8,actor+0x89A)=kind;W(s16,actor+0x898)=timer;W(s16,0x10465500)=1;randomizer_mo2_ground_smoke(actor);assert(W(s16,actor+0x898)==(timer?timer-1:0));assert(smokeends==(timer==1));assert(smokes==((timer>=2&&kind<=4)?1:0));cases++;}
for(unsigned mode=0;mode<4;mode++){reset();W(s8,actor+0x10B1)=mode;W(s8,actor+0x10B0)=1;randomizer_mo2_throw_weapon(actor);assert(creates==!!mode&&judges==!!mode);if(mode)assert(W(s8,actor+0x10B0)==0&&W(s8,actor+0x10B3)==(mode==2));cases++;}
reset();ice_on=1;assert(randomizer_moblin_execute(actor)==1&&plays==1&&calcs==1&&!firecalls&&!damagecalls);cases++;
reset();W(u8,actor+0x3DC)=6;W(f32,actor+0x318)=-5001;W(s8,actor+0x10B0)=1;W(u32,actor+0x2D60)=555;assert(randomizer_moblin_execute(actor)==1&&switchons==1&&actorons==1&&deletes==2&&!firecalls);cases++;
reset();precise=1;ground_valid=1;W(u32,actor+0xE34)=0x20;randomizer_moblin_execute(actor);assert(groundcalls==3&&angles==2);assert(atan_x[0]==(f32)(1.00000008-1.00000006)&&atan_x[0]!=0&&atan_x[1]==atan_x[0]);cases++;
for(unsigned timer=0;timer<3;timer++)for(unsigned blink=0;blink<256;blink++)for(unsigned frame=0;frame<3;frame++){reset();W(s16,actor+0x45C)=timer;W(u8,actor+0x459)=blink;W(u8,actor+0x458)=frame;W(u8,actor+0x45A)=1;randomizer_mo2_eye(actor);assert(W(s16,actor+0x45C)==(timer?timer-1:22));assert(texturecalls==(!timer&&!blink));if(blink)assert(W(u8,actor+0x459)==(frame<1?blink:0));cases++;}
assert(randomizer_mo2_integer(NAN)==(s32)0x80000000u);assert(randomizer_mo2_integer(INFINITY)==0x7FFFFFFF);assert(randomizer_mo2_integer(-INFINITY)==(s32)0x80000000u);assert(randomizer_mo2_integer(1.999)==1&&randomizer_mo2_integer(-1.999)==-1);
reset();W(u8,0x104654E2)=0;mutate=1;randomizer_moblin_execute(actor);assert(moves==1&&W(u8,0x101BA692)==1);cases++;
for(unsigned f=0;f<256;f++)for(unsigned timer=0;timer<33;timer++){reset();W(u8,actor+0x2E4F)=f;W(s16,actor+0x2E50)=timer;randomizer_moblin_execute(actor);assert(stages==!!(f||timer==1));if(f||timer==1)assert(!damagecalls&&W(u8,0x101BA692)==1);else assert(W(s16,actor+0x2E50)==(timer?timer-1:0)&&stops==(timer==31));cases++;}
reset();ice_on=1;for(unsigned i=0;i<12;i++)W(u32,0x1048D0CC+4*i)=0x7F812340+i;randomizer_moblin_execute(actor);for(unsigned i=0;i<12;i++)assert(W(u32,model+0xC8+4*i)==0x7F812340+i);for(unsigned i=0;i<seq;i++)assert(sequence[i]!=8);cases++;
reset();for(unsigned i=0;i<12;i++)W(u32,0x1048D0CC+4*i)=0x80000000u+i;randomizer_moblin_execute(actor);for(unsigned i=0;i<12;i++)assert(W(u32,model+0xC8+4*i)==0x80000000u+i);for(unsigned i=0;i<seq;i++)assert(sequence[i]!=8);cases++;
reset();random_precise=1;random_values[0]=0.49999999999999994;random_values[1]=1.00000095;random_values[2]=1.23456789;reaction=1;W(s8,actor+0x10B0)=1;randomizer_moblin_execute(actor);assert(creates==1&&random_calls==3);assert(W(f32,weapon+0x370)==(f32)(random_values[1]+20.0)&&W(f32,weapon+0x340)==(f32)(random_values[2]+20.0));cases++;
reset();random_precise=1;random_values[0]=12.9999998;W(s16,actor+0x45C)=0;randomizer_mo2_eye(actor);assert(W(s16,actor+0x45C)==(s16)(f32)(random_values[0]+10.0));cases++;
reset();random_precise=1;random_values[0]=1.23456789;hit=1;attack_type=2;randomizer_mo2_damage(actor);assert(W(f32,actor+0xD18)==(f32)(random_values[0]+70.0));cases++;
printf("Moblin execution: %u cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'test.c').write_text(code);(p/'weak.c').write_text(prefix+'\n'.join(weak))
            subprocess.run([os.environ.get("CC", "cc"),'-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-I',str(root),str(p/'test.c'),str(p/'weak.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__':unittest.main()
