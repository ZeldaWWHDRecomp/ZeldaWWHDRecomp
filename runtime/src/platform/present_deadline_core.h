#pragma once
#include <cstdint>
#include <limits>
#include <map>

namespace present_observer::deadline {
struct Interval {
  uint64_t low = 0, high = 0;
};
inline bool add(uint64_t a, uint64_t b, uint64_t &out) {
  if (a > std::numeric_limits<uint64_t>::max() - b)
    return false;
  out = a + b;
  return true;
}
inline bool multiply(uint64_t a, uint64_t b, uint64_t &out) {
  if (b && a > std::numeric_limits<uint64_t>::max() / b)
    return false;
  out = a * b;
  return true;
}
// Native-before / steady-now / native-after are sampled in that order. No
// assumption that the two clocks share an epoch, and no midpoint substitution.
inline bool
translate(int64_t due, int64_t steady_sample, uint64_t native_before,
          uint64_t native_after, Interval &out,
          uint64_t max_bracket_ns = std::numeric_limits<uint64_t>::max()) {
  if (due < 0 || steady_sample < 0 || !native_before ||
      native_after < native_before ||
      native_after - native_before > max_bracket_ns)
    return false;
  if (due >= steady_sample) {
    const auto delta = uint64_t(due) - uint64_t(steady_sample);
    return add(native_before, delta, out.low) &&
           add(native_after, delta, out.high);
  }
  const auto delta = uint64_t(steady_sample) - uint64_t(due);
  if (native_before < delta)
    return false;
  out = {native_before - delta, native_after - delta};
  return true;
}
enum class Status { OnTime, Late, Ambiguous, Invalid };
// The physical phase is independently authenticated, not the engine's target.
// The allowance is an explicitly declared sensitivity budget, never a claim
// that Vulkan queue depth guarantees a fixed presentation latency.
inline Status classify(uint64_t actual, Interval due, uint64_t physical_anchor,
                       uint32_t allowance_slots = 0,
                       uint64_t tolerance_ns = 1000000,
                       uint64_t physical_period_num = 1000000000,
                       uint64_t physical_period_den = 120) {
  if (!actual || !physical_anchor || due.low > due.high ||
      due.low < physical_anchor || !physical_period_num || !physical_period_den)
    return Status::Invalid;
  auto slot_limit = [&](uint64_t native_due, uint64_t &limit) {
    uint64_t numerator, slots, base, slack;
    if (!multiply(native_due - physical_anchor, physical_period_den, numerator))
      return false;
    slots = numerator / physical_period_num +
            (numerator % physical_period_num != 0);
    if (!add(slots, allowance_slots, slots) ||
        !multiply(slots, physical_period_num, numerator) ||
        !multiply(physical_anchor, physical_period_den, base) ||
        !add(base, numerator, base) ||
        !multiply(tolerance_ns, physical_period_den, slack) ||
        !add(base, slack, limit))
      return false;
    return true;
  };
  uint64_t low, high, value;
  if (!slot_limit(due.low, low) || !slot_limit(due.high, high) ||
      !multiply(actual, physical_period_den, value))
    return Status::Invalid;
  if (value <= low)
    return Status::OnTime;
  if (value > high)
    return Status::Late;
  return Status::Ambiguous;
}
// Explicit producer swap ID -> renderer frame. A frame is not inferred from
// mutable producer counters, dequeue timing, or native drawable acquisition.
class FrameJoin {
  std::map<uint64_t, uint64_t> swaps_;
  std::map<uint64_t, uint64_t> frames_;

public:
  bool insert(uint64_t swap, uint64_t frame) {
    if (!swap || swaps_.count(swap) || frames_.count(frame))
      return false;
    swaps_.emplace(swap, frame);
    frames_.emplace(frame, swap);
    return true;
  }
  bool find(uint64_t swap, uint64_t &frame) const {
    auto it = swaps_.find(swap);
    if (it == swaps_.end())
      return false;
    frame = it->second;
    return true;
  }
};
} // namespace present_observer::deadline
