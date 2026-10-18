// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_observation.h"
#include <functional>
namespace story_native_event_collect {
using namespace story_native_event_observation;
using Read=std::function<bool(uint32_t,std::span<uint8_t>)>;
// Pure loaded-session ownership verifier: prove this token/PID/address against
// snapshot-owned declaration records. No native calls or collector recursion.
using VerifyActor=std::function<bool(const Actor&,const story_native_event_lease::Identity&)>;
// Native camera type/style table policy, supplied by the scoped camera guard.
// Exact transition matching alone is not authority for arbitrary table indices.
using VerifyCamera=std::function<bool(const Camera&,const story_native_event_lease::Identity&)>;
using VerifyAllocation=std::function<bool(uint32_t image,uint32_t bytes,uint32_t stamp,uint32_t generation)>;
struct Guards {
 bool exclusive=false,foreign=false;
 uint32_t manager_depth=0,camera_depth=0,npc_depth=0;
};
using GuardSnapshot=std::function<bool(Guards&)>;
struct Clock {
 story_native_event_lease::Identity identity;
 uint64_t logic_step=0,serial=0;
 uint32_t elapsed_ticks=0;
 bool operator==(const Clock&)const=default;
};
// Mandatory TURN host source: independently authenticate the retained lease and
// current idle boundary; guest camera timers cannot implement this callback.
using HostClock=std::function<bool(Clock&)>;
struct Config {
 uint32_t stamp=0;
 std::array<uint8_t,12> manager_exception{},manager_goal{};
 HostClock clock;
};
enum class Error {None,Configuration,Busy,Ownership,Span,Read,Stamp,Stage,Link,Actor,Camera,Manager,Clock};
// Pure original SDK record serialization. Caller owns initialization and snapshot
// allocation; collection is read-only and never repairs a corrupt stamp.
std::array<uint8_t,64> make_stamp(const Expected&);
class Collector {
 Expected expected_;Config config_;Read read_;VerifyActor actor_;VerifyAllocation allocation_;VerifyCamera camera_;GuardSnapshot guards_;
 std::array<uint8_t,story_native_turn_camera_image::kImageBytes> image_{};
 std::optional<Clock> last_clock_;
 bool busy_=false;Error error_=Error::None;
 bool bytes(uint32_t,std::span<uint8_t>,bool runtime=false);
public:
 Collector(Expected,Config,Read,VerifyActor,VerifyAllocation,VerifyCamera,GuardSnapshot);
 bool collect(Captured&); // image span owned by this collector until next call
 Error error()const{return error_;}
};
}
