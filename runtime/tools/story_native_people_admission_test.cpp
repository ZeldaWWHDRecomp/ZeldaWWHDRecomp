// SPDX-License-Identifier: MPL-2.0
#define main people_router_fixture_contract_unused
#include "story_native_people_events_test.cpp"
#include <string>
#include <array>
#include <vector>
#include <cstdint>
#undef main
#include "mods/story_native_people_admission.h"
#include "mods/story_native_event_image.h"
#include "mods/story_actors.h"
#include <string>
#include <array>
#include <vector>
#include <cstdint>
namespace A=story_native_people_admission;
static std::array<story_actors::ResidentEventIdentity,2>owned_rows;
static bool row_valid=true;
namespace story_actors {
bool resident_event_identity(const std::string&mod,const std::string&id,uint32_t actor,uint32_t pid,ResidentEventIdentity&out){out={};if(!row_valid||mod!="test")return false;for(unsigned n=0;n<2;++n)if(id==(n?"second":"first")&&owned_rows[n].actor==actor&&owned_rows[n].pid==pid){out=owned_rows[n];return true;}return false;}
}
struct Integration:Fake {
 A::Adapter*adapter=nullptr;bool outer=true,fence=false,owned_lease=true;unsigned native_calls=0,native_action=0;
 Integration(){memory[e.identity.image]=story_native_event_image::make_wait_fixture();put(memory[e.identity.image],0xE4,2);for(unsigned n=0;n<2;++n){e.actors[n].subtype=1;e.actors[n].fingerprint.fill(uint8_t(0x11+n));auto&bytes=memory[e.actors[n].address];bytes[0x2DD]=bytes[0x905]=1;put(memory[e.identity.image],0xF0+n*0x50+0x38,n);put(memory[e.identity.image],0xF0+n*0x50+0x3C,0);owned_rows[n]={e.actors[n].token,e.actors[n].address,e.actors[n].pid,e.identity.stage,e.identity.room,e.room_number,1,e.actors[n].fingerprint};}row_valid=true;}
 A::Sources sources(){auto b=backend();A::Sources s;s.read=b.read;s.write=b.write;s.lease=[&](const auto&i){return owned_lease&&i==e.identity;};s.current_scope=[&](const auto&,const auto&){return outer;};s.detach_boundary=[&](const auto&){return fence;};s.invoke=[&](Cpu*c,uint32_t fn,std::initializer_list<uint32_t>r){++native_calls;assert(fn==0x0259F858&&r.size()==1);unsigned n=(*r.begin()-e.actors[0].address-0x3E0)/0x1000;assert(n<2);c->r[3]=e.identity.manager;c->r[4]=e.identity.image+0xF0+n*0x50;c->r[5]=c->r[6]=0;assert(adapter->staff_id(c)&&c->r[3]==n);put(memory[e.actors[n].address],0x3E4,n);put(memory[e.actors[n].address],0x3F0,native_action);c->lr=0xBAD;c->r[10]=0xBAD;c->f[2].ps0=99;return 1;};return s;}
};
static std::array<A::Reference,2> refs(){return {{{"test","first"},{"test","second"}}};}
int main(){Cpu c{};c.r[3]=99;c.r[10]=100;c.lr=101;c.f[2].ps0=1;const Cpu before=c;
 Integration f;A::Adapter a(f.e,refs(),f.sources());f.adapter=&a;assert(a.configured()&&!a.publication_enabled());for(unsigned n=0;n<2;++n)assert(a.event_move(&c,f.e.actors[n].address)==Dispatch::Processed&&!memcmp(&before,&c,sizeof c));assert(f.native_calls==2);
 c.r[3]=f.e.identity.manager;c.r[4]=f.e.identity.image+0xF0;c.r[5]=c.r[6]=0;assert(a.staff_id(&c)&&c.r[3]==0xFFFFFFFFu);c=before;
 assert(a.event_move(&c,0x20009000)==Dispatch::Foreign);assert(!a.detach_people(&c)&&f.writes==0);f.fence=true;assert(a.detach_people(&c)&&f.writes==2&&!memcmp(&before,&c,sizeof c));
 for(unsigned fault=0;fault<7;++fault){Integration x;if(fault==0)row_valid=false;if(fault==1)owned_rows[0].token++;if(fault==2)owned_rows[0].fingerprint[0]^=1;if(fault==3)x.owned_lease=false;if(fault==4)x.outer=false;if(fault==5)put(x.memory[x.e.identity.image],0x410+0x30,99);if(fault==6)put(x.memory[x.e.actors[0].address],0x3E0,x.e.identity.image+0x140);A::Adapter q(x.e,refs(),x.sources());x.adapter=&q;auto result=q.event_move(&c,x.e.actors[0].address);assert(result==Dispatch::Refused||result==Dispatch::Quarantined);assert(x.native_calls==0&&x.writes==0&&!memcmp(&before,&c,sizeof c));}
 // The E60 descriptor admits only each actor's TURN then WAIT cuts.
 for(unsigned fault=0;fault<7;++fault){Integration x;story_native_turn_camera_image::Program def;def.camera.start_eye={0,10,20};def.camera.end_eye={10,10,20};x.e.program=story_native_event_program::Program::turn(def,301);x.e.identity.bytes=0xE60;x.memory[x.e.identity.image]=std::vector<uint8_t>(x.e.program->pristine().begin(),x.e.program->pristine().end());auto&image=x.memory[x.e.identity.image];put(image,0xE4,2);for(unsigned n=0;n<2;++n){put(image,0xF0+n*0x50+0x38,7+n);put(image,0xF0+n*0x50+0x3C,3);}x.native_action=3;if(fault==1)put(image,0x128,8);if(fault==2)put(image,0x12C,0);if(fault==3)image[0xD80]^=1;if(fault==4)put(image,0x130,2);if(fault==5)x.e.program.reset();if(fault==6)x.e.frames++;A::Adapter turn(x.e,refs(),x.sources());x.adapter=&turn;c=before;const auto result=turn.event_move(&c,x.e.actors[0].address);assert(result==(fault?Dispatch::Refused:Dispatch::Processed)||(fault&&result==Dispatch::Quarantined));assert(x.native_calls==(fault?0u:1u)&&!memcmp(&before,&c,sizeof c));}
 Integration bad;auto sources=bad.sources();sources.lease={};A::Adapter disabled(bad.e,refs(),sources);assert(!disabled.configured());auto duplicated=refs();duplicated[1]=duplicated[0];A::Adapter duplicate(bad.e,duplicated,bad.sources());assert(!duplicate.configured());
}
