// SPDX-License-Identifier: MPL-2.0
#include "native_actor_profiles.h"
#include "guest_addr.h"
#include "runtime.h"
#include <array>
#include <cstdlib>
#include <cstring>
namespace native_actor_profiles { namespace {
struct Entry {uint16_t id=0;uint32_t address=0;BaseAllocated allocated=nullptr;};
std::array<Entry,10> entries{};
struct Listener {std::string name;BeforeDelete callback=nullptr;};
std::array<Listener,8> listeners{};
void(*original)(Cpu*,uint32_t)=nullptr;uint32_t(*invoke)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr;bool installed=false;
bool tracing(){const char*v=std::getenv("WWHD_MOD_ACTOR_HEAPS");return v&&std::strcmp(v,"1")==0;}
bool reserved(uint32_t id){return id>=0x7FF5&&id<=0x7FFE;}
const Entry* entry(uint32_t id){for(const auto&e:entries)if(e.address&&id==e.id)return &e;return nullptr;}
uint32_t free(Cpu*c,uint32_t heap){if(!heap)return 0;const uint32_t vt=ld32(heap+0xC),fn=vt?ld32(vt+0x74):0;return fn?invoke(c,fn,{heap}):0;}
using Heaps=std::array<std::pair<uint32_t,uint32_t>,4>;
Heaps heaps(Cpu*c){Cpu probe=*c;Heaps out{};const uint32_t getters[]={GC(0x025E3268),GC(0x025E2FBC),GC(0x025EE048),GC(0x025EE060)};for(unsigned i=0;i<4;++i){out[i].first=invoke(&probe,getters[i],{});out[i].second=free(&probe,out[i].first);}return out;}
void lookup(Cpu*c){if(const auto*e=entry(c->r[3]))c->r[3]=e->address;else if(reserved(c->r[3]))c->r[3]=0;else original(c,GC(0x025E10E0));}
void base_create(Cpu*c){const auto*e=entry(c->r[3]);if(!e&&reserved(c->r[3])){c->r[3]=0;return;}const bool measure=e&&invoke&&(tracing()||e->allocated);if(!measure){original(c,GC(0x025DD414));return;}
    const uint32_t params=c->r[5],token=params?ld32(params):0,heap=ld32(GD(0x1018CA3C));Cpu probe=*c;const uint32_t before=free(&probe,heap);
    original(c,GC(0x025DD414));const Cpu returned=*c;probe=returned;const uint32_t after=free(&probe,heap);
    if(returned.r[3]&&e->allocated)e->allocated(token,returned.r[3],before>=after?before-after:0);
    if(tracing())LOG("[mod_actor_heaps] actor=%08X profile=%04X stage=actor_base heap=%08X free_before=%u free_after=%u delta=%lld",returned.r[3],e->id,heap,before,after,static_cast<long long>(before)-after);
    *c=returned;
}
void destroy(Cpu*c){const uint32_t actor=c->r[3];const bool own=invoke&&tracing()&&actor>=0x10000000&&actor<=0x50000000-0x10&&entry(ld16(actor+0xE));const auto before=own?heaps(c):Heaps{};
    if(actor>=0x10000000&&actor<=0x50000000-0x10){const uint32_t pid=ld32(actor+4);const uint16_t profile=ld16(actor+0xE);for(const auto& listener:listeners)if(listener.callback)listener.callback(actor,pid,profile);}
    original(c,GC(0x025D4B4C));const Cpu returned=*c;
    if(own){const auto after=heaps(c);const char*names[]={"archive","game","zelda","command"};for(unsigned i=0;i<4;++i)LOG("[mod_actor_heaps] actor=%08X stage=actor_delete heap=%s pointer=%08X free_before=%u free_after=%u delta=%lld",actor,names[i],after[i].first,before[i].second,after[i].second,static_cast<long long>(before[i].second)-after[i].second);}
    *c=returned;
}
}
void configure(void(*fn)(Cpu*,uint32_t),uint32_t(*calls)(Cpu*,uint32_t,std::initializer_list<uint32_t>)){if(!installed){original=fn;invoke=calls;}}
bool valid_storage(uint32_t a){return a>=mem::kRuntimeStart&&a<=mem::kHostStart-0x30&&!(a&3);}
bool available(const Owner&owner){return original&&(installed||(owner(GC(0x025E10E0)).empty()&&owner(GC(0x025DD414)).empty()&&owner(GC(0x025D4B4C)).empty()));}
bool add(uint16_t id,uint32_t address,BaseAllocated allocated){if(!reserved(id)||!valid_storage(address)||ld16(address+8)!=id)return false;for(auto&e:entries)if(e.id==id&&e.address){if(e.address!=address||(e.allocated&&allocated&&e.allocated!=allocated))return false;if(allocated)e.allocated=allocated;return true;}for(const auto&e:entries)if(e.address==address)return false;for(auto&e:entries)if(!e.address){e={id,address,allocated};return true;}return false;}
bool add_before_delete_listener(const std::string& name,BeforeDelete callback){if(name.empty()||!callback)return false;for(const auto& listener:listeners){if(listener.name==name)return listener.callback==callback;if(listener.callback==callback)return false;}if(installed)return false;for(auto& listener:listeners)if(!listener.callback){listener={name,callback};return true;}return false;}
bool install(const Owner&owner,const Replace&replace){if(installed)return true;if(!available(owner))return false;bool any=false;for(const auto&e:entries)any|=e.address!=0;if(!any)return false;replace(GC(0x025E10E0),lookup);replace(GC(0x025DD414),base_create);replace(GC(0x025D4B4C),destroy);installed=true;return true;}
}
