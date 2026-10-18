// SPDX-License-Identifier: MPL-2.0
#include "story_native_camera_restore.h"
#include "story_native_camera_guard.h"
#include <algorithm>
namespace story_native_camera_restore {namespace {
uint32_t word(std::span<const uint8_t>b,unsigned p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
void put(std::span<uint8_t>b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
bool boundary(const Identity&i,const Boundary&b){return b.identity==i&&b.exclusive&&b.fenced&&!b.foreign&&!b.fault&&b.manager_owned&&!b.manager_depth&&!b.camera_depth&&!b.npc_depth&&!b.callback_depth&&!b.foreign_depth;}
bool control(const Boundary&b){return b.control_mode==3&&b.partner1==0xFFFFFFFFu&&b.partner2==0xFFFFFFFFu&&b.talk_partner==0xFFFFFFFFu&&b.item_partner==0xFFFFFFFFu&&b.event_id==-1;}
bool aliases(const Camera&c){return word(c.work37C,8)==c.timer&&word(c.engine100,8)==c.counter108&&word(c.engine100,0x18)==c.counter118&&word(c.engine100,0x1C)==c.elapsed11C&&std::equal(c.done100.begin(),c.done100.end(),c.engine100.begin());}
story_native_camera_guard::Snapshot snapshot(const Camera&c,const Identity&i){
 using namespace story_native_camera_guard;
 Snapshot s;s.identity=i;s.camera=c.address;s.pid=c.pid;s.flags=c.flags;s.style_table=c.style_table;s.style_count=c.style_count;s.param_pointer=c.param_pointer;s.param_index=int32_t(c.param_index);
 s.tuple={int32_t(c.current_type),int32_t(c.next_type),int32_t(c.event_type),int32_t(word(c.event_data,12)),int32_t(c.style),int32_t(word(c.mode_phase13C)),int32_t(word(c.mode_phase13C,4)),word(c.mode_phase13C,8),word(c.type_gate518),word(c.engine100,0x14)};return s;
}
}
Plan prepare(const Expected&e,const Camera&c,const Boundary&b,VerifyCamera verify){
 Plan p;auto fail=[&](Reason r){p.reason_=r;return p;};
 if(!e.identity.manager||!e.identity.image||!e.identity.generation||!e.identity.program||!e.identity.stage||!e.identity.room||e.camera.address<0x10000000||uint64_t(e.camera.address)+0x8B8>0x50000000||e.camera.address&3||!e.camera.pid||e.camera.pid==0xFFFFFFFFu||c.address!=e.camera.address||c.pid!=e.camera.pid)return fail(Reason::Identity);
 if(!boundary(e.identity,b))return fail(Reason::Boundary);
 if(!control(b))return fail(Reason::Control);
 if(!aliases(e.camera)||!aliases(c))return fail(Reason::Snapshot);
 if(!verify)return fail(Reason::Policy);
 try{if(!verify(e.camera,e.identity)||!verify(c,e.identity))return fail(Reason::Policy);}catch(...){return fail(Reason::Policy);}
 if(!story_native_event_observation::valid_camera_transition(e.camera,c))return fail(Reason::Transition);
 using namespace story_native_camera_guard;
 Guards g;g.exclusive=g.manager_owned=true;
 // Full source-table verification above certifies the tuples. This guard adds
 // coherent return/cache restoration and exact owned transition constraints.
 auto tuple=evaluate(Phase::PlanDetach,snapshot(e.camera,e.identity),snapshot(c,e.identity),g,+[](const Tuple&){return true;});
 if(!tuple.has_restore_plan())return fail(Reason::Transition);
 p.expected_=e;p.boundary_=b;unsigned n=0;
 auto add=[&](uint32_t offset,std::span<const uint8_t>data){auto&s=p.segments_[n++];s.offset=offset;s.bytes=uint32_t(data.size());std::copy(data.begin(),data.end(),s.data.begin());};
 auto scalar=[&](uint32_t offset,uint32_t value){std::array<uint8_t,4>v{};put(v,0,value);add(offset,v);};
 add(0x10,e.camera.initial_view010);add(0x3C,e.camera.view03C);scalar(0x68,e.camera.bg);
 add(0xA4,e.camera.positions0A4);add(0x100,e.camera.engine100);add(0x13C,e.camera.mode_phase13C);
 add(0x37C,e.camera.work37C);add(0x3FC,e.camera.event_data);scalar(0x510,e.camera.flags);
 std::array<uint8_t,16>types{};put(types,0,e.camera.style);put(types,4,word(e.camera.type_gate518));put(types,8,e.camera.current_type);put(types,12,e.camera.next_type);add(0x514,types);
 scalar(0x600,e.camera.trim);std::array<uint8_t,8>cache{};put(cache,0,e.camera.param_pointer);put(cache,4,e.camera.param_index);add(0x8A8,cache);
 // Every word from the independent tuple plan must occur exactly once, with
 // matching bytes. Segments are disjoint, ordered and bounded in this camera.
 for(const auto&w:tuple.writes()){unsigned matches=0;for(const auto&s:p.segments_)if(w.offset>=s.offset&&uint64_t(w.offset)+4<=uint64_t(s.offset)+s.bytes){++matches;if(word(s.data,w.offset-s.offset)!=w.value)return fail(Reason::Snapshot);}if(matches!=1)return fail(Reason::Snapshot);}
 for(unsigned j=0;j<p.segments_.size();++j){const auto&s=p.segments_[j];if(!s.bytes||s.bytes>s.data.size()||uint64_t(s.offset)+s.bytes>0x8B8||(j&&p.segments_[j-1].offset+p.segments_[j-1].bytes>s.offset))return fail(Reason::Snapshot);}
 p.valid_=true;p.reason_=Reason::None;return p;
}
bool Plan::verify_post(const Camera&c,const Boundary&b,VerifyCamera verify)const{
 if(!valid_||!boundary(expected_.identity,b)||!control(b)||b.logic_step!=boundary_.logic_step||b.serial!=boundary_.serial||c!=expected_.camera||!verify)return false;
 try{return aliases(c)&&verify(c,expected_.identity);}catch(...){return false;}
}
}
