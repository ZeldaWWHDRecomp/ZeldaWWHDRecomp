// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <cstdint>
struct Cpu;
namespace threads {class NativeQuiescence;}
namespace story_native_event_depth {class Tracker;}
namespace story_native_manager_cancel_dispatch {class Guard;}
namespace guestmods {std::optional<story_native_manager_cancel_dispatch::Guard> acquire_native_manager_cancel_dispatch(story_native_event_depth::Tracker&,Cpu*,std::shared_ptr<const threads::NativeQuiescence>);}
namespace story_native_manager_cancel_dispatch {
std::span<const uint32_t> required_functions() noexcept;
// Dispatch/list-lifetime prerequisite only; no event, cancellation or release authority.
// Retains real all-other-thread quiescence; destroy on issuing thread after the
// bounded forced-close/readback and before genuine CameraRun original.
class Guard {
 std::function<bool()> verify_;
 explicit Guard(std::function<bool()>);
 friend std::optional<Guard> guestmods::acquire_native_manager_cancel_dispatch(story_native_event_depth::Tracker&,Cpu*,std::shared_ptr<const threads::NativeQuiescence>);
#ifdef WWHD_NATIVE_CANCEL_DISPATCH_TEST
 friend struct Fixture;
#endif
public:
 Guard(const Guard&)=delete;Guard&operator=(const Guard&)=delete;
 Guard(Guard&&)noexcept;Guard&operator=(Guard&&)noexcept;
 bool current()const noexcept;
};
}
