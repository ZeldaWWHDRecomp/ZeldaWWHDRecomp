// SPDX-License-Identifier: MPL-2.0
#include "story_switches.h"
#include "native_actor_profiles.h"
#include "guest_addr.h"
#include "runtime.h"
#include <cmath>
#include <cstring>
#include <map>
#include <vector>
#include <utility>
#include <array>
namespace story_switches {namespace {
struct Declaration {std::string mod,id;Kind kind;uint32_t token,source=0,pid=0xFFFFFFFFu,address=0;bool ready=false,quarantined=false,committing=false;};
std::vector<Declaration> declarations;
std::map<uint32_t,uint32_t> backgrounds;
uint32_t next_token=1,sw_profile=0,door_profile=0;bool installed=false,editor_allowed=false,sw_live=false,door_live=false;
void(*original)(Cpu*,uint32_t)=nullptr;
uint32_t(*invoke)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr;
ReadFlag read_flag;WriteFlag write_flag;Retired retired;
const uint32_t SwCreate=GC(0x023975B0),SwDelete=GC(0x02397AD0),SwExecute=GC(0x02398014),SwDraw=GC(0x02398240),
    Upper=GC(0x02398608),Lower=GC(0x02398AB4),Lowering=GC(0x023988E8),Btp=GC(0x023974AC),
    DemoSwitch=GC(0x02398504),DemoPause=GC(0x023985AC),Ride=GC(0x0239705C),
    DoorCreate=GC(0x02058590),DoorDelete=GC(0x020583D4),DoorExecute=GC(0x0205834C),DoorDraw=GC(0x020582F0),
    DoorClosed=GC(0x02058144),DoorOpen=GC(0x02058230);
Declaration* get(uint32_t token){for(auto& d:declarations)if(d.token==token)return &d;return nullptr;}
bool address(uint32_t a,uint32_t bytes=0x10){return a>=0x10000000u&&a<=0x50000000u-bytes&&!(a&3);}
bool reserved(uint32_t a){return address(a)&&(ld16(a+0xE)==kSwitchProfile||ld16(a+0xE)==kDoorProfile);}
Declaration* find(uint32_t a,bool ready=false,bool allow_quarantined=false){
    if(!address(a))return nullptr;
    const uint16_t profile=ld16(a+0xE);if(profile!=kSwitchProfile&&profile!=kDoorProfile)return nullptr;
    for(auto&d:declarations)if(d.pid!=0xFFFFFFFFu&&ld32(a+4)==d.pid){
        const bool sw=d.kind==Kind::Switch;
        if(profile!=(sw?kSwitchProfile:kDoorProfile)||!address(a,sw?kSwitchClassBytes:kDoorClassBytes)||ld32(a+0xB0)!=parameters(d.kind)||ld8(a+0x2DD)!=0xFF||(d.address&&d.address!=a)||(ready&&!d.ready)||(!allow_quarantined&&d.quarantined))return nullptr;
        if(ready&&sw&&ld32(a+0x46C)!=0)return nullptr;
        return &d;
    }return nullptr;
}
bool flag(Declaration&d,bool&value){const uint32_t source=d.kind==Kind::Switch?d.token:d.source;auto*s=get(source);try{if(s&&s->kind==Kind::Switch&&s->mod==d.mod&&!s->quarantined&&read_flag&&read_flag(source,value))return true;}catch(...){}d.quarantined=true;return false;}
uint32_t call(Cpu*c,uint32_t fn,std::initializer_list<uint32_t> args){Cpu probe=*c;return invoke(&probe,fn,args);}
void deny_void(Cpu*c,uint32_t fn,uint32_t a){if(!reserved(a))original(c,fn);}
void deny(Cpu*c,uint32_t fn,uint32_t a,uint32_t result=1){if(reserved(a))c->r[3]=result;else original(c,fn);}
void btp(Cpu*c){const uint32_t a=c->r[3];auto*d=find(a);if(!d){deny_void(c,Btp,a);return;}if(d->kind!=Kind::Switch){c->r[3]=0;return;}bool on;if(flag(*d,on))stf32(a+0x3FC,on?1.f:0.f);}
void upper(Cpu*c){const uint32_t a=c->r[3];auto*d=find(a,true);if(!d){deny_void(c,Upper,a);return;}bool on;if(!flag(*d,on))return;if(!on){original(c,Upper);return;}st8(a+0x48B,0);call(c,GC(0x02398348),{a});}
void lower(Cpu*c){const uint32_t a=c->r[3];auto*d=find(a,true);if(!d){deny_void(c,Lower,a);return;}bool on;if(flag(*d,on)&&!on)original(c,Lower);}
void lowering(Cpu*c){const uint32_t a=c->r[3];auto*d=find(a,true);if(!d){deny_void(c,Lowering,a);return;}
    const float fraction=ldf32(a+0x490);if(!std::isfinite(fraction)){d->quarantined=true;return;}
    const bool settled=fraction<=0&&ld8(a+0x48B)!=0;
    if(settled){bool on;if(!flag(*d,on))return;if(!on){if(d->committing)return;d->committing=true;bool ok=false;try{ok=write_flag&&write_flag(d->token,true);}catch(...){d->committing=false;d->quarantined=true;return;}d->committing=false;if(!ok){d->quarantined=true;return;}}}
    original(c,Lowering);
}
void demo(Cpu*c,uint32_t fn){const uint32_t a=c->r[3];auto*d=find(a);if(!d){deny_void(c,fn,a);return;}if(d->kind==Kind::Switch){st32(a+0x474,0);st16(a+0x47A,0xFFFF);}}
void demo_switch(Cpu*c){demo(c,DemoSwitch);}void demo_pause(Cpu*c){demo(c,DemoPause);}
void ride(Cpu*c){const uint32_t bg=c->r[3],a=c->r[4];const auto it=backgrounds.find(bg);
    if(it==backgrounds.end()){if(!reserved(a))original(c,Ride);return;}
    auto*d=find(a,true);if(!d||d->kind!=Kind::Switch||d->token!=it->second||ld32(a+0x3BC)!=bg)return;
    original(c,Ride);
}
// Creation must expose matching current/history geometry without running a game tick.
bool initialize_lower_geometry(Cpu*c,uint32_t a){
    const uint32_t bg=ld32(a+0x3BC);if(!address(bg,0xC4)||(ld8(bg+0x6C)&0x83)!=3)return false;
    const uint32_t dzb=ld32(bg+0x94);if(!address(dzb,8))return false;
    const uint32_t count=ld32(dzb);if(!count||count>4096)return false;
    const uint32_t bytes=count*12,current=ld32(bg+0x90),previous=ld32(bg+0xC0),source=ld32(dzb+4);
    if(!address(current,bytes)||!address(previous,bytes)||!address(source,bytes))return false;
    const auto separate=[](uint32_t x,uint32_t nx,uint32_t y,uint32_t ny){return uint64_t(x)+nx<=y||uint64_t(y)+ny<=x;};
    if(!separate(current,bytes,previous,bytes)||!separate(current,bytes,source,bytes)||!separate(previous,bytes,source,bytes)||!separate(current,bytes,bg,0xC4)||!separate(previous,bytes,bg,0xC4)||!separate(current,bytes,dzb,8)||!separate(previous,bytes,dzb,8)||!separate(current,bytes,a,kSwitchClassBytes)||!separate(previous,bytes,a,kSwitchClassBytes))return false;
    std::array<uint8_t,4> indices{};for(size_t i=0;i<indices.size();++i){indices[i]=ld8(GD(0x100303E8)+uint32_t(i));if(indices[i]>=count)return false;for(size_t j=0;j<i;++j)if(indices[i]==indices[j])return false;}
    const float baseline=ldf32(a+0x3F0),top=ldf32(a+0x4AC),y=baseline+top;
    if(!std::isfinite(baseline)||!std::isfinite(top)||!std::isfinite(y))return false;
    for(uint32_t i=0;i<bytes;i+=4)if(!std::isfinite(ldf32(current+i))||!std::isfinite(ldf32(source+i)))return false;
    for(unsigned i=0;i<3;++i)if(!std::isfinite(ldf32(a+0x314+i*4)))return false;
    for(const uint8_t index:indices)stf32(current+uint32_t(index)*12+4,y);
    call(c,GC(0x024F5B08),{bg}); // initial previous positions equal the already-settled pose
    call(c,GC(0x024F43DC),{bg}); // native bounds/polygons, no Execute or pressure tick
    for(unsigned i=0;i<3;++i)st32(a+0x37C+i*4,ld32(a+0x314+i*4));
    return true;
}
void switch_create(Cpu*c){const uint32_t a=c->r[3];auto*d=find(a);if(!d){deny(c,SwCreate,a,5);return;}if(d->kind!=Kind::Switch){c->r[3]=5;return;}bool on;if(!flag(*d,on)){c->r[3]=5;return;}
    d->address=a;original(c,SwCreate);const Cpu returned=*c;
    if(returned.r[3]==4){if(ld32(a+0x46C)!=0||!address(ld32(a+0x3BC))){d->quarantined=true;c->r[3]=5;return;}
        st32(a+0x474,0);st16(a+0x47A,0xFFFF);
        if(on){for(uint32_t off:{0x490u,0x4A8u,0x49Cu,0x48Cu})stf32(a+off,0);stf32(a+0x498,-35.5f);stf32(a+0x4AC,-35.5f);call(c,GC(0x02397584),{a});if(!initialize_lower_geometry(c,a)){d->quarantined=true;c->r[3]=5;return;}}stf32(a+0x3FC,on?1.f:0.f);d->ready=true;backgrounds[ld32(a+0x3BC)]=d->token;
    }else if(returned.r[3]==5)d->quarantined=true;*c=returned;
}
void switch_execute(Cpu*c){const uint32_t a=c->r[3];auto*d=find(a,true);if(!d){deny(c,SwExecute,a);return;}
    if(!address(ld32(a+0x3BC))||ld32(a+0x474)!=0||ld16(a+0x47A)!=0xFFFF||ld32(a+0x470)>3||!std::isfinite(ldf32(a+0x490))){d->quarantined=true;c->r[3]=1;return;}
    bool on;if(!flag(*d,on)){c->r[3]=1;return;}backgrounds[ld32(a+0x3BC)]=d->token;original(c,SwExecute);
}
void switch_draw(Cpu*c){const uint32_t a=c->r[3];auto*d=find(a,true);if(!d){deny(c,SwDraw,a);return;}bool on;if(!flag(*d,on)){c->r[3]=1;return;}stf32(a+0x3FC,on?1.f:0.f);original(c,SwDraw);}
void retire(Cpu*c,uint32_t fn){const uint32_t a=c->r[3];auto*d=find(a,false,true);if(d){const uint32_t token=d->token,pid=d->pid;for(auto it=backgrounds.begin();it!=backgrounds.end();)if(it->second==token)it=backgrounds.erase(it);else ++it;d->pid=0xFFFFFFFFu;d->address=0;d->ready=false;if(retired)try{retired(token,pid);}catch(...){d->quarantined=true;}}original(c,fn);}
void switch_delete(Cpu*c){retire(c,SwDelete);}void door_delete(Cpu*c){retire(c,DoorDelete);}
void door_create(Cpu*c){const uint32_t a=c->r[3];auto*d=find(a);if(!d){deny(c,DoorCreate,a,5);return;}if(d->kind!=Kind::Door){c->r[3]=5;return;}bool on;if(!flag(*d,on)){c->r[3]=5;return;}d->address=a;original(c,DoorCreate);const Cpu returned=*c;
    if(returned.r[3]==4){if(!address(ld32(a+0x3B8))){d->quarantined=true;c->r[3]=5;return;}d->ready=true;st8(a+0x3BC,on?3:1);st16(a+0x3BE,on?0x4000:0);call(c,GC(0x02057F50),{a});call(c,GC(0x024F43DC),{ld32(a+0x3B8)});}else if(returned.r[3]==5)d->quarantined=true;*c=returned;
}
void door_closed(Cpu*c){const uint32_t a=c->r[3];auto*d=find(a,true);if(!d){deny(c,DoorClosed,a);return;}bool on;if(flag(*d,on)&&on){const uint32_t reverb=call(c,GC(0x02520540),{uint32_t(int32_t(int8_t(ld8(a+0x326))))});call(c,GC(0x025E1A40),{0x69A4,a+0x37C,0,reverb});st8(a+0x3BC,4);}c->r[3]=1;}
void door_open(Cpu*c){const uint32_t a=c->r[3];auto*d=find(a,true);if(!d){deny(c,DoorOpen,a);return;}bool on;if(flag(*d,on)&&!on)st8(a+0x3BC,2);c->r[3]=1;}
void door_execute(Cpu*c){const uint32_t a=c->r[3];auto*d=find(a,true);if(!d){deny(c,DoorExecute,a);return;}
    if(!address(ld32(a+0x3B8))||!valid_door_state(ld8(a+0x3BC),int16_t(ld16(a+0x3BE)))){d->quarantined=true;c->r[3]=1;return;}bool on;if(!flag(*d,on)){c->r[3]=1;return;}original(c,DoorExecute);
}
void door_draw(Cpu*c){const uint32_t a=c->r[3];if(!find(a,true)){deny(c,DoorDraw,a);return;}original(c,DoorDraw);}
uint32_t declare(const std::string&mod,const std::string&id,Kind kind,uint32_t source,std::string&why,bool editor=false){if((installed&&!editor)||mod.empty()||id.empty()||declarations.size()>=128||!next_token)return why="invalid native switch/door declaration",0;for(const auto&d:declarations)if(d.mod==mod&&d.id==id)return why="duplicate native switch/door",0;if(kind==Kind::Door){auto*s=get(source);if(!s||s->kind!=Kind::Switch||s->mod!=mod)return why="door must reference a same-mod switch",0;}const uint32_t token=next_token++;declarations.push_back({mod,id,kind,token,source});return token;}
}
uint32_t declare_switch(const std::string&m,const std::string&i,std::string&w){return declare(m,i,Kind::Switch,0,w);}
void editor_enable(){editor_allowed=true;}
bool live(Kind k){return installed&&(k==Kind::Switch?sw_live:door_live);}
uint32_t editor_declare_switch(const std::string&m,const std::string&i,std::string&w){if(!live(Kind::Switch)||!editor_allowed)return w="a switch appears after a restart",0;const uint32_t t=declare(m,i,Kind::Switch,0,w,true);if(t)LOG("[native_switch] %s:%s declared by the editor",m.c_str(),i.c_str());return t;}
uint32_t editor_declare_door(const std::string&m,const std::string&i,uint32_t s,std::string&w){if(!live(Kind::Door)||!editor_allowed)return w="a door appears after a restart",0;const uint32_t t=declare(m,i,Kind::Door,s,w,true);if(t)LOG("[native_door] %s:%s declared by the editor",m.c_str(),i.c_str());return t;}
bool editor_set_source(uint32_t door,uint32_t source,std::string&w){auto*d=get(door);auto*sw=get(source);if(!d||d->kind!=Kind::Door||!sw||sw->kind!=Kind::Switch||sw->mod!=d->mod)return w="door must reference a same-mod switch",false;d->source=source;return true;}
uint32_t declare_door(const std::string&m,const std::string&i,uint32_t s,std::string&w){return declare(m,i,Kind::Door,s,w);}
size_t checkpoint(){return declarations.size();}void rollback(size_t n)noexcept{if(!installed&&n<=declarations.size())declarations.resize(n);}
void bind(uint32_t token,uint32_t pid,bool restored){if(auto*d=get(token)){d->pid=pid;d->address=0;d->ready=restored;}}
bool available(uint32_t token){auto*d=get(token);return d&&!d->quarantined;}
void retry(uint32_t token){if(auto*d=get(token)){d->quarantined=false;d->committing=false;}}
void quarantine(uint32_t token){if(auto*d=get(token))d->quarantined=true;}
void state_loaded(){backgrounds.clear();for(auto&d:declarations){d.pid=0xFFFFFFFFu;d.address=0;d.ready=false;d.quarantined=false;d.committing=false;}}
void configure(void(*o)(Cpu*,uint32_t),uint32_t(*call_fn)(Cpu*,uint32_t,std::initializer_list<uint32_t>),uint32_t sw,uint32_t door,ReadFlag read,WriteFlag write,Retired notify){if(installed)return;original=o;invoke=call_fn;sw_profile=sw;door_profile=door;read_flag=std::move(read);write_flag=std::move(write);retired=std::move(notify);native_actor_profiles::configure(o,call_fn);}
bool install(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){if(installed)return true;if((declarations.empty()&&!editor_allowed)||!original||!invoke||!read_flag||!write_flag||!native_actor_profiles::available(owner))return false;
    bool sw=editor_allowed,door=editor_allowed;for(const auto&d:declarations){sw|=d.kind==Kind::Switch;door|=d.kind==Kind::Door;}
    const std::pair<uint32_t,void(*)(Cpu*)> sw_hooks[]={{SwCreate,switch_create},{SwDelete,switch_delete},{SwExecute,switch_execute},{SwDraw,switch_draw},{Upper,upper},{Lower,lower},{Lowering,lowering},{Btp,btp},{DemoSwitch,demo_switch},{DemoPause,demo_pause},{Ride,ride}};
    const std::pair<uint32_t,void(*)(Cpu*)> door_hooks[]={{DoorCreate,door_create},{DoorDelete,door_delete},{DoorExecute,door_execute},{DoorDraw,door_draw},{DoorClosed,door_closed},{DoorOpen,door_open}};
    if(sw)for(const auto&h:sw_hooks)if(!owner(h.first).empty())return false;if(door)for(const auto&h:door_hooks)if(!owner(h.first).empty())return false;
    if(sw){if(!native_actor_profiles::valid_storage(sw_profile)||ld32(GD(0x101CD034)+0x10)!=kSwitchClassBytes)return false;std::memcpy(mem::ptr(sw_profile),mem::ptr(GD(0x101CD034)),0x30);st16(sw_profile+8,kSwitchProfile);if(!native_actor_profiles::add(kSwitchProfile,sw_profile))return false;}
    if(door){if(!native_actor_profiles::valid_storage(door_profile)||ld32(GD(0x1018FFE8)+0x10)!=kDoorClassBytes)return false;std::memcpy(mem::ptr(door_profile),mem::ptr(GD(0x1018FFE8)),0x30);st16(door_profile+8,kDoorProfile);if(!native_actor_profiles::add(kDoorProfile,door_profile))return false;}
    if(!native_actor_profiles::install(owner,replace))return false;if(sw)for(const auto&h:sw_hooks)replace(h.first,h.second);if(door)for(const auto&h:door_hooks)replace(h.first,h.second);installed=true;sw_live=sw;door_live=door;return true;
}
}
