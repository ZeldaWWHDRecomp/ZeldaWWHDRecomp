// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_lease.h"
#include "story_native_event_observation.h"
#include "story_native_cancel_observation.h"
namespace story_native_event_lease { namespace {
bool valid(const Identity&i,uint32_t extent=0x520){return i.manager&&!(i.manager&3)&&i.image&&!(i.image&15)&&i.bytes==extent&&uint64_t(i.image)+i.bytes<=0xffffffffull&&i.stage&&i.room&&i.generation&&i.program;}
bool same(const Identity&a,const Identity&b){return a.manager==b.manager&&a.image==b.image&&a.bytes==b.bytes&&a.stage==b.stage&&a.room==b.room&&a.generation==b.generation&&a.program==b.program;}
}
bool Lease::prepare(Identity i){if(phase_!=Phase::Empty||!valid(i))return false;identity_=i;phase_=Phase::Prepared;return true;}
bool Lease::prepare(const story_native_event_observation::Result&r){
 if(phase_!=Phase::Empty||!r.valid()||r.operation()!=story_native_event_observation::Operation::Prepare)return false;
 const auto&i=r.identity();const auto*p=r.program();
 if(!valid(i,i.bytes)||(i.bytes!=0x520&&!(i.bytes==0xE60&&p&&p->kind()==story_native_event_program::Kind::TurnCamera&&p->bytes()==i.bytes)))return false;
 prepared_=std::make_shared<const story_native_event_observation::Result>(r);identity_=i;phase_=Phase::Prepared;return true;
}
bool Lease::prepare_restore(const story_native_event_observation::Result&r){
 if(phase_!=Phase::Empty||!r.restorable())return false;
 const auto&i=r.identity();const auto*p=r.program();
 if(!valid(i,i.bytes)||(i.bytes!=0x520&&!(i.bytes==0xE60&&p&&p->kind()==story_native_event_program::Kind::TurnCamera&&p->bytes()==i.bytes)))return false;
 prepared_=std::make_shared<const story_native_event_observation::Result>(r);identity_=i;phase_=Phase::Prepared;return true;
}
bool Lease::publish(const Observation&o){if(phase_!=Phase::Prepared)return false;if(o.foreign||!same(identity_,o.identity)){invalidate();return false;}phase_=Phase::Published;return true;}
bool Lease::started(const Observation&o){if(phase_!=Phase::Published)return false;if(o.foreign||!same(identity_,o.identity)){invalidate();return false;}phase_=Phase::Running;return true;}
bool Lease::finish(const Observation&o){if(phase_!=Phase::Running&&phase_!=Phase::Published)return false;if(o.foreign||!same(identity_,o.identity)){invalidate();return false;}phase_=Phase::Draining;return true;}
bool Lease::retire(const Observation&o){if(phase_!=Phase::Draining)return false;if(o.foreign||!same(identity_,o.identity)){invalidate();return false;}if(!o.consumers_detached)return false;phase_=Phase::Retired;return true;}
bool Lease::restore(Identity captured,Phase p,const Observation&o){
 // Restored retired/quarantined records never grant a new active lease.
 if(!valid(captured,prepared_?prepared_->identity().bytes:0x520)||!same(identity_,captured)||!same(captured,o.identity)||o.foreign||p<Phase::Prepared||p>Phase::Draining){invalidate();return false;}
 phase_=p;return true;
}
bool Lease::can_restore_retail(const story_native_event_observation::Result&r)const{return can_free(r);}
bool Lease::can_free(const story_native_event_observation::Result&r)const{return phase_==Phase::Retired&&r.released()&&same(identity_,r.identity())&&(!prepared_||prepared_->same_definition(r));}
bool Lease::can_free(const story_native_cancel_observation::Result&r)const{return phase_==Phase::Retired&&prepared_&&r.released()&&same(identity_,r.identity())&&r.same_definition(*prepared_);}
void Lease::invalidate(){phase_=Phase::Quarantined;}
}
