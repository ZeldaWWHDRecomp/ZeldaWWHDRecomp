/* Synthetic memory/callback regression, independent of game resources. */
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
typedef uintptr_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef int32_t s32;
static unsigned char memory[0x100000];
static __attribute__((unused)) void *at(u32 a){assert((a&0xFFFFF)<sizeof(memory)-8);return memory+(a&0xFFFFF);}
#define R_PARTICLE(t,a) (*(volatile mem_##t*)at(a))
typedef uint32_t mem_u32;typedef uint16_t mem_u16;typedef uint8_t mem_u8;
#define PARTICLE_DATA(a) ((u32)(a))
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) enum{n##_descriptor=__COUNTER__};r n p
#ifndef WWHD_RANDOMIZER_PARTICLE_PERFORMANCE
#define WWHD_RANDOMIZER_PARTICLE_PERFORMANCE 1
#endif
enum{before=__COUNTER__};
#include "particle_performance.h"
enum{after=__COUNTER__};
#if WWHD_RANDOMIZER_PARTICLE_PERFORMANCE
static const u32 owner=0x80000;
static unsigned mask,allocs,reports,loads,registered,simples,resources,contexts,defaults,asserts,event_count;
static unsigned events[128];
static void event(unsigned e){assert(event_count<128);events[event_count++]=e;}
static void mutate(void){R_PARTICLE(u32,owner+0x128)++;}
u32 particle_context(u32 manager,u32 context){event(1);assert(manager==0x90000+contexts);contexts++;mutate();if(contexts==1)return mask&128?1:7;assert(context==7);return 0;}
u32 particle_load_bank(u32 manager,u32 first,u32 second,u32 flags){event(2);assert(manager==0x2222);assert(((u32*)first)[0]==0x10051D78&&((u32*)second)[0]==0x10051D84);assert(((u32*)first)[1]==0x10051A34);assert(!flags);loads++;mutate();return !(mask&64);}
u32 particle_bank(void){event(3);mutate();return 0x9000;}
u32 particle_heap_size(u32 heap){event(4);assert(heap==R_PARTICLE(u32,owner+0x128));mutate();return 1234;}
void particle_report(u32 format,...){event(5);static const u32 names[]={0x10051D94,0x10051DCC,0x10051DF0,0x10051E2C,0x10051E3C};assert(format==names[reports++]);va_list args;va_start(args,format);assert(va_arg(args,u32)==1234);va_end(args);mutate();}
u32 particle_new_heap(u32 size,u32 heap,u32 align){event(6);assert(heap==R_PARTICLE(u32,owner+0x128)&&align==4);const u32 sizes[]={12,16,0x580BB8,16};assert(size==sizes[allocs]);unsigned index=allocs++;mutate();return mask&(1u<<index)?0:0xA000+index*0x100;}
u32 particle_new(u32 size){(void)size;assert(0);return 0;}
u32 particle_resource_ctor(u32 self,u32 data,u32 heap,u32 bank){event(7);assert(self==0xA000&&data==0xDEAD&&heap==R_PARTICLE(u32,owner+0x128)&&bank==0x9000);mutate();return mask&16?0:0xB000;}
void particle_assert(u32 file,u32 line,u32 message){event(8);assert(file==0x10051DA4);assert((line==0x711&&message==0x10051DB4)||(line==0x726&&message==0x10051DDC)||(line==0x74F&&message==0x10051E14));asserts++;/* Assertions may return and repair missing resources. */if(line==0x711)R_PARTICLE(u32,owner+0x12C)=0xB000;mutate();}
void particle_render_init(u32 self,u32 texture,u32 heap){event(9);assert(self==(mask&2?0:0xA100));if(self)for(unsigned i=0;i<16;i++)assert(!*(u8*)at(self+i));assert(texture==(mask&32?0:0xC05C));assert(heap==R_PARTICLE(u32,owner+0x128));mutate();}
u32 particle_emitter_ctor(u32 self,u32 rm,u32 particles,u32 emitters,u32 groups,u32 heap,u32 render){event(10);assert(self==0xA200&&rm==0xB000&&particles==1500&&emitters==150&&groups==200);assert(heap==R_PARTICLE(u32,owner+0x128)&&render==(mask&2?0:0xA100));mutate();return mask&16?0:0xB200;}
u32 particle_find_texture(u32 table,u32 name){event(11);assert(table==0x906C&&name==0x10051E00);mutate();return 0xD000;}
void particle_register_texture(u32 manager,u32 texture,u32 id){event(12);assert(manager==(registered?0xEE00:((mask&20)?0:0xB200)));assert(texture==(registered?0x7777:0x6666));assert(id==(registered?0x2345:0x1234));registered++;R_PARTICLE(u32,0x1047B2D4)=0xEE00;mutate();}
u32 particle_get_res(u32 controller,u32 name,u32 index){event(13);assert(controller==(index==0x31?0x4444:0x5555));assert(((u32*)name)[0]==0x10051D70);R_PARTICLE(u32,0x101F4F28)=0x5555;mutate();return index+0x123;}
u32 particle_model_ctor(u32 self,u32 first,u32 second,u32 heap){event(14);assert(self==0xA300&&first==0x154&&second==0x153&&heap==R_PARTICLE(u32,owner+0x128));mutate();return mask&16?0:0xB300;}
u32 particle_resource_get(u32 self,u32 id){event(15);assert(self==0xF000&&id==(0x4000u+resources));/* Cached id must survive callback mutation. */R_PARTICLE(u16,0x101EA50C+resources*2)=0;resources++;mutate();return resources&1;}
s32 particle_new_simple(u32 self,u32 id,u32 group){event(16);assert(self==owner&&id==0x4000u+simples*2&&group==4);simples++;R_PARTICLE(u8,owner+0x146)++;mutate();return -1;}
void particle_defaults(u32 self){event(17);assert(self==owner&&R_PARTICLE(u8,self+0x147)==4);defaults++;R_PARTICLE(u32,0x101F8B4C)=0x90001;mutate();}
#endif
int main(void){assert(after-before-1==WWHD_RANDOMIZER_PARTICLE_PERFORMANCE);
#if WWHD_RANDOMIZER_PARTICLE_PERFORMANCE
 for(mask=0;mask<256;mask++){
  memset(memory,0xA5,sizeof memory);allocs=reports=loads=registered=simples=resources=contexts=defaults=asserts=event_count=0;
  R_PARTICLE(u32,owner+0x128)=123;R_PARTICLE(u32,0x101F8B4C)=0x90000;R_PARTICLE(u32,0x101F4F7C)=0x2222;
  R_PARTICLE(u32,0x101F8A94)=0xC000;R_PARTICLE(u32,0xC010)=mask&32?1:0xFFFFFFFF;R_PARTICLE(u32,0xC014)=0xC000;
  R_PARTICLE(u32,0x904C)=0x20;R_PARTICLE(u32,0xD000)=0x20;R_PARTICLE(u32,0xD004)=24;
  R_PARTICLE(u32,0xD020)=0xFFFF1234;R_PARTICLE(u32,0xD024)=0x6666;R_PARTICLE(u32,0xD028)=0;R_PARTICLE(u32,0xD02C)=0;R_PARTICLE(u32,0xD030)=0xFFFF2345;R_PARTICLE(u32,0xD034)=0x7777;
  R_PARTICLE(u32,0xB004)=0xF000;R_PARTICLE(u32,0x101F4F28)=0x4444;R_PARTICLE(u8,owner+0x146)=0;
  for(unsigned i=0;i<8;i++)R_PARTICLE(u16,0x101EA50C+i*2)=0x4000+i;
  randomizer_particle_common(owner,0xDEAD);
  assert(allocs==4&&reports==5&&loads==2&&resources==8&&simples==4&&defaults==1);
  assert(contexts==(mask&128?1:2)&&registered==(mask&64?0:2));
  /* Allocation failures still continue through every report/default/context. */
  unsigned count=0;for(unsigned i=0;i<event_count;i++)if(events[i]==4){assert(i+1<event_count&&events[i+1]==5);count++;}assert(count==5);
 }
#endif
 return 0;
}
