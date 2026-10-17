// SPDX-License-Identifier: MPL-2.0
#include "native_actor_carry.h"
#include "guest_addr.h"
#include "runtime.h"
#include <array>
namespace native_actor_carry { namespace {
struct Entry {std::string name;Guard guard=nullptr;DeniedTrace trace=nullptr;};
std::array<Entry,8> guards{};
void(*original)(Cpu*,uint32_t)=nullptr;
uint32_t(*invoke)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr;
bool installed=false;
const uint32_t Lift=GC(0x023ED0F0);
void lift(Cpu*c){
    const uint32_t link=c->r[3];
    if(link>=0x10000000u&&link<=0x50000000u-0x65BCu&&!(link&3)){
        Cpu probe=*c;const uint32_t play=invoke(&probe,GC(0x025200D4),{});
        if(play>=0x10000000u&&play<=0x50000000u-0x5B38u&&!(play&3)&&ld32(play+0x5B34)==link){
            const uint32_t candidate=ld32(link+0x65B8);
            for(const auto&e:guards)if(e.guard){
                bool denied=true;try{denied=e.guard(candidate);}catch(...){/* predicate faults cannot admit a lift */}
                if(denied){if(e.trace)try{e.trace(candidate);}catch(...){}c->r[3]=0;return;}
            }
        }
    }
    original(c,Lift);
}
}
bool configure(void(*o)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>)){
    if(!o||!call)return false;
    if(original&&(original!=o||invoke!=call))return false;
    original=o;invoke=call;return true;
}
bool register_guard(const std::string& name,Guard fn,DeniedTrace trace){
    if(name.empty()||name.size()>64||!fn)return false;
    for(const auto&e:guards)if(e.guard&&e.name==name)return e.guard==fn&&e.trace==trace;
    for(const auto&e:guards)if(e.guard==fn&&e.name!=name)return false;
    if(installed)return false;
    for(auto&e:guards)if(!e.guard){e={name,fn,trace};return true;}
    return false;
}
bool available(const std::function<std::string(uint32_t)>&owner){return original&&invoke&&(installed||owner(Lift).empty());}
bool install(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){
    if(installed)return true;
    if(!available(owner))return false;
    bool any=false;for(const auto&e:guards)any|=e.guard!=nullptr;if(!any)return false;
    replace(Lift,lift);installed=true;return true;
}
}
