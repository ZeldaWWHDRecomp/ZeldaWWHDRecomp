// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_depth.h"
#include "story_native_restore_resume.h"
#include "story_native_event_game_thread.h"
#include "runtime.h"
#include "native_quiescence.h"
#include <exception>
namespace story_native_event_depth {namespace {
uint64_t serial(const Record&r){auto word=[&](unsigned p){return uint32_t(r[p])<<24|uint32_t(r[p+1])<<16|uint32_t(r[p+2])<<8|r[p+3];};return uint64_t(word(68))<<32|word(72);}
bool idle_source(const story_native_restore_resume::SavedRunOrigin&o){const auto&s=o.scope();return s.identity==o.expected().identity&&s.exclusive&&!s.fenced&&!s.foreign&&!s.fault&&!s.manager_depth&&!s.camera_depth&&!s.npc_depth&&!s.callback_depth&&!s.foreign_depth&&s.logic_step&&s.serial==serial(o.depth_record());}
bool clock(const story_native_restore_resume::SavedRunOrigin&o){const auto&p=o.expected().program;const auto&c=o.clock();if(!p||p->kind()!=story_native_event_program::Kind::TurnCamera)return !c;return c&&c->identity==o.expected().identity&&c->logic_step==o.scope().logic_step&&c->serial==o.scope().serial&&c->elapsed_ticks<p->watchdog();}
}
Tracker::PreparedRestoreOpen::PreparedRestoreOpen(Tracker*owner,std::unique_lock<std::mutex>&&lock)
 :owner_(owner),lock_(std::move(lock)),thread_(std::this_thread::get_id()){}
Tracker::PreparedRestoreOpen::~PreparedRestoreOpen(){if(lock_.owns_lock()&&thread_!=std::this_thread::get_id())std::terminate();}
void Tracker::PreparedRestoreOpen::open_unchecked()noexcept{
 if(!owner_||consumed_||!lock_.owns_lock()||thread_!=std::this_thread::get_id())std::terminate();
 consumed_=true;owner_->restore_pending_=false;owner_->state_=State::Active;
}
std::optional<Tracker::PreparedRestoreOpen>Tracker::prepare_restore_open(
 const story_native_restore_resume::Boundary&b,const threads::NativeQuiescence&q,const Result&r){try{
 if(!r.restorable()||!r.matches(expected_)||q.read_only()||!q.current(restore_cpu_))return {};
 // Reject an already prepared/nested open before restoring() takes this same
 // nonrecursive mutex. No Source callback is invoked while the probe is held.
 {std::unique_lock probe(mutex_,std::try_to_lock);if(!probe.owns_lock())return {};}
 if(!restoring(b))return {};
 std::unique_lock lock(mutex_,std::try_to_lock);
 if(!lock.owns_lock()||!restore_pending_||fault_||!ready_||state_!=State::Draining||!snapshot_locked().idle()||
  serial_!=serial(b.completion().origin().depth_record())||!b.current(restore_cpu_)||!q.current(restore_cpu_))return {};
 return PreparedRestoreOpen(this,std::move(lock));
 }catch(...){return {};}}
std::unique_ptr<Tracker>Tracker::restore_provisional(const story_native_restore_resume::Boundary&b,const Cpu*c,Coverage coverage,std::function<bool()>current){try{
 const auto&e=b.completion().expected();const auto&o=b.completion().origin();const auto ticket=story_native_event_game_thread::ticket(c);
 if(!c||!b.current(c)||!ticket.epoch||ticket.epoch!=b.load_epoch()||!story_native_event_game_thread::verify(c,ticket)||o.expected()!=e||!b.completion().restore_epoch()||!b.completion().original_model()||!valid_idle_record(o.depth_record(),e)||!idle_source(o)||!clock(o)||!complete(coverage)||!current||!current())return {};
 auto result=std::make_unique<Tracker>(e,coverage);if(!result->ready())return {};
 result->restore_pending_=true;result->state_=State::Draining;result->serial_=serial(o.depth_record());
 result->restore_epoch_=b.completion().restore_epoch();result->restore_load_epoch_=b.load_epoch();result->restore_cpu_=c;result->restore_thread_=std::this_thread::get_id();result->restore_origin_=std::make_shared<const story_native_restore_resume::SavedRunOrigin>(o);result->restore_model_=b.completion().original_model();result->restore_coverage_current_=std::move(current);
 if(!result->restoring(b))return {};return result;
 }catch(...){return {};}}
bool Tracker::restoring(const story_native_restore_resume::Boundary&b)const{try{
 if(restore_thread_!=std::this_thread::get_id()||!restore_cpu_||!b.current(restore_cpu_)||!restore_coverage_current_||!restore_coverage_current_())return false;
 const auto ticket=story_native_event_game_thread::ticket(restore_cpu_);if(!ticket.epoch||ticket.epoch!=restore_load_epoch_||!story_native_event_game_thread::verify(restore_cpu_,ticket))return false;
 std::lock_guard lock(mutex_);const auto&o=b.completion().origin();return restore_pending_&&restore_origin_&&restore_origin_->same_run(o)&&restore_origin_->depth_record()==o.depth_record()&&restore_origin_->scope().logic_step==o.scope().logic_step&&restore_origin_->clock()==o.clock()&&restore_model_==b.completion().original_model()&&restore_epoch_==b.completion().restore_epoch()&&restore_load_epoch_==b.load_epoch()&&b.completion().expected()==expected_&&o.expected()==expected_&&serial_==serial(o.depth_record())&&ready_&&state_==State::Draining&&snapshot_locked().idle();
 }catch(...){return false;}}
}
