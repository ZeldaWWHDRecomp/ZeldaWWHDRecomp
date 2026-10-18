// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_invoke.h"
#include "guest_addr.h"
#include "ppc.h"
#include <array>
#include <utility>
namespace story_native_event_invoke {namespace {
struct Rule{uint32_t function;unsigned regs,stack;};
const std::array<Rule,7> Rules={{{GC(0x0253EC0C),8,1},{GC(0x0253FD1C),1,0},{GC(0x0253EFA8),1,0},{GC(0x0254273C),1,0},{GC(0x025443B4),2,0},{GC(0x02544534),2,0},{GC(0x0259F7D4),3,0}}};
bool valid(Range r){return r.address&&r.bytes&&uint64_t(r.address)+r.bytes<=0x100000000ull;}
bool overlap(Range a,Range b){return uint64_t(a.address)<uint64_t(b.address)+b.bytes&&uint64_t(b.address)<uint64_t(a.address)+a.bytes;}
}
Bridge::Bridge(Config c,Read r,Write w,Call call):config_(std::move(c)),read_(std::move(r)),write_(std::move(w)),call_(std::move(call)){}
Result Bridge::invoke(Cpu*c,uint32_t fn,std::initializer_list<uint32_t> regs,std::initializer_list<uint32_t> stack){
 Result result;auto fail=[&](Reason why){result.reason=why;return result;};
 if(!c||!read_||!write_||!call_||!valid(config_.stack)||config_.protected_ranges.empty()||config_.protected_ranges.size()>16)return fail(Reason::Configuration);
 for(auto range:config_.protected_ranges)if(!valid(range))return fail(Reason::Configuration);
 const Rule*rule=nullptr;for(const auto&r:Rules)if(r.function==fn){rule=&r;break;}if(!rule)return fail(Reason::Function);
 if(regs.size()!=rule->regs||stack.size()!=rule->stack||(rule->stack&&*stack.begin()!=0xFF))return fail(Reason::Arity);
 const uint32_t sp=c->r[1];if((sp&15)||sp<kFrameBytes)return fail(Reason::Stack);const Range frame{sp-kFrameBytes,kFrameBytes};
 if(frame.address<config_.stack.address||uint64_t(frame.address)+frame.bytes>uint64_t(config_.stack.address)+config_.stack.bytes)return fail(Reason::Stack);
 for(auto range:config_.protected_ranges)if(overlap(frame,range))return fail(Reason::Stack);
 const Cpu saved=*c;std::array<uint8_t,kFrameBytes> bytes{};
 try{if(!read_(frame.address,bytes)){*c=saved;return fail(Reason::Read);}}catch(...){*c=saved;return fail(Reason::Read);}
 auto restore=[&](){bool ok=false;try{ok=write_(frame.address,bytes);}catch(...){}*c=saved;return ok;};
 try{
  result.reason=Reason::Write;
  if(rule->stack){const std::array<uint8_t,4> ninth={0,0,0,0xFF};if(!write_(frame.address+8,ninth)){result.reason=Reason::Write;result.restored=restore();if(!result.restored)result.reason=Reason::Restore;return result;}}
  result.reason=Reason::Call;
  result.value=call_(c,fn,regs);result.ok=true;result.reason=Reason::None;
 }catch(...){}
 result.restored=restore();if(!result.restored){result.ok=false;result.reason=Reason::Restore;}
 return result;
}
}
