// SPDX-License-Identifier: MPL-2.0
#define main people_router_fixture_contract_unused
#include "story_native_people_events_test.cpp"
#include <algorithm>
#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
#include <cstdio>
#undef main
#include "mods/story_native_people_admission.h"
#include "mods/story_native_event_image.h"
#include "mods/story_actors.h"
namespace A=story_native_people_admission;
static std::array<story_actors::ResidentEventIdentity,2>owned_rows;
static bool row_valid=true;
namespace story_actors {
bool resident_event_identity(const std::string&mod,const std::string&id,uint32_t actor,uint32_t pid,ResidentEventIdentity&out){out={};if(!row_valid||mod!="test")return false;for(unsigned n=0;n<2;++n)if(id==(n?"second":"first")&&owned_rows[n].actor==actor&&owned_rows[n].pid==pid){out=owned_rows[n];return true;}return false;}
}
struct Integration:Fake {
 A::Adapter*adapter=nullptr;bool outer=true,fence=false,owned_lease=true;unsigned native_calls=0;
 Integration(){memory[e.identity.image]=story_native_event_image::make_wait_fixture();put(memory[e.identity.image],0xE4,2);for(unsigned n=0;n<2;++n){e.actors[n].subtype=1;e.actors[n].fingerprint.fill(uint8_t(0x11+n));auto&bytes=memory[e.actors[n].address];bytes[0x2DD]=bytes[0x905]=1;put(memory[e.identity.image],0xF0+n*0x50+0x38,n);put(memory[e.identity.image],0xF0+n*0x50+0x3C,0);owned_rows[n]={e.actors[n].token,e.actors[n].address,e.actors[n].pid,e.identity.stage,e.identity.room,e.room_number,1,e.actors[n].fingerprint};}row_valid=true;}
 A::Sources sources(){auto b=backend();A::Sources s;s.read=b.read;s.write=b.write;s.lease=[&](const auto&i){return owned_lease&&i==e.identity;};s.current_scope=[&](const auto&,const auto&){return outer;};s.detach_boundary=[&](const auto&){return fence;};s.invoke=[&](Cpu*c,uint32_t fn,std::initializer_list<uint32_t>r){++native_calls;assert(fn==0x0259F858&&r.size()==1);unsigned n=(*r.begin()-e.actors[0].address-0x3E0)/0x1000;assert(n<2);c->r[3]=e.identity.manager;c->r[4]=e.identity.image+0xF0+n*0x50;c->r[5]=c->r[6]=0;assert(adapter->staff_id(c)&&c->r[3]==n);put(memory[e.actors[n].address],0x3E4,n);put(memory[e.actors[n].address],0x3F0,0);c->lr=0xBAD;c->r[10]=0xBAD;c->f[2].ps0=99;return 1;};return s;}
};
static std::array<A::Reference,2> refs(){return {{{"test","first"},{"test","second"}}};}
#include "mods/story_native_people_bridge.h"
#include <thread>
#include <cstdio>
#include <algorithm>
#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
namespace P=story_native_people_bridge;
namespace D=story_native_event_depth;
static uint32_t bridge_word(std::span<const uint8_t>b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
struct Native:Integration {
 P::Bridge*bridge=nullptr;unsigned cancellations=0,ends=0,body_calls=0;bool restore_fail=false,throw_call=false,wrong_end=false,bad_pools=false,bad_close=false;
 Native(){e.camera.address=0x20005248;e.camera.pid=90;auto&stack=memory[0x60010000];stack.resize(0x1000,0x55);auto&m=memory[e.identity.manager];m.resize(0x540);const auto image=e.identity.image;const std::array<uint32_t,8>pools={image,image+0x40,image+0xF0,image+0x230,image+0x410,0,image+0x510,0};for(unsigned n=0;n<8;++n)put(m,n*4,pools[n]);put(m,0x53C,image+0x40);}
 P::Sources source(){P::Sources s;s.admission=sources();s.stack={{0x60010000,0x1000},{{e.identity.image,e.identity.bytes}}};s.stack_read=s.admission.read;s.stack_write=[&](uint32_t a,std::span<const uint8_t>b){if(restore_fail)return false;return s_write(a,b);};s.verify_stack=[](auto*,const auto&){return true;};s.verify_routes=[](){return true;};s.verify_owned_thread=[](const Cpu*){return true;};s.queue_cancel=[&](const auto&i){assert(i==e.identity);++cancellations;return true;};
 s.guest_call=[&](Cpu*c,uint32_t fn,std::initializer_list<uint32_t>r){++body_calls;assert(fn==0x0259F858&&r.size()==1);const uint32_t self=*r.begin();unsigned n=(self-e.actors[0].address-0x3E0)/0x1000;c->r[3]=e.identity.manager;c->r[4]=e.identity.image+0xF0+n*0x50;c->r[5]=c->r[6]=0;assert(bridge->staff_id(c)&&c->r[3]==n);put(memory[e.actors[n].address],0x3E4,n);put(memory[e.actors[n].address],0x3F0,0);
 c->r[3]=self;assert(bridge->wait(c,true,[&](){put(memory[e.actors[n].address],0x3F8,e.frames);})==P::Decision::Handled);
 c->r[3]=self;auto process=bridge->wait(c,false,[&](){put(memory[e.actors[n].address],0x3F8,0);c->r[3]=e.identity.manager;c->r[4]=wrong_end?99:n;if(bad_pools)put(memory[e.identity.manager],12,e.identity.image+0x234);const auto end=bridge->cut_end(c,[&](){++ends;});if(wrong_end||bad_pools||bad_close)assert(end==P::Decision::RejectOwned);else assert(end==P::Decision::Handled);});if(!wrong_end&&!bad_pools&&!bad_close)assert(process==P::Decision::Handled);
 std::fill_n(memory[0x60010000].begin()+0x80,0x40,0xAA);c->r[10]=0xBAD;c->f[2].ps0=999;c->lr=0xBAD;if(throw_call){memory[e.actors[n].address][0x440]=0;throw std::runtime_error("synthetic native fault");}return 1;};return s;}
 bool s_write(uint32_t a,std::span<const uint8_t>b){for(auto&[base,v]:memory)if(a>=base&&uint64_t(a)+b.size()<=uint64_t(base)+v.size()){std::copy(b.begin(),b.end(),v.begin()+a-base);return true;}return false;}
};
static D::Coverage coverage(){return {{11,11,true},{0,0,true},{22,22,true},true};}
static D::Call outer(const Native&f,unsigned n=0){D::Call c;c.entry=D::Entry::PeopleExecute;c.function=D::entry_address(c.entry);c.identity=f.e.identity;c.authority=D::Authority::Owned;c.receiver=c.subject=f.e.actors[n].address;c.pid=f.e.actors[n].pid;c.token=f.e.actors[n].token;return c;}
int main(){
 Cpu c{};c.r[1]=0x600100C0;c.r[3]=0x20001000;c.r[10]=10;c.lr=11;c.f[2].ps0=1;const Cpu before=c;
 Native f;D::Tracker tracker(f.e,coverage());P::Bridge b(f.e,refs(),f.source(),tracker);f.bridge=&b;assert(b.configured()&&!b.publication_enabled());{auto scope=tracker.enter(outer(f));assert(scope.allowed());assert(b.event_move(&c)==P::Decision::Handled&&!memcmp(&before,&c,sizeof c));assert(tracker.snapshot().npc_depth==1&&!tracker.snapshot().callback_depth);}assert(tracker.snapshot().idle()&&f.body_calls==1&&f.ends==1&&!f.cancellations);for(unsigned n=0x80;n<0xC0;++n)assert(f.memory[0x60010000][n]==0x55);
 for(unsigned fault=0;fault<5;++fault){Native x;x.restore_fail=fault==0;x.throw_call=fault==1;if(fault==1)x.memory[x.e.actors[0].address][0x440]=1;x.wrong_end=fault==2;x.bad_pools=fault==3;D::Tracker t(x.e,coverage());auto src=x.source();if(fault==4)src.verify_stack=[](auto*,const auto&){return false;};P::Bridge bridge(x.e,refs(),src,t);x.bridge=&bridge;c=before;{auto scope=t.enter(outer(x));assert(scope.allowed());assert(bridge.event_move(&c)==P::Decision::RejectOwned);}assert(bridge.fault()&&bridge.cancel_queued()&&x.cancellations==1&&!memcmp(&before,&c,sizeof c));if(fault==1)assert(x.memory[x.e.actors[0].address][0x440]==0&&x.writes==0);if(fault==2||fault==3||fault==4)assert(x.ends==0);}
 // Typed E60 actors run only the selected TURN/WAIT native entry, with
 // cutEnd using the current canonical cut's flag rather than the staff index.
 for(unsigned fault=0;fault<5;++fault){const bool wrong_kind=fault!=0;Native x;story_native_turn_camera_image::Program def;def.camera.start_eye={0,10,20};def.camera.end_eye={10,10,20};x.e.program=story_native_event_program::Program::turn(def,301);x.e.identity.bytes=0xE60;x.memory[x.e.identity.image]=std::vector<uint8_t>(x.e.program->pristine().begin(),x.e.program->pristine().end());auto&image=x.memory[x.e.identity.image];put(image,0xE4,2);for(unsigned n=0;n<2;++n){put(image,0x128+n*0x50,7+n);put(image,0x12C+n*0x50,0xFFFFFFFFu);put(x.e.actors[n].cut,4,0);put(x.memory[x.e.actors[n].address],0x3E4,0);}const auto offsets=x.e.program->pool_offsets();for(unsigned n=0;n<8;++n)put(x.memory[x.e.identity.manager],n*4,x.e.identity.image+offsets[n]);D::Tracker tracker(x.e,coverage());auto source=x.source();unsigned bodies=0,entries=0;source.guest_call=[&](Cpu*q,uint32_t fn,std::initializer_list<uint32_t>args){++bodies;assert(fn==0x0259F858&&args.size()==1);unsigned n=(*args.begin()-x.e.actors[0].address-0x3E0)/0x1000;const bool turning=bridge_word(image,0x128+n*0x50)>=7;const uint32_t action=turning?3:0;q->r[3]=x.e.identity.manager;q->r[4]=x.e.identity.image+0xF0+n*0x50;q->r[5]=q->r[6]=0;assert(x.bridge->staff_id(q)&&q->r[3]==n);put(x.memory[x.e.actors[n].address],0x3E4,n);put(x.memory[x.e.actors[n].address],0x3F0,action);put(image,0x12C+n*0x50,action);q->r[3]=x.e.actors[n].address+0x3E0;if(wrong_kind){auto&consumer=x.memory[x.e.actors[n].address];if(fault==2)put(consumer,0x3F0,0);if(fault==3)put(consumer,0x3E4,1-n);if(fault==4)put(consumer,0x3E0,x.e.identity.image+0x140);const auto denied=fault==1?x.bridge->wait(q,true,[&]{++entries;}):x.bridge->turn(q,true,[&]{++entries;});assert(denied==P::Decision::RejectOwned);return 1u;}auto invoke=[&](bool start,const P::Body&body){return turning?x.bridge->turn(q,start,body):x.bridge->wait(q,start,body);};assert(invoke(true,[&]{++entries;put(x.memory[x.e.actors[n].address],0x3F8,1);})==P::Decision::Handled);q->r[3]=x.e.actors[n].address+0x3E0;assert(invoke(false,[&]{++entries;put(x.memory[x.e.actors[n].address],0x3F8,0);q->r[3]=x.e.identity.manager;q->r[4]=n;assert(x.bridge->cut_end(q,[&]{++x.ends;})==P::Decision::Handled);})==P::Decision::Handled);return 1u;};P::Bridge bridge(x.e,refs(),source,tracker);x.bridge=&bridge;assert(bridge.configured());for(unsigned phase=0;phase<(wrong_kind?1u:2u);++phase)for(unsigned n=0;n<(wrong_kind?1u:2u);++n){if(phase){put(image,0x128+n*0x50,n);put(image,0x12C+n*0x50,0);}c=before;c.r[3]=x.e.actors[n].address;const Cpu actor_input=c;{auto scope=tracker.enter(outer(x,n));assert(scope.allowed());const auto outcome=bridge.event_move(&c);if(outcome!=(wrong_kind?P::Decision::RejectOwned:P::Decision::Handled))std::fprintf(stderr,"TURN fixture wrong=%u phase=%u actor=%u outcome=%u bodies=%u entries=%u ends=%u fault=%u\n",unsigned(wrong_kind),phase,n,unsigned(outcome),bodies,entries,x.ends,unsigned(bridge.fault()));assert(outcome==(wrong_kind?P::Decision::RejectOwned:P::Decision::Handled));}assert(!memcmp(&actor_input,&c,sizeof c));}assert(wrong_kind?(bridge.fault()&&entries==0&&x.ends==0):(bodies==4&&entries==8&&x.ends==4&&!bridge.fault()));}
 // Missing exact outer authority never invokes common; queued cancel stays local.
 Native missing;D::Tracker mt(missing.e,coverage());P::Bridge mb(missing.e,refs(),missing.source(),mt);missing.bridge=&mb;c=before;assert(mb.event_move(&c)==P::Decision::RejectOwned&&missing.body_calls==0&&missing.cancellations==1);
 // Foreign and recycled same-address retail routes delegate the preexisting owner.
 Native foreign;D::Tracker ft(foreign.e,coverage());P::Bridge fb(foreign.e,refs(),foreign.source(),ft);foreign.bridge=&fb;c=before;c.r[3]=0x20009000;assert(fb.event_move(&c)==P::Decision::DelegateOwner&&foreign.cancellations==0);c=before;foreign.owned_lease=false;++owned_rows[0].pid;put(foreign.memory[foreign.e.actors[0].address],4,owned_rows[0].pid);assert(fb.event_move(&c)==P::Decision::DelegateOwner&&foreign.body_calls==0&&foreign.cancellations==0);
 unsigned retail=0;c.r[3]=0x20009900;c.r[4]=7;const Cpu native_args=c;auto delegate=fb.cut_end(&c,[&](){++retail;});assert(delegate==P::Decision::DelegateOwner&&retail==0&&!memcmp(&native_args,&c,sizeof c));if(delegate==P::Decision::DelegateOwner)++retail;assert(retail==1);
 // Ordinary retail staff0/1 on the SAME global manager never become owned by EA.
 for(unsigned staff=0;staff<2;++staff){Native r;D::Tracker rt(r.e,coverage());P::Bridge rb(r.e,refs(),r.source(),rt);r.bridge=&rb;for(unsigned n=0;n<8;++n)put(r.memory[r.e.identity.manager],n*4,0x10001000+n*0x100);put(r.memory[r.e.identity.manager],0x53C,0x10002000);Cpu request{};request.r[3]=r.e.identity.manager;request.r[4]=staff;const Cpu arguments=request;unsigned originals=0;auto result=rb.cut_end(&request,[&](){++originals;});assert(result==P::Decision::DelegateOwner&&originals==0&&!memcmp(&arguments,&request,sizeof request));if(result==P::Decision::DelegateOwner)++originals;assert(originals==1&&!rb.fault()&&!r.cancellations);}
 // Native ManagerMain temporarily clears current_event for special staff.
 // This bridge delegates without calling native; Routes must authenticate the
 // camera/timekeeper ancestry. People indices still require current_event.
 for(unsigned staff=0;staff<4;++staff){Native r;D::Tracker rt(r.e,coverage());P::Bridge rb(r.e,refs(),r.source(),rt);r.bridge=&rb;put(r.memory[r.e.identity.manager],0x53C,0);Cpu request{};request.r[3]=r.e.identity.manager;request.r[4]=staff;const Cpu arguments=request;unsigned originals=0;auto result=rb.cut_end(&request,[&](){++originals;});assert(result==(staff>=2?P::Decision::DelegateOwner:P::Decision::RejectOwned));assert(originals==0&&!memcmp(&arguments,&request,sizeof request));assert(rb.fault()==(staff<2));}
 // A completed CLOSE permits the genuine owned WAIT to repeat cutEnd while
 // current_event is zero. Partial completion or a PLAY tuple never does.
 for(unsigned fault=0;fault<4;++fault){Native r;r.bad_close=fault!=0;put(r.memory[r.e.identity.image],0xE4,fault==3?2:4);put(r.memory[r.e.identity.manager],0x53C,0);r.memory[r.e.identity.manager][0x3F]=0x3F;if(fault==1)r.memory[r.e.identity.manager][0x3F]=0x1F;if(fault==2)r.memory[r.e.identity.manager][0x40]=1;D::Tracker rt(r.e,coverage());P::Bridge rb(r.e,refs(),r.source(),rt);r.bridge=&rb;c=before;{auto scope=rt.enter(outer(r));assert(scope.allowed());assert(rb.event_move(&c)==(fault?P::Decision::RejectOwned:P::Decision::Handled));}assert(r.ends==(fault?0u:1u)&&rb.fault()==bool(fault)&&!memcmp(&before,&c,sizeof c));}
 // Recycled same-address WAIT self delegates, without consuming native owner.
 owned_rows[0].pid=foreign.e.actors[0].pid+1;c.r[3]=foreign.e.actors[0].address+0x3E0;unsigned waits=0;auto wait=fb.wait(&c,false,[&](){++waits;});assert(wait==P::Decision::DelegateOwner&&waits==0);if(wait==P::Decision::DelegateOwner)++waits;assert(waits==1);
 // Exact own image staff pointer outside Common queues immediate cancellation.
 Native denied;D::Tracker dt(denied.e,coverage());P::Bridge db(denied.e,refs(),denied.source(),dt);denied.bridge=&db;Cpu lookup{};lookup.r[3]=denied.e.identity.manager;lookup.r[4]=denied.e.identity.image+0xF0;lookup.r[10]=77;const Cpu input=lookup;assert(db.staff_id(&lookup)&&lookup.r[3]==0xFFFFFFFFu&&lookup.r[10]==77&&db.fault()&&db.cancel_queued()&&denied.cancellations==1);Cpu expected=input;expected.r[3]=0xFFFFFFFFu;assert(!memcmp(&expected,&lookup,sizeof lookup));
 // Wrong guest thread cannot touch Router/shared active state or invoke native.
 Native threaded;D::Tracker tt(threaded.e,coverage());auto ts=threaded.source();const auto owner=std::this_thread::get_id();ts.verify_owned_thread=[owner](const Cpu*){return std::this_thread::get_id()==owner;};P::Bridge tb(threaded.e,refs(),ts,tt);threaded.bridge=&tb;std::array<std::thread,4>threads;
 for(unsigned n=0;n<4;++n)threads[n]=std::thread([&,n](){Cpu request=before;if(n==0)assert(tb.event_move(&request)==P::Decision::RejectOwned);if(n==1){request.r[3]=threaded.e.identity.manager;request.r[4]=threaded.e.identity.image+0xF0;assert(tb.staff_id(&request)&&request.r[3]==0xFFFFFFFFu);}if(n==2){request.r[3]=threaded.e.actors[0].address+0x3E0;assert(tb.wait(&request,false,[](){assert(false);})==P::Decision::RejectOwned);}if(n==3){request.r[3]=threaded.e.identity.manager;request.r[4]=0;assert(tb.cut_end(&request,[](){assert(false);})==P::Decision::RejectOwned);}});
 for(auto&t:threads)t.join();assert(tb.fault()&&tb.cancel_queued()&&threaded.cancellations==1&&threaded.body_calls==0);
 // Explicit unresolved routing boundary: without an independently proven
 // active installed claim, wrong-thread recycled EA is conservatively rejected.
 // This is NOT a claim that an unconditional global hook preserves retail.
 Native ambiguous;ambiguous.owned_lease=false;++owned_rows[0].pid;D::Tracker at(ambiguous.e,coverage());auto as=ambiguous.source();as.verify_owned_thread=[](const Cpu*){return false;};P::Bridge ab(ambiguous.e,refs(),as,at);ambiguous.bridge=&ab;Cpu candidate=before;assert(ab.event_move(&candidate)==P::Decision::RejectOwned&&ambiguous.cancellations==1&&ambiguous.body_calls==0);
 // Unknown index on an actually retained owned manager is refused BEFOREbody.
 Native unknown;D::Tracker ut(unknown.e,coverage());P::Bridge ub(unknown.e,refs(),unknown.source(),ut);unknown.bridge=&ub;Cpu end{};end.r[3]=unknown.e.identity.manager;end.r[4]=99;unsigned writes=0;assert(ub.cut_end(&end,[&](){++writes;})==P::Decision::RejectOwned&&writes==0&&unknown.cancellations==1);
}
