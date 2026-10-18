// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_people_events.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
using namespace story_native_people_events;
void put(auto&b,size_t p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
struct Fake {
 Expected e;std::map<uint32_t,std::vector<uint8_t>>memory;Router*router=nullptr;
 bool lease=true,actor_ok=true,action_ok=true,throws=false,reenter=false,false_native=false,wrong_staff_request=false,wrong_staff_name=false,clear_attention=false,change_pid=false;
 unsigned action=0,calls=0,writes=0;int fail_write=-1;bool detach_during=false;std::array<uint8_t,0x6C>original[2];
 Fake(){e.identity={0x20000000,0x60000000,0x520,1,2,3,4};e.room_number=11;
  auto&profile=memory[0x101C4CD0];profile.resize(0x30);profile[8]=1;profile[9]=0x75;put(profile,0x10,0x920);
  auto&img=memory[e.identity.image];img.resize(e.identity.bytes);
  for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x20001000+n*0x1000;a.pid=10+n;a.token=1+n;a.profile=0x175;a.subtype=n?9:1;
   put(a.cut,0,0x10003000+n*16);put(a.cut,4,0xFFFFFFFFu);put(a.cut,8,a.address);put(a.cut,0xC,a.address);put(a.cut,0x10,0xFFFFFFFFu);original[n]=a.cut;
   auto&b=memory[a.address];b.resize(0x920);put(b,4,a.pid);b[14]=1;b[15]=0x75;put(b,0x10,0x101C4CD0);put(b,0xB0,0x00FF0000);put(b,0xB4,0x100209C4);b[0x2DD]=uint8_t(a.subtype);b[0x905]=uint8_t(a.subtype);b[0x326]=11;
   std::copy(a.cut.begin(),a.cut.end(),b.begin()+0x3E0);put(b,0x3E0,e.identity.image+0xF0+n*0x50);
   const char*name=n?"SDK_ACTOR_B":"SDK_ACTOR_A";std::memcpy(img.data()+0xF0+n*0x50,name,12);
  }
  auto&foreign=memory[0x10004000];foreign.resize(32);std::memcpy(foreign.data(),"CAMERA",7);
  auto&alias=memory[0x10004100];alias.resize(12);std::memcpy(alias.data(),"SDK_ACTOR_A",12);
 }
 bool read(uint32_t a,std::span<uint8_t>b){for(auto&[p,v]:memory)if(a>=p&&uint64_t(a)+b.size()<=uint64_t(p)+v.size()){std::copy_n(v.begin()+a-p,b.size(),b.begin());return true;}return false;}
 Backend backend(){return {
  [this](uint32_t a,std::span<uint8_t>b){return read(a,b);},
  [this](uint32_t a,std::span<const uint8_t>b){++writes;if(fail_write==int(writes))return false;for(auto&[p,v]:memory)if(a>=p&&uint64_t(a)+b.size()<=uint64_t(p)+v.size()){std::copy(b.begin(),b.end(),v.begin()+a-p);return true;}return false;},
  [this](Cpu*c,uint32_t fn,std::initializer_list<uint32_t>r){++calls;assert(fn==0x0259F858&&r.size()==1);const auto self=*r.begin();const unsigned n=(self-0x20001000-0x3E0)/0x1000;assert(n<2&&router->depth()==1);
   if(detach_during)assert(!router->detach_people(c));
   if(reenter)assert(router->event_move(c,e.actors[1-n].address)==Dispatch::Quarantined);
   c->r[3]=e.identity.manager;c->r[4]=e.identity.image+0xF0+n*0x50;if(wrong_staff_name)c->r[4]=0x10004000;c->r[5]=wrong_staff_request?e.actors[n].address:0;c->r[6]=0;
   assert(router->staff_id(c));if(wrong_staff_request||wrong_staff_name)assert(c->r[3]==0xFFFFFFFFu);else if(!reenter)assert(c->r[3]==n);
   auto&b=memory[e.actors[n].address];put(b,0x3E4,n);put(b,0x3F0,action);if(clear_attention)b[0x440]=0;if(change_pid)put(b,4,99);c->r[10]=0xBAD;c->pc=0x1234;c->f[2].ps0=999;c->lr=0xBAD;
   if(throws)throw std::runtime_error("native synthetic fault");return false_native?0u:1u;
  },
  [this](const Actor&a,const story_native_event_lease::Identity&){return actor_ok&&a.token==e.actors[(a.address-0x20001000)/0x1000].token;},
  [this](const story_native_event_lease::Identity&i){return lease&&i.generation==e.identity.generation&&i.program==e.identity.program;},
  [this](const Binding&b,uint32_t&out){assert(b.staff>=0&&b.staff<2&&b.name==e.identity.image+0xF0+uint32_t(b.staff)*0x50);out=action;return action_ok;},
  {true,true,false,false,false}
 };}
};
int main(){Cpu c{};c.r[3]=0xA;c.r[10]=0xB;c.pc=0xC;c.f[2].ps0=1;c.lr=0xD;const Cpu before=c;
 Fake f;Router r(f.backend(),f.e);f.router=&r;assert(r.configured());assert(r.event_move(&c,0x20009000)==Dispatch::Foreign&&!memcmp(&before,&c,sizeof c));
 for(unsigned n=0;n<2;++n){assert(r.event_move(&c,f.e.actors[n].address)==Dispatch::Processed);assert(r.depth()==0&&!memcmp(&before,&c,sizeof c));}assert(f.calls==2);
 // Both native cut backups may contain staff ID zero; the second actor binds
 // its own index only from the exact fresh, uncached native staff state.
 auto initial=[](Fake&x,uint32_t state=2){auto&img=x.memory[x.e.identity.image];put(img,0xE4,state);for(unsigned n=0;n<2;++n){put(x.e.actors[n].cut,4,0);put(x.memory[x.e.actors[n].address],0x3E4,0);put(img,0xF0+n*0x50+0x38,n);put(img,0xF0+n*0x50+0x3C,0xFFFFFFFFu);}};
 for(uint32_t state:{2u,4u}){Fake x;initial(x,state);Router q(x.backend(),x.e);x.router=&q;for(unsigned n=0;n<2;++n)assert(q.event_move(&c,x.e.actors[n].address)==Dispatch::Processed);assert(x.calls==2&&!q.quarantined()&&!memcmp(&before,&c,sizeof c));assert(x.memory[x.e.actors[1].address][0x3E7]==1);}
 for(unsigned fault=0;fault<7;++fault){Fake x;initial(x);auto&img=x.memory[x.e.identity.image];auto&b=x.memory[x.e.actors[1].address];if(fault==0)put(img,0x17C,0);if(fault==1)put(b,0x3F0,0);if(fault==2)put(img,0x178,0);if(fault==3)put(x.e.actors[1].cut,4,3);if(fault==4)put(b,0x3E4,3);if(fault==5)put(img,0xE4,1);if(fault==6)x.memory.erase(x.e.identity.image);Router q(x.backend(),x.e);x.router=&q;assert(q.event_move(&c,x.e.actors[1].address)==Dispatch::Quarantined&&x.calls==0&&!memcmp(&before,&c,sizeof c));}
 // Outside common scope, even a content-equivalent owned staff name cannot enlist a consumer.
 c.r[3]=f.e.identity.manager;c.r[4]=0x10004100;c.r[5]=0;c.r[6]=0;assert(r.staff_id(&c)&&c.r[3]==0xFFFFFFFFu);c.r[4]=0x10004000;assert(!r.staff_id(&c));c=before;
 assert(r.detach_people(&c)&&r.people_detached()&&!memcmp(&before,&c,sizeof c));for(unsigned n=0;n<2;++n)assert(std::equal(f.original[n].begin(),f.original[n].end(),f.memory[f.e.actors[n].address].begin()+0x3E0));assert(r.event_move(&c,f.e.actors[0].address)==Dispatch::Refused);
 // Source-faithful attention carry-over: exactly one owned byte, native progress retained.
 Fake attention;attention.clear_attention=true;attention.memory[attention.e.actors[0].address][0x440]=1;const auto other=attention.memory[attention.e.actors[1].address];Router at(attention.backend(),attention.e);attention.router=&at;assert(at.event_move(&c,attention.e.actors[0].address)==Dispatch::Processed);assert(attention.writes==1&&attention.memory[attention.e.actors[0].address][0x440]==1&&attention.memory[attention.e.actors[1].address]==other&&!memcmp(&before,&c,sizeof c));
 // Changed identity and reentry after native execution never copy stale attention.
 for(bool nested:{false,true}){Fake bad;bad.clear_attention=true;bad.memory[bad.e.actors[0].address][0x440]=1;bad.reenter=nested;bad.change_pid=!nested;Router guard(bad.backend(),bad.e);bad.router=&guard;assert(guard.event_move(&c,bad.e.actors[0].address)==Dispatch::Quarantined&&bad.writes==0);}
 Fake attention_write;attention_write.clear_attention=true;attention_write.memory[attention_write.e.actors[0].address][0x440]=1;attention_write.fail_write=1;Router failed_attention(attention_write.backend(),attention_write.e);attention_write.router=&failed_attention;assert(failed_attention.event_move(&c,attention_write.e.actors[0].address)==Dispatch::Quarantined);
 // Native common FALSE never falls through to People.privateCut/outer eventMove.
 for(unsigned fault=0;fault<8;++fault){Fake x;if(fault==0)x.false_native=true;if(fault==1)x.throws=true;if(fault==2)x.reenter=true;if(fault==3)x.wrong_staff_request=true;if(fault==4)x.lease=false;if(fault==5)x.actor_ok=false;if(fault==6)x.action_ok=false;if(fault==7)x.wrong_staff_name=true;Router q(x.backend(),x.e);x.router=&q;assert(q.event_move(&c,x.e.actors[0].address)==Dispatch::Quarantined);assert(q.depth()==0&&q.quarantined()&&!memcmp(&before,&c,sizeof c));}
 // Exact identity/profile/config/room/name corruption refuses before native execution.
 for(unsigned fault=0;fault<8;++fault){Fake x;auto&b=x.memory[x.e.actors[0].address];if(fault==0)put(b,4,99);if(fault==1)b[15]=1;if(fault==2)put(b,0x10,0);if(fault==3)put(b,0xB4,0);if(fault==4)put(b,0xB0,0);if(fault==5)b[0x326]=12;if(fault==6)b[0x905]=9;if(fault==7)put(b,0x3E0,x.e.identity.image+0x140);Router q(x.backend(),x.e);x.router=&q;assert(q.event_move(&c,x.e.actors[0].address)==Dispatch::Quarantined&&x.calls==0);}
 // TURN uses the native common interpreter; MOVE requires the separate real safeguards.
 Fake turn;turn.action=3;Router tr(turn.backend(),turn.e);turn.router=&tr;assert(tr.event_move(&c,turn.e.actors[0].address)==Dispatch::Processed);
 Fake move;move.action=4;Router mr(move.backend(),move.e);move.router=&mr;assert(mr.event_move(&c,move.e.actors[0].address)==Dispatch::Refused&&move.calls==0);
 Fake missing;auto mb=missing.backend();mb.capabilities.staff_lookup=false;Router denied(mb,missing.e);assert(!denied.configured()&&denied.event_move(&c,missing.e.actors[0].address)==Dispatch::Refused);
 // Backups must not preserve borrowed owned-image pointers or foreign cut receivers.
 Fake backup;put(backup.e.actors[0].cut,0,backup.e.identity.image+0xF0);Router bad_backup(backup.backend(),backup.e);assert(!bad_backup.configured());
 Fake receiver;put(receiver.e.actors[0].cut,8,receiver.e.actors[1].address);Router bad_receiver(receiver.backend(),receiver.e);assert(!bad_receiver.configured());
 // Detachment cannot occur in-flight, nor write into a replaced actor/PID.
 Fake active;active.detach_during=true;Router ar(active.backend(),active.e);active.router=&ar;assert(ar.event_move(&c,active.e.actors[0].address)==Dispatch::Processed&&active.writes==0);
 Fake stale;Router sr(stale.backend(),stale.e);stale.router=&sr;put(stale.memory[stale.e.actors[1].address],4,99);assert(!sr.detach_people(&c)&&stale.writes==0);
 Fake partial;partial.fail_write=2;Router pr(partial.backend(),partial.e);partial.router=&pr;assert(!pr.detach_people(&c)&&pr.quarantined()&&!pr.people_detached());assert(!memcmp(&before,&c,sizeof c));
 return 0;
}
