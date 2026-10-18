// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <span>
#include <vector>
struct Cpu;
namespace story_native_event_invoke {
inline constexpr uint32_t kFrameBytes=0x40; //64 decimal, actual guest_call reservation
struct Range {uint32_t address=0,bytes=0;};
struct Config {
 Range stack;
 // Include image, stamp, serialized backups and journal allocations. Stack
 // authorization alone is insufficient; caller supplies all protected spans.
 std::vector<Range> protected_ranges;
};
using Read=std::function<bool(uint32_t,std::span<uint8_t>)>;
using Write=std::function<bool(uint32_t,std::span<const uint8_t>)>;
using Call=std::function<uint32_t(Cpu*,uint32_t,std::initializer_list<uint32_t>)>;
enum class Reason {None,Configuration,Function,Arity,Stack,Read,Write,Call,Restore};
struct Result {bool ok=false,restored=false;uint32_t value=0;Reason reason=Reason::Configuration;};
// whitelist is fixed audited native code, mapped once internally. Call must
// implement guest_call's own SP-0x40 frame convention, not subtract it twice.
// No callbacks are retained from arguments, and no native ownership is granted.
class Bridge {
 Config config_;Read read_;Write write_;Call call_;
public:
 Bridge(Config,Read,Write,Call);
 Result invoke(Cpu*,uint32_t function,std::initializer_list<uint32_t> registers,std::initializer_list<uint32_t> stack);
};
}
