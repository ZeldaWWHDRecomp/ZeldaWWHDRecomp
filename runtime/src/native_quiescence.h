// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "mods/story_native_event_game_thread.h"
#include <functional>
#include <optional>
#include <string>
#include <thread>
struct Cpu;
namespace threads {
class NativeQuiescenceIssuer;
// Borrowed proof of this exact successful legacy save-state freeze. It does
// not own/thaw it; revoked by thaw, refreeze, requester or thread-set change.
class QuiescenceWitness {
 std::function<bool(const Cpu*)> current_;std::thread::id thread_;
 explicit QuiescenceWitness(std::function<bool(const Cpu*)>);
 friend class NativeQuiescenceIssuer;
public:
 QuiescenceWitness()=default;
 bool current(const Cpu*)const noexcept;
};
std::optional<QuiescenceWitness> current_quiescence(Cpu*);
// Opaque physical all-other-guest-thread/service parking prerequisite. Only
// threads.cpp can issue it from its real registry, scheduler and freeze owner.
// Acquire only for bounded read-only/native windows with an independently
// verified closed dispatch graph (force1 or model refresh), never CameraRun
// or broad EndControl. Acquire BEFORE Stage::Scan or another mutex a guest
// could need; release those lifetime pins BEFORE releasing this capability.
// No native invocation, event ownership or mutation permission is granted.
// Negative admission only: true while this host thread retains any actual
// NativeQuiescence object, through its physical release callback.
bool current_thread_holds_native_quiescence() noexcept;
bool current_thread_holds_read_only_native_quiescence() noexcept;
class NativeQuiescence final {
 std::function<bool(const Cpu*)> current_;
 std::function<void()> release_;
 std::thread::id thread_;bool counted_=false,read_only_=false;
 NativeQuiescence(std::function<bool(const Cpu*)>,std::function<void()>,bool read_only=false);
 friend class NativeQuiescenceIssuer;
 void release()noexcept;
public:
 NativeQuiescence(const NativeQuiescence&)=delete;
 NativeQuiescence&operator=(const NativeQuiescence&)=delete;
 NativeQuiescence(NativeQuiescence&&)noexcept;
 NativeQuiescence&operator=(NativeQuiescence&&)noexcept;
 ~NativeQuiescence();
 bool current(const Cpu*)const noexcept;
 bool read_only()const noexcept{return read_only_;}
};
// Borrows an already successful legacy park; pins only the actual registry.
// Never acquires a core, freezes or thaws; cannot support native/write gates.
std::optional<NativeQuiescence> borrow_native_read_quiescence(Cpu*,story_native_event_game_thread::Ticket,const QuiescenceWitness&,std::string&why);
// Refuses foreign/unregistered/restored/unbound CPU, service executor, unsafe
// core ownership, existing freeze and timeouts. timeout_ms is bounded 1..250.
std::optional<NativeQuiescence> try_native_quiescence(Cpu*,story_native_event_game_thread::Ticket,int timeout_ms,std::string&why);
}
