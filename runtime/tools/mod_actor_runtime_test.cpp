#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "mods/mod_actor.h"
#include "mods/story_world_collision_native.h"
#include "mods/mod_actor_look_bounds.h"
#include "mods/mod_actor_combat_core.h"
#include "mods/story_enemy_ownership.h"
#include "mods/story_mail.h"
#include "runtime.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
unsigned clip_recalculations=0;std::vector<uint32_t> clip_far_values;
unsigned heading_exec_logs=0,heading_path_logs=0,heading_draw_logs=0;
void log_msg(const char*fmt,...){if(std::getenv("WWHD_RUNTIME_TEST_TRACE")){va_list args;va_start(args,fmt);std::vfprintf(stderr,fmt,args);std::fputc('\n',stderr);va_end(args);}if(std::strstr(fmt,"[mod_actor_heading] phase=execute"))++heading_exec_logs;else if(std::strstr(fmt,"[mod_actor_heading] phase=path"))++heading_path_logs;else if(std::strstr(fmt,"[mod_actor_heading] phase=draw"))++heading_draw_logs;}
namespace {constexpr uint32_t Arena=0x20000000,A=Arena+0x1000,Model=Arena+0x3000,Scratch=0x60004000,Native=Arena+0x5000,Play=Arena+0x10000;
std::map<uint32_t,PpcFunc> registered,hooks;uint32_t next=0xF0000000;constexpr uint32_t Control=Arena+0x1D000,Task=Arena+0x1D800,ArchiveNode=Arena+0x1E000,ReferenceNode=Arena+0x1E100,ArchiveHash=0x6D6F6431;bool mounted=true;unsigned forgotten=0;unsigned original_calls=0,deleted=0,draws=0,registered_collision=0;bool combat_mode=false,carry_mode=false,world_mode=false,floating_mode=false;float wave_phase=0,wind_strength=.5f;uint32_t world_cursor=Arena+0x20000,world_body=0;unsigned world_sets=0,world_registers=0,world_releases=0,world_moves=0,world_wave_queries=0;unsigned carry_claims=0,carry_delete_requests=0,carry_keep_clears=0;bool carry_reward_allowed=true;bool skinned=false;unsigned attack_registrations=0,heart_queues=0,attack_destroys=0,attack_stts_inits=0,knockback_queries=0,attack_status_ctors=0,attack_cylinder_ctors=0,attack_status_destroys=0;unsigned morf_creates=0,morf_advances=0,clip_loads=0;constexpr uint32_t Morf=Arena+0x18000,Animation=Arena+0x6800,Root=Arena+0x6400;bool complete=false,fail_phase=false;bool clobber_search=false;bool target_hit=false;unsigned hit_clears=0,animation_sets=0,ground_queries=0;constexpr uint32_t Attack=Arena+0xF800,AttackStatus=Arena+0xF900;uint32_t radius=0,height=0;unsigned heap_getters=0,resource_allocations=0,resource_models=0;uint32_t backing_allocated=0,base_allocated=0;bool allow_talk=false;unsigned message_starts=0,talk_completions=0;uint32_t message_status=0xE;
void map(uint32_t at,uint32_t size){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(at),size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(at),size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
void original(Cpu*c,uint32_t fn){assert(fn==0x025E10E0||fn==0x02604550||fn==0x02604EB4||fn==0x02612E64||fn==0x025DD414||fn==0x024EB2D4||fn==0x0240A41C||fn==0x023DCF8C||fn==0x023ED0F0);++original_calls;
if(fn==0x0240A41C||fn==0x023ED0F0){c->r[3]=17;return;}if(fn==0x023DCF8C){const uint32_t link=c->r[3],a=ld32(link+0x65A4);if(a==A){st32(a+0x2E0,ld32(a+0x2E0)&~0x2000u);}st32(link+0x65A0,0xFFFFFFFF);st32(link+0x65A4,0);c->r[3]=18;return;}
if(fn==0x024EB2D4){st32(c->r[8],2);c->f[1].ps0=8;return;}
if(fn==0x025DD414){base_allocated=world_mode?base_allocated+1648:1648;c->r[3]=A;return;}
if(fn==0x02612E64){assert(c->r[6]==Arena+0x9000);backing_allocated=364752;c->r[3]=1;return;}
if(fn==0x02604EB4){Cpu nested=*c;nested.r[3]=Arena+0xB000;nested.r[4]=c->r[4];nested.r[5]=Arena+0x9000;hooks.at(0x02604550)(&nested);c->r[3]=1;return;}c->r[3]=Native;}
uint32_t call(Cpu*c,uint32_t fn,std::initializer_list<uint32_t> args){auto*p=args.begin();switch(fn){case 0x0283801C:assert(p[0]==0x1048CFF0);++clip_recalculations;clip_far_values.push_back(ld32(p[0]+0x54));return 0;case 0x0246BA0C:assert(floating_mode);++world_wave_queries;c->f[1].ps0=100+.02*c->f[1].ps0+.01*c->f[2].ps0+20*std::sin(wave_phase);return 0;
case 0x0257DB28:assert(floating_mode);st32(p[0],f32_as_u32(wind_strength));st32(p[0]+4,0);st32(p[0]+8,0);return 0;
case 0x027EC230:return world_mode?Arena+0x7000:0;
case 0xF2000008:assert(world_mode&&(p[0]==Arena+0x7000||p[0]==Arena+0x9000)&&p[1]==32);return 32*1024*1024;
case 0x0273AD10:{assert(world_mode);const uint32_t at=world_cursor;world_cursor=(world_cursor+p[0]+15)&~15u;return at;}
case 0x024F23F4:assert(world_mode&&p[0]==0);world_body=world_cursor;world_cursor+=0xC0;st32(world_body,256);return world_body;
case 0x0200A030:assert(world_mode&&p[0]==world_body&&p[2]==1&&p[3]==A+story_world_collision::native::Matrix);assert(ld32(p[1])==(world_sets==0?8u:3u)&&ld32(p[1]+0x30)==0x80000000);++world_sets;return 0;
case 0x024EEA6C:{assert(world_mode&&p[0]==Play+0x12A0&&p[1]==world_body&&p[2]==A);st32(world_body,0);st32(p[0],world_body);st32(p[0]+4,1);st32(p[0]+8,ld32(A+4));st32(p[0]+12,A);++world_registers;return 0;}
case 0x020087EC:assert(world_mode&&p[1]==world_body);st32(p[0]+4,0);st32(world_body,256);++world_releases;return 0;
case 0x024F43DC:assert(world_mode&&p[0]==world_body);++world_moves;return 0;
case 0x023DC63C:{assert(p[0]==Arena+0xF400+0x65A0);++carry_keep_clears;st32(p[0],0xFFFFFFFF);st32(p[0]+4,0);c->r[8]=0xBAD;c->f[2].ps0=123;return 0;}case 0x023DCF8C:{c->r[3]=p[0];original(c,fn);return c->r[3];}case 0x025D54C4:{const uint32_t destination=p[1];st32(destination,p[0]==ld32(A+4)?A:p[0]==77?Arena+0xF400:0);if(clobber_search){c->r[3]=0xDEADBEEF;c->r[4]=0xBAD;c->pc=0x12345678;c->f[1].ps0=99;}return ld32(destination)!=0;}case 0x025162A4:return target_hit;case 0x02515BBC:return Arena+0xF400;case 0x02516300:return combat_mode?Arena+0xF400+0x7B1C:Attack;case 0x0251621C:target_hit=false;++hit_clears;return 0;case 0x025D57E0:++carry_delete_requests;return 1;case 0x024F08A8:assert((combat_mode||carry_mode)&&p[0]==A+0xC80&&p[1]==Play+0x12A0);++knockback_queries;st32(p[0]+0x28,ld32(p[0]+0x28)|0x20);st32(p[0]+0x94,ld32(A+0x318));return 0;case 0x0237AABC:return p[0];case 0x0237A940:return 0;case 0x02008974:++ground_queries;c->f[1].ps0=0;return 0;case 0x024EF130:return 0;case 0x025E4A98:assert(skinned&&p[0]==Morf&&p[1]==Animation&&(p[2]==0||p[2]==2)&&p[3]==0);assert(c->f[1].ps0>=0&&c->f[2].ps0==1);++animation_sets;return 0;case 0x025F7DB0:++message_starts;return 0;case 0x025F74D0:message_status=p[1];return 0;case 0x025F795C:return message_status;case 0x02612504:return 1;case 0x026123FC:assert(p[0]==Arena+0xE000&&p[1]==Arena+0xD000);return Arena+0xE400;case 0x02755FEC:assert(p[1]==Arena+0xE800);return Arena+0x9000;case 0x02604470:case 0x026044E0:assert(p[0]==Arena+0xB000&&p[1]==(combat_mode?7u:skinned?4u:2u)&&p[2]==Arena+0x9000);++resource_allocations;st32(p[0]+(fn==0x02604470?0x60:0x64),Arena+0xC000);return 0;case 0x02608308:assert(p[0]==Arena+0xB000&&p[1]==1&&p[2]==1&&p[4]==Arena+0x9000);assert(std::strcmp(reinterpret_cast<char*>(mem::ptr(ld32(p[3]))),"mod_unique.bmd")==0);st32(Arena+0xC000+4,Arena+0x6000);++resource_models;return 0;case 0x025E3268:case 0x025E2FBC:case 0x025EE048:case 0x025EE060:++heap_getters;return Arena+0x9000;case 0xF2000000:if(world_mode&&p[0]==Arena+0x7000)return 32*1024*1024;assert(p[0]==Arena+0x9000);return 64u*1024*1024-backing_allocated-base_allocated;case 0xF2000004:assert(p[0]==Arena+0x9000||p[0]==Arena+0x7000);return p[0]==Arena+0x7000?10960:13808;case 0x025D4ED0:st8(p[0]+0x3AA,1);return p[0];case 0x02520460:if(!fail_phase&&complete)st32(p[0]+4,2);return fail_phase?5:complete?4:1;case 0x0273B264:assert(std::strcmp(reinterpret_cast<char*>(mem::ptr(p[0])),"mod_unique")==0&&p[1]==10);return ArchiveHash;case 0x0260ACC8:assert(ld32(p[2])==ArchiveHash&&(p[0]==Control+0x14||p[0]==Task+0xE0));return p[0]==Control+0x14?(mounted?ArchiveNode:0):ReferenceNode;case 0x0260F61C:assert(p[0]==Task&&p[1]==ArchiveHash);++forgotten;return 0;case 0x026066C4:assert(std::strcmp(reinterpret_cast<char*>(mem::ptr(ld32(p[1]))),"mod_unique")==0);assert(p[2]==1|| ((skinned||p[2]==2)&&p[2]>=2&&p[2]<=(combat_mode?6u:3u)));if(!mounted)return 0;return p[2]==1?Arena+0x6000:Animation;case 0x0260873C:{assert(skinned&&p[1]==p[2]&&p[1]>=2&&p[1]<=(combat_mode?6u:3u));const char* names[]={"idle.bck","walk.bck","flinch.bck","stun.bck","death.bck"};assert(std::strcmp(reinterpret_cast<char*>(mem::ptr(ld32(p[3]))),names[p[1]-2])==0);++clip_loads;return 0;}
case 0x025E4F64:{assert(skinned&&args.size()==8&&p[0]==0&&p[1]==Arena+0x6000&&p[2]==0&&p[3]==0&&p[4]==Animation&&p[5]==2&&p[6]==0&&p[7]==0xFFFFFFFFu&&c->f[1].ps0==1.);const uint32_t at=c->r[1]-0x40+8;assert(ld32(at)==0&&ld32(at+4)==0&&ld32(at+8)==0x80000&&ld32(at+12)==0);++morf_creates;st32(Morf+0x90,Model);st32(Morf+0x94,Animation);st32(Morf+0xA8,Morf+0x100);st32(Morf+0xAC,Morf+0x200);st32(Model+0xAC,Arena+0x6000);st32(Arena+0x6000+8,Root);return Morf;}
case 0x025E535C:assert(skinned&&p[0]==Morf&&p[1]==A+0x314&&p[2]==0&&p[3]==0);++morf_advances;return 0;
case 0x025E54D8:assert(skinned&&p[0]==Morf);assert(ld32(Root+0x14)==0x1234&&ld32(Animation)==0x2345&&ld32(0x104B4628)==0x3456&&ld32(0x104B462C)==0x4567);st32(Root+0x14,Morf);st32(Animation,0xFFFFFFFF);st32(0x104B4628,Morf);st32(0x104B462C,Model);++draws;return 0;
case 0x025E7C6C:assert(p[0]==0);return Arena+0x12000;case 0x025E7CE0:assert(args.size()==8&&p[0]==Arena+0x12000&&p[1]==Arena+0x6000&&p[2]==Animation&&p[3]==1&&p[4]==2&&p[5]==0&&p[6]==0xFFFFFFFFu&&p[7]==0&&c->f[1].ps0==1);return 1;case 0x025E742C:assert(p[0]==Arena+0x12000);st32(p[0]+4,f32_as_u32(std::fmod(u32_as_f32(ld32(p[0]+4))+1.f,100.f)));return 0;case 0x025E7FC4:assert(p[0]==Arena+0x12000&&p[1]==Arena+0x6000);st32(p[1]+0x44,p[0]+0x10);st32(Arena+0x12000+4,f32_as_u32(float(c->f[1].ps0)));return 0;case 0x025E38E0:assert(args.size()==2&&p[1]==0x80000);st32(Model+0xAC,Arena+0x6000);return Model;case 0x025D63E8:{Cpu nested=*c;nested.r[3]=p[0];registered.at(p[1])(&nested);st32(p[0]+0xF4,Arena+0x7000);return nested.r[3];}case 0x02515F14:assert(p[1]==255);if(combat_mode){st32(p[0]+0xC,A);st32(p[0]+0x10,ld32(A+4));if(p[0]==A+0xA48)++attack_stts_inits;}return 0;case 0x0200BD2C:if(p[0]==A+0xA48)++attack_status_ctors;return 0;case 0x02515FB8:if(p[0]==A+0xA84)++attack_cylinder_ctors;return 0;case 0x02515860:if(p[0]==A+0xA48)++attack_status_destroys;return 0;
case 0x025D8870:assert(combat_mode&&args.size()==8&&p[0]==A+0x314&&p[1]==0&&p[2]==0xFFFFFFFFu&&p[3]==0&&p[4]==0&&p[5]==0&&p[6]==4&&p[7]==0);++heart_queues;return 0x1234;
case 0x02515A70:if(p[0]==A+0xA84)++attack_destroys;return 0;
case 0x02516518:if(combat_mode&&p[0]==A+0xA84){assert(p[1]==A+0xBB4&&ld32(p[1]+4)==0x800&&ld8(p[1]+8)==2&&ld32(p[1]+0xC)==0xF&&ld32(p[1]+0x18)==0&&ld32(p[1]+0x2C)==0);st32(p[0],ld32(p[1]+0xC));st32(p[0]+0x10,ld32(p[1]+4));st8(p[0]+0x14,ld8(p[1]+8));st8(p[0]+0x6F,ld8(p[1]+0x1E));return 0;}assert(p[0]==A+0x3F4);assert(ld32(p[1]+0x18)==0x19);assert(ld32(p[1]+0x04)==0&&ld32(p[1]+0x0C)==0&&ld32(p[1]+0x10)==(combat_mode?0xFFFFFFFFu:0u)&&ld32(p[1]+0x14)==(combat_mode?0x1Fu:0u)&&ld32(p[1]+0x2C)==0);return 0;case 0x020184DC:radius=f32_as_u32(float(c->f[1].ps0));return 0;case 0x02018428:height=f32_as_u32(float(c->f[1].ps0));return 0;case 0x025200D4:return Play;case 0x0200E240:assert(p[0]==Play+0x26A4&&(p[1]==A+0x3F4||(combat_mode&&p[1]==A+0xA84)));if(p[1]==A+0xA84)++attack_registrations;else ++registered_collision;return 0;case 0x02555D0C:return Arena+0x8000;case 0x025E2DE0:assert(p[0]==Model&&p[1]==0);if(ld32(A+0x854)==Arena+0x12000){assert(ld32(0x104B4634)==0x12345678&&ld32(0x104B4638)==0x23456789);}++draws;return 0;case 0x025204C8:if(ld32(p[0]+4)==2){assert(mounted&&"deleteRes of an archive the table does not have: the game crashes");++deleted;st32(p[0]+4,0);}return 0;default:return 0;}}
void method(Cpu&c,unsigned n){c.r[3]=A;registered.at(ld32(Scratch+0x30+n*4))(&c);}
}
namespace dispatch {PpcFunc lookup(uint32_t){return nullptr;}uint32_t register_host(PpcFunc fn,const char*){registered[next]=fn;next+=4;return next-4;}}
namespace story_mail {void unsay(){}bool can_say(){return allow_talk;}void say(uint32_t,const std::u16string*){}}
void combat_tests(Cpu& c){
    using namespace mod_actor;using namespace mod_actor::combat;
    constexpr uint32_t Link=Arena+0xF400,Body=A+0x3F4,AttackCylinder=A+0xA84,OwnArena=0x60008000;
    constexpr uint32_t link_objects[]={0x765C,0x778C,0x78BC,0x79EC,0x7B1C,0x7C54,0x7D8C,0x7EC4,0x7FFC,0x8128};
    st32(Link+4,77);st16(Link+0xE,0xA8);st32(Link+0x762C,Link);st32(Link+0x7630,77);
    for(uint32_t o:link_objects)st32(Link+o+0x44,Link+0x7620);
    assert(story_enemy_ownership::configure(OwnArena,story_enemy_ownership::kStorageBytes,original,[](auto,auto,auto){return false;}));story_enemy_ownership::initialize_storage();assert(story_enemy_ownership::set_link(Link,77));
    Identity id;assert(identify(&c,A,id));CombatState state;assert(combat_snapshot(&c,id,state)&&state.health==5&&state.phase==uint32_t(Phase::Alive));
    assert(attack_status_ctors==1&&attack_cylinder_ctors==1&&attack_stts_inits==1&&ld32(AttackCylinder+0x44)==A+0xA48&&ld32(A+0xA48+0xC)==A&&ld32(A+0xA48+0x10)==42);assert(ld32(AttackCylinder+0x10)==0x800&&ld8(AttackCylinder+0x14)==2&&clip_loads==10);
    unsigned claims=0;bool claimed=false;assert(set_combat_reward("test","post",[&]{++claims;if(claimed)return CombatReward::AlreadyDone;claimed=true;return CombatReward::Committed;}));
    assert(!set_combat_reward("test","post",[]{return CombatReward::Failed;}));
    method(c,2);assert(registered_collision==1&&attack_registrations==0&&ld32(A+0x39C)==4&&ld32(Body+0x28)==0xFFFFFFFFu);
    using Policy=story_enemy_ownership::PairPolicy;assert(story_enemy_ownership::pair_policy(Link+0x7B1C,Body)==Policy::Delegate);assert(story_enemy_ownership::pair_policy(AttackCylinder,Link+0x765C)==Policy::Reject);
    const uint32_t retail=Arena+0x1B000,retail_status=Arena+0x1B200,retail_actor=Arena+0x1B400;st32(retail+0x44,retail_status);st32(retail_status+0xC,retail_actor);st32(retail_status+0x10,99);st32(retail_actor+4,99);st16(retail_actor+0xE,0xB6);
    assert(attack_begin(&c,id));assert(story_enemy_ownership::pair_policy(AttackCylinder,Link+0x765C)==Policy::Delegate);assert(story_enemy_ownership::pair_policy(AttackCylinder,retail)==Policy::Reject);assert(story_enemy_ownership::pair_policy(retail,Body)==Policy::Reject);
    for(uint32_t bad_status:{0u,retail_status}){st32(AttackCylinder+0x44,bad_status);assert(story_enemy_ownership::pair_policy(AttackCylinder,Link+0x765C)==Policy::Reject&&story_enemy_ownership::pair_policy(AttackCylinder,retail)==Policy::Reject);st32(Body+0x44,bad_status);assert(story_enemy_ownership::pair_policy(Link+0x7B1C,Body)==Policy::Reject);st32(Body+0x44,A+0x3B8);}st32(AttackCylinder+0x44,A+0xA48);
    for(auto field:std::vector<std::pair<uint32_t,uint32_t>>{{0,0},{0x10,2}}){const uint32_t saved=ld32(AttackCylinder+field.first);st32(AttackCylinder+field.first,field.second);assert(story_enemy_ownership::pair_policy(AttackCylinder,Link+0x765C)==Policy::Reject&&story_enemy_ownership::pair_policy(AttackCylinder,retail)==Policy::Reject);st32(AttackCylinder+field.first,saved);}
    for(auto field:std::vector<std::pair<uint32_t,uint8_t>>{{0x14,4},{0x6F,8}}){const uint8_t saved=ld8(AttackCylinder+field.first);st8(AttackCylinder+field.first,field.second);assert(story_enemy_ownership::pair_policy(AttackCylinder,Link+0x765C)==Policy::Reject&&story_enemy_ownership::pair_policy(AttackCylinder,retail)==Policy::Reject);st8(AttackCylinder+field.first,saved);}
    assert(story_enemy_ownership::pair_policy(AttackCylinder,Link+0x765C)==Policy::Delegate);assert(set_rotation(&c,id,{0,45,0}));assert(!set_rotation(&c,id,{1,45,0})&&!set_rotation(&c,id,{0,45,1}));assert(set_rotation(&c,id,{0,0,0}));

    const float active=u32_as_f32(ld32(A+0xA20+32));method(c,4);assert(u32_as_f32(ld32(A+0xA20+32))==active&&attack_registrations==0);method(c,2);method(c,2);assert(attack_registrations==2);method(c,2);assert(attack_registrations==2);for(int i=0;i<3;++i)method(c,2);
    assert(attack_begin(&c,id));assert(begin_scene(&c,id,71));assert(u32_as_f32(ld32(A+0xA20+32))==0&&ld32(A+0x39C)==0);assert(!attack_begin(&c,id));assert(scene_rotation(&c,id,71,{0,90,0})&&!scene_rotation(&c,id,71,{1,90,0})&&!scene_rotation(&c,id,71,{0,90,1}));assert(end_scene(&c,id,71));assert(set_rotation(&c,id,{0,0,0}));for(int i=0;i<5;++i)method(c,2);
    const uint32_t sword=Link+0x7B1C;st32(sword+0x10,2);st32(Body+0xA0,77);st32(Body+0xCC,0);st32(Body+0xD0,0);st32(Body+0xD4,0);st32(Body+0xC0,0);st32(Body+0xC4,0);st32(Body+0xC8,0);
    st8(sword+0x14,0);target_hit=true;method(c,2);assert(combat_snapshot(&c,id,state)&&state.health==5&&!target_hit);
    st32(Link+0x314,f32_as_u32(-100));const uint32_t link_shape=Link+0x765C+0x118;st32(link_shape,f32_as_u32(-100));st32(link_shape+12,f32_as_u32(20));st32(link_shape+16,f32_as_u32(80));st8(sword+0x14,2);target_hit=true;method(c,2);assert(combat_snapshot(&c,id,state)&&state.health==3&&state.phase==uint32_t(Phase::Flinch)&&ld32(A+0x82C)==2);assert(knockback_queries==1&&u32_as_f32(ld32(A+0x314))==3);assert(!set_animation(&c,id,"idle",0,true));assert(stop_path(&c,id)&&ld32(A+0x82C)==2);method(c,2);method(c,2);assert(combat_snapshot(&c,id,state)&&state.phase==uint32_t(Phase::Alive)&&ld32(A+0x82C)==0);assert(knockback_queries==2&&u32_as_f32(ld32(A+0x314))==6);
    std::vector<uint8_t> saved(kInstanceBytes);std::memcpy(saved.data(),mem::ptr(A),saved.size());
    st8(sword+0x14,3);target_hit=true;method(c,2);assert(combat_snapshot(&c,id,state)&&state.health==0&&state.phase==uint32_t(Phase::Dying)&&ld32(A+0x82C)==4);assert(!attack_begin(&c,id));const unsigned body_before=registered_collision;method(c,2);method(c,2);assert(combat_snapshot(&c,id,state)&&state.phase==uint32_t(Phase::Defeated)&&state.reward_status==uint32_t(Reward::Committed));assert(claims==1&&heart_queues==1&&registered_collision==body_before);for(int i=0;i<5;++i)method(c,2);assert(claims==1&&heart_queues==1);
    // A full memory rewind restores health/sequence, then a repeated terminal
    // transaction sees an already-owned marker and must not queue another heart.
    std::memcpy(mem::ptr(A),saved.data(),saved.size());state_loaded();assert(combat_snapshot(&c,id,state)&&state.health==3);method(c,2);target_hit=true;method(c,2);method(c,2);method(c,2);assert(claims==2&&heart_queues==1);
    std::memcpy(mem::ptr(A),saved.data(),saved.size());state_loaded();method(c,2);for(int i=0;i<5;++i)method(c,2);assert(attack_begin(&c,id));set_behavior_sink([&](Cpu*cpu,uint32_t actor,BehaviorEvent event,const Hit*){if(event==BehaviorEvent::Execute){assert(actor==A);quarantine_combat(cpu,id);}});const unsigned at_before=attack_registrations;method(c,2);assert(ld32(A+0xC28)==1&&ld32(A+0x39C)==0&&u32_as_f32(ld32(A+0xA20+32))==0&&attack_registrations==at_before);set_behavior_sink({});
    std::memcpy(mem::ptr(A),saved.data(),saved.size());state_loaded();method(c,2);st8(A+0xC40+8,ld8(A+0xC40+8)^1);assert(!resolve(&c,id));const unsigned at_corrupt=attack_registrations;method(c,2);assert(attack_registrations==at_corrupt&&ld32(A+0x39C)==0);
    // Restore a valid live tuple solely to exercise ordinary room teardown.
    std::memcpy(mem::ptr(A),saved.data(),saved.size());state_loaded();const unsigned claims_before=claims,hearts_before=heart_queues;method(c,1);assert(claims==claims_before&&heart_queues==hearts_before&&deleted==1&&attack_destroys==1&&attack_status_destroys==1);
}

void look_projection_tests(){
    using namespace mod_actor::look_bounds;const double rad=Pi/180;const Euler a{28.769144845564753*rad,-40.18445270120061*rad,93.28182577791847*rad},b{7.014618109257469*rad,47.75850739881972*rad,-47.481120213966335*rad};auto qa=quaternion(a),qb=quaternion(b);double dot=0;for(unsigned k=0;k<4;++k)dot+=qa[k]*qb[k];if(dot<0)for(double&v:qb)v=-v;Quaternion middle;double norm=0;for(unsigned k=0;k<4;++k){middle[k]=qa[k]+qb[k];norm+=middle[k]*middle[k];}for(double&v:middle)v/=std::sqrt(norm);Euler anchor;for(unsigned k=0;k<3;++k)anchor[k]=(a[k]+b[k])/2;Euler e;assert(nearest(middle,anchor,e)&&e[0]>50*rad);Quaternion bounded;assert(project(middle,anchor,70*rad,35*rad,true,bounded));assert(nearest(bounded,anchor,e)&&std::fabs(e[0])<=35*rad+1e-9&&std::fabs(e[1])<=70*rad+1e-9);
    for(Euler source:{Euler{.2,.3,Pi-.01},Euler{.2,.3,-Pi+.01},Euler{.1,Pi/2,.2},Euler{.1,Pi/2-1e-7,.2},Euler{.1,Pi/2+1e-7,.2},Euler{-.1,-Pi/2,-.2},Euler{.1,2.8,-.2}}){auto q=quaternion(source);assert(nearest(q,source,e));auto reconstructed=quaternion(e);double agreement=0;for(unsigned k=0;k<4;++k)agreement+=q[k]*reconstructed[k];assert(std::fabs(std::fabs(agreement)-1)<1e-8);assert(project(q,source,Pi,Pi/2,true,bounded)&&bounded==q);}
    assert(!project({0,0,0,0},{0,0,0},1,.5,true,bounded));assert(!project({NAN,0,0,1},{0,0,0},1,.5,true,bounded));
}
int main(int argc,char**argv){look_projection_tests();const bool visual_mode=argc==2&&std::strcmp(argv[1],"--world-visual")==0;if(visual_mode)map(0x10480000,0x10000);carry_mode=argc==2&&std::strcmp(argv[1],"--carry")==0;combat_mode=argc==2&&std::strcmp(argv[1],"--combat")==0;skinned=combat_mode||(argc==2&&std::strcmp(argv[1],"--skinned")==0);map(Arena,0x20000);map(0x60000000,0x20000);map(0x104B0000,0x10000);map(0x101F0000,0x10000);map(0x10180000,0x10000);st32(0x1018CA3C,Arena+0x9000);st32(Arena+0x9000+0xC,Arena+0xA000);st32(Arena+0x7000+0xC,Arena+0xA000);st32(Arena+0x7000+0x24,Arena+0x9000);st32(Arena+0xA000+0x74,0xF2000000);st32(Arena+0xA000+0x6C,0xF2000004);st32(0x101F3EE0+0xB6*4,Native);st32(Native+4,0x0007FFFD);st16(Native+0x20,700);const uint32_t raft_native=Native+0x100;st32(0x101F3EE0+0x44*4,raft_native);st32(raft_native+4,0x0003FFFD);st16(raft_native+0x20,432);st32(Native+0xC,0x123456);st32(Native+0x1C,0xabcdef);st32(0x101F4F28,Control);st32(0x101F4F54,Task);st32(ReferenceNode+0x18,1);mod_actor::configure(original,call,Scratch);
if(argc==2&&(std::strcmp(argv[1],"--world-proof")==0||std::strcmp(argv[1],"--world-proof-mixed")==0)){
#ifdef _WIN32
    _putenv_s("WWHD_WORLD_OBJECT_PROOF","1");
#else
    setenv("WWHD_WORLD_OBJECT_PROOF","1",1);
#endif
    const uint32_t raft=Native+0x100;st32(0x101F3EE0+0x44*4,raft);st16(raft+4,3);st16(raft+6,0xFFFD);st16(raft+0x20,432);
    story_notebook::ManifestActor spec;spec.kind="mod";spec.id="raft";assert(mod_actor::declare("world-objects-w0",spec,"proof",1,{}));
    if(std::strcmp(argv[1],"--world-proof-mixed")==0){spec.id="ordinary";assert(mod_actor::declare("other-mod",spec,"ordinary",1,{}));assert(!mod_actor::install([](uint32_t){return std::string();},[](uint32_t a,PpcFunc f){hooks[a]=f;}));assert(hooks.empty());return 0;}
    assert(mod_actor::install([](uint32_t){return std::string();},[](uint32_t a,PpcFunc f){hooks[a]=f;}));
    assert(ld16(Scratch+4)==3&&ld16(Scratch+6)==0xFFFD&&ld16(Scratch+0x20)==432);
    assert(hooks.count(0x024EE708)==1&&hooks.size()==7);return 0;
}
// Placing on another world object: an island ground (a plateau with its top at y 500 and a pillar up to 1500) is
// ground; a box (a house) stands on its top, may reach a little into it, and only a real overlap with its solid
// geometry (the pillar, the plateau's top above the step) is in the way; the same mod's object says so.
if(argc==2&&std::strcmp(argv[1],"--world-ground")==0){
    world_mode=true;mod_actor::configure_world_objects("synthetic",[](const std::string&stage,int room){story_world_placement::Index index;index.coverage.push_back({stage,room,true,true,true,{}});return index;});
    namespace mesh=story_world_collision::mesh;std::string why;mesh::Mesh plateau,pillar,ground;
    assert(mesh::make_shape("box",{4000,700,4000},0,0,1,plateau,why)&&mesh::make_shape("box",{300,1100,300},0,0,0,pillar,why));
    for(auto&v:plateau.vertices)v.y+=150;           // -200 .. 500
    for(auto&v:pillar.vertices)v.x+=1200,v.y+=950;  // 400 .. 1500 at x 1200
    ground=plateau;const auto base=uint16_t(ground.vertices.size());ground.vertices.insert(ground.vertices.end(),pillar.vertices.begin(),pillar.vertices.end());
    for(auto t:pillar.triangles){for(auto&i:t.indices)i=uint16_t(i+base);ground.triangles.push_back(t);}
    ground.minimum=ground.maximum=ground.vertices[0];for(auto p:ground.vertices){ground.minimum={std::min(ground.minimum.x,p.x),std::min(ground.minimum.y,p.y),std::min(ground.minimum.z,p.z)};ground.maximum={std::max(ground.maximum.x,p.x),std::max(ground.maximum.y,p.y),std::max(ground.maximum.z,p.z)};}
    assert(mesh::validate(ground,why));assert(mod_actor::register_collision("test","ground",std::make_shared<const mesh::Mesh>(ground)));
    story_notebook::ManifestActor isle;isle.kind="mod";isle.id="isle";isle.stage="sea";isle.room=12;isle.behaviour="static";isle.collision_mesh="ground";isle.position={1000,0,2000};
    assert(mod_actor::declare("test",isle,"mod_unique",1,{})==1);
    auto house=isle;house.id="house";house.collision_mesh.clear();house.collision_shape="box";house.collision_size={400,600,400};house.collision_surface="wood";
    auto query=[&](const std::string&mod,story_notebook::WorldPoint at,float yaw){house.position=at;house.rotation={0,yaw,0};mods::json::Value r;assert(mod_actor::world_object_query(mod,house,false,r,why));return r;};
    // on the plateau's top (box centred: its base at 500), turned
    auto r=query("test",{1000-800,800,2000+600},222.5f);if(!r.get("clear").boolean)std::puts(mods::json::dump(r.get("conflicts")).c_str());assert(r.get("clear").boolean);
    // sunk 100 into the top (a slope under its floor): still standing on it
    r=query("test",{1000-800,700,2000+600},0);assert(r.get("clear").boolean);
    // sunk 400: the top goes through the walls above the step
    r=query("test",{1000-800,400,2000+600},0);assert(!r.get("clear").boolean&&r.get("conflicts").array[0].get("id").string()=="test:isle");
    assert(r.get("conflicts").array[0].get("message").string()=="this mod's world object");
    // into the pillar: in the way; another mod sees "another mod's"
    r=query("test",{1000+1200,800,2000},0);assert(!r.get("clear").boolean&&r.get("conflicts").array[0].get("message").string()=="this mod's world object");
    r=query("other",{1000+1200,800,2000},0);assert(!r.get("clear").boolean&&r.get("conflicts").array[0].get("message").string()=="another mod's world object");
    // beside the pillar (its own box would touch the pillar's turned bounds, the solids don't): clear
    r=query("test",{1780,800,2330},45);assert(r.get("clear").boolean);
    // two houses side by side (the terrace case): only touching collision blocks
    assert(mod_actor::declare("test",house,"mod_unique",1,{})==2);
    house.id="house-2";r=query("test",{2400,800,2950},30);for(const auto&c:r.get("conflicts").array)assert(c.get("id").string()!="test:house");
    r=query("test",{1880,800,2330},0);bool hit=false;for(const auto&c:r.get("conflicts").array)hit|=c.get("id").string()=="test:house";assert(hit);
    // a body of water on the plateau (the editor's pond: its open water at the level, a sand bowl 200 deep dug into
    // the ground): it lies in the plateau, so only what rises through its water is in the way (the pillar), and the
    // check takes the body's own collision before it is made
    mesh::Mesh pond;{const float o=300,f=250,d=-200;pond.vertices={{-o,0,-o},{o,0,-o},{o,0,o},{-o,0,o},{-f,d,-f},{f,d,-f},{f,d,f},{-f,d,f}};
        pond.triangles={{{0,2,1},9},{{0,3,2},9},{{4,5,6},3},{{4,6,7},3}};for(uint16_t k=0;k<4;++k){const uint16_t j=uint16_t((k+1)%4);pond.triangles.push_back({{k,j,uint16_t(j+4)},3});pond.triangles.push_back({{k,uint16_t(j+4),uint16_t(k+4)},3});}
        pond.minimum={-o,d,-o};pond.maximum={o,0,o};assert(mesh::validate(pond,why));}
    auto spring=isle;spring.id="pond";spring.collision_mesh.clear();spring.shoreline.material="pond";assert(spring.water_body());
    auto water_query=[&](story_notebook::WorldPoint at){spring.position=at;mods::json::Value r;assert(mod_actor::world_object_query("test",spring,false,r,why,std::make_shared<const mesh::Mesh>(pond)));return r;};
    r=water_query({1000-800,500,2000+600});if(!r.get("clear").boolean)std::puts(mods::json::dump(r.get("conflicts")).c_str());assert(r.get("clear").boolean);
    r=water_query({1000-800,700,2000+600});assert(r.get("clear").boolean);  // raised: a mound on the plateau
    r=water_query({1000+1200,500,2000});assert(!r.get("clear").boolean&&r.get("conflicts").array[0].get("message").string()=="this mod's world object");  // the pillar in it
    // the same pond made (its collision registered, declared as the game declares it): accepted, and a house put into
    // its water is in the way while one beside it is not
    assert(mod_actor::register_collision("test","pond-col",std::make_shared<const mesh::Mesh>(pond)));
    spring.collision_mesh="pond-col";spring.position={1000-1400,500,2000-1400};assert(mod_actor::declare("test",spring,"mod_unique",1,{})==3);
    house.id="house-3";r=query("test",{1000-1400,800,2000-1400},0);hit=false;for(const auto&c:r.get("conflicts").array)hit|=c.get("id").string()=="test:pond";assert(hit);
    r=query("test",{1000-1400+600,800,2000-1400},0);for(const auto&c:r.get("conflicts").array)assert(c.get("id").string()!="test:pond");
    // the world objects' own ground (the editor's water tool and snapping): the plateau's top, the pillar's, a house's
    // roof; not the pond (a body of water), nothing beside the island or on another stage
    std::vector<float> heights;std::vector<bool> found;
    assert(mod_actor::object_grounds("sea",{{1000,5000,2000},{1000+1200,5000,2000},{1000+2100,5000,2000},{1000-1400,5000,2000-1400},{1000,450,2000},{1780,5000,2330}},heights,found));
    assert(found[0]&&heights[0]==500&&found[1]&&heights[1]==1500&&!found[2]&&found[3]&&heights[3]==500&&!found[4]&&found[5]&&heights[5]==1100);
    assert(!mod_actor::object_grounds("M_NewD2",{{1000,5000,2000}},heights,found)&&!found[0]);
    // lava: its surface is no ground (Link and dropped things sink into it): over a static object's lava the ground is
    // what lies below it (here a stone floor 150 lower), beside the lava its own top
    {mesh::Mesh lava;const float o=300;lava.vertices={{-o,150,-o},{o,150,-o},{o,150,o},{-o,150,o},{-o,0,-o},{o,0,-o},{o,0,o},{-o,0,o},{o,150,-o},{o+200,150,-o},{o+200,150,o},{o,150,o}};
     lava.triangles={{{0,2,1},mesh::SurfaceLava},{{0,3,2},mesh::SurfaceLava},{{4,6,5},0},{{4,7,6},0},{{8,10,9},0},{{8,11,10},0}};lava.minimum={-o,0,-o};lava.maximum={o+200,150,o};
     assert(mesh::validate(lava,why)&&mesh::native_size(lava).triangles==8);
     assert(mod_actor::register_collision("test","lava-col",std::make_shared<const mesh::Mesh>(lava)));
     auto floor=isle;floor.id="lava-floor";floor.collision_mesh="lava-col";floor.position={1000+3500,500,2000+3500};assert(mod_actor::declare("test",floor,"mod_unique",1,{})>0);
     assert(mod_actor::object_grounds("sea",{{1000+3500,5000,2000+3500},{1000+3500+400,5000,2000+3500}},heights,found));
     assert(found[0]&&heights[0]==500&&found[1]&&heights[1]==650);
     // a lava pool (a pond with lava: its shoreline is the game's lava) is a body like a pond
     auto pool=spring;pool.id="lava-pool";pool.shoreline.material="lava";assert(pool.water_body()&&story_notebook::water_surface_material("lava"));}
    std::puts("world ground placement ok");return 0;
}
if(argc==2&&(std::strcmp(argv[1],"--world-static")==0||std::strcmp(argv[1],"--world-floating")==0)){
    floating_mode=std::strcmp(argv[1],"--world-floating")==0;
    world_mode=true;st32(Play+0x5B34,Arena+0xF400);mod_actor::configure_world_objects("synthetic",[](const std::string&stage,int room){story_world_placement::Index index;index.coverage.push_back({stage,room,true,true,true,{}});return index;});map(Arena+0x20000,0x20000);map(0x101D0000,0x10000);if(floating_mode)map(0x10470000,0x10000);st32(Arena+0xA000+0x7C,0xF2000008);
    for(unsigned i=0;i<32;++i)st32(0x101D51CC+i*4,0xFFFFFFFF);{unsigned code=31;for(unsigned i=0;i<story_world_collision::mesh::NativeResultIds.size();++i){bool seen=false;for(unsigned j=0;j<i;++j)seen|=story_world_collision::mesh::NativeResultIds[j]==story_world_collision::mesh::NativeResultIds[i];if(!seen)st32(0x101D51CC+(code--)*4,story_world_collision::mesh::NativeResultIds[i]);}}
    story_notebook::ManifestActor ordinary;ordinary.kind="mod";ordinary.id="ordinary";assert(mod_actor::declare("test",ordinary,"mod_unique",1,{})==1);
    auto shape=ordinary;shape.id="shape";shape.stage="sea";shape.room=12;shape.behaviour="static";shape.collision_shape="box";shape.collision_size={100,80,120};shape.collision_surface="stone";if(floating_mode){shape.behaviour="floating";shape.position.y=40;shape.buoyancy.draft=10;}assert(mod_actor::declare("test",shape,"mod_unique",1,{})==2);
    std::vector<uint8_t> bytes(84);auto put=[&](size_t p,uint32_t v){for(unsigned k=0;k<4;++k)bytes[p+k]=uint8_t(v>>(24-8*k));};put(0,0x57434F4C);put(4,1);put(8,3);put(12,1);put(28,f32_as_u32(100));put(36,f32_as_u32(100));put(60,f32_as_u32(100));put(64,f32_as_u32(100));bytes[79]=1;bytes[81]=2;bytes[83]=1;
    story_world_collision::mesh::Mesh geometry;std::string why;assert(story_world_collision::mesh::decode(bytes,geometry,why));assert(mod_actor::register_collision("test","converted",std::make_shared<const story_world_collision::mesh::Mesh>(geometry)));
    auto converted=shape;converted.id="converted";converted.position.x=1000;converted.collision_shape.clear();converted.collision_mesh="converted";if(floating_mode){converted.behaviour="drifting";converted.position.y=20;converted.drift.wind_speed=20;converted.drift.current.z=3;converted.drift.max_speed=30;converted.anchor.enabled=true;converted.anchor.point=converted.position;converted.anchor.radius=100;}assert(mod_actor::declare("test",converted,"mod_unique",1,{})==3);
    // Grapple placement uses scaled collision bounds, exposes preview poses,
    // and reserves a bounded room total even before native proxy creation.
    auto target=shape;target.id="point-check";target.position={5000,40,0};target.grapple_points={{"beam",{0,40,0},90}};mods::json::Value preview;
    assert(mod_actor::world_object_query("test",target,true,preview,why)&&preview.get("clear").boolean&&preview.get("grapple_targets").array.size()==1);assert(preview.get("grapple_targets").array[0].get("position").get("y").number==80);
    const double with_proxy=preview.get("allocation_bytes").number;auto plain_target=target;plain_target.grapple_points.clear();mods::json::Value plain_preview;assert(mod_actor::world_object_query("test",plain_target,false,plain_preview,why));assert(with_proxy-plain_preview.get("allocation_bytes").number==0x420+64);assert(preview.get("room_allocation_bytes").number-plain_preview.get("room_allocation_bytes").number==0x420+64);
    target.scale.x=2;target.rotation.y=30;target.grapple_points[0].yaw=45;assert(mod_actor::world_object_query("test",target,true,preview,why)&&preview.get("clear").boolean);assert(std::abs(preview.get("grapple_targets").array[0].get("yaw").number-93.43495)<.001);target.scale.x=1;target.rotation.y=0;target.grapple_points[0].yaw=90;
    target.grapple_points[0].position.y=41;assert(!mod_actor::world_object_query("test",target,false,preview,why));target.grapple_points.clear();for(unsigned i=0;i<8;++i)target.grapple_points.push_back({"p"+std::to_string(i),{0,40,0},90});
    const auto before_targets=mod_actor::checkpoint();assert(mod_actor::declare("test",target,"mod_unique",1,{})==4);target.id="other-points";target.position.x=7000;assert(mod_actor::declare("test",target,"mod_unique",1,{})==5);target.id="over-budget";target.position.x=9000;target.grapple_points.resize(1);assert(mod_actor::world_object_query("test",target,false,preview,why)&&!preview.get("clear").boolean&&preview.get("room_grapple_target_count").number==17);mod_actor::rollback(before_targets);
    assert(mod_actor::install([](uint32_t){return std::string();},[](uint32_t a,PpcFunc f){hooks[a]=f;}));assert(!hooks.count(0x024EE708));
    Cpu c{};c.r[1]=Arena+0x1F000;c.r[3]=mod_actor::kWorldProfile;hooks.at(0x025E10E0)(&c);const uint32_t world_profile=c.r[3];assert(world_profile!=Scratch&&ld16(world_profile+8)==mod_actor::kWorldProfile);assert(ld16(world_profile+4)==3&&ld16(world_profile+0x20)==432);assert(ld16(Scratch+4)==7&&ld16(Scratch+0x20)==700);
    // Use the genuine resource wrappers to populate allocation measurements.
    st32(Arena+0xD000,Arena+0xD008);std::memcpy(mem::ptr(Arena+0xD008),"mod_unique",11);c.r[3]=Arena+0xB000;c.r[4]=Arena+0xD000;c.r[5]=Arena+0x9000;hooks.at(0x02604550)(&c);c.r[3]=Arena+0xE000;c.r[4]=Arena+0xD000;hooks.at(0x02604EB4)(&c);st32(Arena+0xE400+0x14,Arena+0xE800);st32(Arena+0xE400+0x18,392000);c.r[3]=Arena+0xE000;c.r[4]=Arena+0xD000;c.r[6]=Arena+0x9000;hooks.at(0x02612E64)(&c);
    for(uint32_t token:{2u,3u}){
        std::memset(mem::ptr(A),0,mod_actor::kInstanceBytes);wave_phase=0;st32(Arena+0xF000,token);c.r[3]=mod_actor::kWorldProfile;c.r[5]=Arena+0xF000;hooks.at(0x025DD414)(&c);assert(c.r[3]==A);st32(A+4,40+token);st16(A+0xE,mod_actor::kWorldProfile);st32(A+0xB0,token);st32(A+0x314,f32_as_u32(token==3?1000.f:0.f));st8(A+0x326,12);for(unsigned k=0;k<3;++k)st32(A+0x330+k*4,f32_as_u32(1));complete=true;method(c,0);assert(c.r[3]==4&&world_registers==token-1&&ld32(A+0x83C)==0);mod_actor::Identity id;assert(mod_actor::identify(&c,A,id));assert(!mod_actor::set_position(&c,id,{1,2,3}));assert(!mod_actor::set_rotation(&c,id,{0,90,0}));mod_actor::MotionState forbidden;forbidden.count=2;forbidden.enabled=true;forbidden.speed=10;forbidden.points[1]={100,0,0};assert(!mod_actor::set_motion(&c,id,forbidden));assert(!mod_actor::start_path(&c,id,{{0,0,0},{100,0,0}},10));assert(!mod_actor::begin_scene(&c,id,1));assert(!mod_actor::set_collision(&c,id,40,40,true,false));method(c,2);assert(world_moves>0&&registered_collision==0);
        if(floating_mode){
            assert(ld32(A+0x10C0)==0x574D5431&&ld32(A+0x10C4)==40+token&&ld32(A+0x10C8)==1);assert(std::abs(u32_as_f32(ld32(A+0x318))-130)<.1f);const float first_y=u32_as_f32(ld32(A+0x318));
            for(unsigned frame=0;frame<120;++frame){wave_phase+=.03f;method(c,2);}assert(std::abs(u32_as_f32(ld32(A+0x318))-first_y)>1);assert(ld16(A+0x328)!=0&&ld16(A+0x32C)!=0);
            if(token==2)assert(u32_as_f32(ld32(A+0x314))==0&&u32_as_f32(ld32(A+0x31C))==0);
            else{
                const float before_x=u32_as_f32(ld32(A+0x314)),before_z=u32_as_f32(ld32(A+0x31C));assert(before_x>1030&&before_z>10);
                constexpr uint32_t ship=Arena+0x35000;st32(0x10474BEC,ship);st8(ship+0x3AA,1);st8(ship+0x326,12);st32(ship+0x314,f32_as_u32(before_x+460));st32(ship+0x31C,f32_as_u32(before_z));for(unsigned frame=0;frame<90;++frame){wave_phase+=.03f;method(c,2);}assert(u32_as_f32(ld32(A+0x314))<before_x+15);st32(0x10474BEC,0);
                for(unsigned frame=0;frame<600;++frame){wave_phase+=.03f;method(c,2);}const float x=u32_as_f32(ld32(A+0x314)),z=u32_as_f32(ld32(A+0x31C));assert(std::hypot(x-1000,z)<=100.001&&std::hypot(x-1000,z)>95);
            }
        }
        const std::vector<uint8_t> snapshot(mem::ptr(Arena),mem::ptr(Arena)+0x40000),scratch_snapshot(mem::ptr(Scratch),mem::ptr(Scratch)+mod_actor::kScratchBytes);const auto registrations_before=world_registers;method(c,1);assert(world_releases==2*(token-2)+1);std::memcpy(mem::ptr(Arena),snapshot.data(),snapshot.size());std::memcpy(mem::ptr(Scratch),scratch_snapshot.data(),scratch_snapshot.size());mod_actor::state_loaded();method(c,2);assert(world_registers==registrations_before&&ld32(Play+0x12A0+4)==1);method(c,1);assert(world_releases==2*(token-1));
        if(token==3)for(unsigned corruption=0;corruption<(floating_mode?8u:4u);++corruption){
            std::memcpy(mem::ptr(Arena),snapshot.data(),snapshot.size());std::memcpy(mem::ptr(Scratch),scratch_snapshot.data(),scratch_snapshot.size());mod_actor::state_loaded();
            if(corruption==0)st32(A+0x314,f32_as_u32(1100));else if(corruption==1)st32(A+0x83C,1);else if(corruption==2)st32(A+0x67C+12,4);else if(corruption==4)st32(A+0x10C0+12,f32_as_u32(2000));else if(corruption==5)st32(A+0x10C0+24,0x7FC00000);else if(corruption==6)st32(A+0x10C0+48,f32_as_u32(1000));else if(corruption==7){st32(A+0x314,f32_as_u32(350001));st32(A+0x10C0+12,f32_as_u32(350001));}else st32(A+story_world_collision::native::Identity+8,ld32(A+story_world_collision::native::Identity+8)^1);
            const auto releases_before=world_releases,moves_before=world_moves,wave_before=world_wave_queries;method(c,2);assert(world_wave_queries==wave_before);assert(world_releases==releases_before+1&&world_moves==moves_before&&ld32(Play+0x12A0+4)==0&&mod_actor::failed(token));
        }
    }
    assert(world_sets==2&&world_registers==2&&world_releases==(floating_mode?12u:8u));
    if(floating_mode){
        // Cold room recreation succeeds while the ship touches the conservative
        // envelope; drift waits rather than permanently rejecting this actor.
        constexpr uint32_t ship=Arena+0x35000;st32(0x10474BEC,ship);st8(ship+0x3AA,1);st8(ship+0x326,12);st32(ship+0x314,f32_as_u32(1000));st32(ship+0x31C,0);mod_actor::retry(3);std::memset(mem::ptr(A),0,mod_actor::kInstanceBytes);wave_phase=0;st32(Arena+0xF000,3);c.r[3]=mod_actor::kWorldProfile;c.r[5]=Arena+0xF000;hooks.at(0x025DD414)(&c);st32(A+4,53);st16(A+0xE,mod_actor::kWorldProfile);st32(A+0xB0,3);st32(A+0x314,f32_as_u32(1000));st8(A+0x326,12);for(unsigned k=0;k<3;++k)st32(A+0x330+k*4,f32_as_u32(1));method(c,0);assert(c.r[3]==4&&world_sets==3&&world_registers==3&&!mod_actor::failed(3));for(unsigned frame=0;frame<30;++frame){wave_phase+=.03f;method(c,2);}assert(u32_as_f32(ld32(A+0x314))==1000&&u32_as_f32(ld32(A+0x31C))==0&&!mod_actor::failed(3));method(c,1);assert(world_releases==13);st32(0x10474BEC,0);
    }
    return 0;
}
unsigned replacements=0;auto owner=[](uint32_t){return std::string();};auto replace=[&](uint32_t a,PpcFunc f){hooks[a]=f;++replacements;};assert(!mod_actor::install(owner,replace)&&replacements==0);story_notebook::ManifestActor spec;spec.kind="mod";spec.id="post";spec.collision_radius=25;spec.collision_height=80;if(visual_mode){spec.room=12;spec.scale={2,3,4};spec.distant.model="lod";}if(carry_mode){spec.carry_enabled=true;spec.carry_break_on_impact=true;spec.broken_flag="post.broken";}if(combat_mode){spec.combat_enabled=true;spec.combat.health=5;spec.combat.invulnerability_frames=0;spec.combat.flinch={"flinch",2,90,2};spec.combat.stun={"stun",4,0,0};spec.combat.death={"death",2,0,0};spec.combat.attack={2,3,5,35,100,40,0};spec.defeated_flag="post.defeated";spec.native_heart_drop=true;}if(skinned){spec.look_at={true,"head","body",70,35,180,500,20,.3f};}assert(!mod_actor::declare("test",spec,"",1,{}));assert(!mod_actor::declare("test",spec,"bad_bounds",1,{}, {2,0,0},{1,1,1}));if(skinned){auto bad=spec;bad.rotation.x=10;assert(!mod_actor::declare("test",bad,"bad_tilt",1,{}, {-12,-4,-8},{15,80,10},true,{"idle"},{{"root","body","head"},{-1,0,1},{true,true,true}}));bad=spec;bad.scale.y=2;assert(!mod_actor::declare("test",bad,"bad_scale",1,{}, {-12,-4,-8},{15,80,10},true,{"idle"},{{"root","body","head"},{-1,0,1},{true,true,true}}));assert(!mod_actor::declare("test",spec,"bad_basis",1,{}, {-12,-4,-8},{15,80,10},true,{"idle"},{{"root","body","head"},{-1,0,1},{true,true,false}}));assert(!mod_actor::declare("test",spec,"bad_ancestor",1,{}, {-12,-4,-8},{15,80,10},true,{"idle"},{{"root","body","head"},{-1,0,0},{true,true,true}}));}uint32_t token=mod_actor::declare("test",spec,"mod_unique",1,[]{++talk_completions;},{-12,-4,-8},{15,80,10},skinned,combat_mode?std::vector<std::string>{"idle","walk","flinch","stun","death"}:skinned?std::vector<std::string>{"idle","walk"}:std::vector<std::string>{},skinned?mod_actor::ModelRig{{"root","body","head"},{-1,0,1},{true,true,true}}:mod_actor::ModelRig{});assert(token==1);if(visual_mode){assert(!mod_actor::visual_role(0,true));assert(mod_actor::visual_role(token,true));}if(carry_mode)assert(mod_actor::set_carry_break("test","post",[]{++carry_claims;return carry_reward_allowed;}));assert(mod_actor::install(owner,replace)&&replacements==(carry_mode?9u:combat_mode?7u:6u));assert(ld32(Scratch+0xC)==0x123456&&ld32(Scratch+0x1C)==0xabcdef);assert(ld16(Scratch+8)==mod_actor::kProfile&&ld32(Scratch+0x10)==mod_actor::kInstanceBytes&&ld8(Scratch+0x2D)==14&&(ld32(Scratch+0x28)&0x100));Cpu c{};c.r[1]=Arena+0x1F000;for(unsigned i=0;i<4;++i)st32(c.r[1]-0x40+8+i*4,0xAABBCC00+i);st32(Root+0x14,0x1234);st32(Animation,0x2345);st32(0x104B4628,0x3456);st32(0x104B462C,0x4567);c.r[3]=0xB6;hooks.at(0x025E10E0)(&c);assert(c.r[3]==Native&&original_calls==1);c.r[3]=mod_actor::kProfile;hooks.at(0x025E10E0)(&c);assert(c.r[3]==Scratch&&original_calls==1);st32(Arena+0xD000,Arena+0xD008);std::memcpy(mem::ptr(Arena+0xD008),"retail",7);c.r[3]=Arena+0xB000;c.r[4]=Arena+0xD000;c.r[5]=Arena+0x9000;hooks.at(0x02604550)(&c);assert(original_calls==2&&resource_models==0);
std::memcpy(mem::ptr(Arena+0xD008),"mod_unique",11);c.r[3]=Arena+0xB000;c.r[4]=Arena+0xD000;c.r[5]=Arena+0x9000;hooks.at(0x02604550)(&c);assert(original_calls==2&&resource_allocations==2&&resource_models==1&&c.r[3]==Arena+0xB000);
c.r[3]=Arena+0xE000;c.r[4]=Arena+0xD000;hooks.at(0x02604EB4)(&c);assert(c.r[3]==1&&resource_models==2);
st32(Arena+0xE400+0x14,Arena+0xE800);st32(Arena+0xE400+0x18,392000);c.r[3]=Arena+0xE000;c.r[4]=Arena+0xD000;c.r[6]=Arena+0x9000;hooks.at(0x02612E64)(&c);assert(c.r[3]==1);
st32(Arena+0xF000,token);c.r[3]=mod_actor::kProfile;c.r[5]=Arena+0xF000;hooks.at(0x025DD414)(&c);assert(c.r[3]==A&&ld32(Scratch+512+24)==1648);
st32(Play+0x5B34,Arena+0xF400);st32(A+4,42);st16(A+0xE,mod_actor::kProfile);st32(A+0xB0,token);for(int i=0;i<3;++i)st32(A+0x330+i*4,f32_as_u32(1));method(c,0);assert(c.r[3]==1&&ld8(A+0x3AA)==1&&!ld32(A+0x3B4));complete=true;method(c,0);assert(c.r[3]==4&&ld32(A+0x3B4)==Model);assert(ld32(A+0x3F4+0x44)==A+0x3B8);if(visual_mode){
    const uint32_t link=Arena+0xF400,far_plane_address=0x1048CFF0+0x54,old_far=0x47712345;
    auto observer=[&](unsigned room,unsigned square){st8(link+0x326,room);st32(link+0x314,f32_as_u32(float(int((square-1)%7)-3)*100000));st32(link+0x31C,f32_as_u32(float(int((square-1)/7)-3)*100000));};
    unsigned behavior_events=0;mod_actor::set_behavior_sink([&](Cpu*,uint32_t,mod_actor::BehaviorEvent,const mod_actor::Hit*){++behavior_events;});
    assert(mod_actor::owns_visual(A,token));assert(u32_as_f32(ld32(A+0x330))==2&&u32_as_f32(ld32(A+0x334))==3&&u32_as_f32(ld32(A+0x338))==4&&u32_as_f32(ld32(A+0x364))==32);
    st8(A+0x326,13);observer(13,13);st32(far_plane_address,old_far);method(c,2);assert(registered_collision==0&&attack_registrations==0&&ld32(A+0x39C)==0&&behavior_events==0);
    method(c,4);assert(draws==1&&clip_recalculations==2&&clip_far_values[0]==f32_as_u32(500000.f)&&clip_far_values[1]==old_far&&ld32(far_plane_address)==old_far&&behavior_events==0);
    observer(12,12);method(c,4);observer(14,14);method(c,4);assert(draws==1&&clip_recalculations==2);
    std::array<unsigned char,mod_actor::kInstanceBytes> saved;std::memcpy(saved.data(),mem::ptr(A),saved.size());mod_actor::state_loaded();observer(13,13);method(c,2);method(c,4);assert(mod_actor::owns_visual(A,token)&&draws==2&&registered_collision==0&&behavior_events==0&&ld32(far_plane_address)==old_far);
    st32(A+4,43);assert(!mod_actor::owns_visual(A,token));method(c,2);method(c,4);assert(draws==2&&registered_collision==0);std::memcpy(mem::ptr(A),saved.data(),saved.size());
    st16(A+0xE,0xB6);assert(!mod_actor::owns_visual(A,token));std::memcpy(mem::ptr(A),saved.data(),saved.size());
    st32(A+0xC48,ld32(A+0xC48)^1);assert(!mod_actor::owns_visual(A,token));method(c,4);assert(draws==2);std::memcpy(mem::ptr(A),saved.data(),saved.size());
    // Open ocean retains native room zero across geographic square boundaries.
    st8(A+0x326,0);observer(0,11);method(c,4);assert(draws==3&&clip_recalculations==6&&ld32(far_plane_address)==old_far);observer(0,12);method(c,4);assert(draws==3&&clip_recalculations==6);std::memcpy(mem::ptr(A),saved.data(),saved.size());observer(13,13);
    // Recreate the same declaration as home-square shoreline, with no far_plane_address-clip scope.
    st16(Play+0x5D00,42);st32(Play+0x5D70,0x12345678);st32(Play+0x5D74,0x23456789);st32(0x104B4634,0x34567890);st32(0x104B4638,0x45678901);assert(mod_actor::visual_role(token,false));assert(mod_actor::owns_visual(A,token));method(c,4);assert(draws==3);st8(A+0x3AA,0);method(c,0);assert(c.r[3]==4&&mod_actor::owns_visual(A,token));st8(A+0x326,12);observer(13,13);method(c,4);assert(draws==3);observer(12,12);method(c,2);method(c,4);assert(draws==4&&clip_recalculations==6&&ld32(far_plane_address)==old_far&&registered_collision==0&&behavior_events==0);
    assert(ld32(Arena+0x12000+4)==f32_as_u32(43)&&ld32(0x104B4634)==0x34567890&&ld32(0x104B4638)==0x45678901);st16(Play+0x5D00,77);st8(A+0x326,0);observer(0,12);method(c,4);assert(ld32(Arena+0x12000+4)==f32_as_u32(43));assert(draws==5&&clip_recalculations==6&&ld32(far_plane_address)==old_far);observer(0,11);method(c,4);assert(draws==5);observer(12,12);method(c,4);assert(draws==5); // Wrong native room cannot draw a stale proxy.
    method(c,1);assert(!mod_actor::owns_visual(A,token)&&behavior_events==0);
    // Main near_token geometry with a distant replacement is hidden outside its home room.
    spec.id="near";const auto near_token=mod_actor::declare("test",spec,"mod_unique",1,{}, {-12,-4,-8},{15,80,10});assert(near_token&&near_token!=token);mod_actor::set_behavior_sink({});
    std::memset(mem::ptr(A),0,mod_actor::kInstanceBytes);base_allocated=0;st32(Arena+0xF000,near_token);c.r[3]=mod_actor::kProfile;c.r[5]=Arena+0xF000;hooks.at(0x025DD414)(&c);st32(A+4,44);st16(A+0xE,mod_actor::kProfile);st32(A+0xB0,near_token);st8(A+0x326,12);method(c,0);assert(c.r[3]==4&&!mod_actor::owns_visual(A,near_token));observer(13,13);method(c,4);assert(draws==5);observer(0,12);method(c,4);assert(draws==6&&clip_recalculations==6&&ld32(far_plane_address)==old_far);observer(12,12);method(c,4);assert(draws==7);
    std::puts("world visual runtime tests passed");return 0;
}if(combat_mode){combat_tests(c);return 0;}if(carry_mode){
    const uint32_t link=Arena+0xF400;st16(link+0xE,0xA8);st32(link+4,77);st32(link+0x65B8,A);st32(link+0x65A0,42);st32(link+0x65A4,A);st32(A+0x2E0,0);
    mod_actor::Identity id;assert(mod_actor::identify(&c,A,id));mod_actor::MotionState path;path.count=2;path.enabled=true;path.speed=30;path.points[1]={100,0,0};assert(mod_actor::set_motion(&c,id,path));st32(A+0x2E0,0x2000);
    c.r[3]=link;Cpu expected=c;original(&expected,0x0240A41C);hooks.at(0x0240A41C)(&c);assert(std::memcmp(&c,&expected,sizeof c)==0&&ld32(A+0xF44)==1);mod_actor::MotionState paused;assert(mod_actor::get_motion(&c,id,paused)&&!paused.enabled);assert(!mod_actor::set_position(&c,id,{50,0,0})&&!mod_actor::set_motion(&c,id,path)&&!mod_actor::begin_scene(&c,id,7));const auto registrations=registered_collision;method(c,2);assert(registered_collision==registrations);
    std::array<unsigned char,mod_actor::kInstanceBytes> held;std::memcpy(held.data(),mem::ptr(A),held.size());mod_actor::state_loaded();assert(ld32(A+0xF44)==1);method(c,2);assert(ld32(A+0xF44)==1);
    // Foreign/stale PID cannot establish a new carry, and original CPU effects survive.
    st32(link+0x65A0,43);c.r[3]=link;hooks.at(0x0240A41C)(&c);assert(c.r[3]==link&&ld32(A+0xF44)==1);c.r[3]=link;hooks.at(0x023DCF8C)(&c);assert(c.r[3]==link&&ld32(link+0x65A4)==A&&ld32(A+0xF44)==1);st32(link+0x65A0,42);
    // Restored invalid private copies must release the actual Link keep without
    // dereferencing or cancelling the actor whose configuration/state is invalid.
    for(unsigned corruption=0;corruption<3;++corruption){
        std::memcpy(mem::ptr(A),held.data(),held.size());st32(link+0x65A0,42);st32(link+0x65A4,A);
        if(corruption==0)st32(A+0xC48,ld32(A+0xC48)^1);else if(corruption==1)st32(A+0xF40,2);else st32(A+0xF4C,0);
        mod_actor::state_loaded();std::array<unsigned char,mod_actor::kInstanceBytes> invalid;std::memcpy(invalid.data(),mem::ptr(A),invalid.size());
        c.r[3]=link;expected=c;const auto clears_before=carry_keep_clears;clobber_search=true;
        if(corruption==1){Cpu empty_expected=expected;st32(link+0x65A0,0xFFFFFFFF);st32(link+0x65A4,0);original(&empty_expected,0x023DCF8C);st32(link+0x65A0,42);st32(link+0x65A4,A);hooks.at(0x023DCF8C)(&c);assert(std::memcmp(&c,&empty_expected,sizeof c)==0);}
        else {original(&expected,0x0240A41C);hooks.at(0x0240A41C)(&c);assert(std::memcmp(&c,&expected,sizeof c)==0);}
        clobber_search=false;assert(carry_keep_clears==clears_before+1&&ld32(link+0x65A0)==0xFFFFFFFF&&ld32(link+0x65A4)==0&&std::memcmp(invalid.data(),mem::ptr(A),invalid.size())==0&&carry_claims==0);
    }
    // A forged Flight state with the native carry bit still set cannot award
    // an impact reward after the player's keep was safely detached.
    std::memcpy(mem::ptr(A),held.data(),held.size());st32(A+0xF44,2);st32(A+0xF4C,0);st32(A+0xF54,f32_as_u32(19));st32(A+0xF58,f32_as_u32(34));st32(link+0x65A0,42);st32(link+0x65A4,A);c.r[3]=link;hooks.at(0x0240A41C)(&c);assert(ld32(link+0x65A4)==0);method(c,2);assert(ld32(A+0xF44)==4&&carry_claims==0&&carry_delete_requests==0);
    // Restore cannot redirect native Acch writes to foreign old-position,
    // speed or circle buffers, including bindings omitted by the former guard.
    std::memcpy(mem::ptr(A),held.data(),held.size());st32(link+0x65A0,42);st32(link+0x65A4,A);st32(A+0x370,f32_as_u32(19));st32(A+0x340,f32_as_u32(34));st32(link+0x65F0,0x71);c.r[3]=link;hooks.at(0x023DCF8C)(&c);method(c,2);assert(ld32(A+0xF44)==2&&ld32(A+0xF1C)==1);std::array<unsigned char,mod_actor::kInstanceBytes> flight;std::memcpy(flight.data(),mem::ptr(A),flight.size());
    for(const auto binding:{std::pair<uint32_t,uint32_t>{0x2C,Arena+0x1B000},{0x30,Arena+0x1B00C},{0x34,Arena+0x1B018},{0x38,Arena+0x1B024},{0x3C,Arena+0x1B030},{0x80,Arena+0x1B000},{8,43},{0x84,4},{0x88,Arena+0x1B000}}){
        std::memcpy(mem::ptr(A),flight.data(),flight.size());st32(A+0xC80+binding.first,binding.second);const auto bg_calls=knockback_queries;unsigned char position_before[12];std::memcpy(position_before,mem::ptr(A+0x314),12);method(c,2);assert(ld32(A+0xF44)==4&&knockback_queries==bg_calls&&carry_claims==0&&carry_delete_requests==0&&std::memcmp(position_before,mem::ptr(A+0x314),12)==0);
    }
    // A retail object at another keep is delegated untouched by SDK cleanup.
    std::memcpy(mem::ptr(A),held.data(),held.size());const uint32_t retail=Arena+0x1B000;st16(retail+0xE,0x1C5);st32(retail+4,99);st32(link+0x65A0,99);st32(link+0x65A4,retail);const auto retail_clears=carry_keep_clears,retail_calls=original_calls;c.r[3]=link;hooks.at(0x0240A41C)(&c);assert(c.r[3]==17&&original_calls==retail_calls+1&&carry_keep_clears==retail_clears&&ld32(link+0x65A4)==retail);
    // The old SDK keep must not attach/cancel a retail process reusing its address.
    for(bool releasing:{false,true}){
        std::memcpy(mem::ptr(A),held.data(),held.size());st16(A+0xE,0x1C5);st32(A+4,99);std::array<unsigned char,mod_actor::kInstanceBytes> recycled;std::memcpy(recycled.data(),mem::ptr(A),recycled.size());st32(link+0x65A0,42);st32(link+0x65A4,A);const auto stale_clears=carry_keep_clears;c.r[3]=link;expected=c;
        if(releasing){st32(link+0x65A0,0xFFFFFFFF);st32(link+0x65A4,0);original(&expected,0x023DCF8C);st32(link+0x65A0,42);st32(link+0x65A4,A);hooks.at(0x023DCF8C)(&c);}else {original(&expected,0x0240A41C);hooks.at(0x0240A41C)(&c);}
        assert(std::memcmp(&c,&expected,sizeof c)==0&&carry_keep_clears==stale_clears+1&&ld32(link+0x65A4)==0&&std::memcmp(recycled.data(),mem::ptr(A),recycled.size())==0);
        st32(link+0x65A0,99);st32(link+0x65A4,A);const auto current_clears=carry_keep_clears;c.r[3]=link;hooks.at(0x0240A41C)(&c);assert(c.r[3]==17&&carry_keep_clears==current_clears&&ld32(link+0x65A4)==A&&ld32(link+0x65A0)==99);
    }
    std::memcpy(mem::ptr(A),held.data(),held.size());const auto recycled_clears=carry_keep_clears;
    // Recycled private address/PID without a matching retained generation grants
    // no cleanup authority and cannot clear or cancel the current keep.
    st32(link+0x65A0,43);st32(link+0x65A4,A);st32(A+4,43);c.r[3]=link;hooks.at(0x023DCF8C)(&c);assert(c.r[3]==link&&carry_keep_clears==recycled_clears&&ld32(link+0x65A4)==A);std::memcpy(mem::ptr(A),held.data(),held.size());st32(link+0x65A0,42);st32(link+0x65A4,A);
    st32(A+0x370,0);st32(A+0x340,0);c.r[3]=link;expected=c;original(&expected,0x023DCF8C);st32(link+0x65A0,42);st32(link+0x65A4,A);st32(A+0x2E0,0x2000);hooks.at(0x023DCF8C)(&c);assert(std::memcmp(&c,&expected,sizeof c)==0&&ld32(A+0xF44)==0);assert(mod_actor::get_motion(&c,id,paused)&&paused.enabled&&carry_claims==0);
    std::memcpy(mem::ptr(A),held.data(),held.size());st32(link+0x65A0,42);st32(link+0x65A4,A);st32(A+0x370,f32_as_u32(19));st32(A+0x340,f32_as_u32(34));st16(A+0x322,0x4000);st32(link+0x65F0,0x71);c.r[3]=link;hooks.at(0x023DCF8C)(&c);assert(ld32(A+0xF44)==2&&std::fabs(u32_as_f32(ld32(A+0xF54))-19)<.001);assert(carry_claims==0);
    // Native BG reports a ground contact only on the descending half of flight.
    for(unsigned i=0;i<20&&ld32(A+0xF44)==2;++i)method(c,2);assert(ld32(A+0xF44)==3&&carry_claims==1&&carry_delete_requests==1);method(c,2);assert(carry_claims==1&&carry_delete_requests==1);
    std::memcpy(mem::ptr(A),held.data(),held.size());st32(link+0x65A0,42);st32(link+0x65A4,A);st32(A+0x370,f32_as_u32(19));st32(A+0x340,f32_as_u32(34));st16(A+0x322,0x4000);carry_reward_allowed=false;c.r[3]=link;hooks.at(0x023DCF8C)(&c);for(unsigned i=0;i<20&&ld32(A+0xF44)==2;++i)method(c,2);assert(ld32(A+0xF44)==4&&ld32(A+0xC28)==1&&carry_claims==2&&carry_delete_requests==1);method(c,2);assert(carry_claims==2&&carry_delete_requests==1);
    // Invalid configuration still detaches the exact stale keep before its
    // retained slot is retired by native destruction; no impact reward runs.
    std::memcpy(mem::ptr(A),held.data(),held.size());st32(A+0xC48,ld32(A+0xC48)^1);st32(link+0x65A0,42);st32(link+0x65A4,A);const auto delete_clears=carry_keep_clears;method(c,1);assert(ld32(link+0x65A4)==0&&carry_keep_clears==delete_clears+1&&carry_claims==2);st32(Scratch+512,A);st32(Scratch+516,42);
    // Ordinary deletion of an unbroken held copy only detaches; no reward.
    std::memcpy(mem::ptr(A),held.data(),held.size());st32(link+0x65A0,42);st32(link+0x65A4,A);method(c,1);assert(ld32(link+0x65A4)==0&&carry_claims==2);
    return 0;
}assert(u32_as_f32(ld32(A+0x34C))==-12&&u32_as_f32(ld32(A+0x350))==-4&&u32_as_f32(ld32(A+0x354))==-8&&u32_as_f32(ld32(A+0x358))==15&&u32_as_f32(ld32(A+0x35C))==80&&u32_as_f32(ld32(A+0x360))==10);method(c,2);assert(c.r[3]==1&&registered_collision==1&&u32_as_f32(radius)==25&&u32_as_f32(height)==80);st32(Play+0x5B34,0);method(c,2);assert(c.r[3]==1&&registered_collision==1);st32(Play+0x5B34,Arena+0xF400);const unsigned before_draw=morf_advances;method(c,4);assert(c.r[3]==1&&draws==1);assert(morf_advances==before_draw);if(skinned){assert(morf_creates==1&&clip_loads==4&&morf_advances==1);for(unsigned i=0;i<4;++i)assert(ld32(c.r[1]-0x40+8+i*4)==0xAABBCC00+i);assert(ld32(Root+0x14)==0x1234&&ld32(Animation)==0x2345&&ld32(0x104B4628)==0x3456&&ld32(0x104B462C)==0x4567);}
    // Native ZXY Euler order is RY*RX*RZ. Check actual submitted model matrices, translations and separate scale.
    auto matrix=[&](const float (&expected)[12]){for(unsigned i=0;i<12;++i)assert(std::fabs(u32_as_f32(ld32(Model+0xC8+i*4))-expected[i])<0.00001f);};
    st32(A+0x314,f32_as_u32(12));st32(A+0x318,f32_as_u32(34));st32(A+0x31C,f32_as_u32(56));
    for(unsigned i=0;i<3;++i)st32(A+0x330+i*4,f32_as_u32(float(i+2)));
    st16(A+0x328,0x4000);method(c,2);matrix({1,0,0,12,0,0,-1,34,0,1,0,56});
    st16(A+0x328,0);st16(A+0x32C,0x4000);method(c,2);matrix({0,-1,0,12,1,0,0,34,0,0,1,56});
    st16(A+0x328,0x4000);st16(A+0x32A,0x4000);st16(A+0x32C,0);method(c,2);matrix({0,1,0,12,0,0,-1,34,-1,0,0,56});
    st16(A+0x328,0x2000);st16(A+0x32A,0x1000);st16(A+0x32C,0x3000);method(c,2);
    matrix({0.603553391f,-0.75f,0.270598050f,12,0.653281482f,0.270598050f,-0.707106781f,34,0.457106781f,0.603553391f,0.653281482f,56});
    assert(u32_as_f32(ld32(A+0x34C))==-24&&u32_as_f32(ld32(A+0x350))==-12&&u32_as_f32(ld32(A+0x354))==-32);
    assert(u32_as_f32(ld32(A+0x358))==30&&u32_as_f32(ld32(A+0x35C))==240&&u32_as_f32(ld32(A+0x360))==40);
    assert(ld32(A+0x348)==Model+0xC8); // Native culling applies the unscaled Euler/world matrix exactly once.
    for(unsigned i=0;i<3;++i){assert(u32_as_f32(ld32(Model+0xBC+i*4))==float(i+2));st32(A+0x314+i*4,0);st16(A+0x328+i*2,0);st32(A+0x330+i*4,f32_as_u32(1));}
    method(c,2);
    st32(Play+0x5B34,Arena+0xF400);st32(Arena+0xF400+0x314,f32_as_u32(100));st32(Arena+0xF400+0x31C,f32_as_u32(0));st16(A+0x328,0x2000);st16(A+0x32C,0x1000);st16(A+0xF8,1);method(c,2);assert(ld16(A+0x32A)==0x4000&&ld16(A+0x328)==0x2000&&ld16(A+0x32C)==0x1000);st16(A+0xF8,0);
    // A restored native talk already owns its message process; re-arm its text without spawning twice.
    allow_talk=true;st16(A+0xF8,1);st32(A+0x5E0,1);mod_actor::state_loaded();method(c,2);assert(mod_actor::talking(A)&&message_starts==0);
    st16(A+0xF8,0);method(c,2);assert(!mod_actor::talking(A)&&ld32(A+0x5E0)==0&&talk_completions==0);
    st16(A+0xF8,1);method(c,2);assert(mod_actor::talking(A)&&message_starts==1&&ld32(A+0x5E0)==1);mod_actor::state_loaded();method(c,2);assert(message_starts==1&&mod_actor::talking(A));message_status=0xE;method(c,2);assert(message_status==0x10&&talk_completions==0);message_status=0x11;method(c,2);assert(talk_completions==0);message_status=0x12;method(c,2);assert(talk_completions==1&&!mod_actor::talking(A)&&ld32(A+0x5E0)==2);// Native event reset can lag the completed message; keep it closed across extra steps and a state load.
    method(c,2);mod_actor::state_loaded();method(c,2);assert(message_starts==1&&talk_completions==1&&!mod_actor::talking(A));st16(A+0xF8,0);method(c,2);assert(ld32(A+0x5E0)==0);allow_talk=false;
    mod_actor::state_loaded();method(c,2);method(c,4);assert(registered_collision==18&&draws==2&&ld32(A+0x3B4)==Model);
    clobber_search=true;const Cpu saved_cpu=c;mod_actor::Identity identity;assert(mod_actor::identify(&c,A,identity));assert(std::memcmp(&c,&saved_cpu,sizeof(Cpu))==0);assert(mod_actor::resolve(&c,identity)==A&&std::memcmp(&c,&saved_cpu,sizeof(Cpu))==0);assert(identity.mod=="test"&&identity.id=="post"&&identity.process_id==42&&identity.token==1);auto foreign=identity;foreign.mod="foreign";assert(!mod_actor::resolve(&c,foreign));foreign=identity;foreign.process_id=99;assert(!mod_actor::resolve(&c,foreign));
    mod_actor::State state;assert(mod_actor::snapshot(&c,identity,state));assert(mod_actor::set_rotation(&c,identity,{0,90,0}));assert(ld16(A+0x32A)==0x4000);assert(mod_actor::snapshot(&c,identity,state)&&state.rotation.y==90);assert(!mod_actor::set_rotation(&c,identity,{0,361,0}));assert(mod_actor::set_rotation(&c,identity,{0,0,0}));
    const uint32_t userdata=mod_actor::userdata_address(&c,identity);assert(userdata&&mod_actor::hit_event_address(&c,identity)==userdata+0x160);st8(userdata,0x42);assert(mod_actor::set_behavior_handle(&c,identity,0x123));
    std::vector<mod_actor::BehaviorEvent> events;unsigned notifications=0;mod_actor::set_behavior_sink([&](Cpu*,uint32_t actor,mod_actor::BehaviorEvent event,const mod_actor::Hit*hit){assert(actor==A);events.push_back(event);if(event==mod_actor::BehaviorEvent::Hit){assert(!target_hit&&hit&&hit->source_kind==1&&hit->source_process_id==77&&hit->attack_type==2&&hit->attack_power==3);++notifications;}});
    mod_actor::state_loaded();assert(mod_actor::behavior_handle(&c,identity)==0&&ld8(userdata)==0x42);method(c,2);assert(events.size()>=2&&events[0]==mod_actor::BehaviorEvent::CreateRestored&&events[1]==mod_actor::BehaviorEvent::Execute);const auto restored_count=std::count(events.begin(),events.end(),mod_actor::BehaviorEvent::CreateRestored);method(c,2);assert(std::count(events.begin(),events.end(),mod_actor::BehaviorEvent::CreateRestored)==restored_count);
    assert(mod_actor::set_position(&c,identity,{0,0,0}));assert(mod_actor::begin_scene(&c,identity,55));assert(!mod_actor::set_position(&c,identity,{1,0,0})&&!mod_actor::set_rotation(&c,identity,{0,45,0}));assert(mod_actor::snapshot(&c,identity,state)&&state.scene_owned);assert(mod_actor::scene_rotation(&c,identity,55,{0,45,0}));assert(!mod_actor::end_scene(&c,identity,54));assert(mod_actor::end_scene(&c,identity,55));
    if(skinned){assert(mod_actor::has_animation(&c,identity,"walk"));assert(mod_actor::set_animation(&c,identity,"walk",6,false)&&animation_sets>0);assert(!mod_actor::set_animation(&c,identity,"missing",6,true));assert(mod_actor::begin_scene(&c,identity,56));assert(mod_actor::scene_animation(&c,identity,56,"idle",0,true));assert(!mod_actor::set_animation(&c,identity,"walk",0,true));assert(mod_actor::end_scene(&c,identity,56));assert(mod_actor::snapshot(&c,identity,state)&&state.animation=="walk");
        assert(mod_actor::set_rotation(&c,identity,{0,0,0}));st32(Arena+0xF400+0x37C,f32_as_u32(100));st32(Arena+0xF400+0x380,f32_as_u32(200));st32(Arena+0xF400+0x384,f32_as_u32(100));for(unsigned tick=0;tick<20;++tick)method(c,2);const uint32_t callback=ld32(ld32(ld32(Morf+0xC0))+0xC);assert(callback);const uint32_t info=Arena+0xFA00;st16(info+0xC,100);st16(info+0xE,200);c.r[3]=ld32(Morf+0xC0);c.r[4]=2;c.r[5]=info;registered.at(callback)(&c);assert(int16_t(ld16(info+0xC))<100&&int16_t(ld16(info+0xE))>200);const auto pitch=ld16(info+0xC),yaw=ld16(info+0xE);st16(info+0xC,100);st16(info+0xE,200);c.r[3]=ld32(Morf+0xC0);c.r[4]=2;c.r[5]=info;registered.at(callback)(&c);assert(ld16(info+0xC)==pitch&&ld16(info+0xE)==yaw);
        st16(info+0xC,0x4000);st16(info+0xE,0x4000);c.r[3]=ld32(Morf+0xC0);c.r[4]=2;c.r[5]=info;registered.at(callback)(&c);assert(std::abs(int16_t(ld16(info+0xC)))<=int(35*32768/180)&&std::abs(int16_t(ld16(info+0xE)))<=int(70*32768/180));
        auto over=mod_actor::look_bounds::quaternion({1.0,1.4,.2});for(unsigned k=0;k<4;++k)st32(Morf+0x200+2*16+k*4,f32_as_u32(float(over[k])));const uint32_t post=ld32(ld32(ld32(Morf+0xC4))+0xC);assert(post);c.r[3]=ld32(Morf+0xC4);c.r[4]=2;registered.at(post)(&c);mod_actor::look_bounds::Quaternion final;for(unsigned k=0;k<4;++k)final[k]=u32_as_f32(ld32(Morf+0x200+2*16+k*4));mod_actor::look_bounds::Euler projected;assert(mod_actor::look_bounds::nearest(final,{0,0,0},projected));assert(std::fabs(projected[0])<=35*mod_actor::look_bounds::Pi/180+1e-6&&std::fabs(projected[1])<=70*mod_actor::look_bounds::Pi/180+1e-6);assert(!mod_actor::set_rotation(&c,identity,{10,0,0}));}
    assert(mod_actor::set_rotation(&c,identity,{0,0,0}));const uint32_t linkshape=Arena+0xF400+0x765C+0x118;st32(linkshape,f32_as_u32(30));st32(linkshape+4,0);st32(linkshape+8,0);st32(linkshape+12,f32_as_u32(20));st32(linkshape+16,f32_as_u32(80));assert(mod_actor::start_path(&c,identity,{{0,0,0},{100,0,0}},100,false));method(c,2);assert(u32_as_f32(ld32(A+0x314))==0&&ld16(A+0x32A)==0);st32(linkshape,f32_as_u32(1000));method(c,2);assert(u32_as_f32(ld32(A+0x314))>3&&u32_as_f32(ld32(A+0x314))<4&&ground_queries==1);assert(mod_actor::stop_path(&c,identity));
    const auto yaw_degrees=[&]{return int16_t(ld16(A+0x32A))*180.0/32768;};
    assert(std::fabs(yaw_degrees()-8)<180.0/32768);
    // Draws retain the game-step angle, including repeated render frames.
    const auto moving_yaw=ld16(A+0x32A);method(c,3);method(c,3);assert(ld16(A+0x32A)==moving_yaw);
    const auto turn_case=[&](float initial,story_notebook::WorldPoint end,double expected){
        assert(mod_actor::set_position(&c,identity,{0,0,0}));assert(mod_actor::set_rotation(&c,identity,{0,initial,0}));
        assert(mod_actor::start_path(&c,identity,{{0,0,0},end},100,false));
        method(c,2);assert(std::fabs(yaw_degrees()-expected)<2*180.0/32768);assert(mod_actor::stop_path(&c,identity));
    };
    turn_case(0,{0,0,-100},-8); // exact reversal uses the negative shortest arc
    turn_case(179,{-1.74524f,0,-99.9848f},-179); // crosses the signed wrap by two degrees
    assert(mod_actor::set_position(&c,identity,{0,0,0}));assert(mod_actor::set_rotation(&c,identity,{0,0,0}));
    assert(mod_actor::start_path(&c,identity,{{0,0,0},{1000,0,0}},100,false));
    double previous=0;for(unsigned tick=0;tick<15;++tick){method(c,2);const double heading=yaw_degrees();assert(heading>=previous&&heading<=90&&heading-previous<=8+180.0/32768);previous=heading;}
    assert(previous==90);assert(mod_actor::stop_path(&c,identity));
    assert(mod_actor::set_rotation(&c,identity,{0,-90,0})&&ld16(A+0x32A)==0xC000); // explicit Face remains immediate
    assert(mod_actor::set_hit_notifications("test","post",true));st32(Arena+0xF400+4,77);st32(A+0x3F4+0xA0,77);st32(Attack+0x44,AttackStatus);st32(AttackStatus+0xC,Arena+0xF400);st32(AttackStatus+0x10,77);st32(Attack+0x10,2);st8(Attack+0x14,3);target_hit=true;const auto clears=hit_clears;method(c,2);assert(notifications==1&&hit_clears==clears+1);method(c,2);assert(notifications==1);st32(AttackStatus+0x10,78);target_hit=true;method(c,2);assert(notifications==1&&!target_hit);
    // The dynamic provider is entered only at the genuine native start/resume
    // boundary. Its latched completion is used once, never the legacy callback.
    unsigned selected=1,saved_selection=0,begins=0,resumes=0;std::array<unsigned,3>custom_completions{};bool reject_resume=false;
    const auto legacy_completions=talk_completions;
    assert(mod_actor::set_talk_provider("test","post",[&](uint32_t actor,bool resumed,mod_actor::TalkSelection&out){
        assert(actor==A&&ld32(actor+4)==42);if(resumed){++resumes;if(reject_resume)return false;}else{++begins;saved_selection=selected;}
        out.lines=saved_selection==1?"First":"Second";out.completed=[&,choice=saved_selection]{++custom_completions[choice];};return true;
    }));
    allow_talk=true;st16(A+0xF8,0);method(c,2);const auto starts_before_provider=message_starts;
    message_status=0x11;st16(A+0xF8,1);method(c,2);assert(begins==1&&resumes==0&&message_starts==starts_before_provider+1);
    selected=2;method(c,2);assert(begins==1);mod_actor::state_loaded();method(c,2);assert(resumes==1&&message_starts==starts_before_provider+1);
    message_status=0x12;method(c,2);assert(custom_completions[1]==1&&!custom_completions[2]&&talk_completions==legacy_completions);
    method(c,2);mod_actor::state_loaded();method(c,2);assert(custom_completions[1]==1);
    st16(A+0xF8,0);method(c,2);st16(A+0xF8,1);method(c,2);assert(begins==2);
    reject_resume=true;mod_actor::state_loaded();method(c,2);assert(!mod_actor::talking(A)&&ld32(A+0x5E0)==2&&custom_completions[2]==0&&talk_completions==legacy_completions);
    method(c,2);assert(custom_completions[2]==0);st16(A+0xF8,0);method(c,2);allow_talk=false;
    assert(mod_actor::set_talk_provider("test","post",{}));
    // Failed callback identity revalidation must not read a now-invalid Morf in
    // the execute diagnostic destructor, even with tracing explicitly enabled.
    const uint32_t saved_process=ld32(A+4),saved_morf=ld32(A+0x654);const auto logs_before_invalidation=heading_exec_logs;
    mod_actor::set_behavior_sink([&](Cpu*,uint32_t,mod_actor::BehaviorEvent event,const mod_actor::Hit*){if(event==mod_actor::BehaviorEvent::Execute){st32(A+4,saved_process+1);st32(A+0x654,0xFFFFFF00);}});
    method(c,2);assert(heading_exec_logs==logs_before_invalidation);st32(A+4,saved_process);st32(A+0x654,saved_morf);
    mod_actor::set_behavior_sink([&](Cpu*,uint32_t,mod_actor::BehaviorEvent event,const mod_actor::Hit*){events.push_back(event);});
    method(c,1);assert(events.back()==mod_actor::BehaviorEvent::Destroy);mod_actor::set_behavior_sink({});assert(c.r[3]==1&&deleted==1&&ld8(A+0x3AA)==1);assert(std::getenv("WWHD_MOD_ACTOR_HEAPS")?heap_getters>=16:heap_getters==0);st32(Scratch+512+8,2u*1024*1024);method(c,0);assert(c.r[3]==5&&mod_actor::failed(token)&&ld32(Scratch+512)==0);method(c,1);st32(Scratch+512+8,364752);mod_actor::retry(token);
    fail_phase=true;method(c,0);assert(c.r[3]==5&&mod_actor::failed(token));mod_actor::state_loaded();assert(mod_actor::failed(token));mod_actor::retry(token);assert(!mod_actor::failed(token));
    // A load that completes without its archive in the table (its file could not be read, #281): the actor fails with
    // the reason said, releases only its request's reference (the last one: the id is forgotten, not deleteRes'd), and
    // its delete phase releases nothing, as after a failed resLoad; a second holder's reference is only counted down.
    {fail_phase=false;complete=true;mounted=false;std::vector<std::string> said;mod_actor::set_create_failure_sink([&](const std::string& mod,const std::string& id,const std::string& why){said.push_back(mod+":"+id+": "+why);});
     const unsigned deleted_before=deleted;st8(A+0x3AA,0);st32(ReferenceNode+0x18,1);method(c,0);
     assert(c.r[3]==5&&mod_actor::failed(token)&&forgotten==1&&ld32(A+0x3AC+4)==0&&!ld32(A+0x3B4)&&said.size()==1&&said[0].find("did not load")!=std::string::npos);
     method(c,1);assert(c.r[3]==1&&deleted==deleted_before);
     mod_actor::retry(token);st8(A+0x3AA,0);st32(ReferenceNode+0x18,2);method(c,0);assert(c.r[3]==5&&forgotten==1&&ld32(ReferenceNode+0x18)==1&&ld32(A+0x3AC+4)==0&&said.size()==2);method(c,1);assert(deleted==deleted_before);
     // the file back: the next create loads, makes its model and its delete releases the archive
     mod_actor::retry(token);mounted=true;st8(A+0x3AA,0);assert(mod_actor::set_hit_notifications("test","post",false));method(c,0);assert(c.r[3]==4&&ld32(A+0x3AC+4)==2&&ld32(A+0x3B4)==Model&&said.size()==2);method(c,1);assert(deleted==deleted_before+1);mod_actor::set_create_failure_sink({});}
    if(std::getenv("WWHD_MOD_ACTOR_HEADING_TRACE")&&std::strcmp(std::getenv("WWHD_MOD_ACTOR_HEADING_TRACE"),"1")==0&&(!std::getenv("WWHD_MOD_ACTOR_HEADING_START")||std::atoi(std::getenv("WWHD_MOD_ACTOR_HEADING_START"))<=1)){assert(heading_exec_logs>0&&heading_path_logs>0&&heading_draw_logs>0);assert(heading_exec_logs<=1024&&heading_path_logs<=1024&&heading_draw_logs<=2048);}if(std::getenv("WWHD_MOD_ACTOR_HEADING_START")&&std::strcmp(std::getenv("WWHD_MOD_ACTOR_HEADING_START"),"1000000")==0)assert(heading_exec_logs==0&&heading_path_logs==0&&heading_draw_logs==0);return 0;}
