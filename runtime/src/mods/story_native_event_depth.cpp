// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_depth.h"
#include "guest_addr.h"
#include <algorithm>
#include <exception>
#include <limits>
#include <utility>
namespace story_native_event_depth {
namespace detail {
struct OwnerLife {mutable std::mutex mutex;Tracker*tracker=nullptr;Expected expected;};
struct FrameLife {
 std::shared_ptr<OwnerLife>owner;Call call;uint64_t serial=0;
 std::thread::id thread;uint64_t return_epoch=0;std::atomic<bool>token_issued{false};std::atomic<unsigned>state{0}; // active, returned, faulted
};
}
namespace {
struct Frame{Tracker*tracker;Call call;uint64_t serial;std::shared_ptr<detail::FrameLife>life;};
thread_local std::array<Frame,32>stack{};thread_local unsigned size=0;thread_local uint64_t entry_epoch=0;
bool same(const story_native_event_lease::Identity&a,const story_native_event_lease::Identity&b){return a.manager==b.manager&&a.image==b.image&&a.bytes==b.bytes&&a.stage==b.stage&&a.room==b.room&&a.generation==b.generation&&a.program==b.program;}
bool outer(Entry e){return e==Entry::PeopleExecute||e==Entry::CameraExecute||e==Entry::ManagerRun;}
unsigned family(Entry e){if(e==Entry::CameraExecute||e==Entry::CameraEvent)return 1;if(e==Entry::ManagerRun||e==Entry::ManagerMain||e==Entry::Timekeeper||e==Entry::StaffLookup||e==Entry::CutEnd)return 2;return 0;}
bool parent(Entry child,Entry p){switch(child){
 case Entry::PeopleEventMove:return p==Entry::PeopleExecute;
 case Entry::CommonCut:return p==Entry::PeopleEventMove;
 case Entry::CameraEvent:return p==Entry::CameraExecute;
 case Entry::ManagerMain:return p==Entry::ManagerRun;
 case Entry::Timekeeper:return p==Entry::ManagerMain;
 case Entry::StaffLookup:return p==Entry::CommonCut||p==Entry::CameraEvent||p==Entry::Timekeeper;
 case Entry::CutEnd:return p==Entry::TurnProcess||p==Entry::WaitProcess||p==Entry::MoveProcess||p==Entry::CameraEvent||p==Entry::Timekeeper;
 case Entry::TurnStart:case Entry::TurnProcess:case Entry::WaitStart:case Entry::WaitProcess:case Entry::MoveStart:case Entry::MoveProcess:return p==Entry::CommonCut;
 default:return false;
}}
uint32_t word(const Record&b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
void put(Record&b,size_t p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
uint32_t checksum(const Record&b){uint32_t h=2166136261u;for(unsigned n=0;n<92;++n)h=(h^b[n])*16777619u;return h;}
}
uint32_t entry_address(Entry e){switch(e){
 case Entry::PeopleExecute:return GC(0x022C36D0);case Entry::CameraExecute:return GC(0x024FFA3C);case Entry::ManagerRun:return GC(0x02544374);
 case Entry::PeopleEventMove:return GC(0x022C2958);case Entry::CommonCut:return GC(0x0259F858);case Entry::CameraEvent:return GC(0x024FF164);case Entry::ManagerMain:return GC(0x02543D80);case Entry::Timekeeper:return GC(0x02541664);
 case Entry::StaffLookup:return GC(0x02542D88);case Entry::CutEnd:return GC(0x02543280);case Entry::TurnStart:return GC(0x025A0984);case Entry::TurnProcess:return GC(0x025A0B3C);case Entry::WaitStart:return GC(0x0259FA00);case Entry::WaitProcess:return GC(0x0259FA64);case Entry::MoveStart:return GC(0x025A0D5C);case Entry::MoveProcess:return GC(0x025A0F00);
 }return 0;
}
bool complete(Coverage c){const auto ok=[](HookObserver h){return h.before_and_after&&h.current_owner==h.delegated_owner;};return c.inner_callbacks&&ok(c.people)&&ok(c.camera)&&ok(c.manager);}
Tracker::Tracker(Expected e,Coverage c):expected_(std::move(e)),coverage_(c){const auto&i=expected_.identity;ready_=complete(c)&&i.manager&&i.image&&i.bytes&&i.stage&&i.room&&i.generation&&i.program&&expected_.room_number<64&&expected_.camera.address>=0x248&&expected_.camera.pid;for(const auto&a:expected_.actors)ready_&=a.address&&a.pid&&a.pid!=0xFFFFFFFFu&&a.token&&a.profile==0x175&&(a.subtype==1||a.subtype==9);ready_&=expected_.actors[0].address!=expected_.actors[1].address&&expected_.actors[0].pid!=expected_.actors[1].pid&&expected_.actors[0].token!=expected_.actors[1].token;lifetime_=std::make_shared<detail::OwnerLife>();lifetime_->tracker=this;lifetime_->expected=expected_;}
Tracker::~Tracker(){std::lock_guard lock(lifetime_->mutex);lifetime_->tracker=nullptr;}
bool Tracker::matches(const Call&c)const{
 if(!same(c.identity,expected_.identity))return false;
 const auto&i=expected_.identity;
 if(c.entry==Entry::ManagerRun||c.entry==Entry::ManagerMain||c.entry==Entry::Timekeeper){return c.subject==i.manager&&!c.pid&&!c.token&&c.receiver==(c.entry==Entry::Timekeeper?i.image+0x1E0:i.manager);}
 if(c.entry==Entry::StaffLookup||c.entry==Entry::CutEnd)return c.receiver==i.manager;
 if(c.entry==Entry::CameraExecute||c.entry==Entry::CameraEvent)return c.subject==expected_.camera.address-0x248&&c.pid==expected_.camera.pid&&!c.token&&c.receiver==(c.entry==Entry::CameraExecute?c.subject:expected_.camera.address);
 for(const auto&a:expected_.actors)if(c.subject==a.address&&c.pid==a.pid&&c.token==a.token){const bool cut=c.entry==Entry::CommonCut||c.entry==Entry::TurnStart||c.entry==Entry::TurnProcess||c.entry==Entry::WaitStart||c.entry==Entry::WaitProcess||c.entry==Entry::MoveStart||c.entry==Entry::MoveProcess;return c.receiver==a.address+(cut?0x3E0:0);}
 return false;
}
bool Tracker::enter(const Call&c,uint64_t&serial){std::lock_guard lock(mutex_);
 last_returned_.reset();if(entry_epoch==std::numeric_limits<uint64_t>::max()){fault_=true;return false;}++entry_epoch;
 if(state_==State::Draining){if(c.authority!=Authority::Owned)fault_=true;return false;}
 if(!c.function||c.function!=entry_address(c.entry)||c.authority==Authority::InvalidOwned||size==stack.size()||serial_==std::numeric_limits<uint64_t>::max()){fault_=true;return false;}
 if(c.authority==Authority::Owned){
  if(!ready_||fault_||!matches(c)){fault_=true;return false;}
  if(outer(c.entry)){if(size){fault_=true;return false;}}
  else {if(!size){fault_=true;return false;}const auto&p=stack[size-1];if(p.tracker!=this||p.call.authority!=Authority::Owned||!parent(c.entry,p.call.entry)||p.call.subject!=c.subject||p.call.pid!=c.pid||p.call.token!=c.token||!same(p.call.identity,c.identity)){fault_=true;return false;}}
 }
 std::shared_ptr<detail::FrameLife>life;try{life=std::make_shared<detail::FrameLife>();}catch(...){fault_=true;return false;}
 serial=++serial_;life->owner=lifetime_;life->call=c;life->serial=serial;life->thread=std::this_thread::get_id();stack[size++]={this,c,serial,std::move(life)};auto*f=family(c.entry)==0?&npc_:family(c.entry)==1?&camera_:&manager_;++*f;if(!outer(c.entry))++callbacks_;if(c.authority==Authority::Foreign)++foreign_;return true;
}
void Tracker::leave(uint64_t serial,bool exception,std::thread::id thread){std::lock_guard lock(mutex_);
 if(thread!=std::this_thread::get_id()){fault_=true;return;}
 unsigned n=size;for(unsigned k=0;k<size;++k)if(stack[k].tracker==this&&stack[k].serial==serial)n=k;
 if(n==size){fault_=true;return;}const auto frame=stack[n];
 if(n+1!=size){frame.life->state=2;fault_=true;for(unsigned k=n+1;k<size;++k)stack[k-1]=stack[k];--size;stack[size].life.reset();return;}
 --size;stack[size].life.reset();frame.life->state=(exception||fault_)?2:1;if(!size&&!exception&&!fault_){frame.life->return_epoch=entry_epoch;last_returned_=frame.life;}auto*f=family(frame.call.entry)==0?&npc_:family(frame.call.entry)==1?&camera_:&manager_;if(!f->load()){fault_=true;return;}--*f;if(!outer(frame.call.entry))--callbacks_;if(frame.call.authority==Authority::Foreign)--foreign_;if(exception)fault_=true;
}
Scope::Scope(Tracker*t,const Call&c):exceptions_(std::uncaught_exceptions()),thread_(std::this_thread::get_id()){if(t->enter(c,serial_))tracker_=t;}
Scope::~Scope(){if(tracker_)tracker_->leave(serial_,std::uncaught_exceptions()>exceptions_,thread_);}
Snapshot Tracker::snapshot_locked()const{return {npc_.load(),camera_.load(),manager_.load(),callbacks_.load(),foreign_.load(),fault_,ready_,state_,serial_};}
Snapshot Tracker::snapshot()const{std::lock_guard lock(mutex_);return snapshot_locked();}
bool Tracker::ready()const{std::lock_guard lock(mutex_);return ready_&&!fault_&&state_==State::Active;}
bool Tracker::begin_drain(){std::lock_guard lock(mutex_);if(!ready_||state_!=State::Active||!snapshot_locked().idle())return false;state_=State::Draining;return true;}
bool Tracker::cancel_drain(){std::lock_guard lock(mutex_);if(restore_pending_||state_!=State::Draining||!snapshot_locked().idle())return false;state_=State::Active;return true;}
std::optional<Call> Tracker::current_owned_call_locked()const{
 if(!ready_||fault_||state_!=State::Active||!size)return {};
 for(unsigned n=0;n<size;++n){
  const auto&f=stack[n];const auto&c=f.call;
  if(f.tracker!=this||c.authority!=Authority::Owned||!f.serial||
     c.function!=entry_address(c.entry)||!matches(c))return {};
  if(!n){if(!outer(c.entry))return {};}
  else {const auto&p=stack[n-1].call;
   if(outer(c.entry)||!parent(c.entry,p.entry)||c.subject!=p.subject||
      c.pid!=p.pid||c.token!=p.token||!same(c.identity,p.identity))return {};
  }
 }
 return stack[size-1].call;
}
std::optional<Call> Tracker::current_owned_call()const{std::lock_guard lock(mutex_);return current_owned_call_locked();}
std::optional<FrameWitness> Tracker::current_owned_frame()const{
 std::lock_guard lock(mutex_);if(!current_owned_call_locked())return {};
 return FrameWitness(stack[size-1].life,stack[0].life);
}
bool Tracker::frame_current_locked(const FrameWitness&w)const{
 return current_owned_call_locked()&&w.current_&&w.root_&&stack[size-1].life==w.current_&&stack[0].life==w.root_&&w.current_->state==0&&w.root_->state==0;
}
bool Tracker::frame_returned_locked(const FrameWitness&w,bool draining)const{
 return !size&&ready_&&!fault_&&(state_==State::Active||(draining&&state_==State::Draining))&&snapshot_locked().idle()&&w.current_&&w.root_&&w.current_==w.root_&&w.root_->owner==lifetime_&&w.root_->thread==std::this_thread::get_id()&&w.root_->state==1&&w.root_->return_epoch==entry_epoch&&last_returned_.lock()==w.root_;
}
FrameWitness::FrameWitness(std::shared_ptr<detail::FrameLife>c,std::shared_ptr<detail::FrameLife>r):current_(std::move(c)),root_(std::move(r)){}
bool FrameWitness::valid()const{if(!current_||!root_)return false;std::lock_guard lock(root_->owner->mutex);return root_->owner->tracker&&current_->state!=2&&root_->state!=2;}
bool FrameWitness::current(const Tracker&t)const{
 if(!current_||!root_)return false;std::lock_guard life(root_->owner->mutex);if(root_->owner->tracker!=&t)return false;std::lock_guard lock(t.mutex_);return t.frame_current_locked(*this);
}
bool FrameWitness::returned(const Tracker&t)const{
 if(!current_||!root_)return false;std::lock_guard life(root_->owner->mutex);if(root_->owner->tracker!=&t)return false;std::lock_guard lock(t.mutex_);return t.frame_returned_locked(*this,false);
}
bool FrameWitness::returned_now(bool draining)const{
 if(!current_||!root_)return false;std::lock_guard life(root_->owner->mutex);const Tracker*t=root_->owner->tracker;if(!t)return false;std::lock_guard lock(t->mutex_);return t->frame_returned_locked(*this,draining);
}
bool FrameWitness::claim_outer_return(const Tracker&t)const{
 if(!current_||!root_)return false;std::lock_guard life(root_->owner->mutex);if(root_->owner->tracker!=&t)return false;std::lock_guard lock(t.mutex_);if(!t.frame_returned_locked(*this,false))return false;bool unused=false;return root_->token_issued.compare_exchange_strong(unused,true);
}
uint64_t FrameWitness::current_serial()const{return current_?current_->serial:0;}
uint64_t FrameWitness::root_serial()const{return root_?root_->serial:0;}
const Call& FrameWitness::root_call()const{static const Call empty;return root_?root_->call:empty;}
const Expected& FrameWitness::expected()const{static const Expected empty;return root_?root_->owner->expected:empty;}
bool Tracker::current_actor_scope(const Actor&a,const story_native_event_lease::Identity&i)const{
 // Both observations are on this thread's immutable TLS chain. The mutex in
 // current_owned_call rejects any concurrent sticky fault or draining state.
 const auto current=current_owned_call();
 if(!current||!same(i,expected_.identity)||!size)return false;
 const auto&root=stack[0].call;
 if(root.entry!=Entry::PeopleExecute||root.subject!=a.address||
    root.pid!=a.pid||root.token!=a.token||!same(root.identity,i))return false;
 for(const auto&want:expected_.actors)
  if(a.address==want.address&&a.pid==want.pid&&a.token==want.token&&
     a.profile==want.profile&&a.subtype==want.subtype&&a.fingerprint==want.fingerprint)return true;
 return false;
}
Record Tracker::record()const{std::lock_guard lock(mutex_);Record b{};const auto&i=expected_.identity;const auto s=snapshot_locked();const std::array<uint32_t,20>v={0x4E455044,1,i.manager,i.image,i.bytes,i.stage,i.room,i.generation,uint32_t(i.program>>32),uint32_t(i.program),s.npc_depth,s.camera_depth,s.manager_depth,s.callback_depth,s.foreign_depth,uint32_t(s.fault),uint32_t(s.state),uint32_t(s.serial>>32),uint32_t(s.serial),uint32_t(s.coverage)};for(unsigned n=0;n<v.size();++n)put(b,n*4,v[n]);put(b,92,checksum(b));return b;}
bool Tracker::valid_idle_record(const Record&b,const Expected&e){const auto&i=e.identity;const std::array<uint32_t,10>v={0x4E455044,1,i.manager,i.image,i.bytes,i.stage,i.room,i.generation,uint32_t(i.program>>32),uint32_t(i.program)};for(unsigned n=0;n<v.size();++n)if(word(b,n*4)!=v[n])return false;for(unsigned p=40;p<=60;p+=4)if(word(b,p))return false;return word(b,64)==uint32_t(State::Active)&&word(b,76)==1&&!(word(b,68)==0xFFFFFFFFu&&word(b,72)==0xFFFFFFFFu)&&std::all_of(b.begin()+80,b.begin()+92,[](auto x){return x==0;})&&word(b,92)==checksum(b);}
}
