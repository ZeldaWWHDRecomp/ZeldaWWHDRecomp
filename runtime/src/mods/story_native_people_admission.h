// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_people_events.h"
#include <memory>
#include <string>
namespace story_native_people_admission {
using namespace story_native_event_observation;
struct Reference {std::string mod,id;};
struct Sources {
 story_native_people_events::Read read;
 story_native_people_events::Write write;
 story_native_people_events::Invoke invoke;
 // Actual exclusive native-only journal/program generation, independently
 // authenticated against current immutable scene + actor configuration.
 story_native_people_events::VerifyLease lease;
 // Exact persistent observer/TLS outer actor scope, not a positive global count.
 story_native_people_events::VerifyActor current_scope;
 // Real shared entry fence, all depths zero, stable lease generation/serial.
 story_native_people_events::VerifyLease detach_boundary;
};
// WAIT-only production-facing composition. Uses the real story_actors pure
// resident accessor; no SearchByID or name/profile-only ownership fallback.
// Native entry/lookup/detach must be explicitly called by a reviewed dispatcher;
// this object installs NO hooks and enables NO scene publication.
class Adapter {
 Expected expected_;std::array<Reference,2>references_;Sources sources_;
 std::unique_ptr<story_native_people_events::Router>router_;
 bool owned(const Actor&,const story_native_event_lease::Identity&)const;
 bool lease(const story_native_event_lease::Identity&)const;
 bool action(const story_native_people_events::Binding&,uint32_t&)const;
public:
 Adapter(Expected,std::array<Reference,2>,Sources);
 Adapter(const Adapter&)=delete;Adapter&operator=(const Adapter&)=delete;
 static constexpr bool publication_enabled(){return false;}
 bool configured()const;
 story_native_people_events::Dispatch event_move(Cpu*,uint32_t actor);
 bool staff_id(Cpu*); // exact getMyStaffId ABI through scoped Router
 bool detach_people(Cpu*); // no camera or image release authority
};
}
