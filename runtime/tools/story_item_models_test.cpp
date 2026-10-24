// SPDX-License-Identifier: MPL-2.0
#include "mods/story_item_models.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace I=story_item_models;
namespace {constexpr uint32_t A=0x20000000,S=0x60010000,M=0x20010000,H=0x20020000,CL=0x20030000,B=0x20040000,MT=0x20050000;bool loading=false,cached=false,hidden=false;uint32_t freeBytes=0x400000;unsigned tableCalls=0;unsigned loads=0,models=0,draws=0,destroys=0,deletes=0;
// the item get (hold_up): the demo item the game creates, its search, the event order, the end check
uint32_t demoItem=0,presented=0,orders=0,actorDeletes=0;bool eventOver=false;std::vector<uint32_t> presentArgs;std::vector<std::pair<uint16_t,bool>> armed;bool armOk=true;std::map<uint32_t,void(*)(Cpu*)> hooks;
void map(uint32_t p,uint32_t n){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(p),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(p),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
void original(Cpu*c,uint32_t){c->r[3]=77;c->pc=1234;}
uint32_t call(Cpu*c,uint32_t f,std::initializer_list<uint32_t>args){std::vector<uint32_t>a(args);c->r[4]=888;c->f[1].ps0=99;switch(f){case 0x02520460:++loads;st32(a[0]+4,loading?1:2);return loading?1:4;case 0x026066C4:assert(a[2]==1);return 0x20060000;case 0x02001000:return freeBytes;case 0x02612504:return cached?1:0;case 0x02604470:case 0x026044E0:assert(a[1]==2);++tableCalls;return 0;case 0x02608308:assert(a[1]==1&&a[2]==1);++tableCalls;return 0;case 0x027EC230:return 0x20070000;case 0x025E2FBC:return 0x20070000;case 0x025E33A8:assert(a[0]==0x40000);return H;case 0x027EC220:return 0;case 0x025E38E0:++models;assert(a[1]==0x80000);return M;case 0x025E3868:++destroys;return 0;case 0x025204C8:assert(ld32(a[0]+4)==2);++deletes;st32(a[0]+4,0);return 0;case 0x02444E58:assert(a.size()==1&&a[0]==A);return hidden?1:0;case 0x02555D0C:return 0x20080000;case 0x025626A4:std::memset(mem::ptr(a[3]),0xEE,0x1C8);return 0;case 0x02562F5C:return 0;case 0x025E2DE0:++draws;return 0;
case 0x025D7DEC:++presented;presentArgs=a;return 0x4242;
case 0x025D54C4:assert(a[0]==0x4242);st32(a[1],demoItem);return demoItem?1:0;
case 0x025D7B98:assert(a[0]==demoItem&&a[1]==0xFFFF);++orders;return 1;
case 0x0254457C:assert(a[0]==0x1046F0B0+0x52C4&&a[1]==0x10011EB8);return eventOver?1:0;
case 0x025D57E0:assert(a[0]==demoItem);++actorDeletes;return 1;
default:assert(false);return 0;}}
bool arm(uint16_t key,bool on){armed.push_back({key,on});return !on||armOk;}
}
int main(){map(A,0x100000);map(S,0x10000);map(0x10000000,0x600000);std::string why;I::reset();
// #269: no held model declared (nothing configured or installed, the record's address 0): a chest of a mod
// item ends its get (chest_hold_up off) and the demo item draws; nothing reads guest memory (address 8 was
// read before), nothing is drawn by us
{I::chest_hold_up(7,true);I::chest_hold_up(7,false);assert(!I::demo_active());Cpu z{};z.r[3]=A;assert(!I::draw_demo_item(&z));I::tick(&z);}
assert(!I::declare(1,"../retail",90,why));assert(!I::declare(1,"modown",301,why));assert(I::declare(1,"modown",3,why));assert(!I::declare(1,"other",3,why));I::configure(S,original,call);assert(!I::install([](uint32_t f){return f==0x02403C24?"foreign":"";},[](uint32_t,void(*)(Cpu*)){}));assert(I::install([](uint32_t){return std::string();},[](uint32_t f,void(*h)(Cpu*)){hooks[f]=h;}));assert(hooks.size()==5&&!hooks.count(0x02123B60));  // daDitem_Draw is story_item_get_icon's hook
st32(0x20070000+0xC,0x10001000);st32(0x10001000+0x74,0x02001000);st32(0x1046F0B0+0x5B34,A);st32(A+4,10);st16(A+0xE,0xA8);st8(A+0x326,11);st32(A+0x448,CL);st32(CL+0x2C,B);st32(B+0x10,MT);for(unsigned i=0;i<12;++i)st32(MT+8*0x30+i*4,f32_as_u32(float(i)));st32(A+0x4440,0xDEADBEEF);st16(A+0x69B0,0x103);
Cpu c{};c.r[3]=A;c.r[4]=23;c.pc=44;Cpu before=c;I::request(1);I::tick(&c);assert(std::memcmp(&c,&before,sizeof c)==0&&models==1);I::hooks::hand(&c);assert(c.r[3]==77&&c.pc==1234);assert(std::memcmp(mem::ptr(M+0xC8),mem::ptr(MT+8*0x30),0x30)==0);c.r[3]=A;unsigned ticks=ld32(S+20);I::hooks::draw(&c);c.r[3]=A;I::hooks::draw(&c);assert(draws==2&&ld32(S+20)==ticks);
// Native Draw returnsTRUE even when Link's own geometry is hidden; separate model must follow visibility.
hidden=true;c.r[3]=A;I::hooks::draw(&c);assert(draws==2&&c.r[3]==77&&c.pc==1234);hidden=false;
// A short F8-byte span would accept this near-host pointer although native model class144 crosses the bound.
const uint32_t realModel=ld32(S+28);st32(S+28,mem::kHostStart-0x100);c.r[3]=A;I::hooks::draw(&c);assert(draws==2);c.r[3]=A;I::hooks::hand(&c);assert(draws==2);st32(S+28,realModel);
assert(ld32(A+0x4440)==0xDEADBEEF&&ld16(A+0x69B0)==0x103);
// Full restore keeps guest allocation; no model recreation or replayed Use.
uint8_t snapshot[I::kMemoryBytes];std::memcpy(snapshot,mem::ptr(S),sizeof snapshot);I::state_loaded();I::tick(&c);assert(models==1);std::memcpy(mem::ptr(S),snapshot,sizeof snapshot);I::state_loaded();I::tick(&c);assert(models==1);I::progress_loaded();I::tick(&c);assert(destroys==1&&deletes==1&&!ld32(S));
I::request(1);I::tick(&c);assert(models==2);st32(A+4,11);I::tick(&c);assert(destroys==2&&!ld32(S)); // recycled native address
I::request(1);I::tick(&c);assert(models==3);st32(S+280,0);unsigned d=destroys;I::tick(&c);assert(!ld32(S)&&destroys==d);I::request(1);I::tick(&c);assert(models==4&&destroys==d); // replacement of invalid record never frees old pointers
 // mismatched restored config never calls arbitrary native pointers
// Async resource phase cannot be deleted until native load finishes; replacement stays queued.
I::progress_loaded();I::tick(&c);loading=true;I::request(1);I::tick(&c);assert(ld32(S+44)==1);unsigned oldModels=models,oldDeletes=deletes;
I::request(1);I::tick(&c);assert(models==oldModels&&deletes==oldDeletes&&ld32(S+8)==1);
loading=false;I::tick(&c);assert(models==oldModels+1&&deletes==oldDeletes+1);

// Exact own archive handler preserves CPU and index1; foreign names delegate untouched.
const uint32_t key=0x20090000,chars=key+16;st32(key,chars);std::memcpy(mem::ptr(chars),"foreign",8);c={};c.r[3]=0x200A0000;c.r[4]=key;c.r[5]=H;c.r[6]=0x20070000;before=c;
assert(!I::resource_table(&c)&&!I::archive_wrapper(&c)&&!I::backing_archive_wrapper(&c)&&std::memcmp(&c,&before,sizeof c)==0);
std::memcpy(mem::ptr(chars),"modown",7);assert(I::resource_table(&c)&&tableCalls==3&&std::memcmp(&c,&before,sizeof c)==0);
freeBytes=0x100000;assert(I::backing_archive_wrapper(&c)&&c.r[3]==0);c=before;cached=true;assert(I::backing_archive_wrapper(&c)&&c.r[3]==77);cached=false;

// Native heap destructor must never see packet.mEntryPtr linked. A pending replacement stops draw
// and waits for the engine's actual reset, rather than blindly delaying/freeing or clearing its list.
I::progress_loaded();I::tick(&c);freeBytes=0x400000;I::request(1);I::tick(&c);
const uint32_t packets=0x200B0000;st32(M+0x128,1);st32(M+0x12C,packets);st32(packets+0x94,0x200C0000);
unsigned beforeDestroy=destroys,beforeModels=models,beforeDraws=draws;I::request(1);I::tick(&c);
assert(destroys==beforeDestroy&&models==beforeModels&&ld32(S+488)==1&&ld32(S+8)==1);
c.r[3]=A;I::hooks::hand(&c);c.r[3]=A;I::hooks::draw(&c);assert(draws==beforeDraws);
I::state_loaded();I::tick(&c);assert(destroys==beforeDestroy&&ld32(packets+0x94)==0x200C0000);
st32(packets+0x94,0);I::tick(&c);assert(destroys==beforeDestroy+1&&models==beforeModels+1&&ld32(S+488)==0);
// Expiry/progress uses the same link-aware retirement path; no special unsafe teardown route.
st32(packets+0x94,0x200C0000);I::progress_loaded();I::tick(&c);assert(destroys==beforeDestroy+1&&ld32(S+488)==1);
st32(packets+0x94,0);I::tick(&c);assert(destroys==beforeDestroy+2&&!ld32(S));

// Item get (hold_up): queued, started in a free moment, the demo item orders the item event and becomes the
// event's item partner; its draw shows the held model with its own matrix; the end resets the event.
st32(M+0x128,0);I::set_text(arm);assert(!I::hold_up(9)&&I::hold_up(1)&&I::demo_active());
const uint32_t P=0x1046F0B0;st8(P+0x5292,1);unsigned m0=models;I::tick(&c);assert(presented==0&&armed.empty());  // an event runs: later
st8(P+0x5292,0);armOk=false;I::tick(&c);assert(presented==0&&armed.size()==1);armOk=true;armed.clear();       // the get message can't be armed yet
I::tick(&c);assert(presented==1&&presentArgs[1]==0x9D&&presentArgs[2]==1&&presentArgs[0]==A+0x314&&presentArgs[4]==11);
assert(armed.size()==1&&armed[0]==std::make_pair(uint16_t(1),true));
assert(ld32(S+976)==0x48555031&&ld32(S+980)==1&&ld32(S+984)==0x4242&&ld32(S+36)==1&&models==m0+1);
I::tick(&c);assert(orders==0);  // the demo item isn't there yet
const uint32_t DI=0x200D0000,DM=0x200E0000;demoItem=DI;st32(DI+4,0x4242);st8(DI+0x74E,0x9D);st32(DI+0x3B4,DM);
for(unsigned i=0;i<12;++i)st32(DM+0xC8+i*4,f32_as_u32(float(100+i)));
I::tick(&c);assert(orders==1&&(ld16(DI+0xFA)&8));
st16(DI+0xF8,4);I::tick(&c);assert(ld32(P+0x52A0)==0x4242&&ld32(S+980)==2);
// the demo item's draw: the held model in its place, never the stand-in's card; Link's hand shows nothing
unsigned d0=draws;c={};c.r[3]=DI;assert(I::draw_demo_item(&c));assert(c.r[3]==1&&draws==d0+1&&std::memcmp(mem::ptr(M+0xC8),mem::ptr(DM+0xC8),0x30)==0);
c.r[3]=A;I::hooks::draw(&c);assert(draws==d0+1);
st8(DI+0x74E,0x01);c.r[3]=DI;assert(!I::draw_demo_item(&c));assert(c.r[3]==DI&&draws==d0+1);st8(DI+0x74E,0x9D);  // another demo item: not ours, untouched
// a full state taken now brings the demo back with its text
uint8_t during[I::kMemoryBytes];std::memcpy(during,mem::ptr(S),sizeof during);armed.clear();
I::state_loaded();std::memcpy(mem::ptr(S),during,sizeof during);I::tick(&c);assert(armed.size()==1&&armed[0].second);
assert(ld32(S+976)==0x48555031&&ld32(S+980)==2);  // the light settings the draws write (0x1C8 bytes at 512) never reach the record
// DEFAULT_GETITEM over: the event is reset, the demo item deleted, the text disarmed, the model retired
st16(P+0x52B8,0);eventOver=true;armed.clear();I::tick(&c);
assert((ld16(P+0x52B8)&8)&&actorDeletes==1&&armed.size()==1&&!armed[0].second&&ld32(S+976)==0&&!I::demo_active());
I::tick(&c);assert(!ld32(S));
// a chest's own demo: only the model, on and off
I::chest_hold_up(1,true);I::tick(&c);assert(ld32(S+36)==1&&ld32(S)!=0);I::chest_hold_up(1,false);I::tick(&c);I::tick(&c);assert(!ld32(S));

}
