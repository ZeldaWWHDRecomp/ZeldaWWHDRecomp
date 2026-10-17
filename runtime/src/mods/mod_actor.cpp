#include "mod_actor.h"
#include "mod_actor_carry.h"
#include "native_actor_carry.h"
#include "mod_actor_combat_core.h"
#include "mod_actor_native_combat.h"
#include "mod_actor_knockback.h"
#include "mod_actor_attention.h"
#include "story_enemy_ownership.h"
#include "story_enemy_projectiles.h"
#include "story_actor_identity.h"
#include "story_item_models.h"
#include "native_actor_profiles.h"
#include "mod_actor_shadow_proof.h"
#include "mod_actor_budget.h"
#include "mod_actor_motion.h"
#include "mod_actor_look_bounds.h"
#include "story_mail.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <array>
#include <algorithm>
#include <vector>
#include <set>
namespace mod_actor { namespace {
// The extension is guest memory: model, resource phase and collision survive full states.
constexpr uint32_t Phase=0x3AC,Model=0x3B4,Stts=0x3B8,Cyl=0x3F4,Name=0x524,Safe=0x564,Matrix=0x56C,Source=0x59C,Talk=0x5E0,TalkStatus=0x5E4,HeapBaseline=0x630,HeapStarted=0x650,Morf=0x654,LookObject=0x658,LookVtable=0x660,LookAngles=0x670,Motion=0x67C,SavedMotion=0x74C,SceneLease=0x81C,SavedClip=0x820,SavedFrame=0x824,SavedLoop=0x828,ClipIndex=0x82C,ClipLoop=0x830,Radius=0x834,Height=0x838,CollisionFlags=0x83C,BehaviorHandle=0x840,Userdata=0x850,HitSequence=0x950,Ground=0x954,GroundReady=0x9A8,BehaviorCreated=0x9AC,HitPacket=0x9B0,PostObject=0x9F0,PostVtable=0x9F8,HeadAnchor=0xA08,UpperAnchor=0xA14,Combat=0xA20,AttackStts=0xA48,AttackCyl=0xA84,AttackSource=0xBB4,Heart=0xBF8,Knockback=0xC18,KnockbackLeft=0xC24,Quarantined=0xC28,CombatIdentity=0xC40,Acch=0xC80,AcchCircles=0xE44,AcchOld=0xF04,AcchSpeed=0xF10,AcchReady=0xF1C,Carry=0xF40,CarryMotion=0xF70,Size=kInstanceBytes;
struct Decl {std::string mod,archive;story_notebook::ManifestActor spec;int index;std::function<void()> talked;story_notebook::WorldPoint minimum,maximum;bool skinned=false;std::vector<std::string> clips;ModelRig rig;int head=-1,upper=-1;bool hits=false;CombatRewardSink reward;story_actors::identity::Fingerprint fingerprint{};std::function<bool()> carry_break;};
std::vector<Decl> declarations;
struct ArchiveMeasurement {const Decl* declaration=nullptr;uint32_t heap=0,info=0;};
thread_local ArchiveMeasurement* archive_measurement=nullptr;
uint32_t scratch=0,heap_callback=0,look_callback=0,post_callback=0;BehaviorSink behavior_sink;std::set<std::pair<uint32_t,uint32_t>> restored;bool installed=false;
void(*original)(Cpu*,uint32_t)=nullptr;
uint32_t(*invoke)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr;
uint32_t talk_actor=0;std::u16string text;
uint32_t call(Cpu*c,uint32_t f,std::initializer_list<uint32_t> a){return invoke(c,f,a);}
struct KeepCpu{Cpu* cpu;Cpu saved;explicit KeepCpu(Cpu*c):cpu(c),saved(*c){}~KeepCpu(){*cpu=saved;}};
uint32_t heap_size(Cpu*c,uint32_t h){if(!h)return 0;uint32_t vt=ld32(h+0xC);if(!vt)return 0;uint32_t fn=ld32(vt+0x6C);return fn?invoke(c,fn,{h}):0;}
uint32_t heap_free(Cpu*c,uint32_t h){if(!h)return 0;uint32_t vt=ld32(h+0xC);if(!vt)return 0;uint32_t fn=ld32(vt+0x74);return fn?invoke(c,fn,{h}):0;}

bool talk_trace(){const char* value=std::getenv("WWHD_MOD_ACTOR_TALK_TRACE");return value&&std::strcmp(value,"1")==0;}
bool heap_trace(){const char* value=std::getenv("WWHD_MOD_ACTOR_HEAPS");return value&&std::strcmp(value,"1")==0;}
using HeapSnapshot=std::array<std::pair<uint32_t,uint32_t>,4>;
HeapSnapshot heaps(Cpu*c){HeapSnapshot out{};const uint32_t getter[]={GC(0x025E3268),GC(0x025E2FBC),GC(0x025EE048),GC(0x025EE060)};for(unsigned i=0;i<4;++i){out[i].first=call(c,getter[i],{});out[i].second=heap_free(c,out[i].first);}return out;}
void report_heaps(uint32_t a,const char* stage,const HeapSnapshot& before,const HeapSnapshot& after){const char* names[]={"archive","game","zelda","command"};for(unsigned i=0;i<4;++i)LOG("[mod_actor_heaps] actor=%08X stage=%s heap=%s pointer=%08X free_before=%u free_after=%u delta=%lld",a,stage,names[i],after[i].first,before[i].second,after[i].second,static_cast<long long>(before[i].second)-after[i].second);}
constexpr uint32_t RecordStart=512,RecordStride=28;
static_assert(RecordStart+128*RecordStride<=kScratchBytes);
uint32_t record(size_t i){return scratch+RecordStart+uint32_t(i)*RecordStride;}
// A declaration added while the game runs (the world editor) whose archive is loaded already: the
// archive's measurements were taken when it loaded, for the declarations of that time.
void inherit_measurements(size_t i){if(!scratch||i>=declarations.size())return;for(size_t j=0;j<declarations.size();++j)if(j!=i&&declarations[j].archive==declarations[i].archive)for(uint32_t field:{8u,12u})st32(record(i)+field,std::max(ld32(record(i)+field),ld32(record(j)+field)));}
void model_cost(const Decl* own,uint32_t field,uint32_t bytes){for(size_t i=0;i<declarations.size();++i)if(declarations[i].archive==own->archive){const uint32_t at=record(i)+field;st32(at,std::max(ld32(at),bytes));}}
bool check_budget(Cpu*c,uint32_t actor,std::string& why){
    std::vector<budget::Record> records;records.reserve(declarations.size());
    for(size_t i=0;i<declarations.size();++i){const auto& d=declarations[i];const uint32_t r=record(i),a=ld32(r);
        const bool active=a&&ld32(a+4)==ld32(r+4)&&ld16(a+0xE)==kProfile&&ld8(a+0x3AA)==1&&ld32(a+0xB0)==i+1;
        std::memcpy(mem::ptr(scratch+400),d.archive.c_str(),d.archive.size()+1);st32(scratch+472,scratch+400);st32(scratch+476,GD(0x10012504));
        const bool loaded=call(c,GC(0x02612504),{ld32(GD(0x101F4F7C)),scratch+472})!=0;
        records.push_back({d.mod,d.archive,int(d.spec.room),ld32(r+8),ld32(r+12),ld32(r+16),ld32(r+24),loaded,active});
    }
    budget::Usage usage;const int room=int8_t(ld8(actor+0x326));
    const bool ok=budget::check(records,room,usage,why);
    if(heap_trace())LOG("[mod_actor_budget] actor=%08X room=%d game_heap_bytes=%llu limit=%llu accepted=%u",actor,room,static_cast<unsigned long long>(usage.room),static_cast<unsigned long long>(budget::RoomLimit),unsigned(ok));
    return ok;
}
Decl* declaration(uint32_t a){auto t=ld32(a+0xB0);return t&&t<=declarations.size()?&declarations[t-1]:nullptr;}
bool valid_actor(uint32_t a){
    if(a<0x10000000u||a>=0xC0000000u||!scratch||ld16(a+0xE)!=kProfile||ld8(a+0x3AA)!=1)return false;
    const uint32_t token=ld32(a+0xB0);if(!token||token>declarations.size())return false;
    if((declarations[token-1].spec.combat_enabled||declarations[token-1].spec.carry_enabled)&&!story_actors::identity::matches(mem::ptr(a+CombatIdentity),declarations[token-1].fingerprint))return false;
    return ld32(record(token-1))==a&&ld32(record(token-1)+4)==ld32(a+4);
}
float number(uint32_t at){return u32_as_f32(ld32(at));}
void number(uint32_t at,float value){st32(at,f32_as_u32(value));}
story_notebook::WorldPoint point(uint32_t at){return {number(at),number(at+4),number(at+8)};}
void point(uint32_t at,story_notebook::WorldPoint value){number(at,value.x);number(at+4,value.y);number(at+8,value.z);}
motion::Point motion_point(story_notebook::WorldPoint p){return {p.x,p.y,p.z};}
combat::Config combat_config(const Decl&d){const auto&v=d.spec.combat;return {v.health,float(v.invulnerability_frames),float(v.flinch.frames),float(v.stun.frames),float(v.death.frames),float(v.attack.active_frames),float(v.attack.cooldown_frames)};}
combat::State read_combat(uint32_t a){const uint32_t p=a+Combat;return {ld32(p),ld32(p+4),ld32(p+8),ld32(p+12),ld32(p+16),number(p+20),number(p+24),number(p+28),number(p+32),number(p+36)};}
void write_combat(uint32_t a,const combat::State&s){const uint32_t p=a+Combat;const uint32_t words[]={s.version,s.phase,s.health,s.last_sequence,s.reward};for(unsigned i=0;i<5;++i)st32(p+i*4,words[i]);const float timers[]={s.invulnerability_left,s.reaction_left,s.death_left,s.active_left,s.cooldown_left};for(unsigned i=0;i<5;++i)number(p+20+i*4,timers[i]);}
bool combat_blocked(uint32_t a){return ld32(a+Quarantined)||ld32(a+SceneLease)||ld16(a+0xF8)==1;}
void transform(uint32_t);
MotionState read_motion(uint32_t);void write_motion(uint32_t,const MotionState&);
carry::State read_carry(uint32_t a){const uint32_t p=a+Carry;return {ld32(p),ld32(p+4),ld32(p+8),ld32(p+12),ld32(p+16),number(p+20),number(p+24),number(p+28)};}
void write_carry(uint32_t a,const carry::State&s){const uint32_t p=a+Carry;st32(p,s.version);st32(p+4,s.phase);st32(p+8,s.pid);st32(p+12,s.link_pid);st32(p+16,s.frames);number(p+20,s.vx);number(p+24,s.vy);number(p+28,s.vz);}
bool carry_busy(uint32_t a){return declaration(a)->spec.carry_enabled&&(ld32(a+Carry+4)!=0||(ld32(a+0x2E0)&0x2000));}
bool carry_link(Cpu*c,uint32_t link,uint32_t a){
    if(a<mem::kMem2Start||a>mem::kMem2End-Size||!valid_actor(a)||!declaration(a)->spec.carry_enabled||link<mem::kMem2Start||link>mem::kMem2End-0x65F4||ld16(link+0xE)!=0xA8||ld32(link+0x65A0)!=ld32(a+4)||ld32(link+0x65A4)!=a)return false;
    KeepCpu saved(c);const uint32_t play=call(c,GC(0x025200D4),{});if(!play||ld32(play+0x5B34)!=link)return false;
    st32(scratch+80,0);call(c,GC(0x025D54C4),{ld32(a+4),scratch+80});return ld32(scratch+80)==a;
}
// Denial-only authority: the actual Link keep must match a retained SDK tuple.
// Native search distinguishes a live private actor from a stale/recycled tuple.
// Neither bad configuration nor stale identity grants actor mutation.
bool clear_invalid_carry_keep(Cpu*c,uint32_t link,uint32_t a,bool stale_only=false){
    if(!c||!scratch||a<mem::kMem2Start||a>mem::kMem2End-Size||link<mem::kMem2Start||link>mem::kMem2End-0x65F4||ld16(link+0xE)!=0xA8||ld32(link+0x65A4)!=a)return false;
    const uint32_t pid=ld32(link+0x65A0);if(!pid||pid==0xFFFFFFFFu)return false;
    bool retained=false;for(size_t i=0;i<declarations.size();++i)if(ld32(record(i))==a&&ld32(record(i)+4)==pid){retained=true;break;}if(!retained)return false;
    KeepCpu saved(c);const uint32_t play=call(c,GC(0x025200D4),{});if(!play||ld32(play+0x5B34)!=link)return false;
    st32(scratch+80,0);call(c,GC(0x025D54C4),{pid,scratch+80});const uint32_t resolved=ld32(scratch+80);
    // A stale SDK tuple cannot authorize writes to a retail process reusing its
    // address. A genuinely current retail keep is delegated normally.
    if(resolved==a&&(stale_only||ld16(a+0xE)!=kProfile||ld32(a+4)!=pid))return false;
    // Native clearData only writes this keep's PID/pointer. With the keep empty,
    // freeGrabItem cannot dereference/cancel either the invalid or recycled actor.
    call(c,GC(0x023DC63C),{link+0x65A0});return ld32(link+0x65A0)==0xFFFFFFFFu&&ld32(link+0x65A4)==0;
}
bool carry_attach_allowed(Cpu*c,uint32_t link,uint32_t a){
    if(!carry_link(c,link,a)||!(ld32(a+0x2E0)&0x2000)||ld32(a+SceneLease)||ld32(a+Quarantined))return false;
    const auto state=read_carry(a);return carry::valid(state,ld32(a+4))&&(state.phase==0||(state.phase==1&&state.link_pid==ld32(link+4)));
}
bool deny_carry(uint32_t a){
    if(a<mem::kMem2Start||a>mem::kMem2End-Size||ld16(a+0xE)!=kProfile)return false;
    if(!valid_actor(a))return true;const auto*d=declaration(a);const auto state=read_carry(a);
    return !d->spec.carry_enabled||(ld32(a+0x2E0)&0x2000)||ld32(a+SceneLease)||ld32(a+Quarantined)||!carry::valid(state,ld32(a+4))||state.phase!=0;
}
void carry_quarantine(uint32_t a){auto state=read_carry(a);carry::quarantine(state);write_carry(a,state);st32(a+Quarantined,1);st32(a+0x39C,0);}
void carry_attach(Cpu*c){
    const uint32_t link=c->r[3];if(link>=mem::kMem2Start&&link<=mem::kMem2End-0x65F4){const uint32_t candidate=ld32(link+0x65A4);if(candidate>=mem::kMem2Start&&candidate<=mem::kMem2End-Size){
        const bool private_actor=ld16(candidate+0xE)==kProfile;
        if(private_actor&&!carry_attach_allowed(c,link,candidate)){
            if(clear_invalid_carry_keep(c,link,candidate)){Cpu reset=*c;reset.r[3]=link;original(&reset,GC(0x023DCF8C));original(c,GC(0x0240A41C));}return;
        }
        if(!private_actor&&clear_invalid_carry_keep(c,link,candidate,true)){Cpu reset=*c;reset.r[3]=link;original(&reset,GC(0x023DCF8C));original(c,GC(0x0240A41C));return;}
    }}original(c,GC(0x0240A41C));KeepCpu returned(c);
    if(link<mem::kMem2Start||link>mem::kMem2End-0x65F4)return;const uint32_t a=ld32(link+0x65A4);
    if(!carry_link(c,link,a)||!(ld32(a+0x2E0)&0x2000))return;
    auto state=read_carry(a);if(!carry::valid(state,ld32(a+4))){carry_quarantine(a);return;}
    if(state.phase==0){if(!carry::hold(state,ld32(a+4),ld32(link+4))){carry_quarantine(a);return;}write_motion(a+CarryMotion,read_motion(a+Motion));auto stopped=read_motion(a+Motion);stopped.enabled=false;write_motion(a+Motion,stopped);write_carry(a,state);}
    else if(state.phase!=1||state.link_pid!=ld32(link+4)){carry_quarantine(a);return;}
    st32(a+0x39C,0);transform(a);
}
void carry_release(Cpu*c){
    const uint32_t link=c->r[3];uint32_t a=0;carry::State state;bool own=false,thrown=false;float forward=0,up=0;int16_t yaw=0;
    if(link>=mem::kMem2Start&&link<=mem::kMem2End-0x65F4){a=ld32(link+0x65A4);own=carry_link(c,link,a);if(own){state=read_carry(a);own=carry::valid(state,ld32(a+4))&&state.phase==1&&state.link_pid==ld32(link+4);if(own){forward=number(a+0x370);up=number(a+0x340);yaw=int16_t(ld16(a+0x322));thrown=ld32(link+0x65F0)==0x71&&std::fabs(forward-19.f)<.01f&&std::fabs(up-34.f)<.01f;}}}
    if(a>=mem::kMem2Start&&a<=mem::kMem2End-Size&&ld16(a+0xE)==kProfile&&!own){if(clear_invalid_carry_keep(c,link,a))original(c,GC(0x023DCF8C));return;}
    if(!own&&a>=mem::kMem2Start&&a<=mem::kMem2End-Size&&clear_invalid_carry_keep(c,link,a,true)){original(c,GC(0x023DCF8C));return;}
    original(c,GC(0x023DCF8C));KeepCpu returned(c);
    if(!own||!valid_actor(a))return;
    if(ld32(link+0x65A4)==a||(ld32(a+0x2E0)&0x2000)||!carry::release(state,thrown,forward,up,yaw)){carry_quarantine(a);return;}
    write_carry(a,state);if(!thrown)write_motion(a+Motion,read_motion(a+CarryMotion));
}
bool source_identity(const mod_actor_native_combat::SourceIdentity&i){return valid_actor(i.actor)&&declaration(i.actor)->spec.combat_enabled&&ld32(i.actor+4)==i.pid&&i.generation==ld32(i.actor+0xB0)&&i.epoch==i.pid&&i.heart_state==i.actor+Heart;}
mod_actor_knockback::Context knockback_context(uint32_t a){return {a,ld32(a+4),a+Acch,a+AcchCircles,a+AcchOld,a+AcchSpeed};}
bool knockback_identity(const mod_actor_knockback::Context&x){return valid_actor(x.actor)&&(declaration(x.actor)->spec.combat_enabled||declaration(x.actor)->spec.carry_enabled)&&ld32(x.actor+4)==x.pid&&x.acch==x.actor+Acch&&x.circles==x.actor+AcchCircles&&x.old_position==x.actor+AcchOld&&x.speed==x.actor+AcchSpeed;}
attention::Priority combat_priority(uint32_t a){if(!valid_actor(a))return attention::Priority::None;const auto*d=declaration(a);if(!d->spec.combat_enabled||combat_blocked(a))return attention::Priority::None;const auto state=read_combat(a);if(!combat::valid(state,combat_config(*d))||!combat::alive(state))return attention::Priority::None;switch(d->spec.combat.target_priority){case 0:return attention::Priority::Low;case 1:return attention::Priority::Normal;case 2:return attention::Priority::High;default:return attention::Priority::None;}}
story_enemy_ownership::ModCollider classify_combat_collider(uint32_t object){
    using Role=story_enemy_ownership::ModCollider;
    if(object<0x10000000u||object>=0xC0000000u-0x130)return Role::Foreign;
    const auto classify_owned=[&](uint32_t a)->Role{
        const auto*d=declaration(a);if(!d||!d->spec.combat_enabled||!valid_actor(a)||combat_blocked(a))return Role::Invalid;
        const auto state=read_combat(a);if(!combat::valid(state,combat_config(*d))||!combat::alive(state))return Role::Invalid;
        const uint32_t expected=object==a+Cyl?a+Stts:object==a+AttackCyl?a+AttackStts:0;
        if(!expected||ld32(object+0x44)!=expected||ld32(expected+0xC)!=a||ld32(expected+0x10)!=ld32(a+4))return Role::Invalid;
        if(object==a+Cyl)return Role::Body;
        if(!combat::attacking(state)||ld32(object)!=mod_actor_native_combat::kPhysicalAtFlags||ld32(object+0x10)!=mod_actor_native_combat::kPhysicalAtType||ld8(object+0x14)!=d->spec.combat.attack.damage||ld8(object+0x6F)!=0)return Role::Invalid;
        return Role::Attack;
    };
    // Claimed exact objects remain denial-owned when their Stts is damaged.
    // A retail process genuinely reusing the allocation is left untouched.
    if(scratch)for(size_t i=0;i<declarations.size();++i){if(!declarations[i].spec.combat_enabled)continue;const uint32_t a=ld32(record(i));if(a<0x10000000u||a>=0xC0000000u-Size||ld16(a+0xE)!=kProfile||(object!=a+Cyl&&object!=a+AttackCyl))continue;
        if(ld32(record(i)+4)!=ld32(a+4)){const auto*current=declaration(a);if(current&&!current->spec.combat_enabled)continue;}
        return classify_owned(a);
    }
    const uint32_t status=ld32(object+0x44);if(status<0x10000000u||status>=0xC0000000u-0x14)return Role::Foreign;
    const uint32_t a=ld32(status+0xC);if(a<0x10000000u||a>=0xC0000000u-Size||ld16(a+0xE)!=kProfile)return Role::Foreign;
    const auto*d=declaration(a);if(d&&!d->spec.combat_enabled)return Role::Foreign;
    return classify_owned(a);
}
// Opt-in proof bookkeeping only: bounded host storage, never simulation state.
struct HeadingProof {uint32_t actor=0,process=0;uint64_t serial=0;bool selected=false;};
std::array<HeadingProof,128> heading_proofs{};
bool heading_enabled(){static const bool enabled=[](){const char*v=std::getenv("WWHD_MOD_ACTOR_HEADING_TRACE");return v&&std::strcmp(v,"1")==0;}();return enabled;}
uint64_t heading_start(){static const uint64_t start=[](){const char*v=std::getenv("WWHD_MOD_ACTOR_HEADING_START");return v?std::min<uint64_t>(std::strtoull(v,nullptr,10),1000000):0;}();return start;}
HeadingProof* heading_proof(uint32_t actor){if(!heading_enabled())return nullptr;const uint32_t process=ld32(actor+4);for(auto&entry:heading_proofs)if(entry.actor==actor&&entry.process==process)return &entry;for(auto&entry:heading_proofs)if(!entry.actor){entry={actor,process,0,false};return &entry;}return nullptr;}
void heading_execute(uint32_t actor){auto*p=heading_proof(actor);if(!p||!p->selected)return;const auto position=point(actor+0x314);const auto morf=ld32(actor+Morf);
    LOG("[mod_actor_heading] phase=execute actor=%08X process=%u serial=%llu yaw=%d pos=%.6f,%.6f,%.6f clipframe=%.6f",actor,p->process,static_cast<unsigned long long>(p->serial),int(int16_t(ld16(actor+0x32A))),double(position.x),double(position.y),double(position.z),morf?double(number(morf+0x9C)):0.);
}
struct HeadingExecuteScope {uint32_t actor;bool complete=false;explicit HeadingExecuteScope(uint32_t a):actor(a){static unsigned remaining=1024;if(auto*p=heading_proof(a)){++p->serial;p->selected=p->serial>=heading_start()&&remaining;if(p->selected)--remaining;}}~HeadingExecuteScope(){if(complete)heading_execute(actor);}};
void heading_movement(uint32_t actor,int before,const motion::Move&m){auto*p=heading_proof(actor);if(!p||!p->selected)return;
    LOG("[mod_actor_heading] phase=path actor=%08X process=%u serial=%llu before=%d after=%d target=%.9f traveled=%.9f status=%u",actor,p->process,static_cast<unsigned long long>(p->serial),before,int(int16_t(ld16(actor+0x32A))),double(m.yaw),double(m.traveled),unsigned(m.status));
}
void heading_draw(uint32_t actor,int before,uint32_t model){static unsigned remaining=2048;auto*p=heading_proof(actor);if(!p||!p->selected||!remaining)return;--remaining;
    const uint32_t joints=ld32(model+0x2C),matrix=joints?ld32(joints+0x10):0;const double yaw=matrix?std::atan2(double(number(matrix+8)),double(number(matrix+40))):0.;
    LOG("[mod_actor_heading] phase=draw actor=%08X process=%u serial=%llu before=%d after=%d rootyaw=%.9f rootvalid=%u",actor,p->process,static_cast<unsigned long long>(p->serial),before,int(int16_t(ld16(actor+0x32A))),yaw,unsigned(matrix!=0));
}
MotionState read_motion(uint32_t at){MotionState m;m.count=ld32(at);m.next=ld32(at+4);m.speed=number(at+8);const auto flags=ld32(at+12);m.loop=flags&1;m.finished=flags&2;m.enabled=flags&4;for(unsigned i=0;i<16;++i)m.points[i]=point(at+16+12*i);return m;}
void write_motion(uint32_t at,const MotionState&m){st32(at,m.count);st32(at+4,m.next);number(at+8,m.speed);st32(at+12,(m.loop?1:0)|(m.finished?2:0)|(m.enabled?4:0));for(unsigned i=0;i<16;++i)point(at+16+12*i,m.points[i]);}
bool emit(Cpu*c,uint32_t a,BehaviorEvent event,const Hit*hit=nullptr){
    if(!valid_actor(a))return false;Identity id;const auto*d=declaration(a);id={d->mod,d->spec.id,ld32(a+0xB0),ld32(a+4)};
    if(!behavior_sink)return true;behavior_sink(c,a,event,hit);return resolve(c,id)==a&&(!(d->spec.combat_enabled||d->spec.carry_enabled)||!ld32(a+Quarantined));
}
void pre_joint(Cpu*c){
    const uint32_t self=c->r[3],a=self?ld32(self+4):0,index=c->r[4],info=c->r[5];
    if(valid_actor(a)&&info>=0x10000000u&&info<0xC0000000u){const auto*d=declaration(a);constexpr double scale=32768.0/motion::Pi,rad=motion::Pi/32768.;
        const bool head=int(index)==d->head,upper=int(index)==d->upper;
        if(head||upper){const auto& limits=d->spec.look_at;const double yaw_limit=(head?limits.yaw_degrees:limits.upper_yaw_degrees)*motion::Pi/180;
            double yaw=int16_t(ld16(info+0xE))*rad+number(a+LookAngles+(head?0:8));yaw=std::clamp(yaw,-yaw_limit,yaw_limit);
            st16(info+0xE,uint16_t(int32_t(yaw*scale)));
            if(head){const double pitch_limit=limits.pitch_degrees*motion::Pi/180;double pitch=int16_t(ld16(info+0xC))*rad-number(a+LookAngles+4);pitch=std::clamp(pitch,-pitch_limit,pitch_limit);st16(info+0xC,uint16_t(int32_t(pitch*scale)));}
            const uint32_t anchor=a+(head?HeadAnchor:UpperAnchor);for(unsigned k=0;k<3;++k)number(anchor+4*k,float(int16_t(ld16(info+0xC+2*k))*rad));
        }
    }c->r[3]=1;
}
void post_joint(Cpu*c){
    const uint32_t self=c->r[3],a=self?ld32(self+4):0,index=c->r[4];
    if(valid_actor(a)){const auto*d=declaration(a);const bool head=int(index)==d->head,upper=int(index)==d->upper;const uint32_t morf=ld32(a+Morf),quaternions=morf?ld32(morf+0xAC):0;
        if((head||upper)&&quaternions&&index<d->rig.joint_names.size()){const uint32_t at=quaternions+index*16,anchor=a+(head?HeadAnchor:UpperAnchor);look_bounds::Quaternion input,output;look_bounds::Euler reference;for(unsigned k=0;k<4;++k)input[k]=number(at+4*k);for(unsigned k=0;k<3;++k)reference[k]=number(anchor+4*k);const auto&limits=d->spec.look_at;
            if(look_bounds::project(input,reference,(head?limits.yaw_degrees:limits.upper_yaw_degrees)*motion::Pi/180,limits.pitch_degrees*motion::Pi/180,head,output)){for(unsigned k=0;k<4;++k)if(output[k]!=input[k])number(at+4*k,float(output[k]));}
            else {if(!std::all_of(reference.begin(),reference.end(),[](double v){return std::isfinite(v);}))reference={0,0,0};const auto fallback=look_bounds::quaternion(reference);for(unsigned k=0;k<4;++k)number(at+4*k,float(fallback[k]));}
        }
    }c->r[3]=1;
}
void result(Cpu*c,uint32_t r){c->r[3]=r;}
void fail_create(uint32_t actor){const uint32_t token=ld32(actor+0xB0);if(scratch&&token&&token<=128)st8(scratch+256+token-1,1);if(auto*d=declaration(actor);d&&d->spec.combat_enabled){st32(actor+Quarantined,1);number(actor+Combat+32,0);st32(actor+0x39C,0);}LOG("[mod_actor] create failed actor=%08X token=%u; retry deferred until room reload",actor,token);}
void transform(uint32_t a){
    constexpr float radians=float(3.14159265358979323846/32768.0);
    const float x=float(int16_t(ld16(a+0x328)))*radians;
    const float y=float(int16_t(ld16(a+0x32A)))*radians;
    const float z=float(int16_t(ld16(a+0x32C)))*radians;
    const float cx=std::cos(x),sx=std::sin(x),cy=std::cos(y),sy=std::sin(y),cz=std::cos(z),sz=std::sin(z);
    // Native mDoMtx_ZXYrotS builds RY * RX * RZ (025F1AA4); translation stays in world space.
    const float m[12]={cy*cz+sy*sx*sz,-cy*sz+sy*sx*cz,sy*cx,u32_as_f32(ld32(a+0x314)),
                      cx*sz,cx*cz,-sx,u32_as_f32(ld32(a+0x318)),
                      -sy*cz+cy*sx*sz,sy*sz+cy*sx*cz,cy*cx,u32_as_f32(ld32(a+0x31C))};
    for(int i=0;i<12;++i)st32(a+Matrix+i*4,f32_as_u32(m[i]));
    if(const auto* d=declaration(a)){
        const float minimum[]={d->minimum.x,d->minimum.y,d->minimum.z},maximum[]={d->maximum.x,d->maximum.y,d->maximum.z};
        for(unsigned i=0;i<3;++i){const float scale=u32_as_f32(ld32(a+0x330+i*4));st32(a+0x34C+i*4,f32_as_u32(minimum[i]*scale));st32(a+0x358+i*4,f32_as_u32(maximum[i]*scale));}
    }
    const uint32_t model=ld32(a+Model);
    if(model){for(int i=0;i<3;++i)st32(model+0xBC+i*4,ld32(a+0x330+i*4));for(int i=0;i<12;++i)st32(model+0xC8+i*4,ld32(a+Matrix+i*4));}
}
// Native morf evaluation temporarily selects a calculator and frame on shared
// archive resources. Restore those exact words before another instance can draw.
struct PoseScope {
    uint32_t root=0,animation=0,root_value=0,frame=0,calculator=0,current_model=0;
    explicit PoseScope(uint32_t morf){
        const uint32_t model=ld32(morf+0x90),data=model?ld32(model+0xAC):0;
        root=data?ld32(data+8):0;animation=ld32(morf+0x94);
        if(root)root_value=ld32(root+0x14);if(animation)frame=ld32(animation);
        calculator=ld32(GD(0x104B4628));current_model=ld32(GD(0x104B462C));
    }
    ~PoseScope(){if(root)st32(root+0x14,root_value);if(animation)st32(animation,frame);st32(GD(0x104B4628),calculator);st32(GD(0x104B462C),current_model);}
};
uint32_t create_morf(Cpu* c,uint32_t data,uint32_t animation){
    // guest_call opens a0x40-byte PPC frame; its outgoing stack arguments are
    // below the incoming SP. Keep the caller's bytes intact after construction.
    if(c->r[1]<0x40)return 0;
    const uint32_t args=c->r[1]-0x40+8;std::array<uint32_t,4> saved{};
    for(unsigned i=0;i<4;++i)saved[i]=ld32(args+i*4);
    struct Restore{uint32_t at;std::array<uint32_t,4> words;~Restore(){for(unsigned i=0;i<4;++i)st32(at+i*4,words[i]);}} restore{args,saved};
    st32(args,0);st32(args+4,0);st32(args+8,0x80000);st32(args+12,0);
    c->f[1].ps0=1.0;
    return call(c,GC(0x025E4F64),{0,data,0,0,animation,2,0,0xFFFFFFFFu});
}
bool animate(Cpu*c,uint32_t a,const std::string&clip,float blend,bool loop){
    KeepCpu keep(c);
    auto*d=declaration(a);const auto it=std::find(d->clips.begin(),d->clips.end(),clip);const uint32_t morf=ld32(a+Morf);
    if(!morf||it==d->clips.end()||!std::isfinite(blend)||blend<0||blend>900)return false;
    const uint32_t index=uint32_t(it-d->clips.begin());if(ld32(a+ClipIndex)==index&&bool(ld32(a+ClipLoop))==loop)return true;
    const uint32_t animation=call(c,GC(0x026066C4),{ld32(GD(0x101F4F28)),a+Safe,uint32_t(d->index)+1+index});if(!animation)return false;
    PoseScope scope(morf);c->f[1].ps0=blend;c->f[2].ps0=1;c->f[3].ps0=0;c->f[4].ps0=-1;
    call(c,GC(0x025E4A98),{morf,animation,loop?2u:0u,0});st32(a+ClipIndex,index);st32(a+ClipLoop,loop);return true;
}
void heap(Cpu*c){uint32_t a=c->r[3];auto*d=declaration(a);if(!d){result(c,0);return;}st32(a+Safe,a+Name);st32(a+Safe+4,GD(0x10012504));const std::string filename=d->archive+".bmd";uint32_t data=call(c,GC(0x026066C4),{ld32(GD(0x101F4F28)),a+Safe,uint32_t(d->index)});LOG("[mod_actor] %s:%s archive=%s named_resource=%s data=%08X",d->mod.c_str(),d->spec.id.c_str(),d->archive.c_str(),filename.c_str(),data);uint32_t current=call(c,GC(0x027EC230),{}),before=heap_free(c,current);uint32_t model=0;
if(data&&d->skinned){
    auto idle=std::find(d->clips.begin(),d->clips.end(),d->spec.animation.empty()?"idle":d->spec.animation);const size_t clip=idle==d->clips.end()?0:size_t(idle-d->clips.begin());
    const uint32_t animation=d->clips.empty()?0:call(c,GC(0x026066C4),{ld32(GD(0x101F4F28)),a+Safe,uint32_t(d->index)+1+uint32_t(clip)});
    const uint32_t morf=(animation||d->clips.empty())?create_morf(c,data,animation):0;st32(a+Morf,morf);st32(a+ClipIndex,d->clips.empty()?0xFFFFFFFFu:uint32_t(clip));st32(a+ClipLoop,1);if(morf&&d->head>=0){st32(morf+0xC0,a+LookObject);st32(morf+0xC4,a+PostObject);}
    if(morf&&ld32(morf+0xA8)&&ld32(morf+0xAC)&&!ld32(morf+0xBC))model=ld32(morf+0x90);
}else if(data)model=call(c,GC(0x025E38E0),{data,0x80000});uint32_t after=heap_free(c,current);LOG("[mod_actor] %s:%s model heap=%08X free_before=%u free_after=%u model_bytes=%u",d->mod.c_str(),d->spec.model.c_str(),current,before,after,before>=after?before-after:0);st32(a+Model,model);st32(a+0x368,model);st32(a+0x348,model?model+0xC8:0);transform(a);result(c,model!=0);}
void create(Cpu*c){uint32_t a=c->r[3];auto*d=declaration(a);if(!d){result(c,5);return;}if(!ld8(a+0x3AA)){call(c,GC(0x025D4ED0),{a});std::memset(mem::ptr(a+Phase),0,Size-Phase);if(d->spec.combat_enabled){combat::State state;if(!combat::initialize(state,combat_config(*d))){result(c,5);return;}write_combat(a,state);const auto captured=story_actors::identity::record(d->fingerprint);std::memcpy(mem::ptr(a+CombatIdentity),captured.data(),captured.size());}if(d->spec.carry_enabled){carry::State state;state.pid=ld32(a+4);write_carry(a,state);const auto captured=story_actors::identity::record(d->fingerprint);std::memcpy(mem::ptr(a+CombatIdentity),captured.data(),captured.size());}number(a+Radius,d->spec.collision_radius);number(a+Height,d->spec.collision_height);st32(a+CollisionFlags,1|(d->spec.pushable?2:0));st32(a+LookObject,a+LookVtable);st32(a+LookObject+4,a);st32(a+LookVtable+0xC,look_callback);st32(a+PostObject,a+PostVtable);st32(a+PostObject+4,a);st32(a+PostVtable+0xC,post_callback);MotionState motion_;motion_.count=unsigned(d->spec.path.points.size());motion_.speed=d->spec.path.speed;motion_.loop=d->spec.path.loop;motion_.enabled=motion_.count>=2;for(unsigned i=0;i<motion_.count;++i)motion_.points[i]=d->spec.path.points[i];write_motion(a+Motion,motion_);std::memcpy(mem::ptr(a+Name),d->archive.c_str(),d->archive.size()+1);call(c,GC(0x0200BD2C),{a+Stts});call(c,GC(0x02515DA0),{a+Stts+0x1C});st32(a+Stts+0x18,GD(0x1004AE88));st32(a+Stts+0x1C,GD(0x1004AEC0));call(c,GC(0x02515FB8),{a+Cyl});st32(a+Cyl+0x114,GD(0x100015A8));st32(a+Cyl+0x110,GD(0x1001251C));call(c,GC(0x02018590),{a+Cyl+0x118});st32(a+Cyl+0x3C,GD(0x1004B108));st32(a+Cyl+0x114,GD(0x1004B160));st32(a+Cyl+0x12C,GD(0x1004B150));}
if(heap_trace()&&!ld32(a+HeapStarted)){const auto before=heaps(c);for(unsigned i=0;i<4;++i){st32(a+HeapBaseline+i*8,before[i].first);st32(a+HeapBaseline+i*8+4,before[i].second);}st32(a+HeapStarted,1);}
uint32_t phase=call(c,GC(0x02520460),{a+Phase,a+Name});if(phase!=4){if(phase>=5)fail_create(a);result(c,phase);return;}if(!ld32(a+Model)){
    if(heap_trace()){HeapSnapshot before{};for(unsigned i=0;i<4;++i)before[i]={ld32(a+HeapBaseline+i*8),ld32(a+HeapBaseline+i*8+4)};report_heaps(a,"archive_load",before,heaps(c));}
    const auto before=heap_trace()?heaps(c):HeapSnapshot{};
    if(!call(c,GC(0x025D63E8),{a,heap_callback,0x40000})){fail_create(a);result(c,5);return;}
    if(heap_trace())report_heaps(a,"model_create",before,heaps(c));
}
const uint32_t token=ld32(a+0xB0),account=record(token-1),solid=ld32(a+0xF4),parent=solid?ld32(solid+0x24):0;
if(heap_trace())LOG("[mod_actor_budget] solid=%08X parent=%08X base_bytes=%u",solid,parent,ld32(account+24));
if(!solid||parent<0x10000000u||parent>=0xC0000000u||!ld32(account+8)||!ld32(account+12)||!ld32(account+24)){fail_create(a);LOG("[mod_actor_budget] missing native allocation measurements");result(c,5);return;}
st32(account,a);st32(account+4,ld32(a+4));st32(account+16,heap_size(c,solid));st32(account+20,uint32_t(int8_t(ld8(a+0x326))));
std::string budget_error;
if(heap_free(c,parent)<budget::SolidReserve||!check_budget(c,a,budget_error)){st32(account,0);fail_create(a);LOG("[mod_actor_budget] rejected %s:%s (%s)",d->mod.c_str(),d->spec.id.c_str(),budget_error.empty()?"solid parent reserve below 1 MiB":budget_error.c_str());result(c,5);return;}
call(c,GC(0x02515F14),{a+Stts,d->spec.pushable?100u:255u,0,a});for(unsigned i=0;i<0x44;i+=4)st32(a+Source+i,0);st32(a+Source+0x18,0x19);if(d->hits){st32(a+Source+0x10,d->spec.combat_enabled?0xFFFFFFFFu:2u);st32(a+Source+0x14,0x1F);st32(a+Source+0x28,6);}call(c,GC(0x02516518),{a+Cyl,a+Source});st32(a+Cyl+0x44,a+Stts);st8(a+0x389,5);st8(a+0x38B,6);st32(a+0x39C,0xA|(d->spec.prompt=="check"?0x20000000u:0u));if(heap_trace())LOG("[mod_actor] ready actor=%08X model=%08X pos=(%.2f,%.2f,%.2f) scale=(%.2f,%.2f,%.2f) cull=%u",a,ld32(a+Model),double(u32_as_f32(ld32(a+0x314))),double(u32_as_f32(ld32(a+0x318))),double(u32_as_f32(ld32(a+0x31C))),double(u32_as_f32(ld32(a+0x330))),double(u32_as_f32(ld32(a+0x334))),double(u32_as_f32(ld32(a+0x338))),unsigned(ld8(a+0x2DB)));if(d->spec.combat_enabled){
    call(c,GC(0x0200BD2C),{a+AttackStts});call(c,GC(0x02515DA0),{a+AttackStts+0x1C});st32(a+AttackStts+0x18,GD(0x1004AE88));st32(a+AttackStts+0x1C,GD(0x1004AEC0));call(c,GC(0x02515F14),{a+AttackStts,255,0,a});
    call(c,GC(0x02515FB8),{a+AttackCyl});st32(a+AttackCyl+0x114,GD(0x100015A8));st32(a+AttackCyl+0x110,GD(0x1001251C));call(c,GC(0x02018590),{a+AttackCyl+0x118});st32(a+AttackCyl+0x3C,GD(0x1004B108));st32(a+AttackCyl+0x114,GD(0x1004B160));st32(a+AttackCyl+0x12C,GD(0x1004B150));
    const auto&at=d->spec.combat.attack;if(!mod_actor_native_combat::write_attack_source(a+AttackSource,{at.radius,at.height,at.forward_offset,at.y_offset,at.damage})){fail_create(a);result(c,5);return;}call(c,GC(0x02516518),{a+AttackCyl,a+AttackSource});st32(a+AttackCyl+0x44,a+AttackStts);st32(a+0x39C,4);st8(a+0x38A,3);
}if(d->spec.carry_enabled){st32(a+0x39C,0x10);st8(a+0x38C,8);}if(!ld32(a+BehaviorCreated)){st32(a+BehaviorCreated,1);emit(c,a,BehaviorEvent::CreateNew);}result(c,4);}
void destroy(Cpu*c){uint32_t a=c->r[3];if(!valid_actor(a)){const uint32_t play=call(c,GC(0x025200D4),{}),link=play?ld32(play+0x5B34):0;if(clear_invalid_carry_keep(c,link,a)){Cpu reset=*c;reset.r[3]=link;original(&reset,GC(0x023DCF8C));}}if(valid_actor(a)&&declaration(a)->spec.carry_enabled){const uint32_t play=call(c,GC(0x025200D4),{}),link=play?ld32(play+0x5B34):0;if(carry_link(c,link,a))call(c,GC(0x023DCF8C),{link});if(ld32(a+AcchReady)){mod_actor_knockback::destroy(c,knockback_context(a));st32(a+AcchReady,0);}}for(auto&entry:heading_proofs)if(entry.actor==a)entry={};restored.erase({a,ld32(a+4)});emit(c,a,BehaviorEvent::Destroy);if(ld32(a+GroundReady)){call(c,GC(0x0237A940),{a+Ground,0});st32(a+GroundReady,0);}if(auto*d=declaration(a);d&&d->spec.combat_enabled){if(ld32(a+AcchReady)){mod_actor_knockback::destroy(c,knockback_context(a));st32(a+AcchReady,0);}call(c,GC(0x02515A70),{a+AttackCyl,2});call(c,GC(0x02515860),{a+AttackStts,2});}mod_actor_shadow_proof::forget_model(ld32(a+Model));const uint32_t token=ld32(a+0xB0);if(token&&token<=declarations.size()&&ld32(record(token-1))==a)st32(record(token-1),0);const auto before=heap_trace()?heaps(c):HeapSnapshot{};if(talk_actor==a){story_mail::unsay();talk_actor=0;text.clear();}call(c,GC(0x02515A70),{a+Cyl,2});call(c,GC(0x02515860),{a+Stts,2});call(c,GC(0x025204C8),{a+Phase,a+Name});if(heap_trace())report_heaps(a,"resource_delete",before,heaps(c));result(c,1);}
void is_delete(Cpu*c){result(c,1);}
motion::Blocker link_blocker(uint32_t player){
    motion::Blocker blocker;const uint32_t shape=player+0x765C+0x118;
    const auto p=point(shape);blocker.position=motion_point(p);blocker.radius=number(shape+12);blocker.height=number(shape+16);
    blocker.enabled=motion::valid(blocker.position)&&std::isfinite(blocker.radius)&&blocker.radius>0&&blocker.radius<=1000&&std::isfinite(blocker.height)&&blocker.height>0&&blocker.height<=2000;return blocker;
}
void knockback_movement(Cpu*c,uint32_t a,uint32_t play,uint32_t player){
    const auto*d=declaration(a);if(!d->spec.combat_enabled||combat_blocked(a))return;
    const auto state=read_combat(a);float remaining=number(a+KnockbackLeft);if(state.phase!=uint32_t(combat::Phase::Flinch)){number(a+KnockbackLeft,0);return;}if(remaining==0)return;
    const auto speed=point(a+Knockback);if(!std::isfinite(remaining)||remaining<0||remaining>30||!motion::valid(motion_point(speed))||std::hypot(speed.x,speed.z)>1500.01f){fail_create(a);return;}
    const auto blocker=link_blocker(player);if(!blocker.enabled){number(a+KnockbackLeft,0);return;}
    if(!ld32(a+AcchReady)){if(!mod_actor_knockback::initialize(c,knockback_context(a),number(a+Radius),number(a+Height))){fail_create(a);return;}st32(a+AcchReady,1);}
    const auto from=point(a+0x314);const auto proposed=motion::Point{from.x+speed.x/30,from.y,from.z+speed.z/30};const auto clipped=motion::sweep(motion_point(from),proposed,number(a+Radius),number(a+Height),blocker);
    if(!clipped.valid){number(a+KnockbackLeft,0);return;}
    const bool moved=mod_actor_knockback::move(c,knockback_context(a),play,{clipped.position.x,clipped.position.y,clipped.position.z},50,40,[&](mod_actor_knockback::Point corrected){const auto result=motion::sweep(motion_point(from),{corrected.x,corrected.y,corrected.z},number(a+Radius),number(a+Height),blocker);return result.valid&&!result.blocked;});
    number(a+KnockbackLeft,moved?std::max(0.f,remaining-1):0);
}
void movement(Cpu*c,uint32_t a,uint32_t play,uint32_t player){
    auto state=read_motion(a+Motion);if(!state.enabled||ld16(a+0xF8)==1)return;
    motion::Path path;path.count=state.count;path.speed=state.speed;path.loop=state.loop;for(unsigned i=0;i<16;++i)path.points[i]=motion_point(state.points[i]);
    motion::PathState cursor{state.next,state.finished};const auto from=motion_point(point(a+0x314));
    const auto blocker=link_blocker(player);
    // A malformed native Link cylinder is an admission failure, never a guessed
    // radius that could permit passing through Link.
    if(!blocker.enabled)return;
    const int heading_before=int16_t(ld16(a+0x32A));
    const auto moved=motion::advance(path,cursor,from,1.f/30,number(a+Radius),number(a+Height),blocker,[&](motion::Point old,motion::Point proposed){
        if(motion::horizontal(old,proposed)<1e-6)return old;
        if(!ld32(a+GroundReady)){call(c,GC(0x0237AABC),{a+Ground});st32(a+GroundReady,1);}
        point(a+Ground+0x24,{proposed.x,old.y+100,proposed.z});st32(a+Ground+8,ld32(a+4));
        call(c,GC(0x02008974),{play+0x12A0,a+Ground});const float y=float(c->f[1].ps0);
        if(!std::isfinite(y)||std::fabs(y-old.y)>30)return old;
        const int room=int(call(c,GC(0x024EF130),{play+0x12A0,a+Ground+0x14}));if(room!=int(int8_t(ld8(a+0x326))))return old;
        return motion::Point{proposed.x,y,proposed.z};
    });
    state.next=cursor.next;state.finished=cursor.finished;write_motion(a+Motion,state);
    if(moved.status!=motion::Status::Invalid){
        point(a+0x314,{moved.position.x,moved.position.y,moved.position.z});
        if(moved.has_direction&&moved.traveled>0){
            // Restorable game-step heading; rendering never advances this turn.
            // Quantize the target first so an exact half-turn has a stable sign.
            constexpr double radians=motion::Pi/32768;
            const auto target=int16_t(uint16_t(int32_t(std::lround(double(moved.yaw)/radians))));
            const double current=int16_t(ld16(a+0x32A))*radians;
            const double turned=motion::approach(current,target*radians,8*motion::Pi/180);
            st16(a+0x32A,uint16_t(int32_t(std::lround(turned/radians))));
        }
    }
    heading_movement(a,heading_before,moved);
    auto*d=declaration(a);const std::string clip=moved.traveled>0.001f?"walk":"idle";
    if(std::find(d->clips.begin(),d->clips.end(),clip)!=d->clips.end())animate(c,a,clip,6,true);
}
void look(Cpu*c,uint32_t a,uint32_t player){
    auto*d=declaration(a);if(d->head<0)return;
    auto head=point(a+0x37C);const uint32_t model=ld32(a+Model),block=model?ld32(model+0x2C):0,matrices=block?ld32(block+0x10):0;
    if(matrices&&unsigned(d->head)<ld16(block+0x2C)){const uint32_t m=matrices+uint32_t(d->head)*48;const story_notebook::WorldPoint candidate{number(m+12),number(m+28),number(m+44)};if(motion::valid(motion_point(candidate))&&std::hypot(candidate.x-head.x,candidate.z-head.z)<2000)head=candidate;}
    const auto& spec=d->spec.look_at;constexpr float rad=float(motion::Pi/180);motion::LookLimits limits{spec.yaw_degrees*rad,spec.pitch_degrees*rad,spec.rate_degrees*rad,spec.radius,d->upper>=0,spec.upper_yaw_degrees*rad,spec.upper_fraction};
    motion::LookState state{number(a+LookAngles),number(a+LookAngles+4),number(a+LookAngles+8)};
    auto target=point(player+0x37C);if(!motion::valid(motion_point(target)))target=point(player+0x314);
    if(motion::look_at(motion_point(head),motion_point(target),float(int16_t(ld16(a+0x32A)))*float(motion::Pi/32768),1.f/30,limits,state)){number(a+LookAngles,state.head_yaw);number(a+LookAngles+4,state.head_pitch);number(a+LookAngles+8,state.upper_yaw);} (void)c;
}
bool consume_hit(Cpu*c,uint32_t a,uint32_t player){
    auto*d=declaration(a);const uint32_t cyl=a+Cyl;
    const bool reported=call(c,GC(0x025162A4),{cyl})!=0;if(!reported)return true;
    const uint32_t source_id=ld32(cyl+0xA0),cached=call(c,GC(0x02515BBC),{cyl+0x94}),attack=call(c,GC(0x02516300),{cyl});
    st32(scratch+80,0);call(c,GC(0x025D54C4),{source_id,scratch+80});const uint32_t actor=ld32(scratch+80);
    Hit hit;bool verified_projectile=false;bool valid=d->hits&&(actor==player||d->spec.combat_enabled)&&actor==cached&&actor>=0x10000000u&&ld32(actor+4)==source_id&&attack>=0x10000000u&&attack<0xC0000000u;
    if(valid){const uint32_t status=ld32(attack+0x44);valid=status>=0x10000000u&&status<0xC0000000u&&ld32(status+0xC)==actor&&ld32(status+0x10)==source_id;
        if(valid){hit.attack_type=ld32(attack+0x10);hit.attack_power=ld8(attack+0x14);hit.source_kind=1;hit.source_process_id=source_id;hit.position=point(cyl+0xCC);hit.reaction=point(cyl+0xC0);verified_projectile=d->spec.combat_enabled&&actor!=player&&story_enemy_projectiles::owns_attack(attack);const bool permitted=d->spec.combat_enabled?((actor==player||verified_projectile)&&story_enemy_ownership::pair_policy(attack,cyl)==story_enemy_ownership::PairPolicy::Delegate):bool(hit.attack_type&2);valid=permitted&&motion::valid(motion_point(hit.position))&&motion::valid(motion_point(hit.reaction));}}
    call(c,GC(0x0251621C),{cyl}); // complete native clear ALWAYS precedes guest callback
    if(!valid||ld32(a+HitSequence)==0xFFFFFFFFu)return true;hit.sequence=ld32(a+HitSequence)+1;st32(a+HitSequence,hit.sequence);
    if(d->spec.combat_enabled){const bool stun_only=verified_projectile&&(hit.attack_type==0x40||hit.attack_type==0x8000);if(!stun_only&&!hit.attack_power)return emit(c,a,BehaviorEvent::Hit,&hit);auto state=read_combat(a);const auto result=stun_only?combat::stun_hit(state,combat_config(*d),hit.sequence,combat_blocked(a)):combat::hit(state,combat_config(*d),hit.sequence,hit.attack_power,actor==player&&(hit.attack_type&0x10000),combat_blocked(a));if(result==combat::HitResult::Invalid){fail_create(a);return false;}write_combat(a,state);if(!combat::alive(state))st32(a+0x39C,0);
        if(result==combat::HitResult::Damaged||result==combat::HitResult::Lethal||result==combat::HitResult::Stunned){const auto&reaction=result==combat::HitResult::Lethal?d->spec.combat.death:state.phase==uint32_t(combat::Phase::Stunned)?d->spec.combat.stun:d->spec.combat.flinch;if(!reaction.clip.empty())animate(c,a,reaction.clip,0,false);if(result==combat::HitResult::Damaged&&reaction.knockback_frames){const auto from=point(player+0x314),to=point(a+0x314);const float length=std::hypot(to.x-from.x,to.z-from.z);if(length>0.001f){point(a+Knockback,{(to.x-from.x)/length*reaction.knockback_speed,0,(to.z-from.z)/length*reaction.knockback_speed});number(a+KnockbackLeft,float(reaction.knockback_frames));}}}
    }
    return emit(c,a,BehaviorEvent::Hit,&hit);
}
bool combat_step(Cpu*c,uint32_t a){
    auto*d=declaration(a);if(!d->spec.combat_enabled)return true;if(!valid_actor(a)||ld32(a+Quarantined)){fail_create(a);return false;}auto state=read_combat(a);const auto previous=state.phase;
    if(!combat::tick(state,combat_config(*d),1,combat_blocked(a))){fail_create(a);st32(a+0x39C,0);return false;}write_combat(a,state);
    if(previous!=state.phase&&state.phase==uint32_t(combat::Phase::Alive))animate(c,a,"idle",0,true);
    if(combat::reserve_reward(state)){
        write_combat(a,state);const uint32_t pid=ld32(a+4);CombatReward result=CombatReward::Failed;
        try{if(d->reward)result=d->reward();}catch(...){}
        if(!valid_actor(a)||ld32(a+4)!=pid)return false;
        combat::settle_reward(state,result!=CombatReward::Failed);write_combat(a,state);
        if(result==CombatReward::Committed&&d->spec.native_heart_drop)mod_actor_native_combat::request_heart(c,a+Heart,{a,pid,ld32(a+0xB0),pid,a+Heart},a+0x314,int8_t(ld8(a+0x326)));
    }
    st32(a+0x39C,combat::alive(state)&&!combat_blocked(a)?4:0);return true;
}
void attack_collision(Cpu*c,uint32_t a,uint32_t play){
    const auto*d=declaration(a);if(!d->spec.combat_enabled||combat_blocked(a)||!combat::attacking(read_combat(a)))return;
    const auto&at=d->spec.combat.attack;const float angle=float(int16_t(ld16(a+0x32A)))*float(motion::Pi/32768);auto center=point(a+0x314);center.x+=std::sin(angle)*at.forward_offset;center.z+=std::cos(angle)*at.forward_offset;center.y+=at.y_offset;
    point(a+AttackSource+0x30,center);call(c,GC(0x020182E0),{a+AttackCyl+0x118,a+AttackSource+0x30});call(c,GC(0x02515E50),{a+AttackStts+0x1C});call(c,GC(0x0200E240),{play+0x26A4,a+AttackCyl});
}
bool carry_acch_bound(uint32_t a){
    // Same immutable native context contract as mod_actor_knockback::bound.
    // Full states retain native objects; validate every borrowed write target
    // before CrrPos, never repair a foreign pointer by silently reinitializing it.
    return ld32(a+Acch+0x2C)==a+0x314&&ld32(a+Acch+0x30)==a+AcchOld&&ld32(a+Acch+0x34)==a+AcchSpeed&&ld32(a+Acch+0x38)==0&&ld32(a+Acch+0x3C)==0&&ld32(a+Acch+0x80)==a&&ld32(a+Acch+8)==ld32(a+4)&&ld32(a+Acch+0x84)==mod_actor_knockback::kCircles&&ld32(a+Acch+0x88)==a+AcchCircles;
}
bool carry_step(Cpu*c,uint32_t a,uint32_t play,uint32_t player){
    auto*d=declaration(a);if(!d->spec.carry_enabled)return false;
    auto state=read_carry(a);if(!valid_actor(a)||!carry::valid(state,ld32(a+4))||ld32(a+Quarantined)){carry_quarantine(a);return true;}
    if(state.phase==0&&(ld32(a+0x2E0)&0x2000)){st32(a+0x39C,0);return true;}
    if(state.phase!=1&&(ld32(a+0x2E0)&0x2000)){carry_quarantine(a);return true;}
    if(state.phase==1){if(state.link_pid!=ld32(player+4)||!carry_link(c,player,a)||!(ld32(a+0x2E0)&0x2000)){carry_quarantine(a);return true;}st32(a+0x39C,0);return true;}
    if(state.phase==3&&state.frames==0){if(call(c,GC(0x025D57E0),{a})){state.frames=1;write_carry(a,state);}}
    if(state.phase!=2){st32(a+0x39C,state.phase==0&&!ld32(a+SceneLease)?0x10:0);return state.phase!=0;}
    st32(a+0x39C,0);if(ld32(a+SceneLease)||!carry::advance(state)){carry_quarantine(a);return true;}
    if(!ld32(a+AcchReady)){if(!mod_actor_knockback::initialize(c,knockback_context(a),number(a+Radius),number(a+Height))){carry_quarantine(a);return true;}st32(a+AcchReady,1);}
    const auto before=point(a+0x314);story_notebook::WorldPoint desired{before.x+state.vx,before.y+state.vy,before.z+state.vz};
    if(!motion::valid(motion_point(desired))||!carry_acch_bound(a)){carry_quarantine(a);return true;}
    point(a+AcchOld,before);point(a+AcchSpeed,{state.vx,state.vy,state.vz});point(a+0x314,desired);call(c,GC(0x024F08A8),{a+Acch,play+0x12A0});const auto accepted=point(a+0x314);
    if(!valid_actor(a)||!carry_acch_bound(a)||!motion::valid(motion_point(accepted))||std::hypot(accepted.x-before.x,accepted.z-before.z)>51||std::fabs(accepted.y-before.y)>100){point(a+0x314,before);carry_quarantine(a);return true;}
    const bool ground=state.vy<=0&&(ld32(a+Acch+0x28)&0x20);const bool wall=std::hypot(accepted.x-desired.x,accepted.z-desired.z)>.5f;
    if(wall&&!ground&&!d->spec.carry_break_on_impact){state.vx=state.vz=0;write_carry(a,state);return true;}
    if(ground||wall){carry::impact(state,d->spec.carry_break_on_impact);write_carry(a,state);if(d->spec.carry_break_on_impact){bool committed=false;try{if(d->carry_break)committed=d->carry_break();}catch(...){}if(!valid_actor(a))return true;if(!committed){carry_quarantine(a);return true;}if(call(c,GC(0x025D57E0),{a}))state.frames=1;write_carry(a,state);}else write_motion(a+Motion,read_motion(a+CarryMotion));}
    else write_carry(a,state);return true;
}
void execute(Cpu*c){uint32_t a=c->r[3];auto*d=declaration(a);if(!d){result(c,1);return;}HeadingExecuteScope heading_scope(a);uint32_t play=call(c,GC(0x025200D4),{});if(!play||!ld32(play+0x5B34)){result(c,1);return;}const uint32_t player=ld32(play+0x5B34);if(restored.erase({a,ld32(a+4)})){call(c,GC(0x0251621C),{a+Cyl});if(!emit(c,a,BehaviorEvent::CreateRestored)){result(c,1);return;}}if(!combat_step(c,a)||!emit(c,a,BehaviorEvent::Execute)||!consume_hit(c,a,player)){result(c,1);return;}const bool carry_motion=carry_step(c,a,play,player);knockback_movement(c,a,play,player);if(!carry_motion&&(!d->spec.combat_enabled||read_combat(a).phase==uint32_t(combat::Phase::Alive)))movement(c,a,play,player);look(c,a,player);if(const uint32_t morf=ld32(a+Morf))call(c,GC(0x025E535C),{morf,a+0x314,0,0});if(ld32(a+CollisionFlags)&2)for(unsigned i=0;i<3;++i)st32(a+0x314+i*4,f32_as_u32(u32_as_f32(ld32(a+0x314+i*4))+u32_as_f32(ld32(a+Stts+i*4))));call(c,GC(0x02515E50),{a+Stts+0x1C});for(unsigned i=0;i<3;++i){float v=u32_as_f32(ld32(a+0x314+i*4))+(i==1?(d->spec.carry_enabled?30.f:number(a+Height)*0.7f):0);st32(a+0x37C+i*4,f32_as_u32(v));st32(a+0x390+i*4,f32_as_u32(v));}call(c,GC(0x020182E0),{a+Cyl+0x118,a+0x314});c->f[1].ps0=number(a+Radius);call(c,GC(0x020184DC),{a+Cyl+0x118});c->f[1].ps0=number(a+Height);call(c,GC(0x02018428),{a+Cyl+0x118});st32(a+Cyl+0x18,d->hits?0x1F:0);st32(a+Cyl+0x28,d->spec.combat_enabled?0xFFFFFFFFu:d->hits?2u:0u);st32(a+Cyl+0x94,d->hits?6:0);if(!carry_busy(a)&&(ld32(a+CollisionFlags)&1)&&(!d->spec.combat_enabled||combat::alive(read_combat(a))))call(c,GC(0x0200E240),{play+0x26A4,a+Cyl});attack_collision(c,a,play);
if(ld16(a+0xF8)==1){
    const uint32_t player=ld32(play+0x5B34);
    if(player){const float dx=u32_as_f32(ld32(player+0x314))-u32_as_f32(ld32(a+0x314)),dz=u32_as_f32(ld32(player+0x31C))-u32_as_f32(ld32(a+0x31C));st16(a+0x32A,uint16_t(int32_t(std::atan2(dx,dz)*32768.0/3.14159265358979323846)));}
    // Guest talk state is independent of native message IDs: zero is a successful messageSet result.
    // 0 = inactive, 1 = native message started, 2 = closed, waiting for event reset.
    if(!talk_actor&&ld32(a+Talk)!=2&&story_mail::can_say()){
        const bool resumed=ld32(a+Talk)==1;talk_actor=a;
        if(!resumed){animate(c,a,"talk",6,true);if(!emit(c,a,BehaviorEvent::TalkBegin)){result(c,1);return;}}story_mail::append_utf16(d->spec.lines,true,text);story_mail::say(0x2CF3,&text);
        uint32_t message=0;
        if(!resumed){message=call(c,GC(0x025F7DB0),{ld32(GD(0x101F4B5C)),0x2CF3,a+0x37C});
            if(message!=0xFFFFFFFFu)st32(a+Talk,1);
            else {story_mail::unsay();talk_actor=0;text.clear();st32(a+Talk,2);animate(c,a,"idle",6,true);if(!emit(c,a,BehaviorEvent::TalkCancel)){result(c,1);return;}}}
        if(talk_trace())LOG("[mod_actor_talk] %s actor=%08X message=%08X state=%u",resumed?"resume":"start",a,message,ld32(a+Talk));
    }else if(talk_actor==a){
        const uint32_t status=call(c,GC(0x025F795C),{ld32(GD(0x101F4B5C))});
        if(talk_trace()&&ld32(a+TalkStatus)!=status){LOG("[mod_actor_talk] status actor=%08X state=%u status=%u",a,ld32(a+Talk),status);st32(a+TalkStatus,status);}
        if(status==0xE){
            // Native NPC next_msgStatus chooses ENDS at DISPLAYED; the native window then handles A.
            call(c,GC(0x025F74D0),{ld32(GD(0x101F4B5C)),0x10});
        }else if(status==0x12){
            call(c,GC(0x025F74D0),{ld32(GD(0x101F4B5C)),0x13});
            st16(play+0x51D0+0xE8,ld16(play+0x51D0+0xE8)|8);
            story_mail::unsay();talk_actor=0;text.clear();st32(a+Talk,2);
            if(talk_trace())LOG("[mod_actor_talk] finish actor=%08X command=%u event_flags=%04X",a,unsigned(ld16(a+0xF8)),unsigned(ld16(play+0x51D0+0xE8)));
            animate(c,a,"idle",6,true);if(!emit(c,a,BehaviorEvent::TalkEnd)){result(c,1);return;}if(d->talked)d->talked();
        }
    }
}else {
    if(talk_actor==a){if(talk_trace())LOG("[mod_actor_talk] cancel actor=%08X",a);story_mail::unsay();talk_actor=0;text.clear();animate(c,a,"idle",6,true);if(!emit(c,a,BehaviorEvent::TalkCancel)){result(c,1);return;}}
    st32(a+Talk,0);st32(a+TalkStatus,0);st16(a+0xFA,ld16(a+0xFA)|1);
}
transform(a);heading_scope.complete=true;result(c,1);}
void trace_pose(uint32_t actor,uint32_t model,uint32_t morf){
    static int remaining=[](){const char* value=std::getenv("WWHD_MOD_ACTOR_POSE_TRACE");return value?std::clamp(std::atoi(value),0,120):0;}();
    if(!remaining||!morf)return;const uint32_t joints=ld32(model+0x2C),matrix=joints?ld32(joints+0x10):0;const unsigned count=joints?ld16(joints+0x2C):0;
    if(!matrix||!count||count>32)return;--remaining;
    const uint32_t data=ld32(model+0xAC),resource=data?ld32(data+8):0;
    LOG("[mod_actor_pose_identity] actor=%08X model=%08X morf=%08X modeldata=%08X resource=%08X",actor,model,morf,data,resource);
    for(unsigned j=0;j<count;++j){float m[12];for(unsigned k=0;k<12;++k)m[k]=u32_as_f32(ld32(matrix+j*48+k*4));
        LOG("[mod_actor_pose] actor=%08X model=%08X frame=%.6f joint=%u matrix=%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f",actor,model,double(u32_as_f32(ld32(morf+0x9C))),j,double(m[0]),double(m[1]),double(m[2]),double(m[3]),double(m[4]),double(m[5]),double(m[6]),double(m[7]),double(m[8]),double(m[9]),double(m[10]),double(m[11]));}
}
void draw(Cpu*c){uint32_t a=c->r[3];const int heading_before=int16_t(ld16(a+0x32A));if(!emit(c,a,BehaviorEvent::Draw)){result(c,1);return;}uint32_t m=ld32(a+Model);if(m){uint32_t env=call(c,GC(0x02555D0C),{});call(c,GC(0x025626A4),{env,0,a+0x314,a+0x110});call(c,GC(0x02562F5C),{env,m,a+0x110});mod_actor_shadow_proof::begin_model(m);if(const uint32_t morf=ld32(a+Morf)){PoseScope scope(morf);if(scope.root)call(c,GC(0x025E54D8),{morf});}else call(c,GC(0x025E2DE0),{m,0});trace_pose(a,m,ld32(a+Morf));heading_draw(a,heading_before,m);mod_actor_shadow_proof::end_model();}result(c,1);}
void resource_table(Cpu*c){
    const uint32_t info=c->r[3],key=c->r[4],heap=c->r[5];
    const uint32_t chars=key?ld32(key):0;
    const Decl* found=nullptr;
    if(chars)for(const auto& d:declarations)if(std::strncmp(reinterpret_cast<const char*>(mem::ptr(chars)),d.archive.c_str(),64)==0){found=&d;break;}
    if(!found){if(!story_item_models::resource_table(c))original(c,GC(0x02604550));return;}
    const Cpu incoming=*c;
    if(archive_measurement&&archive_measurement->declaration==found){archive_measurement->heap=heap;archive_measurement->info=info;}
    const uint32_t before=heap_trace()?heap_free(c,heap):0;
    const std::string filename=found->archive+".bmd";
    std::memcpy(mem::ptr(scratch+128),filename.c_str(),filename.size()+1);
    st32(scratch+200,scratch+128);st32(scratch+204,GD(0x10012504));
    const uint32_t count=uint32_t(found->index)+1+uint32_t(found->clips.size());
    call(c,GC(0x02604470),{info,count,heap});call(c,GC(0x026044E0),{info,count,heap});
    const uint32_t model_before=heap_trace()?heap_free(c,heap):0;
    call(c,GC(0x02608308),{info,uint32_t(found->index),uint32_t(found->index),scratch+200,heap});
    for(size_t i=0;i<found->clips.size();++i){
        const std::string clip=found->clips[i]+".bck";std::memcpy(mem::ptr(scratch+128),clip.c_str(),clip.size()+1);
        const uint32_t index=uint32_t(found->index)+1+uint32_t(i);call(c,GC(0x0260873C),{info,index,index,scratch+200,heap});
    }
    if(heap_trace()){const uint32_t after=heap_free(c,heap);LOG("[mod_actor_heaps] archive=%s stage=resource_model heap=%08X free_before=%u free_after=%u delta=%lld",found->archive.c_str(),heap,model_before,after,static_cast<long long>(model_before)-after);}
    if(heap_trace()){const uint32_t after=heap_free(c,heap);LOG("[mod_actor_heaps] archive=%s stage=resource_table heap=%08X free_before=%u free_after=%u delta=%lld",found->archive.c_str(),heap,before,after,static_cast<long long>(before)-after);}
    LOG("[mod_actor] native archive table %s index=%d model=%08X",found->archive.c_str(),found->index,ld32(info+0x60)?ld32(ld32(info+0x60)+uint32_t(found->index)*4):0);
    *c=incoming;
}
void backing_archive_wrapper(Cpu*c){
    const uint32_t manager=c->r[3],key=c->r[4],allocation_heap=c->r[6],chars=key?ld32(key):0;const Decl* own=nullptr;
    if(chars)for(const auto& d:declarations)if(std::strncmp(reinterpret_cast<const char*>(mem::ptr(chars)),d.archive.c_str(),64)==0){own=&d;break;}
    if(!own){if(!story_item_models::backing_archive_wrapper(c))original(c,GC(0x02612E64));return;}
    const Cpu incoming=*c;const uint32_t before=heap_free(c,allocation_heap);
    const bool cached=call(c,GC(0x02612504),{manager,key})!=0;
    if(!cached&&before<budget::BackingReserve+budget::ModelLimit){*c=incoming;result(c,0);LOG("[mod_actor_budget] backing heap reserve prevents loading %s",own->archive.c_str());return;}
    *c=incoming;
    original(c,GC(0x02612E64));const Cpu returned=*c;
    const uint32_t after=heap_free(c,allocation_heap);
    uint32_t resource=0,buffer=0,bytes=0,owner=0;
    if(returned.r[3]){resource=call(c,GC(0x026123FC),{manager,key});if(resource){buffer=ld32(resource+0x14);bytes=ld32(resource+0x18);if(buffer)owner=call(c,GC(0x02755FEC),{ld32(GD(0x101F8B4C)),buffer});}}
    if(returned.r[3]&&before>after)model_cost(own,8,before-after);
    if(heap_trace())LOG("[mod_actor_heaps] archive=%s stage=archive_backing heap=%08X free_before=%u free_after=%u delta=%lld resource=%08X buffer=%08X buffer_bytes=%u buffer_owner=%08X",own->archive.c_str(),allocation_heap,before,after,static_cast<long long>(before)-after,resource,buffer,bytes,owner);
    *c=returned;
}
void archive_wrapper(Cpu*c){
    const uint32_t key=c->r[4],chars=key?ld32(key):0;const Decl* own=nullptr;
    if(chars)for(const auto& d:declarations)if(std::strncmp(reinterpret_cast<const char*>(mem::ptr(chars)),d.archive.c_str(),64)==0){own=&d;break;}
    if(!own){if(!story_item_models::archive_wrapper(c))original(c,GC(0x02604EB4));return;}
    ArchiveMeasurement current{own,0,0};ArchiveMeasurement* previous=archive_measurement;archive_measurement=&current;
    try{original(c,GC(0x02604EB4));}catch(...){archive_measurement=previous;throw;}
    archive_measurement=previous;const Cpu returned=*c;
    if(returned.r[3]&&current.heap){const uint32_t size=heap_size(c,current.heap),free=heap_free(c,current.heap);model_cost(own,12,size);if(heap_trace())LOG("[mod_actor_heaps] archive=%s stage=archive_adjusted heap=%08X info=%08X size=%u free=%u used=%u",own->archive.c_str(),current.heap,current.info,size,free,size>=free?size-free:0);}
    *c=returned;
}
void base_allocated(uint32_t token,uint32_t,uint32_t bytes){if(token&&token<=declarations.size())st32(record(token-1)+24,bytes);}
}
size_t checkpoint(){return declarations.size();}
void rollback(size_t count) noexcept{if(!installed&&count<=declarations.size())declarations.resize(count);}
void configure(void(*o)(Cpu*,uint32_t),uint32_t(*i)(Cpu*,uint32_t,std::initializer_list<uint32_t>),uint32_t s){original=o;invoke=i;scratch=s;native_actor_profiles::configure(o,i);declarations.reserve(128);}
bool live_ready(){return installed;}
namespace {
// declare's checks, shared with redeclare so both validate identically: false for a spec the
// model can't serve; head/upper are the look_at joints
bool checked(const story_notebook::ManifestActor&spec,const std::string&archive,int index,story_notebook::WorldPoint minimum,story_notebook::WorldPoint maximum,bool skinned,const std::vector<std::string>&clips,const ModelRig&rig,int&head,int&upper){
    if(clips.size()>8||(!skinned&&!clips.empty())||(!spec.animation.empty()&&std::find(clips.begin(),clips.end(),spec.animation)==clips.end()))return false;
    for(size_t i=0;i<clips.size();++i)if(!story_mail::valid_id(clips[i])||std::find(clips.begin(),clips.begin()+i,clips[i])!=clips.begin()+i)return false;
    if(rig.joint_names.size()!=rig.joint_parents.size()||rig.joint_names.size()>32)return false;
    head=-1;upper=-1;
    if(spec.look_at.enabled){if(!skinned||rig.joint_look_compatible.size()!=rig.joint_names.size()||std::fabs(std::remainder(spec.rotation.x,360.f))>1e-4||std::fabs(std::remainder(spec.rotation.z,360.f))>1e-4||spec.scale.x<=0||std::fabs(spec.scale.x-spec.scale.y)>1e-4||std::fabs(spec.scale.x-spec.scale.z)>1e-4)return false;const auto find=[&](const std::string&name){const auto it=std::find(rig.joint_names.begin(),rig.joint_names.end(),name);return it==rig.joint_names.end()?-1:int(it-rig.joint_names.begin());};head=find(spec.look_at.head);if(head<0||!rig.joint_look_compatible[size_t(head)])return false;
        if(!spec.look_at.upper_bone.empty()){upper=find(spec.look_at.upper_bone);if(upper<0||upper>=head||!rig.joint_look_compatible[size_t(upper)])return false;int parent=rig.joint_parents[size_t(head)];unsigned limit=0;while(parent>=0&&parent!=upper&&++limit<=32){if(size_t(parent)>=rig.joint_parents.size())return false;parent=rig.joint_parents[size_t(parent)];}if(parent!=upper)return false;}}
    const float lo[]={minimum.x,minimum.y,minimum.z},hi[]={maximum.x,maximum.y,maximum.z};for(unsigned i=0;i<3;++i)if(!std::isfinite(lo[i])||!std::isfinite(hi[i])||lo[i]>hi[i])return false;
    if(archive.empty()||archive.size()>63||index<0||index>16)return false;
    if(spec.combat_enabled){if(!skinned||spec.defeated_flag.empty()||spec.combat.stun.clip.empty()||spec.combat.death.clip.empty()||spec.pushable||spec.collision_radius>200||spec.collision_height>400||std::find(clips.begin(),clips.end(),"idle")==clips.end())return false;for(const auto*reaction:{&spec.combat.flinch,&spec.combat.stun,&spec.combat.death})if(!reaction->clip.empty()&&std::find(clips.begin(),clips.end(),reaction->clip)==clips.end())return false;}
    if(spec.carry_enabled&&(spec.combat_enabled||spec.pushable||spec.look_at.enabled||!std::isfinite(spec.collision_radius)||!std::isfinite(spec.collision_height)||spec.collision_radius<=0||spec.collision_radius>100||spec.collision_height<=0||spec.collision_height>200||spec.rotation.x!=0||spec.rotation.z!=0||spec.scale.x!=1||spec.scale.y!=1||spec.scale.z!=1||!spec.lines.empty()||!spec.then.empty()||(spec.carry_break_on_impact&&spec.broken_flag.empty())))return false;
    return true;
}
}
uint32_t declare(const std::string&mod,const story_notebook::ManifestActor&spec,const std::string&archive,int index,std::function<void()> talked,story_notebook::WorldPoint minimum,story_notebook::WorldPoint maximum,bool skinned,std::vector<std::string> clips,ModelRig rig){
    int head=-1,upper=-1;
    if(!checked(spec,archive,index,minimum,maximum,skinned,clips,rig,head,upper)||declarations.size()>=128)return 0;
    declarations.push_back({mod,archive,spec,index,std::move(talked),minimum,maximum,skinned,std::move(clips),std::move(rig),head,upper,spec.combat_enabled,{}});declarations.back().fingerprint=story_actors::identity::fingerprint(mod+std::string(1,'\0')+archive,spec);if(installed)inherit_measurements(declarations.size()-1);return uint32_t(declarations.size());
}
bool redeclare(uint32_t token,const story_notebook::ManifestActor&spec,const std::string&archive,int index,story_notebook::WorldPoint minimum,story_notebook::WorldPoint maximum,bool skinned,std::vector<std::string> clips,ModelRig rig,bool apply){
    if(!token||token>declarations.size())return false;auto&d=declarations[token-1];if(d.spec.id!=spec.id)return false;
    // combat and carry keep install-time hooks, reward/break sinks and saved identities: a restart applies them
    if(d.spec.combat_enabled||spec.combat_enabled||d.spec.carry_enabled||spec.carry_enabled)return false;
    int head=-1,upper=-1;if(!checked(spec,archive,index,minimum,maximum,skinned,clips,rig,head,upper))return false;
    if(!apply)return true;
    d.spec=spec;d.archive=archive;d.index=index;d.minimum=minimum;d.maximum=maximum;d.skinned=skinned;d.clips=std::move(clips);d.rig=std::move(rig);d.head=head;d.upper=upper;
    d.fingerprint=story_actors::identity::fingerprint(d.mod+std::string(1,'\0')+archive,spec);if(installed)inherit_measurements(size_t(token-1));return true;
}
bool install(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){if(installed)return true;const bool has_carry=std::any_of(declarations.begin(),declarations.end(),[](const Decl&d){return d.spec.carry_enabled;});if(has_carry){for(const auto&d:declarations)if(d.spec.carry_break_on_impact&&!d.carry_break)return false;if(!native_actor_carry::configure(original,invoke)||!native_actor_carry::register_guard("mod-actor-carry",deny_carry)||!native_actor_carry::available(owner)||!owner(GC(0x0240A41C)).empty()||!owner(GC(0x023DCF8C)).empty()||!mod_actor_knockback::configure(invoke,knockback_identity))return false;}const bool has_combat=std::any_of(declarations.begin(),declarations.end(),[](const Decl&d){return d.spec.combat_enabled;});if(has_combat&&(!attention::configure(original,combat_priority)||!attention::available(owner)||!mod_actor_native_combat::configure(invoke,source_identity)||!mod_actor_knockback::configure(invoke,knockback_identity)))return false;if(declarations.empty()||!scratch||!invoke||!original||!native_actor_profiles::available(owner)||!owner(GC(0x02604550)).empty()||!owner(GC(0x02604EB4)).empty()||!owner(GC(0x02612E64)).empty())return false;std::memset(mem::ptr(scratch+256),0,kScratchBytes-256);uint32_t native=ld32(GD(0x101F3EE0)+0xB6*4);if(!native)return false;for(unsigned i=0;i<0x30;i+=4)st32(scratch+i,ld32(native+i));st16(scratch+8,kProfile);st32(scratch+0x10,Size);st32(scratch+0x14,0);st32(scratch+0x18,0);st32(scratch+0x24,scratch+0x30);st32(scratch+0x28,0x44100);st8(scratch+0x2C,0);st8(scratch+0x2D,14);void(*methods[])(Cpu*)={create,destroy,execute,is_delete,draw};for(unsigned i=0;i<5;++i)st32(scratch+0x30+i*4,dispatch::register_host(methods[i],"mod_actor_method"));LOG("[mod_actor] profile=%08X execute=%08X class_bytes=%u",scratch,ld32(scratch+0x38),Size);heap_callback=dispatch::register_host(heap,"mod_actor_heap");look_callback=dispatch::register_host(pre_joint,"mod_actor_look_joint");post_callback=dispatch::register_host(post_joint,"mod_actor_look_post_joint");if(!native_actor_profiles::add(kProfile,scratch,base_allocated)||!native_actor_profiles::install(owner,replace))return false;replace(GC(0x02604550),resource_table);replace(GC(0x02604EB4),archive_wrapper);replace(GC(0x02612E64),backing_archive_wrapper);if(has_combat){story_enemy_ownership::set_mod_collider_verifier(classify_combat_collider);if(!attention::install(owner,replace))return false;}if(has_carry){if(!native_actor_carry::install(owner,replace))return false;replace(GC(0x0240A41C),carry_attach);replace(GC(0x023DCF8C),carry_release);}installed=true;return true;}
bool set_carry_break(const std::string&mod,const std::string&id,std::function<bool()>cb){if(installed||!cb)return false;for(auto&d:declarations)if(d.mod==mod&&d.spec.id==id&&d.spec.carry_break_on_impact){d.carry_break=std::move(cb);return true;}return false;}

void set_behavior_sink(BehaviorSink sink){behavior_sink=std::move(sink);}
void quarantine_combat(Cpu*c,const Identity&id){const uint32_t a=resolve(c,id);if(!a)return;if(declaration(a)->spec.combat_enabled)fail_create(a);if(declaration(a)->spec.carry_enabled){const uint32_t play=call(c,GC(0x025200D4),{}),link=play?ld32(play+0x5B34):0;if(carry_link(c,link,a))call(c,GC(0x023DCF8C),{link});carry_quarantine(a);}}
bool set_combat_reward(const std::string&mod,const std::string&id,CombatRewardSink sink){for(auto&d:declarations)if(d.mod==mod&&d.spec.id==id&&d.spec.combat_enabled&&!d.reward&&sink){d.reward=std::move(sink);return true;}return false;}
uint32_t declaration_token(const std::string&mod,const std::string&id){for(size_t i=0;i<declarations.size();++i)if(declarations[i].mod==mod&&declarations[i].spec.id==id)return uint32_t(i+1);return 0;}
bool identify(Cpu*c,uint32_t actor,Identity&out){if(!valid_actor(actor))return false;const auto*d=declaration(actor);Identity candidate{d->mod,d->spec.id,ld32(actor+0xB0),ld32(actor+4)};if(resolve(c,candidate)!=actor)return false;out=std::move(candidate);return true;}
uint32_t resolve(Cpu*c,const Identity&id){
    KeepCpu keep(c);
    if(!scratch||!id.token||id.token>declarations.size())return 0;const auto&d=declarations[id.token-1];if(d.mod!=id.mod||d.spec.id!=id.id)return 0;
    const uint32_t actor=ld32(record(id.token-1));if(!valid_actor(actor)||ld32(actor+4)!=id.process_id)return 0;
    st32(scratch+80,0);call(c,GC(0x025D54C4),{id.process_id,scratch+80});return ld32(scratch+80)==actor?actor:0;
}
bool combat_snapshot(Cpu*c,const Identity&id,CombatState&out){
    const uint32_t a=resolve(c,id);if(!a)return false;const auto*d=declaration(a);if(!d->spec.combat_enabled)return false;
    const auto s=read_combat(a);if(!combat::valid(s,combat_config(*d)))return false;
    CombatState next;next.health=s.health;next.max_health=d->spec.combat.health;next.phase=s.phase;next.flags=(combat::alive(s)?1u:0u)|(combat::attacking(s)?2u:0u)|(ld32(a+SceneLease)?4u:0u)|((ld32(a+Quarantined)||s.reward==uint32_t(combat::Reward::Failed))?8u:0u);next.last_damage_sequence=s.last_sequence;next.reward_status=s.reward;mod_actor_native_combat::HeartState heart{};if(mod_actor_native_combat::read_heart_state(a+Heart,heart))next.native_drop_status=uint32_t(heart.disposition);
    next.invulnerability_frames=uint32_t(std::ceil(s.invulnerability_left));next.reaction_frames=uint32_t(std::ceil(s.reaction_left));next.attack_active_frames=uint32_t(std::ceil(s.active_left));next.attack_cooldown_frames=uint32_t(std::ceil(s.cooldown_left));out=next;return true;
}
bool attack_begin(Cpu*c,const Identity&id){const uint32_t a=resolve(c,id);if(!a)return false;const auto*d=declaration(a);if(!d->spec.combat_enabled)return false;auto s=read_combat(a);if(!combat::begin_attack(s,combat_config(*d),combat_blocked(a)))return false;write_combat(a,s);return true;}
bool attack_cancel(Cpu*c,const Identity&id){const uint32_t a=resolve(c,id);if(!a)return false;const auto*d=declaration(a);if(!d->spec.combat_enabled||combat_blocked(a))return false;auto s=read_combat(a);if(!combat::valid(s,combat_config(*d)))return false;combat::cancel_attack(s);write_combat(a,s);return true;}
bool snapshot(Cpu*c,const Identity&id,State&out){const uint32_t a=resolve(c,id);if(!a)return false;auto*d=declaration(a);out.position=point(a+0x314);out.rotation={float(int16_t(ld16(a+0x328)))*180.f/32768,float(int16_t(ld16(a+0x32A)))*180.f/32768,float(int16_t(ld16(a+0x32C)))*180.f/32768};out.collision_radius=number(a+Radius);out.collision_height=number(a+Height);out.collision_enabled=ld32(a+CollisionFlags)&1;out.pushable=ld32(a+CollisionFlags)&2;out.scene_owned=ld32(a+SceneLease)!=0;const unsigned index=ld32(a+ClipIndex);out.animation=index<d->clips.size()?d->clips[index]:std::string();const uint32_t morf=ld32(a+Morf);out.animation_frame=morf?number(morf+0x9C):0;return true;}
bool set_position(Cpu*c,const Identity&id,story_notebook::WorldPoint p){const uint32_t a=resolve(c,id);if(!a||carry_busy(a)||ld32(a+SceneLease)||!motion::valid(motion_point(p)))return false;point(a+0x314,p);transform(a);return true;}
bool rotation_impl(uint32_t a,story_notebook::WorldPoint r){if((declaration(a)->spec.look_at.enabled||declaration(a)->spec.combat_enabled||declaration(a)->spec.carry_enabled)&&(std::fabs(std::remainder(r.x,360.f))>1e-4||std::fabs(std::remainder(r.z,360.f))>1e-4))return false;if(!std::isfinite(r.x)||!std::isfinite(r.y)||!std::isfinite(r.z)||r.x<-360||r.x>360||r.y<-360||r.y>360||r.z<-360||r.z>360)return false;st16(a+0x328,uint16_t(int32_t(r.x*32768/180)));st16(a+0x32A,uint16_t(int32_t(r.y*32768/180)));st16(a+0x32C,uint16_t(int32_t(r.z*32768/180)));transform(a);return true;}
bool set_rotation(Cpu*c,const Identity&id,story_notebook::WorldPoint r){const uint32_t a=resolve(c,id);return a&&!carry_busy(a)&&!ld32(a+SceneLease)&&rotation_impl(a,r);}
bool set_collision(Cpu*c,const Identity&id,float radius,float height,bool enabled,bool pushable){const uint32_t a=resolve(c,id);if(!a||carry_busy(a)||ld32(a+SceneLease)||!std::isfinite(radius)||radius<=0||radius>1000||!std::isfinite(height)||height<=0||height>2000)return false;if(declaration(a)->spec.carry_enabled&&(pushable||!enabled||radius>100||height>200))return false;if(declaration(a)->spec.combat_enabled||declaration(a)->spec.carry_enabled){if(pushable||radius>200||height>400)return false;if(ld32(a+AcchReady)&&!mod_actor_knockback::update_dimensions(knockback_context(a),radius,height))return false;}number(a+Radius,radius);number(a+Height,height);st32(a+CollisionFlags,(enabled?1:0)|(pushable?2:0));st8(a+Stts+0x14,pushable?100:255);return true;}
bool has_animation(Cpu*c,const Identity&id,const std::string&clip){const uint32_t a=resolve(c,id);if(!a)return false;const auto*d=declaration(a);return std::find(d->clips.begin(),d->clips.end(),clip)!=d->clips.end();}
bool set_animation(Cpu*c,const Identity&id,const std::string&clip,float blend,bool loop){const uint32_t a=resolve(c,id);return a&&!carry_busy(a)&&!ld32(a+SceneLease)&&(!declaration(a)->spec.combat_enabled||(read_combat(a).phase==uint32_t(combat::Phase::Alive)&&!ld32(a+Quarantined)))&&animate(c,a,clip,blend,loop);}
uint32_t userdata_address(Cpu*c,const Identity&id){const uint32_t a=resolve(c,id);return a?a+Userdata:0;}
uint32_t hit_event_address(Cpu*c,const Identity&id){const uint32_t a=resolve(c,id);return a?a+HitPacket:0;}
uint32_t behavior_handle(Cpu*c,const Identity&id){const uint32_t a=resolve(c,id);return a?ld32(a+BehaviorHandle):0;}
bool set_behavior_handle(Cpu*c,const Identity&id,uint32_t handle){const uint32_t a=resolve(c,id);if(!a)return false;st32(a+BehaviorHandle,handle);return true;}
bool set_hit_notifications(const std::string&mod,const std::string&id,bool enabled){const uint32_t token=declaration_token(mod,id);if(!token)return false;declarations[token-1].hits=enabled||declarations[token-1].spec.combat_enabled;return true;}
bool get_motion(Cpu*c,const Identity&id,MotionState&out){const uint32_t a=resolve(c,id);if(!a)return false;out=read_motion(a+Motion);return true;}
bool apply_motion(uint32_t a,const MotionState&m){
    if(!m.enabled){write_motion(a+Motion,m);return true;}motion::Path path;path.count=m.count;path.loop=m.loop;path.speed=m.speed;for(unsigned i=0;i<16;++i)path.points[i]=motion_point(m.points[i]);if(!motion::valid(path)||m.next>=m.count)return false;write_motion(a+Motion,m);return true;
}
bool set_motion(Cpu*c,const Identity&id,const MotionState&m){const uint32_t a=resolve(c,id);return a&&!carry_busy(a)&&!ld32(a+SceneLease)&&apply_motion(a,m);}
bool path_impl(uint32_t a,const std::vector<story_notebook::WorldPoint>&points,float speed,bool loop){if(points.size()<2||points.size()>16)return false;MotionState m;m.count=unsigned(points.size());m.speed=speed;m.loop=loop;m.enabled=true;for(unsigned i=0;i<m.count;++i)m.points[i]=points[i];return apply_motion(a,m);}
bool start_path(Cpu*c,const Identity&id,const std::vector<story_notebook::WorldPoint>&points,float speed,bool loop){const uint32_t a=resolve(c,id);return a&&!carry_busy(a)&&!ld32(a+SceneLease)&&path_impl(a,points,speed,loop);}
bool stop_path(Cpu*c,const Identity&id){const uint32_t a=resolve(c,id);if(!a||carry_busy(a)||ld32(a+SceneLease))return false;auto state=read_motion(a+Motion);state.enabled=false;write_motion(a+Motion,state);if(!declaration(a)->spec.combat_enabled||read_combat(a).phase==uint32_t(combat::Phase::Alive))animate(c,a,"idle",6,true);return true;}
bool begin_scene(Cpu*c,const Identity&id,uint32_t lease){const uint32_t a=resolve(c,id);if(!a||!lease||carry_busy(a)||ld32(a+SceneLease))return false;write_motion(a+SavedMotion,read_motion(a+Motion));st32(a+SavedClip,ld32(a+ClipIndex));st32(a+SavedLoop,ld32(a+ClipLoop));number(a+SavedFrame,ld32(a+Morf)?number(ld32(a+Morf)+0x9C):0);st32(a+SceneLease,lease);if(declaration(a)->spec.combat_enabled){auto state=read_combat(a);combat::cancel_attack(state);write_combat(a,state);st32(a+0x39C,0);}auto stopped=read_motion(a+Motion);stopped.enabled=false;write_motion(a+Motion,stopped);return true;}
bool end_scene(Cpu*c,const Identity&id,uint32_t lease){const uint32_t a=resolve(c,id);if(!a||!lease||carry_busy(a)||ld32(a+SceneLease)!=lease)return false;write_motion(a+Motion,read_motion(a+SavedMotion));const auto*d=declaration(a);const uint32_t clip=ld32(a+SavedClip);if(clip<d->clips.size()){const float frame=number(a+SavedFrame);if(!std::isfinite(frame)||frame<0||frame>900)return false;st32(a+ClipIndex,0xFFFFFFFFu);if(!animate(c,a,d->clips[clip],0,ld32(a+SavedLoop)!=0))return false;if(ld32(a+Morf))number(ld32(a+Morf)+0x9C,frame);}st32(a+SceneLease,0);return true;}
bool scene_path(Cpu*c,const Identity&id,uint32_t lease,const std::vector<story_notebook::WorldPoint>&points,float speed){const uint32_t a=resolve(c,id);return a&&lease&&ld32(a+SceneLease)==lease&&path_impl(a,points,speed,false);}
bool scene_rotation(Cpu*c,const Identity&id,uint32_t lease,story_notebook::WorldPoint degrees){const uint32_t a=resolve(c,id);return a&&lease&&ld32(a+SceneLease)==lease&&rotation_impl(a,degrees);}
bool scene_animation(Cpu*c,const Identity&id,uint32_t lease,const std::string&clip,float blend,bool loop){const uint32_t a=resolve(c,id);return a&&lease&&ld32(a+SceneLease)==lease&&animate(c,a,clip,blend,loop);}
void state_loaded(){restored.clear();if(scratch)for(size_t i=0;i<declarations.size();++i){const uint32_t a=ld32(record(i));if(valid_actor(a)){restored.insert({a,ld32(a+4)});st32(a+BehaviorHandle,0);if(declaration(a)->spec.combat_enabled){auto state=read_combat(a);if(combat::abandon_inflight_reward(state))write_combat(a,state);}}}mod_actor_shadow_proof::state_loaded();if(talk_actor)story_mail::unsay();talk_actor=0;text.clear();}
bool talking(uint32_t actor){return actor&&talk_actor==actor;}
bool failed(uint32_t token){return scratch&&token&&token<=128&&ld8(scratch+256+token-1)!=0;}
void retry(uint32_t token){if(scratch&&token&&token<=128)st8(scratch+256+token-1,0);}
}
