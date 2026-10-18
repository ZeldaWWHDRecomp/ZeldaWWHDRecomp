// SPDX-License-Identifier: MPL-2.0
#include "story_native_scene_run_owner.h"
#include <limits>
#include <atomic>
#include <stdexcept>
namespace story_native_scene_playback {
struct RunOwnerCell::State {
 std::shared_ptr<const Activation>activation;std::shared_ptr<const void>original_run_life;Endpoint endpoint;
 std::thread::id thread=std::this_thread::get_id();
 uint64_t version=1;std::atomic<bool>alive{true},busy{false};
 std::atomic<unsigned>pending_stops{0};
 std::optional<uint64_t>frontend_generation;
 std::atomic<uint64_t>requested_load_generation{0};
 uint64_t notice_generation=0,notice_target_generation=0,notice_version=0;bool notice_pending=false;
 struct Release {std::atomic<bool>&busy;~Release(){busy.store(false,std::memory_order_release);}};
 void drain(){
  auto pending=pending_stops.exchange(0,std::memory_order_acq_rel);
  for(unsigned n=1;n<=unsigned(story_scenes::StopReason::TimedOut);++n){
   const auto bit=1u<<n;if(!(pending&bit))continue;
   try{endpoint.queue_stop(story_scenes::StopReason(n));pending&=~bit;}
   catch(...){pending_stops.fetch_or(pending,std::memory_order_release);throw;}
  }
 }
};
RunOwnerCell::RunOwnerCell(std::shared_ptr<const Activation>a,std::shared_ptr<const void>life,Endpoint e):state_(std::make_shared<State>()){
 if(!a||!life||!e.poll||!e.queue_stop)throw std::invalid_argument("run owner requires activation and endpoint");
 state_->activation=std::move(a);state_->original_run_life=std::move(life);state_->endpoint=std::move(e);
}
RunOwnerCell::~RunOwnerCell(){state_->alive=false;}
const std::shared_ptr<const Activation>&RunOwnerCell::activation()const noexcept{return state_->activation;}
RunOwnerCell::ResumeCertificate::ResumeCertificate(RunOwnerCell&cell,std::shared_ptr<const Activation>a,std::shared_ptr<const void>life,Endpoint e):owner_(cell.state_),activation_(std::move(a)),original_run_life_(std::move(life)),version_(cell.state_->version),endpoint_(std::move(e)){}
RunOwnerCell::PreparedResume::PreparedResume(std::shared_ptr<State>s,Endpoint e):owner_(std::move(s)),endpoint_(std::move(e)),thread_(std::this_thread::get_id()){}
void RunOwnerCell::PreparedResume::release()noexcept{
 if(!owner_)return;if(thread_!=std::this_thread::get_id())std::terminate();
 owner_->busy.store(false,std::memory_order_release);owner_.reset();
}
RunOwnerCell::PreparedResume::~PreparedResume(){release();}
RunOwnerCell::PreparedResume::PreparedResume(PreparedResume&&other)noexcept{
 if(other.owner_&&other.thread_!=std::this_thread::get_id())std::terminate();
 owner_=std::move(other.owner_);endpoint_=std::move(other.endpoint_);thread_=other.thread_;committed_=other.committed_;
}
RunOwnerCell::PreparedResume&RunOwnerCell::PreparedResume::operator=(PreparedResume&&other)noexcept{
 if(this==&other)return *this;if(other.owner_&&other.thread_!=std::this_thread::get_id())std::terminate();
 release();owner_=std::move(other.owner_);endpoint_=std::move(other.endpoint_);thread_=other.thread_;committed_=other.committed_;return *this;
}
void RunOwnerCell::PreparedResume::commit_unchecked()noexcept{
 if(!owner_||committed_||thread_!=std::this_thread::get_id())std::terminate();
 owner_->endpoint.poll.swap(endpoint_.poll);owner_->endpoint.queue_stop.swap(endpoint_.queue_stop);++owner_->version;
 if(owner_->frontend_generation){
  owner_->notice_generation=*owner_->frontend_generation;owner_->notice_target_generation=owner_->requested_load_generation.load(std::memory_order_acquire);owner_->notice_version=owner_->version;owner_->notice_pending=true;
  // Only this authentic final transaction reconciles the old load-induced
  // stop. Every ordinary stop remains pending for the replacement endpoint.
  owner_->pending_stops.fetch_and(~(1u<<unsigned(story_scenes::StopReason::StateLoaded)),std::memory_order_acq_rel);
 }
 committed_=true;
}
std::optional<RunOwnerCell::PreparedResume>RunOwnerCell::prepare_resume(ResumeCertificate&&certificate)noexcept{
 if(certificate.consumed_)return {};certificate.consumed_=true;
 auto&s=*state_;
 if(s.thread!=std::this_thread::get_id()||s.busy.exchange(true,std::memory_order_acq_rel))return {};
 if(!s.alive.load(std::memory_order_acquire)||certificate.owner_!=state_||
    certificate.activation_!=s.activation||certificate.original_run_life_!=s.original_run_life||certificate.version_!=s.version||
    s.version==std::numeric_limits<uint64_t>::max()||!certificate.endpoint_.poll||!certificate.endpoint_.queue_stop){s.busy.store(false,std::memory_order_release);return {};}
 return PreparedResume(state_,std::move(certificate.endpoint_));
}
bool RunOwnerCell::commit(ResumeCertificate&&certificate)noexcept{
 auto prepared=prepare_resume(std::move(certificate));if(!prepared)return false;prepared->commit_unchecked();return true;
}
std::optional<uint64_t>RunOwnerCell::frontend_binding(const Activation&a)noexcept{
 auto&s=*state_;if(s.thread!=std::this_thread::get_id()||s.busy.exchange(true,std::memory_order_acq_rel))return {};
 State::Release release{s.busy};if(!s.alive||s.activation.get()!=&a||!s.frontend_generation||s.notice_pending)return {};return s.frontend_generation;
}
std::shared_ptr<const void>RunOwnerCell::checkpoint_life(const Activation&a,uint64_t generation)noexcept{
 auto&s=*state_;if(s.thread!=std::this_thread::get_id()||s.busy.exchange(true,std::memory_order_acq_rel))return {};
 State::Release release{s.busy};if(!s.alive||s.activation.get()!=&a||!s.frontend_generation||*s.frontend_generation!=generation||s.notice_pending||s.pending_stops.load(std::memory_order_acquire))return {};
 return s.original_run_life;
}
bool RunOwnerCell::bind_frontend(const Activation&a,uint64_t generation)noexcept{
 auto&s=*state_;if(s.thread!=std::this_thread::get_id()||s.busy.exchange(true,std::memory_order_acq_rel))return false;
 State::Release release{s.busy};if(!s.alive||s.activation.get()!=&a||s.frontend_generation)return false;
 s.frontend_generation=generation;return true;
}
void RunOwnerCell::record_load(const Activation&a,uint64_t prior,uint64_t generation)noexcept{
 auto&s=*state_;if(s.activation.get()==&a&&generation>prior)s.requested_load_generation.store(generation,std::memory_order_release);
}
bool RunOwnerCell::consume_resume(const Activation&a,uint64_t prior,uint64_t generation)noexcept{
 auto&s=*state_;if(s.thread!=std::this_thread::get_id()||s.busy.exchange(true,std::memory_order_acq_rel))return false;
 State::Release release{s.busy};
 if(!s.alive||s.activation.get()!=&a||!s.original_run_life||!s.frontend_generation||*s.frontend_generation!=prior||
    !s.notice_pending||s.notice_generation!=prior||s.notice_version!=s.version||s.notice_target_generation!=generation||s.requested_load_generation.load(std::memory_order_acquire)!=generation||generation<=prior)return false;
 s.notice_pending=false;s.frontend_generation=generation;s.requested_load_generation.store(0,std::memory_order_release);return true;
}
Progress RunOwnerCell::poll(Cpu*cpu){
 auto&s=*state_;if(!cpu||s.thread!=std::this_thread::get_id()||s.busy.exchange(true,std::memory_order_acq_rel))throw std::logic_error("run owner unavailable");
 State::Release release{s.busy};if(!s.alive.load(std::memory_order_acquire))throw std::logic_error("run owner expired");
 s.drain();return s.endpoint.poll(cpu);
}
void RunOwnerCell::queue_stop(story_scenes::StopReason reason){
 const auto n=unsigned(reason);if(!n||n>unsigned(story_scenes::StopReason::TimedOut))return;
 auto&s=*state_;s.pending_stops.fetch_or(1u<<n,std::memory_order_release);
 // Host-only stop callbacks may arrive from abort/load outside the logic
 // thread. Never wait for a parked callback or drop its cancellation request.
 if(s.busy.exchange(true,std::memory_order_acq_rel))return;
 State::Release release{s.busy};if(!s.alive.load(std::memory_order_acquire))return;s.drain();
}
}
