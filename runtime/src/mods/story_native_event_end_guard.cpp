// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_end_guard.h"
namespace story_native_event_end_guard {
bool Guard::exact(const Observation&o)const{
 const uint64_t end=uint64_t(expected_.image)+expected_.bytes;
 const uint64_t event=uint64_t(expected_.image)+0x40+uint64_t(uint32_t(expected_.event_index))*176;
 return expected_.manager&&expected_.image&&expected_.generation&&expected_.program&&expected_.event_index>=0&&
 end<=0x100000000ULL&&event+176<=end&&o.identity==expected_&&o.identity_valid&&!o.foreign_takeover&&
 o.table==expected_.image+0x40&&o.event==event&&o.mode==3&&o.partner1==0xFFFFFFFFu&&o.partner2==0xFFFFFFFFu&&
 o.talk_partner==0xFFFFFFFFu&&o.item_partner==0xFFFFFFFFu&&o.control_event_id==-1;
}
Decision Guard::before_control(const Observation&o){
 if(!o.retained_claim)return Decision::Delegate;
 if(quarantined_||!exact(o)){quarantined_=true;return Decision::Quarantine;}
 return Decision::Queue; // permission for owned mode3 only, never an instruction to skip native control
}
Decision Guard::end_proc(const Observation&o,bool force,bool terminal){
 const auto d=before_control(o);if(d!=Decision::Queue)return d;
 if(intent_.pending&&!(intent_.identity==expected_)){quarantined_=true;return Decision::Quarantine;}
 intent_.identity=expected_;intent_.pending=true;intent_.force_seen|=force;
 intent_.natural_terminal|=terminal&&!force;return Decision::Queue;
}
bool Guard::take(const Identity&i,bool idle,bool covered,Intent&out){
 if(quarantined_||!intent_.pending||!(i==expected_)||!idle||!covered)return false;
 out=intent_;intent_={};return true;
}
}
