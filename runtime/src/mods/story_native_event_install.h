// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_observation.h"
#include "story_native_camera_return.h"
#include "story_native_manager_terminal.h"
#include "story_native_cancel_observation.h"
#include <functional>
#include <initializer_list>
#include <span>
struct Cpu;
namespace story_native_event_install {
using namespace story_native_event_observation;
// Native bridge must marshal stack words at ABI outgoing+8 and preserve its
// caller. The adapter additionally saves Cpu around the whole operation.
using Invoke=std::function<uint32_t(Cpu*,uint32_t,std::initializer_list<uint32_t>,std::initializer_list<uint32_t>)>;
using Read=std::function<bool(uint32_t,std::span<uint8_t>)>;
using Write=std::function<bool(uint32_t,std::span<const uint8_t>)>;
using Capture=std::function<bool(Cpu*,Captured&)>;
// These mandatory callbacks are NOT installed by this isolated helper.
// Scope must verify exclusive generation + image allocation + scoped staff
// search/camera/end guards. Detach must restore audited owned consumer backups
// only under exact PID/token/generation; failure retains the resident image.
using Scope=std::function<bool(const Expected&)>;
using Detach=std::function<bool(Cpu*,const Expected&)>;
struct Backend {Invoke invoke;Read read;Write write;Capture capture;Scope scope;Detach detach;};
enum class Status {Empty,Started,Running,Draining,Released,Refused,RolledBack,Quarantined};
class Install {
 Backend backend_;Expected expected_;story_native_event_lease::Lease lease_;
 Status status_=Status::Empty;
 std::optional<story_native_cancel_observation::Result>cancel_closed_,cancel_detached_;
 bool capture(Cpu*,Captured&,bool idle);
 bool rollback(Cpu*,const std::array<uint8_t,0xF4>&,bool bound) noexcept;
 bool restore_manager();
 bool write_word(uint32_t,uint32_t);
 uint32_t control()const{return expected_.identity.manager-0xF4;}
public:
 Install(Backend,Expected);
 Status status()const{return status_;}
 // Requires an already installed, read-back canonical image and an external
 // persisted Preparing record/entry fence. Never initializes image memory.
 Status start(Cpu*);
 // Pure pre-thaw reconstruction only. Caller first decodes snapshot-owned
 // Expected+phase, then obtains fresh Restore certificate after actor rebind.
 // No native calls/writes/replay. Prepublication/busy checkpoints unsupported.
 Status rehydrate(const Result&,story_native_event_lease::Phase);
 Status observe(Cpu*); // never manually ticks native interpreters
 // Pre-resume cancellation only. Once native staff have run, use the terminal
 // path below; synchronous camera restoration does not prove native return.
 Status stop(Cpu*,bool require_completed=true);
 // Completed native scenes require the exact manager terminal operation and
 // real camera Run return barrier before any consumer restore/unpublication.
 Status stop_terminal(Cpu*,const story_native_manager_terminal::Terminal&,
                      const story_native_camera_return::Barrier&);
 // Distinct synchronous cancellation phases. The current genuine claimed
 // outer return and the same opaque cancellation chain are mandatory at each
 // phase; no normal Terminal result is projected from partial completion.
 Status begin_cancel(Cpu*,story_native_event_depth::Tracker&,const story_native_outer_return::OuterReturn&,const story_native_cancel_observation::Result&);
 Status release_cancel_native(Cpu*,story_native_event_depth::Tracker&,const story_native_outer_return::OuterReturn&,const story_native_cancel_observation::Result&);
 Status retire_cancel(Cpu*,story_native_event_depth::Tracker&,const story_native_outer_return::OuterReturn&,const story_native_cancel_observation::Result&);
 bool can_free(const story_native_cancel_observation::Result&r)const{return status_==Status::Released&&r.matches(expected_)&&lease_.can_free(r);}
 bool can_free(const Result&r)const{return status_==Status::Released&&r.matches(expected_)&&lease_.can_free(r);}
};
}
