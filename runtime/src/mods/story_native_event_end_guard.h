// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
namespace story_native_event_end_guard {
struct Identity {uint32_t manager{},image{},bytes{},generation{};uint64_t program{};int32_t event_index{};uint32_t stage_pid{},room_pid{};bool operator==(const Identity&)const=default;};
struct Observation {
 Identity identity{}; uint32_t table{},event{},mode{},partner1{},partner2{},talk_partner{},item_partner{};
 int32_t control_event_id{-1}; bool retained_claim{},identity_valid{},foreign_takeover{};
};
enum class Decision {Delegate,Queue,Quarantine};
struct Intent {Identity identity{};bool pending{},force_seen{},natural_terminal{};};
// Pure observations only: producer must validate native allocation and stage/room.
class Guard {
 Identity expected_;Intent intent_{};bool quarantined_{};
 bool exact(const Observation&)const;
 public:
 explicit Guard(Identity expected):expected_(expected){}
 Decision before_control(const Observation&);
 Decision end_proc(const Observation&,bool force,bool native_terminal);
 const Intent& intent()const{return intent_;}
 bool quarantined()const{return quarantined_;}
 // No completion inference: consumers still require independent barrier evidence.
 bool take(const Identity&,bool depths_idle, bool active_coverage,Intent&);
};
}
