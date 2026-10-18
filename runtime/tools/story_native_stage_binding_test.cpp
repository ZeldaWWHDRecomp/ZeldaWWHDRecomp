// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_stage_binding.h"
#include <cassert>
#include <thread>
#include <optional>
#include <utility>
#include <functional>
#include <memory>
// Isolated physical scheduler/ticket and runtime validator issuers only. No
// production runtime Context, native routes or admission consumer is linked.
namespace {const Cpu*const cpu=reinterpret_cast<const Cpu*>(uintptr_t(0x1000));bool parked=true,ticket_live=true;std::thread::id host=std::this_thread::get_id();}
namespace story_native_event_game_thread {bool verify(const Cpu*c,Ticket t)noexcept{return ticket_live&&c==cpu&&t.epoch==1&&t.guest==2&&std::this_thread::get_id()==host;}}
namespace threads {class NativeQuiescenceIssuer {public:static auto make(bool readonly=false){return std::shared_ptr<const NativeQuiescence>(new NativeQuiescence([](const Cpu*c){return parked&&c==cpu;},[]{},readonly));}};}
namespace story_native_wait_runtime_controller {class Context {public:
 using Cell=story_native_stage_binding::Cell;using Scan=story_native_stage_owner::Tracker::Scan;using Snapshot=story_native_stage_owner_observer::Snapshot;
 static auto create(const Scan&s,Snapshot v,std::shared_ptr<const threads::NativeQuiescence>q){return Cell::create(s,v,cpu,{1,2},std::make_shared<int>(1),q);}
 static auto stage(std::shared_ptr<Cell>c,Snapshot v,std::shared_ptr<const threads::NativeQuiescence>q,const Scan&s,std::function<bool()>f){return Cell::stage(Cell::Validation(std::move(c),v,std::move(q),std::move(f)),s);}
 static bool commit(Cell&c,Cell::Certificate&&p,const Scan&s){return c.commit(std::move(p),s);}
};}
using namespace story_native_stage_owner;using Context=story_native_wait_runtime_controller::Context;
Receipt candidate(){Receipt r;auto&o=r.owner;o.info=0x60810000;o.archive_wrapper=0x60811000;o.archive_holder=0x60812000;o.archive_resource=0x60818000;o.archive_base=0x6081F000;o.archive_bytes=0x3000;o.outer_base=0x60820000;o.outer_bytes=0x1000;o.resource=0x60820240;o.resource_bytes=0x80;o.entry=0x60820118;o.archive_key=0x1004DF68;o.resource_key=0x1004DF70;o.request_heap=o.pack_heap=0x60830000;o.data_heap=0x60831000;o.data_heap_parent=0x60832000;o.stage_name[0]='s';return r;}
void issue(Tracker&t,Receipt r){assert(t.finish_setinfo(t.begin_native(),[&]{return std::optional(r);}));}
int main(){
 auto q=threads::NativeQuiescenceIssuer::make();
 for(unsigned mode=0;mode<14;++mode){Tracker t;auto r=candidate();issue(t,r);std::shared_ptr<story_native_stage_binding::Cell>cell;
  {auto scan=t.scan();cell=Context::create(scan,{0x2000,scan.receipt()},q);assert(cell&&cell->current(scan));
   auto ro=threads::NativeQuiescenceIssuer::make(true);assert(!Context::create(scan,{0x2000,scan.receipt()},ro));}
  if(mode==1)t.state_loaded();if(mode==2)++r.owner.info;issue(t,r);
  auto scan=t.scan();auto next=story_native_stage_owner_observer::Snapshot{0x2000,scan.receipt()};
  assert(!cell->current(scan));if(mode==3)++next.control;
  auto use_q=q;if(mode==4)use_q=threads::NativeQuiescenceIssuer::make(true);
  if(mode==5)parked=false;if(mode==6)ticket_live=false;
  bool source_live=true,source_throw=false;
  auto proof=Context::stage(cell,next,use_q,scan,[&]{if(source_throw)throw 1;if(!source_live)return false;if(mode==7)return false;if(mode==8)throw 1;return true;});
  if(mode>=1&&mode<=8){assert(!proof&&!cell->current(scan));parked=ticket_live=true;continue;}
  assert(proof);if(mode==9)parked=false;if(mode==10)ticket_live=false;
  if(mode==11){issue(t,r);assert(!scan.current());}
  if(mode==12)source_live=false;if(mode==13)source_throw=true;
  const bool ok=Context::commit(*cell,std::move(*proof),scan);assert(ok==(mode==0));
  assert(!Context::commit(*cell,std::move(*proof),scan));
  if(ok){assert(cell->current(scan));bool foreign=true;std::thread([&]{foreign=bool(cell->current(scan));}).join();assert(!foreign);}
  parked=ticket_live=true;
 }
 // A certificate staged against one revision cannot replay over a separately
 // committed certificate; wrong actual Tracker scans also refuse atomically.
 Tracker t,other;auto r=candidate();issue(t,r);issue(other,r);std::shared_ptr<story_native_stage_binding::Cell>cell;
 {auto s=t.scan();cell=Context::create(s,{0x2000,s.receipt()},q);}issue(t,r);auto s=t.scan();auto foreign=other.scan();auto v=story_native_stage_owner_observer::Snapshot{0x2000,s.receipt()};
 assert(!Context::stage(cell,v,q,foreign,[]{return true;}));
 auto a=Context::stage(cell,v,q,s,[]{return true;});auto b=Context::stage(cell,v,q,s,[]{return true;});assert(a&&b);
 assert(Context::commit(*cell,std::move(*a),s));assert(!Context::commit(*cell,std::move(*b),s));assert(cell->current(s));
}
