// SPDX-License-Identifier: MPL-2.0
#include "mod_actor_attention.h"
#include "guest_addr.h"
#include "runtime.h"
#include <cmath>
namespace mod_actor::attention { namespace {
const uint32_t CalcWeight=GC(0x024EB2D4);
void(*original)(Cpu*,uint32_t)=nullptr;Verify verify=nullptr;bool installed=false;
bool actor_range(uint32_t a,uint32_t n){return !(a&3)&&a>=mem::kMem2Start&&a<=mem::kMem2End-n;}
bool output_range(uint32_t a){return !(a&3)&&((a>=mem::kMem2Start&&a<=mem::kMem2End-4)||(a>=mem::kRuntimeStart&&a<=mem::kHostStart-4));}
void weight(Cpu*c){
    // dAttention_c::calcWeight: r4 list kind, r5 candidate, r8 u32 outType.
    // Native code writes the chosen type only on a positive eligible weight.
    const uint32_t list=c->r[4],actor=c->r[5],out=c->r[8];
    original(c,CalcWeight);
    const double native=c->f[1].ps0;
    if(list!='L'||!actor_range(actor,0x3AC)||!output_range(out)||!std::isfinite(native)||native<=0||ld32(out)!=2)return;
    Priority p=Priority::None;try{p=verify(actor);}catch(...){return;}
    if(p!=Priority::Low&&p!=Priority::High)return;
    // Native list insertion prefers lower weights. Eligibility/range remains
    // entirely native; never mutate the shared distance table or actor group.
    const float scaled=static_cast<float>(native*(p==Priority::Low?2.0:0.5));
    if(std::isfinite(scaled)&&scaled>0)c->f[1].ps0=scaled;
}
}
bool configure(void(*o)(Cpu*,uint32_t),Verify v){if(!o||!v||(original&&(original!=o||verify!=v)))return false;original=o;verify=v;return true;}
bool available(const std::function<std::string(uint32_t)>&owner){return original&&verify&&(installed||owner(CalcWeight).empty());}
bool install(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){if(installed)return true;if(!available(owner))return false;replace(CalcWeight,weight);installed=true;return true;}
}
