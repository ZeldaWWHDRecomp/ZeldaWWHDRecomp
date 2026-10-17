// SPDX-License-Identifier: MPL-2.0
// Adapter contract test: deterministic ownership backend; real lineage persistence
// and checksum/tombstone tests live in story_enemy_ownership_test.
#include "mods/story_enemy_children.h"
#include "mods/native_actor_profiles.h"
#include "mods/native_actor_carry.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <map>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
namespace o=story_enemy_ownership;
namespace {
constexpr uint32_t Parent=0x20000000,Child=0x20004000,Other=0x20006000;
std::map<uint32_t,void(*)(Cpu*)> hooks;std::map<uint32_t,o::LineageInfo> rows;
uint32_t epoch=3,nextPID=50;unsigned queues=0,executes=0,candidates=0,notices=0;bool capacity=true,active=true;
native_actor_profiles::BeforeDelete listener=nullptr;native_actor_carry::Guard guard=nullptr;
void original(Cpu*c,uint32_t f){
 if(f==0x025D5834){++queues;c->r[3]=nextPID;return;}
 if(f==0x020C25A4){Cpu nested{};nested.r[3]=Child;hooks.at(0x020C1D0C)(&nested);assert(executes==0);c->r[3]=4;return;}
 if(f==0x020C1D0C){++executes;c->r[3]=77;c->pc=123;return;}
 if(f==0x0209A624){++candidates;c->r[3]=1;return;}
 if(f==0x0209AB64){st32(0x10462320,99);st32(0x10462330,Child);Cpu p{};p.r[3]=Child;p.r[4]=Other;hooks.at(0x0209A624)(&p);assert(p.r[3]==1);p.r[3]=Other;hooks.at(0x0209A624)(&p);assert(p.r[3]==0);c->r[3]=Child;return;}
 c->r[3]=1;
}
uint32_t invoke(Cpu*,uint32_t,std::initializer_list<uint32_t>){return 0;}
void actor(uint32_t a,uint32_t pid,uint16_t profile,uint32_t params){st32(a+4,pid);st16(a+0xE,profile);st32(a+0xB0,params);st8(a+0x2DD,255);st32(a+0x2E8,0xFFFFFFFF);}
void call(uint32_t fn,uint32_t a,uint32_t result){Cpu c{};c.r[3]=a;hooks.at(fn)(&c);assert(c.r[3]==result);}
}
namespace story_enemy_ownership {
const Descriptor& descriptor(Kind k){static Descriptor bk{0x7FF9,0xBD,0x19A4,nullptr,0},mo{0x7FF8,0xBC,0x343C,nullptr,0};return k==Kind::Bokoblin?bk:mo;}
uint32_t source_epoch(Token t){return t==1?epoch:0;}
Token lookup_actor(uint32_t a){return active&&a==Parent&&ld32(a+4)==10?1:0;}
uint32_t reserve_child_pending(Token t,uint32_t a,uint32_t p,uint16_t f,uint32_t params,uint32_t e){if(!capacity)return 0;rows[0]={Lineage::EnemyChild,t,a,p,0,0,f,params,e,1,0,true};return 1;}
bool commit_child_pending(uint32_t ticket,uint32_t pid){assert(ticket==1);auto r=rows.at(0);rows.erase(0);r.pid=pid;rows[pid]=r;return true;}
void cancel_child_pending(uint32_t){rows.erase(0);}
bool lineage_info(uint32_t pid,LineageInfo& out){auto i=rows.find(pid);if(i==rows.end())return false;out=i->second;return true;}
bool lineage_claimed(uint32_t pid,Lineage){return rows.count(pid)!=0;}
bool admit_lineage(uint32_t pid,uint32_t a){auto i=rows.find(pid);if(i==rows.end()||!i->second.source_valid)return false;i->second.address=a;return true;}
void retire_lineage(uint32_t pid){rows.erase(pid);}
}
namespace native_actor_profiles {bool add_before_delete_listener(const std::string&,BeforeDelete f){listener=f;return true;}}
namespace native_actor_carry {
bool configure(void(*)(Cpu*,uint32_t),uint32_t(*)(Cpu*,uint32_t,std::initializer_list<uint32_t>)){return true;}
bool register_guard(const std::string&,Guard g,DeniedTrace){guard=g;return true;}
bool available(const std::function<std::string(uint32_t)>&){return true;}
bool install(const std::function<std::string(uint32_t)>&,const std::function<void(uint32_t,void(*)(Cpu*))>&){return true;}
}
int main(){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(Parent),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(VirtualAlloc(mem::ptr(0x10460000),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(Parent),0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);assert(mmap(mem::ptr(0x10460000),0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
using namespace story_enemy_children;assert(configure(original,invoke,[](const o::LineageInfo&i){assert(i.source==1&&i.source_pid==10);++notices;}));
auto owner=[](uint32_t){return std::string();};auto conflict=[](uint32_t){return std::string("foreign");};auto replace=[](uint32_t f,void(*fn)(Cpu*)){hooks[f]=fn;};assert(!install(conflict,replace)&&hooks.empty());assert(install(owner,replace)&&hooks.size()==9);assert(install(owner,replace));
actor(Parent,10,0x7FF9,0xFFFF0004);actor(Child,50,0x1CF,0);actor(Other,51,0x1CF,0);assert(!enter(1,o::Kind::Bokoblin,Parent,2));assert(enter(1,o::Kind::Bokoblin,Parent,3));
capacity=false;Cpu q{};q.r[3]=0x1CF;q.r[4]=0;hooks.at(0x025D5834)(&q);assert(q.r[3]==0xFFFFFFFF&&queues==0&&rows.empty());capacity=true;
q.r[3]=0x1CF;q.r[4]=4;hooks.at(0x025D5834)(&q);assert(queues==0);q.r[3]=0x1CF;q.r[4]=0;hooks.at(0x025D5834)(&q);assert(queues==1&&rows.count(50));leave();
st32(Parent+0x1360,50);assert(!ready(1,Parent));st32(Child+0x39C,0x10);st32(Child+0x2E0,0x2000|0x400);call(0x020C25A4,Child,4);assert(ready(1,Parent)&&guard(Child));assert(ld32(Child+0x2E0)==0x2000&&!ld32(Child+0x39C));
Cpu ex{};ex.r[3]=Child;hooks.at(0x020C1D0C)(&ex);assert(ex.r[3]==77&&ex.pc==123&&executes==1);
call(0x0209A624,Child,0);assert(candidates==0);st32(0x10462320,7);st32(0x10462330,0x12345678);call(0x0209AB64,Parent,Child);assert(candidates==1&&ld32(0x10462320)==7&&ld32(0x10462330)==0x12345678);
st32(Child+4,99);assert(!guard(Child)&&!ready(1,Parent));st32(Child+4,50);rows[50].source_valid=false;call(0x020C1D0C,Child,1);assert(executes==1&&guard(Child));rows[50].source_valid=true;
listener(Other,50,0x1CF);assert(rows.count(50)&&notices==0);listener(Parent,10,0x7FF9);active=false;assert(rows.count(50)&&guard(Child)); // orphan child survives parent retirement
state_loaded();assert(rows.count(50)&&guard(Child)); // host scope reset does not erase snapshot lineage
listener(Child,50,0x1CF);assert(rows.empty()&&notices==1);listener(Child,50,0x1CF);assert(notices==1);
active=true;st32(Parent+0x1360,0xFFFFFFFF);st32(Parent+0xC90,0);st32(Parent+0xC94,0);st8(Parent+0x1374,0);assert(ready(1,Parent));st32(Parent+0xC90,1);assert(!ready(1,Parent));st32(Parent+0xC90,0);assert(ready(1,Parent));st32(Parent+0xC94,2);assert(!ready(1,Parent));st32(Parent+0xC94,0);assert(ready(1,Parent));st8(Parent+0x1374,1);assert(!ready(1,Parent));st8(Parent+0x1374,0);assert(ready(1,Parent));
actor(Parent,10,0x7FF8,0xFFFF0000);st32(Parent+0x2D60,0xFFFFFFFF);for(unsigned innate=0;innate<2;++innate)for(unsigned pending=0;pending<3;++pending){st8(Parent+0x10B0,innate);st8(Parent+0x10B1,pending);st8(Parent+0x10B3,0);assert(ready(1,Parent));}st8(Parent+0x10B3,1);assert(!ready(1,Parent));st8(Parent+0x10B3,0);st8(Parent+0x10B1,3);assert(!ready(1,Parent));st8(Parent+0x10B1,0);st8(Parent+0x10B0,2);assert(!ready(1,Parent));actor(Parent,10,0x7FF9,0xFFFF0004);
active=true;nextPID=0xFFFFFFFF;assert(enter(1,o::Kind::Bokoblin,Parent,3));q.r[3]=0x1CF;q.r[4]=0;hooks.at(0x025D5834)(&q);leave();assert(rows.empty());
}
