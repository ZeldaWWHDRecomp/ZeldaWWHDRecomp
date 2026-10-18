// SPDX-License-Identifier: MPL-2.0
#include "story_native_stage_binding.h"
#include <limits>
namespace story_native_stage_binding {
struct Cell::Version {Snapshot snapshot;uint64_t native_epoch{},restore_epoch{},revision{};};
bool Cell::parked(const std::shared_ptr<const threads::NativeQuiescence>&q,const Cpu*c)noexcept{
 return q&&!q->read_only()&&q->current(c);
}
bool Cell::owned()const noexcept{return thread_==std::this_thread::get_id()&&
 story_native_event_game_thread::verify(cpu_,ticket_);}
Cell::Cell(const Scan&s,Snapshot v,const Cpu*c,Ticket t,std::shared_ptr<const void>run)
 :tracker_(s.owner_),cpu_(c),ticket_(t),thread_(std::this_thread::get_id()),lineage_(std::move(run)){
 std::atomic_store(&version_,std::make_shared<const Version>(Version{std::move(v),tracker_->epoch_,tracker_->restore_epoch_,1}));
}
std::shared_ptr<Cell>Cell::create(const Scan&s,Snapshot v,const Cpu*c,Ticket t,
 std::shared_ptr<const void>run,std::shared_ptr<const threads::NativeQuiescence>q){
 if(!c||!run||!story_native_event_game_thread::verify(c,t)||!parked(q,c)||!s.current()||
  !s.owner_||!v.control||v.receipt!=s.receipt())return {};
 return std::shared_ptr<Cell>(new Cell(s,std::move(v),c,t,std::move(run)));
}
Cell::Validation::Validation(std::shared_ptr<Cell>c,Snapshot v,
 std::shared_ptr<const threads::NativeQuiescence>q,std::function<bool()>source)
 :cell_(std::move(c)),before_(cell_?std::atomic_load(&cell_->version_):nullptr),next_(std::move(v)),
 park_(std::move(q)),actual_source_(std::move(source)){}
Cell::Certificate::Certificate(Validation&&v,std::shared_ptr<const Version>after)
 :cell_(std::move(v.cell_)),before_(std::move(v.before_)),after_(std::move(after)),
 park_(std::move(v.park_)),actual_source_(std::move(v.actual_source_)){}
std::optional<Cell::Snapshot>Cell::current(const Scan&s)const noexcept{
 try{const auto v=std::atomic_load(&version_);if(!owned()||!v||!s.holds_lifetime_of(*tracker_)||!s.current()||
  s.receipt()!=v->snapshot.receipt||tracker_->epoch_!=v->native_epoch||
  tracker_->restore_epoch_!=v->restore_epoch)return {};return v->snapshot;}catch(...){return {};}
}
std::optional<Cell::Certificate>Cell::stage(Validation&&p,const Scan&s){
 if(p.consumed_)return {};p.consumed_=true;
 try{auto c=p.cell_;auto old=p.before_;
 if(!c||!old||!c->owned()||!parked(p.park_,c->cpu_)||!p.actual_source_||
  !s.holds_lifetime_of(*c->tracker_)||!s.current()||std::atomic_load(&c->version_)!=old||
  p.next_.receipt!=s.receipt()||p.next_.control!=old->snapshot.control||
  p.next_.receipt.owner!=old->snapshot.receipt.owner||
  p.next_.receipt.generation==old->snapshot.receipt.generation||
  c->tracker_->restore_epoch_!=old->restore_epoch||c->tracker_->epoch_<=old->native_epoch||
  old->revision==std::numeric_limits<uint64_t>::max()||!p.actual_source_()||
  !parked(p.park_,c->cpu_)||!s.current()||std::atomic_load(&c->version_)!=old)return {};
 auto next=std::make_shared<const Version>(Version{p.next_,c->tracker_->epoch_,old->restore_epoch,old->revision+1});
 return Certificate(std::move(p),std::move(next));}catch(...){return {};}
}
bool Cell::commit(Certificate&&p,const Scan&s)noexcept{
 if(p.consumed_)return false;p.consumed_=true;
 try{if(p.cell_.get()!=this||!p.before_||!p.after_||!owned()||!parked(p.park_,cpu_)||
  !s.holds_lifetime_of(*tracker_)||!s.current()||s.receipt()!=p.after_->snapshot.receipt||
  tracker_->epoch_!=p.after_->native_epoch||tracker_->restore_epoch_!=p.after_->restore_epoch||
  !p.actual_source_||!p.actual_source_()||!parked(p.park_,cpu_)||!s.current())return false;
 auto expected=p.before_;return std::atomic_compare_exchange_strong(&version_,&expected,p.after_);
 }catch(...){return false;}
}
}
