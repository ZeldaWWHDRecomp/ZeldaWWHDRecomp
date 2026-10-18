// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <functional>
#include <optional>
#include <span>
#include <cstdint>
namespace story_native_refresh_dispatch {class Guard;}
namespace guestmods {std::optional<story_native_refresh_dispatch::Guard> acquire_native_people_refresh_dispatch();}
namespace story_native_refresh_dispatch {
// Fixed audited code closure only. The list itself grants no authority.
std::span<const uint32_t> required_functions() noexcept;
class Guard {
 std::function<bool()> verify_;
 explicit Guard(std::function<bool()>);
 friend std::optional<Guard> guestmods::acquire_native_people_refresh_dispatch();
#ifdef WWHD_NATIVE_DISPATCH_GUARD_TEST
 friend struct Fixture;
#endif
public:
 Guard(const Guard&)=delete;Guard&operator=(const Guard&)=delete;
 Guard(Guard&&)noexcept;Guard&operator=(Guard&&)noexcept;
 bool current()const noexcept;
};
}
