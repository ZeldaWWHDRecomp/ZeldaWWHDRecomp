// SPDX-License-Identifier: MPL-2.0
#include "savestate_native_aggregate.h"
#include "savestate_native_finished_load.h"
#include <type_traits>
#include <algorithm>
#include <cassert>
#include <functional>
#include <thread>
#include <stdexcept>
#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include <utility>
#include <memory>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include <utility>
#include <memory>
#endif
// Protocol-only stand-in. This target does not link the real Stage group and
// cannot create resource, lease, restore or native admission authority.
namespace story_native_stage_restore_group {
class StageRestoreGroup {
public:
 std::vector<int>*events;std::thread::id thread=std::this_thread::get_id();unsigned closes=0,close_calls=0;
 explicit StageRestoreGroup(std::vector<int>&e):events(&e){}
 static void close(StageRestoreGroup&g)noexcept{assert(g.thread==std::this_thread::get_id());++g.close_calls;if(g.closes)return;g.closes=1;g.events->push_back(900);}
};
}
namespace story_native_restore_resume {
class CurrentLoadOwner {
public:
 std::vector<int>*events;std::thread::id thread=std::this_thread::get_id();unsigned closes=0;
 std::function<bool()>all_released;
 std::function<bool(const ss::native_gate::FinishedLoad&)>finalize;
 static bool finish(const ss::native_gate::FinishedLoad&t)noexcept{
  assert(t.current()&&t.owner());auto&o=*t.owner();assert(o.finalize);return o.finalize(t);
 }
 explicit CurrentLoadOwner(std::vector<int>&e):events(&e){}
 static void close(CurrentLoadOwner&o)noexcept{assert(o.thread==std::this_thread::get_id());if(o.closes)return;assert(!o.all_released||o.all_released());o.closes=1;o.events->push_back(901);}
};
}
namespace G=ss::native_gate;
using Owner=story_native_restore_resume::CurrentLoadOwner;
using Group=story_native_stage_restore_group::StageRestoreGroup;
struct Child final:G::Controller {
 unsigned id;std::vector<int>&events;bool held=false,revoked=false,fail=false,throws=false,finish_fail=false,finish_throw=false;unsigned finishes=0,save_finishes=0;
 std::function<void()>during_begin,during_finish;
 std::shared_ptr<Group>received_group;std::shared_ptr<Owner>received_owner;unsigned group_calls=0;
 Child(unsigned n,std::vector<int>&e):id(n),events(e){}
 struct Pin final:G::Hold{Child&c;explicit Pin(Child&x):c(x){assert(!c.held);c.held=true;}~Pin()override{c.events.push_back(-int(c.id));c.held=false;}};
 std::unique_ptr<G::Hold>begin(unsigned mode){events.push_back(int(mode*100+id));if(mode>1)revoked=true;if(during_begin)during_begin();if(throws)throw std::runtime_error("begin");if(fail)return {};return std::make_unique<Pin>(*this);}
 std::unique_ptr<G::Hold>begin_save()override{return begin(1);}std::unique_ptr<G::Hold>begin_load(const G::SavedView&)override{return begin(2);}std::unique_ptr<G::Hold>begin_progress_load()override{return begin(3);}
 std::unique_ptr<G::Hold>begin_group_load(const G::SavedView&v,std::shared_ptr<Group>g,std::shared_ptr<Owner>owner={})override{++group_calls;received_group=std::move(g);received_owner=std::move(owner);return begin_load(v);}
 bool finish_load(const G::SavedView&)override{assert(held);events.push_back(400+int(id));++finishes;if(during_finish)during_finish();if(finish_throw)throw std::runtime_error("finish");return !finish_fail;}
 bool finish_save()override{assert(held);events.push_back(500+int(id));++save_finishes;if(during_finish)during_finish();if(finish_throw)throw std::runtime_error("save finish");return !finish_fail;}
};
struct Fixture {
 std::vector<int>events;Child a{1,events},b{2,events},c{3,events};std::array<G::Controller*,3>children{&a,&b,&c};std::string why;
 std::unique_ptr<G::Aggregate>aggregate=G::Aggregate::create(children,why);
 ss::MemoryChunks chunks;std::array<uint8_t,8>allocation_bytes{};std::array<ss::MemoryRegion,1>regions{{{0x60000000,0x10000}}};
 G::SavedView view(){return {ss::Reader(allocation_bytes.data(),allocation_bytes.size()),regions,chunks};}
 bool all_held()const{return a.held&&b.held&&c.held;}
};
static void group_protocol(){
 // Factory is load-only, and a missing/throwing factory cannot touch a child.
 for(unsigned fault=0;fault<4;++fault){Fixture x;unsigned calls=0;auto malformed=std::make_shared<Group>(x.events);
  x.aggregate=G::Aggregate::create(x.children,x.why,[&](const G::SavedView&v)->G::Aggregate::LoadGroup{assert(&v.chunks==&x.chunks);++calls;if(fault==1)throw std::runtime_error("group");if(fault==2)return {malformed,nullptr,{},nullptr};if(fault==3)return {{},&Group::close,{},nullptr};return {};});
  {auto save=x.aggregate->begin_save();assert(save&&calls==0);}{auto progress=x.aggregate->begin_progress_load();assert(progress&&calls==0);}x.events.clear();
  assert(!x.aggregate->begin_load(x.view())&&calls==1&&x.events.empty());
  assert(!x.aggregate->begin_save()&&!x.aggregate->begin_load(x.view())&&calls==1);
 }
 for(unsigned fault=0;fault<6;++fault){Fixture x;auto group=std::make_shared<Group>(x.events);unsigned calls=0;
  x.aggregate=G::Aggregate::create(x.children,x.why,[&](const G::SavedView&v){assert(v.allocations.p==x.allocation_bytes.data());++calls;return G::Aggregate::LoadGroup{group,&Group::close,{},nullptr};});
  if(fault==1)x.b.fail=true;if(fault==2)x.b.throws=true;
  auto hold=x.aggregate->begin_load(x.view());assert(calls==1);
  if(fault==1||fault==2){assert(!hold&&group->closes==1);assert((x.events==std::vector<int>{201,202,900,-1}));assert(!x.c.group_calls);}
  else{
   assert(hold&&x.all_held()&&group->closes==0);
   for(auto*child:{&x.a,&x.b,&x.c})assert(child->received_group==group&&child->group_calls==1);
   if(fault==3)x.b.finish_throw=true;if(fault==5)x.b.finish_fail=true;
   if(fault==3||fault==5)x.c.during_finish=[&]{assert(group->closes==1&&x.all_held());};
   if(fault==4)x.a.during_finish=[&]{hold.reset();assert(x.all_held()&&group->closes==0);};
   std::thread foreign([&]{assert(!x.aggregate->finish_load(x.view())&&group->closes==0);});foreign.join();
   assert(x.aggregate->finish_load(x.view())==(fault!=3&&fault!=5));
   assert(x.a.finishes==1&&x.b.finishes==1&&x.c.finishes==1);
   hold.reset();assert(group->closes==1);
   if(fault==3||fault==5){assert(group->close_calls>=2);assert((x.events==std::vector<int>{201,202,203,401,402,900,403,-3,-2,-1}));}
   else assert((x.events==std::vector<int>{201,202,203,401,402,403,900,-3,-2,-1}));
  }
  assert(!x.a.held&&!x.b.held&&!x.c.held);if(fault==1||fault==2||fault==3||fault==5)assert(!x.aggregate->begin_save());
 }
#ifndef _WIN32
 const auto pid=fork();assert(pid>=0);if(pid==0){std::set_terminate([]{std::_Exit(84);});Fixture x;
  auto group=std::make_shared<Group>(x.events);x.aggregate=G::Aggregate::create(x.children,x.why,[&](const G::SavedView&){return G::Aggregate::LoadGroup{group,&Group::close,{},nullptr};});
  auto hold=x.aggregate->begin_load(x.view());assert(hold);std::thread wrong([h=std::move(hold)]()mutable{h.reset();});wrong.join();std::_Exit(1);
 }int status=0;assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==84);
