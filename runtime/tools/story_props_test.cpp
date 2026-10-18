// SPDX-License-Identifier: MPL-2.0
// Synthetic native actor lifecycle/collision contracts; no game data.
#include "mods/story_props.h"
#include "mods/native_actor_profiles.h"
#include "mods/story_items.h"
#include "runtime.h"
#include <cassert>
#include <map>
#include <cstring>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {
constexpr uint32_t Arena=0x20000000,Profile=0x60001000,Actor=Arena+0x2000,Link=Arena+0x10000,Play=Arena+0x18000;
std::map<uint32_t,void(*)(Cpu*)> hooks;unsigned originals=0,effects=0,claims=0,at=0,tg=0,co=0,lifts=0,deletes=0;bool nested=false,fail_create=false;story_items::PropClaimStatus outcome=story_items::PropClaimStatus::Committed;std::map<uint32_t,bool> markers;std::map<uint32_t,uint32_t> retired;uint32_t heap_charge=0;unsigned heap_reads=0;
constexpr uint32_t Heap=Arena+0x1C000,Vtable=Arena+0x1D000,Solid=Arena+0x1E000;
void map(uint32_t a,uint32_t size){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(a),size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(a),size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
void original(Cpu*c,uint32_t f){++originals;const uint32_t a=c->r[3];
if(f==0x024D09B8){heap_charge+=1000;st32(a+0xF4,Solid);assert(!(ld32(a+0x2E0)&0x400));st32(a+0x790,ld32(a+0xB0)>>24);st32(a+0x794,2);st32(a+0x2E0,ld32(a+0x2E0)|0x400|0x80000);st32(a+0x39C,0x90);st32(a+0x624,0xA5);st32(a+0x624+0x18,3);st32(a+0x624+0x2C,5);st32(a+0x624+0x44,a+0x754);st32(a+0x754+0xC,a);st32(a+0x754+0x10,ld32(a+4));c->r[3]=fail_create?5:4;}
else if(f==0x024D09C0){assert(!(ld32(a+0x2E0)&0x280400));st32(a+0x624,ld32(a+0x624)|1);Cpu reg=*c;reg.r[3]=Play+0x26A4;reg.r[4]=a+0x624;hooks.at(0x0200E240)(&reg);st32(a+0x39C,ld32(a+0x39C)|0x10);st32(a+0x2E0,ld32(a+0x2E0)|0x280400);c->r[3]=1;}
else if(f==0x0200E240){const uint32_t o=c->r[4];at+=(ld32(o)&1)!=0;tg+=(ld32(o+0x18)&1)!=0;co+=(ld32(o+0x2C)&1)!=0;}
else if(f==0x024CC004){++effects;if(nested){nested=false;Cpu again=*c;hooks.at(f)(&again);}}
else if(f==0x023ED0F0){++lifts;c->r[3]=1;}
else if(f==0x024D09BC){++deletes;heap_charge-=700;c->r[3]=1;}
else if(f==0x024D09C4||f==0x027F50F8)c->r[3]=1;
else if(f==0x025DD414){heap_charge+=2368;c->r[3]=Actor;}
else if(f==0x025D4B4C){Cpu native=*c;hooks.at(0x024D09BC)(&native);heap_charge-=300;c->r[3]=1;}
else if(f==0x025E10E0)c->r[3]=0xDEAD;
else assert(false);
}
uint32_t call(Cpu*c,uint32_t f,std::initializer_list<uint32_t>args){c->r[4]=0xBAD;if(f==0x025200D4)return Play;if(f==0x025E3268||f==0x025E2FBC||f==0x025EE048||f==0x025EE060){++heap_reads;return Heap;}if(f==0xF2000000){++heap_reads;return *args.begin()==Solid?512:16*1024*1024-heap_charge;}if(f==0xF2000004)return 8192;assert(false);return 0;}
void setup(uint32_t a,uint32_t token,unsigned type){st32(a+4,100+token);st16(a+0xE,story_props::kProfile);st8(a+0x2DD,0xFF);st32(a+0xB0,(type<<24)|0x007FFF3F);story_props::bind(token,100+token);Cpu c{};c.r[3]=a;hooks.at(0x024D09B8)(&c);assert(c.r[3]==4&&!(ld32(a+0x2E0)&0x280400)&&ld32(a+0x39C)==0x80);}
}
namespace story_items {
std::optional<Item> find(const std::string&mod,const std::string&id){if(mod=="test"&&id=="shard")return Item{};return {};}
PropMarkerHandle register_prop_marker(const std::string&,const std::string&,const std::string&,std::string&){static unsigned id=0;return ++id;}
bool forget_prop_marker(PropMarkerHandle){return true;}
bool prop_broken(PropMarkerHandle h){return markers[h];}
PropClaimStatus claim_prop_drops(PropMarkerHandle h,const std::string&mod,const std::vector<PropDrop>&drops,bool){assert(mod=="test"&&drops.size()==1&&drops[0].item=="shard"&&drops[0].quantity==2);++claims;if(markers[h])return PropClaimStatus::AlreadyBroken;if(outcome==PropClaimStatus::Committed)markers[h]=true;return outcome;}
}
int main(){map(Arena,0x20000);map(0x60000000,0x10000);map(0x101D0000,0x10000);map(0x10180000,0x10000);st32(0x1018CA3C,Heap);st32(Heap+0xC,Vtable);st32(Solid+0xC,Vtable);st32(Vtable+0x74,0xF2000000);st32(Vtable+0x6C,0xF2000004);
#ifdef _WIN32
_putenv_s("WWHD_MOD_ACTOR_HEAPS","1");
#else
setenv("WWHD_MOD_ACTOR_HEAPS","1",1);
#endif
for(unsigned i=0;i<0x30;++i)st8(0x101D2B1C+i,uint8_t(i+1));st32(0x101D2B1C+0x10,0x93C);std::string why;story_props::configure(original,call,Profile,[](uint32_t token,uint32_t pid){retired[token]=pid;});story_notebook::ManifestActor spec;spec.id="pot";spec.actor="pot";spec.broken_flag="broken";spec.drops.push_back({"shard",2});auto bad=spec;bad.scale.y=2;assert(!story_props::declare("test",bad,why));bad=spec;bad.drops[0].item="missing";assert(!story_props::declare("test",bad,why));uint32_t pot=story_props::declare("test",spec,why);spec.actor="barrel";spec.id="barrel";spec.broken_flag="barrel.broken";uint32_t barrel=story_props::declare("test",spec,why);spec.actor="crate";spec.id="crate";spec.broken_flag="crate.broken";uint32_t crate=story_props::declare("test",spec,why);assert(pot==1&&barrel==2&&crate==3);spec.actor="pot";spec.id="textured";spec.broken_flag="textured.broken";spec.texture_token=1;const uint32_t textured=story_props::declare("test",spec,why);assert(textured==4);auto replace=[](uint32_t a,void(*f)(Cpu*)){hooks[a]=f;};auto owner=[](uint32_t){return std::string();};assert(!story_props::install([](uint32_t a){return a==0x0200E240?"foreign":"";},replace)&&hooks.empty());assert(!story_props::install([](uint32_t a){return a==0x023ED0F0?"foreign":"";},replace)&&hooks.empty());assert(story_props::install(owner,replace)&&hooks.size()==11);for(unsigned i=0;i<0x30;++i)if(i!=8&&i!=9)assert(ld8(Profile+i)==ld8(0x101D2B1C+i));assert(ld16(Profile+8)==0x7FFD&&ld32(Profile+0x10)==0x93C);st16(Profile+0x88,0x7FFE);assert(native_actor_profiles::add(0x7FFE,Profile+0x80));Cpu c{};c.r[3]=0x7FFE;hooks.at(0x025E10E0)(&c);assert(c.r[3]==Profile+0x80);c.r[3]=0x1C5;hooks.at(0x025E10E0)(&c);assert(c.r[3]==0xDEAD);
c={};c.r[3]=story_props::kProfile;c.r[4]=99;c.f[1].ps0=7.5;Cpu allocation_before=c;hooks.at(0x025DD414)(&c);allocation_before.r[3]=Actor;assert(std::memcmp(&allocation_before,&c,sizeof c)==0&&heap_charge==2368);
setup(Actor,pot,0);setup(Actor+0x1000,barrel,3);setup(Actor+0x2000,crate,15);
c={};c.r[3]=Actor;c.r[4]=9;c.f[1].ps0=2.5;hooks.at(0x024D09C0)(&c);assert(c.r[3]==1&&c.r[4]==9&&c.f[1].ps0==2.5&&at==0&&tg==1&&co==1&&ld32(Actor+0x624)==0xA4&&ld32(Actor+0x39C)==0x80&&!(ld32(Actor+0x2E0)&0x280400));
// Retail/unregistered objects remain native, even with identical Tsubo layout.
const uint32_t retail=Arena+0x7000;st32(retail,7);st32(retail+0x18,1);st32(retail+0x2C,1);c.r[3]=Play+0x26A4;c.r[4]=retail;Cpu before=c;hooks.at(0x0200E240)(&c);assert(at==1&&std::memcmp(&before,&c,sizeof c)==0&&ld32(retail)==7);
// Stale status owner identity never admits a mask to an unrelated object.
st32(Actor+0x624,7);st32(Actor+0x754+0x10,999);c.r[4]=Actor+0x624;hooks.at(0x0200E240)(&c);assert(ld32(Actor+0x624)==7&&at==1&&!story_props::available(pot));story_props::retry(pot);st32(Actor+0x754+0x10,101);c.r[3]=Actor;hooks.at(0x024D09C0)(&c);assert(!(ld32(Actor+0x624)&1));
st32(Play+0x5B34,Link);st32(Link+0x65B8,Actor);c={};c.r[3]=Link;c.r[4]=123;c.f[2].ps0=4.5;before=c;hooks.at(0x023ED0F0)(&c);before.r[3]=0;assert(lifts==0&&std::memcmp(&before,&c,sizeof c)==0);st32(Link+0x65B8,retail);c.r[3]=Link;hooks.at(0x023ED0F0)(&c);assert(lifts==1);
// Reserved private-profile corruption cannot fall into retail item/attack behavior.
st32(Actor+0xB0,0);c={};c.r[3]=Actor;unsigned calls_before=originals;hooks.at(0x024CC004)(&c);assert(originals==calls_before&&claims==0&&effects==0);hooks.at(0x024D09C0)(&c);assert(originals==calls_before&&c.r[3]==1);st32(Actor+0xB0,0x007FFF3F);
const uint32_t unknown=Arena+0x8000;st16(unknown+0xE,story_props::kProfile);st32(unknown+4,999);c.r[3]=unknown;hooks.at(0x024D09B8)(&c);assert(c.r[3]==5&&originals==calls_before);
c={};c.r[3]=Actor;nested=true;hooks.at(0x024CC004)(&c);hooks.at(0x024CC004)(&c);assert(claims==1&&effects==1&&!story_props::available(pot));c.r[3]=Actor;hooks.at(0x024D09BC)(&c);assert(deletes==1&&claims==1);assert(retired[pot]==101);c.r[3]=Actor+0x2000;hooks.at(0x024D09BC)(&c);assert(retired[crate]==103);const auto old_claims=claims,old_effects=effects;c.r[3]=Actor+0x2000;hooks.at(0x024CC004)(&c);assert(claims==old_claims&&effects==old_effects);
outcome=story_items::PropClaimStatus::StackFull;c.r[3]=Actor+0x1000;hooks.at(0x024CC004)(&c);assert(claims==2&&effects==2&&!story_props::available(barrel)&&!markers[2]);story_props::retry(barrel);assert(story_props::available(barrel));
// Restore clears host admission map/latches and rebind requires the restored native identity.
story_props::state_loaded();const uint32_t textured_actor=Actor+0x3000;st32(textured_actor+4,104);st16(textured_actor+0xE,story_props::kProfile);st32(textured_actor+0xB0,0x007FFF3F);st8(textured_actor+0x2DD,0xFF);story_props::bind(textured,104);c={};c.r[3]=textured_actor;const unsigned before_textured_draw=originals;hooks.at(0x024D09C4)(&c);assert(c.r[3]==1&&originals==before_textured_draw);c.r[3]=retail;hooks.at(0x024D09C4)(&c);assert(originals==before_textured_draw+1);assert(story_props::available(barrel)&&!story_props::available(pot));story_props::bind(barrel,102);c.r[3]=Actor+0x1000;hooks.at(0x024D09C0)(&c);outcome=story_items::PropClaimStatus::Committed;c.r[3]=Actor+0x1000;hooks.at(0x024CC004)(&c);assert(claims==3&&markers[2]);
story_props::bind(crate,103);fail_create=true;c.r[3]=Actor+0x2000;hooks.at(0x024D09B8)(&c);assert(c.r[3]==5&&!story_props::available(crate));fail_create=false;story_props::retry(crate);assert(story_props::available(crate));st32(Actor+0x2000+0x2E0,0x2000);c.r[3]=Actor+0x2000;const unsigned old=originals;hooks.at(0x024D09C0)(&c);assert(originals==old&&c.r[3]==1&&!story_props::available(crate));c={};c.r[3]=Actor+0x1000;c.r[4]=321;c.f[2].ps0=9.5;Cpu deletion_before=c;hooks.at(0x025D4B4C)(&c);deletion_before.r[3]=1;assert(std::memcmp(&deletion_before,&c,sizeof c)==0&&heap_reads>40);
}
