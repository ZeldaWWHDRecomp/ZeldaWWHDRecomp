// SPDX-License-Identifier: MPL-2.0
#include "native_actor_refusal.h"
#include "story_props.h"
#include "native_actor_profiles.h"
#include "native_actor_carry.h"
#include "story_items.h"
#include "guest_addr.h"
#include "runtime.h"
#include <cstring>
#include <cstdlib>
#include <map>
#include <vector>
#include <cmath>
#include <array>
#include <algorithm>
namespace story_props {namespace {
using HeapSnapshot=std::array<std::pair<uint32_t,uint32_t>,4>;
struct Declaration {std::string mod;story_notebook::ManifestActor spec;story_items::PropMarkerHandle marker=0;std::vector<story_items::PropDrop> drops;uint32_t process=0xFFFFFFFFu;bool quarantined=false,breaking=false,broken=false,heap_started=false,texture_pending=false;HeapSnapshot heap_before{};};
std::vector<Declaration> declarations;std::map<uint32_t,uint32_t> cylinders;
void(*original)(Cpu*,uint32_t)=nullptr;uint32_t(*invoke)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr;
uint32_t profile=0;bool installed=false,carry_bridge=false;std::function<void(uint32_t,uint32_t)> retired;
TextureApply texture_apply;
const uint32_t create=GC(0x024D09B8),destroy=GC(0x024D09BC),execute=GC(0x024D09C0),draw=GC(0x024D09C4),model_texture_update=GC(0x027F50F8),damaged=GC(0x024CC004),set=GC(0x0200E240);
struct DrawScope {uint32_t actor=0,model=0,pid=0;Declaration* declaration=nullptr;};
thread_local DrawScope active_draw;
bool heap_trace(){const char*v=std::getenv("WWHD_MOD_ACTOR_HEAPS");return v&&std::strcmp(v,"1")==0;}
uint32_t heap_value(Cpu*c,uint32_t heap,uint32_t slot){if(!heap)return 0;const uint32_t vt=ld32(heap+0xC),fn=vt?ld32(vt+slot):0;return fn?invoke(c,fn,{heap}):0;}
HeapSnapshot heaps(Cpu*c){Cpu probe=*c;HeapSnapshot out{};const uint32_t getter[]={GC(0x025E3268),GC(0x025E2FBC),GC(0x025EE048),GC(0x025EE060)};for(unsigned i=0;i<4;++i){out[i].first=invoke(&probe,getter[i],{});out[i].second=heap_value(&probe,out[i].first,0x74);}return out;}
void heap_report(const Declaration&d,uint32_t a,const char*stage,const HeapSnapshot&before,const HeapSnapshot&after){const char*names[]={"archive","game","zelda","command"};for(unsigned i=0;i<4;++i)LOG("[native_prop_heaps] %s:%s actor=%08X stage=%s heap=%s pointer=%08X free_before=%u free_after=%u delta=%lld",d.mod.c_str(),d.spec.id.c_str(),a,stage,names[i],after[i].first,before[i].second,after[i].second,static_cast<long long>(before[i].second)-after[i].second);}

bool address(uint32_t a){return a>=0x10000000&&a<=0x50000000-kClassBytes&&!(a&3);}
bool private_heap(uint32_t actor,uint32_t& begin,uint32_t& end){
    begin=end=0;const uint32_t heap=ld32(actor+0xF4);
    if(heap<mem::kMem2Start||heap>mem::kMem2End-0x24||(heap&3))return false;
    const uint32_t vt=ld32(heap+0xC);
    if(vt!=GD(0x10145B38)||ld32(vt+0x5C)!=GC(0x02755280)||ld32(vt+0x6C)!=GC(0x02755298)||ld32(vt+0x64)!=GC(0x02755288))return false;
    const uint32_t start=ld32(heap+0x1C),size=ld32(heap+0x20);
    if(size<0x9C||size>0x200000||start<mem::kMem2Start||uint64_t(start)+size>mem::kMem2End)return false;
    begin=start+0x9C;end=start+size;
    const uint32_t model=ld32(actor+0x3B4);
    return model>=begin&&uint64_t(model)+0x144<=end;
}
bool apply_texture(Cpu*c,Declaration&d,uint32_t actor,bool ready_only=false){
    if(!d.spec.texture_token)return true;
    uint32_t begin=0,end=0;if(!c||!texture_apply||!private_heap(actor,begin,end))return false;
    return texture_apply(c,uint32_t(&d-declarations.data())+1,actor,d.process,d.spec.texture_token,begin,end,ready_only);
}
bool reserved(uint32_t a){return address(a)&&ld16(a+0xE)==kProfile;}
unsigned type(const story_notebook::ManifestActor&s){return s.actor=="pot"?0:s.actor=="barrel"?3:15;}
Declaration* find(uint32_t a,bool initialized=true){
    if(!address(a)||ld16(a+0xE)!=kProfile)return nullptr;
    const uint32_t pid=ld32(a+4);for(auto&d:declarations)if(d.process!=0xFFFFFFFFu&&d.process==pid){
        if(ld32(a+0xB0)!=parameters(d.spec)||ld8(a+0x2DD)!=0xFF||(initialized&&ld32(a+0x790)!=type(d.spec)))return nullptr;return &d;
    }return nullptr;
}
void trace(const Declaration&d,uint32_t a,const char*event,unsigned value=0){
    const char*e=std::getenv("WWHD_NATIVE_ACTOR_TRACE");if(!e||std::strcmp(e,"1"))return;
    static std::array<unsigned,128> collision_counts{};static unsigned collisions=0,lifecycle=0;
    if(std::strcmp(event,"at-denied")==0){const unsigned n=++collision_counts[size_t(&d-declarations.data())];if((n>4&&n%256)||collisions>=512)return;++collisions;}else {if(lifecycle>=512)return;++lifecycle;}
    LOG("[native_prop] %s:%s event=%s actor=%08X pid=%08X profile=%04X type=%u params=%08X value=%u",d.mod.c_str(),d.spec.id.c_str(),event,a,d.process,unsigned(ld16(a+0xE)),ld32(a+0x790),ld32(a+0xB0),value);
}
void mask(uint32_t a){st32(a+0x39C,ld32(a+0x39C)&~0x10u);st32(a+0x2E0,ld32(a+0x2E0)&~(0x400u|0x80000u|0x200000u));st32(a+0x624,ld32(a+0x624)&~1u);}
void remember(Declaration*d,uint32_t a){cylinders[a+0x624]=uint32_t(d-declarations.data())+1;}
void create_hook(Cpu*c){const uint32_t a=c->r[3];Declaration*d=find(a,false);if(!d){if(reserved(a)){native_actor_refusal::refuse(c,a,invoke);return;}original(c,create);return;}
    if(heap_trace()&&!d->heap_started){d->heap_before=heaps(c);d->heap_started=true;}
    st32(a+0x2E0,ld32(a+0x2E0)&~(0x400u|0x80000u|0x200000u));original(c,create);const Cpu returned=*c;
    if(c->r[3]==4){if(find(a)!=d||!apply_texture(c,*d,a)){d->quarantined=true;c->r[3]=5;return;}d->texture_pending=false;mask(a);remember(d,a);trace(*d,a,"created");
        if(heap_trace()){heap_report(*d,a,"native_create",d->heap_before,heaps(c));Cpu probe=*c;const uint32_t solid=ld32(a+0xF4),size=heap_value(&probe,solid,0x6C),free=heap_value(&probe,solid,0x74);LOG("[native_prop_heaps] %s:%s actor=%08X stage=solid heap=%08X size=%u free=%u used=%u",d->mod.c_str(),d->spec.id.c_str(),a,solid,size,free,size>=free?size-free:0);}
    }else if(returned.r[3]==5){d->quarantined=true;trace(*d,a,"create-failed");}*c=returned;
}
void execute_hook(Cpu*c){const uint32_t a=c->r[3];Declaration*d=find(a);if(!d){if(reserved(a)){c->r[3]=1;return;}original(c,execute);return;}
    // Never claim that clearing a status bit releases a native player's held pointer.
    if((ld32(a+0x2E0)&0x102000u)||ld32(a+0x794)==4){d->quarantined=true;c->r[3]=1;trace(*d,a,"unsupported-carried");return;}
    if(d->texture_pending){if(!apply_texture(c,*d,a)){d->quarantined=true;c->r[3]=1;trace(*d,a,"texture-restore-rejected");return;}d->texture_pending=false;}
    mask(a);remember(d,a);original(c,execute);const Cpu returned=*c;if(find(a)==d)mask(a);*c=returned;
}
void model_texture_update_hook(Cpu*c){
    const uint32_t model=c->r[3];
    // Native viewCalc refreshes every private material reference. Its caller
    // submits those materials immediately after this synchronous call returns.
    original(c,model_texture_update);const Cpu returned=*c;
    const DrawScope scope=active_draw;
    // The native refresh has restored its own original references. If the
    // ready-only rebind fails, that frame uses the native original material;
    // quarantine suppresses subsequent owned Draw calls until a safe retry.
    if(scope.declaration&&model==scope.model&&scope.actor&&
       find(scope.actor)==scope.declaration&&ld32(scope.actor+4)==scope.pid&&
       ld32(scope.actor+0x3B4)==model&&!scope.declaration->quarantined&&
       !apply_texture(c,*scope.declaration,scope.actor,true)){
        scope.declaration->quarantined=true;
        trace(*scope.declaration,scope.actor,"texture-draw-rejected");
    }
    *c=returned;
}
void draw_hook(Cpu*c){const uint32_t a=c->r[3];Declaration*d=find(a,false);
    if(!d){if(reserved(a)){c->r[3]=1;return;}original(c,draw);return;}
    // A loaded state can reach Draw before its first safe Execute. Never let a
    // private model submit a restored descriptor until its own cache rebinds.
    if(d->spec.texture_token&&(d->quarantined||d->texture_pending)){c->r[3]=1;return;}
    if(d->spec.texture_token){
        if(!apply_texture(c,*d,a,true)){d->quarantined=true;c->r[3]=1;trace(*d,a,"texture-draw-preflight-rejected");return;}
        struct RestoreScope{DrawScope old;~RestoreScope(){active_draw=old;}} restore{active_draw};
        active_draw={a,ld32(a+0x3B4),d->process,d};
        original(c,draw);return;
    }
    original(c,draw);
}
void delete_hook(Cpu*c){const uint32_t a=c->r[3];Declaration*d=find(a,false);const bool measure=d&&heap_trace();const auto before=measure?heaps(c):HeapSnapshot{};if(d){cylinders.erase(a+0x624);trace(*d,a,"delete");const uint32_t old_pid=d->process,token=uint32_t(d-declarations.data())+1;d->process=0xFFFFFFFFu;if(retired)retired(token,old_pid);}if(native_actor_refusal::base_only(a)){c->r[3]=1;return;}original(c,destroy);const Cpu returned=*c;if(measure)heap_report(*d,a,"resource_delete",before,heaps(c));*c=returned;}
void break_hook(Cpu*c){const uint32_t a=c->r[3];Declaration*d=find(a);if(!d){if(reserved(a))return;original(c,damaged);return;}if(d->breaking||d->broken)return;d->breaking=true;d->broken=true;
    const auto outcome=story_items::claim_prop_drops(d->marker,d->mod,d->drops);
    if(outcome!=story_items::PropClaimStatus::Committed&&outcome!=story_items::PropClaimStatus::AlreadyBroken)d->quarantined=true;
    trace(*d,a,"break-claim",unsigned(outcome));
    try{original(c,damaged);}catch(...){d->breaking=false;throw;}d->breaking=false;
}
void set_hook(Cpu*c){const uint32_t obj=c->r[4];const auto it=cylinders.find(obj);if(it!=cylinders.end()&&it->second&&it->second<=declarations.size()){
    auto&d=declarations[it->second-1];const uint32_t a=obj-0x624;
    if(find(a)==&d&&ld32(obj+0x44)==a+0x754&&ld32(a+0x754+0xC)==a&&ld32(a+0x754+0x10)==d.process){const uint32_t before=ld32(obj);st32(obj,before&~1u);trace(d,a,"at-denied",before);}
    else if(reserved(a)){d.quarantined=true;return;}
    else cylinders.erase(it);
}original(c,set);}
bool deny_lift(uint32_t target){return find(target)!=nullptr||reserved(target);}
void trace_lift(uint32_t target){if(auto*d=find(target))trace(*d,target,"lift-denied");}
}
bool is_prop(const story_notebook::ManifestActor&s){return s.actor=="pot"||s.actor=="barrel"||s.actor=="crate";}
uint32_t parameters(const story_notebook::ManifestActor&s){return (type(s)<<24)|0x007FFF3Fu;}
namespace {bool editor_allowed=false;}
static bool prop_spec_ok(const std::string&mod,const story_notebook::ManifestActor&s,std::vector<story_items::PropDrop>&drops,std::string&why){
    if(!is_prop(s)||s.broken_flag.empty()||s.drops.size()>8)return why="invalid native prop declaration",false;
    if(!std::isfinite(s.position.x)||!std::isfinite(s.position.y)||!std::isfinite(s.position.z)||!std::isfinite(s.rotation.y)||s.rotation.x!=0||s.rotation.z!=0||!std::isfinite(s.scale.x)||s.scale.x<.1f||s.scale.x>4||s.scale.x!=s.scale.y||s.scale.x!=s.scale.z||!s.kind.empty()||!s.model.empty()||!s.lines.empty()||!s.then.empty()||!s.flag.empty()||s.params||!s.path.points.empty()||s.look_at.enabled)return why="unsupported native prop settings",false;
    drops.clear();
    for(const auto&drop:s.drops){if(!drop.quantity||drop.quantity>254||!story_items::find(mod,drop.item))return why="prop names an unavailable same-mod item",false;for(const auto&old:drops)if(old.item==drop.item)return why="duplicate prop item",false;drops.push_back({drop.item,drop.quantity});}
    return true;
}
void editor_enable(){editor_allowed=true;}
bool live(){return installed;}
uint32_t editor_declare(const std::string&mod,const story_notebook::ManifestActor&s,std::string&why){
    if(!installed||!editor_allowed)return why="a pot, barrel or crate appears after a restart",0;
    if(declarations.size()>=128)return why="128 props were declared in this session: a restart makes room",0;
    if(s.texture_token)return why="a prop with its own texture appears after a restart",0;
    std::vector<story_items::PropDrop> drops;if(!prop_spec_ok(mod,s,drops,why))return 0;
    const auto marker=story_items::register_prop_marker(mod,s.id,s.broken_flag,why);if(!marker)return 0;
    declarations.push_back({mod,s,marker,std::move(drops)});LOG("[native_prop] %s:%s declared by the editor (token %zu)",mod.c_str(),s.id.c_str(),declarations.size());return uint32_t(declarations.size());
}
bool editor_redeclare(uint32_t token,const std::string&mod,const story_notebook::ManifestActor&s,std::string&why){
    if(!installed||!editor_allowed||!token||token>declarations.size())return why="the prop takes this change after a restart",false;
    auto&d=declarations[token-1];if(d.mod!=mod)return why="not this mod's prop",false;
    if(d.process!=0xFFFFFFFFu)return why="the prop is still in the game",false;
    if(s.texture_token!=d.spec.texture_token)return why="a prop's texture changes after a restart",false;
    std::vector<story_items::PropDrop> drops;if(!prop_spec_ok(mod,s,drops,why))return false;
    auto marker=d.marker;
    if(s.broken_flag!=d.spec.broken_flag||s.id!=d.spec.id){story_items::forget_prop_marker(d.marker);marker=story_items::register_prop_marker(mod,s.id,s.broken_flag,why);if(!marker){d.marker=story_items::register_prop_marker(mod,d.spec.id,d.spec.broken_flag,why);return false;}}
    d.spec=s;d.marker=marker;d.drops=std::move(drops);d.quarantined=false;d.breaking=d.broken=false;return true;
}
uint32_t declare(const std::string&mod,const story_notebook::ManifestActor&s,std::string&why){
    if(installed||!is_prop(s)||declarations.size()>=128||s.broken_flag.empty()||s.drops.size()>8)return why="invalid native prop declaration",0;
    if(!std::isfinite(s.position.x)||!std::isfinite(s.position.y)||!std::isfinite(s.position.z)||!std::isfinite(s.rotation.y)||s.rotation.x!=0||s.rotation.z!=0||!std::isfinite(s.scale.x)||s.scale.x<.1f||s.scale.x>4||s.scale.x!=s.scale.y||s.scale.x!=s.scale.z||!s.kind.empty()||!s.model.empty()||!s.lines.empty()||!s.then.empty()||!s.flag.empty()||s.params||!s.path.points.empty()||s.look_at.enabled)return why="unsupported native prop settings",0;
    std::vector<story_items::PropDrop> drops;
    for(const auto&drop:s.drops){if(!drop.quantity||drop.quantity>254||!story_items::find(mod,drop.item))return why="prop names an unavailable same-mod item",0;for(const auto&old:drops)if(old.item==drop.item)return why="duplicate prop item",0;drops.push_back({drop.item,drop.quantity});}
    const auto marker=story_items::register_prop_marker(mod,s.id,s.broken_flag,why);if(!marker)return 0;
    declarations.push_back({mod,s,marker,std::move(drops)});return uint32_t(declarations.size());
}
size_t checkpoint(){return declarations.size();}
void rollback(size_t count) noexcept{if(!installed&&count<=declarations.size())declarations.resize(count);}
void bind(uint32_t token,uint32_t pid){if(token&&token<=declarations.size()){auto&d=declarations[token-1];if(d.process!=pid)d.heap_started=false;d.process=pid;}}
bool available(uint32_t token){return token&&token<=declarations.size()&&!declarations[token-1].quarantined&&!story_items::prop_broken(declarations[token-1].marker);}
void retry(uint32_t token){if(token&&token<=declarations.size()){auto&d=declarations[token-1];d.quarantined=false;d.breaking=false;d.broken=false;}}
void quarantine(uint32_t token){if(token&&token<=declarations.size()){auto&d=declarations[token-1];d.process=0xFFFFFFFFu;d.quarantined=true;}}
void state_loaded(){cylinders.clear();for(auto&d:declarations){d.process=0xFFFFFFFFu;d.heap_started=false;d.breaking=false;d.broken=false;d.quarantined=false;d.texture_pending=d.spec.texture_token!=0;}}
void set_texture_apply(TextureApply apply){if(!installed)texture_apply=std::move(apply);}
bool texture_heap(uint32_t actor,uint32_t& begin,uint32_t& end){return address(actor)&&private_heap(actor,begin,end);}
void configure(void(*o)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>),uint32_t guest,std::function<void(uint32_t,uint32_t)> notify){original=o;invoke=call;profile=guest;retired=std::move(notify);native_actor_profiles::configure(o,call);carry_bridge=native_actor_carry::configure(o,call);if(!declarations.empty()||editor_allowed)native_actor_carry::register_guard("props",deny_lift,trace_lift);}
bool install(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){if(installed)return true;if((declarations.empty()&&!editor_allowed)||!original||!invoke){LOG("[native_prop] install failed: declarations or native bridge unavailable");return false;}if(!native_actor_profiles::valid_storage(profile)){LOG("[native_prop] install failed: profile storage %08X outside snapshot runtime region",profile);return false;}if(!native_actor_profiles::available(owner)){LOG("[native_prop] install failed: shared native profile hook conflict");return false;}
    std::vector<std::pair<uint32_t,void(*)(Cpu*)>> hooks={{create,create_hook},{destroy,delete_hook},{execute,execute_hook},{damaged,break_hook},{set,set_hook}};
    if(std::any_of(declarations.begin(),declarations.end(),[](const Declaration&d){return d.spec.texture_token!=0;})){
        hooks.push_back({draw,draw_hook});hooks.push_back({model_texture_update,model_texture_update_hook});
    }
    for(const auto&h:hooks){const auto conflict=owner(h.first);if(!conflict.empty()){LOG("[native_prop] install failed: hook %08X owned by %s",h.first,conflict.c_str());return false;}}
    if(!carry_bridge||!native_actor_carry::available(owner)||!native_actor_carry::register_guard("props",deny_lift,trace_lift)){LOG("[native_prop] install failed: shared carry guard conflict");return false;}
    const uint32_t native=GD(0x101D2B1C);if(ld32(native+0x10)!=kClassBytes){LOG("[native_prop] install failed: native class size %u expected %u",ld32(native+0x10),kClassBytes);return false;}
    std::memcpy(mem::ptr(profile),mem::ptr(native),kProfileBytes);st16(profile+8,kProfile);
    if(!native_actor_profiles::add(kProfile,profile)||!native_actor_profiles::install(owner,replace)){LOG("[native_prop] install failed: reserved profile registration rejected");return false;}
    if(!native_actor_carry::install(owner,replace))return false;
    for(const auto&h:hooks)replace(h.first,h.second);installed=true;return true;
}
}
