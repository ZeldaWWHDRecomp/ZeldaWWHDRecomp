// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_cancel_consumer_write.h"
#include <cassert>
#include <cstdint>
#include <span>
namespace CW=story_native_cancel_consumer_write;
static threads::NativeQuiescence read_only_quiescence(bool&);
template<class F,class T,class Outer,class Q,class Closed,class Source>
void writer_cases(F&f,T&t,const Outer&outer,const Q&q,const Closed&closed,Source&source){
 // Borrowed save-state parking never grants write/native authority, even
 // with an otherwise genuine Closed result and current claimed outer token.
 {bool readonly_live=true;auto readonly=read_only_quiescence(readonly_live);
 unsigned callbacks=0;CW::Sources refused;
 refused.retained=[&](const auto&){++callbacks;return true;};
 refused.capture=[&](auto&){++callbacks;return true;};
 refused.camera=[&](const auto&,const auto&){++callbacks;return true;};
 refused.write=[&](uint32_t,std::span<const uint8_t>){++callbacks;return true;};
 refused.quarantine=[&]{++callbacks;};CW::Writer readonly_writer;
 assert(readonly.current(&f.cpu)&&readonly.read_only());
 assert(readonly_writer.apply(&f.cpu,t,outer,readonly,f.e,closed,refused)==CW::Result::Refused&&callbacks==0);}
 const auto saved=f.mem;const auto original=f.current;
 for(unsigned mode=0;mode<5;++mode){
  f.mem=saved;f.current=original;unsigned writes=0,quarantines=0;CW::Writer writer;
  CW::Sources s;s.retained=[&](const auto&e){return e==f.e;};s.camera=[](const auto&,const auto&){return true;};s.quarantine=[&]{++quarantines;};
  s.capture=[&](auto&out){story_native_cancel_observation::Sample sample;if(!source.sample(&f.cpu,sample))return false;out.people=sample.poses;out.camera=sample.observation.camera;out.boundary=sample.boundary;if(mode==1)++out.people[1].identity.pid;return true;};
  s.write=[&](uint32_t at,std::span<const uint8_t>bytes){++writes;
   if(at>=f.e.camera.address&&uint64_t(at)+bytes.size()<=uint64_t(f.e.camera.address)+0x8B8){
    const auto offset=at-f.e.camera.address;auto&c=f.current;const auto&b=f.e.camera;
   switch(offset){
    case 0x10:c.initial_view010=b.initial_view010;break;
    case 0x3C:c.view03C=b.view03C;break;
    case 0x68:c.bg=b.bg;break;
    case 0xA4:c.positions0A4=b.positions0A4;break;
    case 0x100:c.engine100=b.engine100;c.done100=b.done100;c.counter108=b.counter108;c.counter118=b.counter118;c.elapsed11C=b.elapsed11C;break;
    case 0x13C:c.mode_phase13C=b.mode_phase13C;break;
    case 0x37C:c.work37C=b.work37C;c.timer=b.timer;break;
    case 0x3FC:c.event_data=b.event_data;break;
    case 0x510:c.flags=b.flags;break;
    case 0x514:c.style=b.style;c.type_gate518=b.type_gate518;c.current_type=b.current_type;c.next_type=b.next_type;break;
    case 0x600:c.trim=b.trim;break;
    case 0x8A8:c.param_pointer=b.param_pointer;c.param_index=b.param_index;break;
    default:assert(false);
   }
   }else assert(f.write(at,bytes));
   if(mode==2&&writes==3)return false;
   if(mode==3&&writes==3)throw 1;
   if(mode==4&&writes==1)f.mem[f.e.actors[1].address][4]^=1;
   return true;
  };
  std::thread foreign([&]{assert(writer.apply(&f.cpu,t,outer,q,f.e,closed,s)==CW::Result::Refused);});foreign.join();assert(!writes&&!quarantines);
  const auto result=writer.apply(&f.cpu,t,outer,q,f.e,closed,s);
  if(mode==0){assert(result==CW::Result::Complete&&writes==34&&!quarantines);assert(f.current==f.e.camera);}
  else if(mode==1)assert(result==CW::Result::Refused&&!writes&&!quarantines);
  else assert(result==CW::Result::Quarantined&&writes&&(quarantines==1));
  const auto count=writes;writer.apply(&f.cpu,t,outer,q,f.e,closed,s);assert(writes==count);
 }
 f.mem=saved;f.current=original;
}

#define STORY_CANCEL_WRITER_TEST_HOOK(...) writer_cases(__VA_ARGS__)
#define main cancellation_observation_fixture_main
#include "story_native_cancel_observation_test.cpp"
#include <cstdint>
#include <span>
#undef main
static threads::NativeQuiescence read_only_quiescence(bool&live){return threads::NativeQuiescenceIssuer::test(live,true);}
int main(){return cancellation_observation_fixture_main();}