#endif
}
static void owner_protocol(){
 for(unsigned fault=0;fault<7;++fault){Fixture x;auto group=std::make_shared<Group>(x.events);auto owner=std::make_shared<Owner>(x.events);
  owner->all_released=[&]{return !x.a.held&&!x.b.held&&!x.c.held;};
  x.aggregate=G::Aggregate::create(x.children,x.why,[&](const G::SavedView&v){assert(&v.chunks==&x.chunks);return G::Aggregate::LoadGroup{group,&Group::close,owner,fault==6?nullptr:&Owner::close};});
  if(fault==1)x.b.fail=true;if(fault==2)x.b.throws=true;
  auto hold=x.aggregate->begin_load(x.view());
  if(fault==6){assert(!hold&&x.a.group_calls==0&&group->closes==1&&owner->closes==0);continue;}
  if(fault==1||fault==2){assert(!hold&&group->closes==1&&owner->closes==1);assert((x.events==std::vector<int>{201,202,900,-1,901}));continue;}
  assert(hold&&x.all_held()&&owner->closes==0);
  for(auto*child:{&x.a,&x.b,&x.c})assert(child->received_owner==owner);
  if(fault==3)x.b.finish_throw=true;if(fault==4)x.b.finish_fail=true;
  if(fault==3||fault==4)x.c.during_finish=[&]{assert(group->closes==1&&owner->closes==0&&x.all_held());};
  if(fault==5){hold.reset();assert((x.events==std::vector<int>{201,202,203,900,-3,-2,-1,901}));}
  else{assert(x.aggregate->finish_load(x.view())==(fault!=3&&fault!=4));assert(owner->closes==0);hold.reset();}
  assert(group->closes==1&&owner->closes==1&&!x.all_held());assert(x.events.back()==901);
  Owner::close(*owner);assert(x.events.back()==901&&owner->closes==1);
 }
}
static void finished_protocol(){
 static_assert(!std::is_default_constructible_v<G::FinishedLoad>);
 static_assert(!std::is_copy_constructible_v<G::FinishedLoad>);
 static_assert(!std::is_move_constructible_v<G::FinishedLoad>);
 for(unsigned fault=0;fault<7;++fault){
  Fixture x;auto group=std::make_shared<Group>(x.events);auto owner=std::make_shared<Owner>(x.events);unsigned calls=0;
  std::unique_ptr<G::Hold>hold;
  owner->all_released=[&]{return !x.a.held&&!x.b.held&&!x.c.held;};
  owner->finalize=[&](const G::FinishedLoad&t){
   ++calls;assert(x.all_held()&&group->closes==0&&owner->closes==0&&t.owner()==owner&&&t.view().chunks==&x.chunks);
   assert(t.view().allocations.p==x.allocation_bytes.data());
   assert(x.a.finishes==1&&x.b.finishes==1&&x.c.finishes==1);
   std::thread foreign([&]{assert(!t.current());});foreign.join();assert(t.current());
   x.events.push_back(800);
   if(fault==5){hold.reset();assert(x.all_held()&&group->closes==0&&owner->closes==0);}
   return fault!=3;
  };
  x.aggregate=G::Aggregate::create(x.children,x.why,[&](const G::SavedView&){return G::Aggregate::LoadGroup{group,&Group::close,fault==6?nullptr:owner,&Owner::close,&Owner::finish};});
  if(fault==1)x.b.finish_fail=true;if(fault==2)x.b.finish_throw=true;
  hold=x.aggregate->begin_load(x.view());
  if(fault==6){assert(!hold&&!calls&&!x.a.group_calls);continue;}
  assert(hold&&x.all_held());
  if(fault==4){std::thread foreign([&]{assert(!x.aggregate->finish_load(x.view()));});foreign.join();assert(!calls&&x.all_held());}
  assert(x.aggregate->finish_load(x.view())==(fault!=1&&fault!=2&&fault!=3));
  assert(calls==unsigned(fault!=1&&fault!=2));
  hold.reset();assert(group->closes==1&&owner->closes==1&&x.events.back()==901);
  const auto close=std::find(x.events.begin(),x.events.end(),900),released=std::find(x.events.begin(),x.events.end(),-3);
  assert(close<released);
 }
}
int main(){group_protocol();owner_protocol();finished_protocol();
 Fixture f;assert(f.aggregate&&f.aggregate->child_count()==3);std::string why;
 assert(!f.aggregate->finish_save());
 {Fixture x;auto hold=x.aggregate->begin_save();assert(hold&&x.all_held());
  std::thread foreign([&]{assert(!x.aggregate->finish_save());});foreign.join();
  for(auto*child:{&x.a,&x.b,&x.c})child->during_finish=[&]{assert(x.all_held()&&!x.aggregate->finish_save()&&!x.aggregate->begin_save());};
  assert(x.aggregate->finish_save()&&!x.aggregate->finish_save());
  assert(x.a.save_finishes==1&&x.b.save_finishes==1&&x.c.save_finishes==1);hold.reset();
  assert((x.events==std::vector<int>{101,102,103,501,502,503,-3,-2,-1}));
 }
 for(bool throwing:{false,true}){Fixture x;auto hold=x.aggregate->begin_save();x.b.finish_fail=!throwing;x.b.finish_throw=throwing;
  assert(!x.aggregate->finish_save()&&x.all_held());assert(x.a.save_finishes==1&&x.b.save_finishes==1&&x.c.save_finishes==1);
  assert(!x.aggregate->finish_save());hold.reset();assert(x.aggregate->begin_save());
 }
 {Fixture x;auto hold=x.aggregate->begin_load(x.view());assert(hold&&!x.aggregate->finish_save());}
 {Fixture x;auto hold=x.aggregate->begin_save();x.a.during_finish=[&]{hold.reset();assert(x.all_held());};
  x.b.during_finish=x.c.during_finish=[&]{assert(x.all_held());};assert(x.aggregate->finish_save());
  assert(!x.a.held&&!x.b.held&&!x.c.held);
 }
 assert(!G::Aggregate::create({},why));std::array<G::Controller*,2>nulls{&f.a,nullptr},duplicates{&f.a,&f.a};assert(!G::Aggregate::create(nulls,why)&&!G::Aggregate::create(duplicates,why));std::vector<G::Controller*>huge(130,&f.a);assert(!G::Aggregate::create(huge,why));
 assert(!f.aggregate->finish_load(f.view()));
 {auto hold=f.aggregate->begin_save();assert(hold&&f.all_held()&&!f.aggregate->begin_save()&&!f.aggregate->begin_load(f.view())&&!f.aggregate->finish_load(f.view()));std::thread foreign([&]{assert(!f.aggregate->begin_progress_load()&&!f.aggregate->finish_load(f.view()));});foreign.join();}
 assert((f.events==std::vector<int>{101,102,103,-3,-2,-1}));
 for(bool throwing:{false,true}){Fixture x;x.b.fail=!throwing;x.b.throws=throwing;assert(!x.aggregate->begin_save()&&!x.a.held&&!x.b.held&&!x.c.held);assert((x.events==std::vector<int>{101,102,-1}));x.b.fail=x.b.throws=false;assert(x.aggregate->begin_save());}
 {Fixture x;x.a.during_begin=[&]{assert(!x.aggregate->begin_save());};assert(x.aggregate->begin_save());}
 for(unsigned fault=0;fault<3;++fault){Fixture x;if(fault==0)x.b.fail=true;if(fault==1)x.b.throws=true;if(fault<2){assert(!x.aggregate->begin_load(x.view()));assert(x.a.revoked&&x.b.revoked&&!x.c.revoked);}else{auto load=x.aggregate->begin_load(x.view());assert(load&&x.all_held());load.reset();assert(!x.aggregate->finish_load(x.view()));}assert(!x.aggregate->begin_save()&&!x.aggregate->begin_progress_load());}
 {Fixture x;auto load=x.aggregate->begin_load(x.view());assert(load&&x.all_held());for(auto*child:{&x.a,&x.b,&x.c})child->during_finish=[&]{assert(x.all_held()&&!x.aggregate->begin_save());};std::thread foreign([&]{assert(!x.aggregate->finish_load(x.view()));});foreign.join();assert(x.aggregate->finish_load(x.view())&&!x.aggregate->finish_load(x.view()));assert(x.a.finishes==1&&x.b.finishes==1&&x.c.finishes==1);load.reset();assert((x.events==std::vector<int>{201,202,203,401,402,403,-3,-2,-1}));assert(x.aggregate->begin_save());}
 for(bool throwing:{false,true}){Fixture x;auto load=x.aggregate->begin_load(x.view());x.b.finish_fail=!throwing;x.b.finish_throw=throwing;assert(!x.aggregate->finish_load(x.view()));assert(x.a.finishes==1&&x.b.finishes==1&&x.c.finishes==1&&x.all_held());load.reset();assert(!x.aggregate->begin_save()&&!x.aggregate->begin_load(x.view()));}
 {Fixture x;auto load=x.aggregate->begin_load(x.view());auto wrong=x.view();ss::MemoryChunks different;G::SavedView substituted{wrong.allocations,wrong.regions,different};assert(!x.aggregate->finish_load(substituted)&&!x.a.finishes&&!x.b.finishes&&!x.c.finishes);load.reset();assert(!x.aggregate->begin_save());}
 for(bool throwing:{false,true}){Fixture x;x.b.fail=!throwing;x.b.throws=throwing;assert(!x.aggregate->begin_progress_load()&&x.a.revoked&&x.b.revoked&&x.c.revoked);assert((x.events==std::vector<int>{301,302,303,-3,-1}));assert(!x.a.held&&!x.c.held&&!x.aggregate->begin_save());}
 {Fixture x;auto progress=x.aggregate->begin_progress_load();assert(progress&&x.all_held());progress.reset();assert(x.a.revoked&&x.b.revoked&&x.c.revoked);}
 {Fixture x;auto load=x.aggregate->begin_load(x.view());x.a.during_finish=[&]{load.reset();assert(x.all_held());};x.b.during_finish=x.c.during_finish=[&]{assert(x.all_held());};assert(x.aggregate->finish_load(x.view()));assert(!x.a.held&&!x.b.held&&!x.c.held);assert((x.events==std::vector<int>{201,202,203,401,402,403,-3,-2,-1}));}
#ifndef _WIN32
 const auto pid=fork();assert(pid>=0);if(pid==0){std::set_terminate([]{std::_Exit(83);});Fixture x;auto hold=x.aggregate->begin_save();std::thread wrong([h=std::move(hold)]()mutable{h.reset();});wrong.join();std::_Exit(1);}int status=0;assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==83);
#endif
}
