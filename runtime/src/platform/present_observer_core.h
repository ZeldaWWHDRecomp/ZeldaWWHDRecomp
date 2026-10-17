#pragma once

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <vector>

namespace present_observer::core {
struct Native {
  uint64_t sequence, drawable, acquired, actual, callback;
  double raw_seconds;
  bool visible;
  uint64_t epoch = 0, frame = 0;
  uint32_t present_id = 0;
  bool identity_valid = false;
};
// GOOGLE history publication does not order our independent native handler.
inline bool await_callbacks(std::condition_variable &ready,
                            std::unique_lock<std::mutex> &lock,
                            const std::vector<Native> &native,
                            size_t history_count,
                            std::chrono::milliseconds timeout) {
  return ready.wait_for(lock, timeout,
                        [&] { return native.size() >= history_count; });
}
enum class MatchStatus { Matched, Missing, Ambiguous, Invalid };
struct Match {
  MatchStatus status;
  size_t index;
};
inline uint64_t distance(uint64_t a, uint64_t b) {
  return a > b ? a - b : b - a;
}
inline Match match_native(const std::vector<Native> &values,
                          uint64_t google_actual, uint64_t tolerance = 2) {
  constexpr size_t none = std::numeric_limits<size_t>::max();
  if (!google_actual)
    return {MatchStatus::Invalid, none};
  size_t candidate = none;
  for (size_t i = 0; i < values.size(); ++i) {
    const auto &v = values[i];
    // A zero native timestamp never authenticates GOOGLE's fallback time.
    if (!v.actual || distance(v.actual, google_actual) > tolerance)
      continue;
    if (candidate != none)
      return {MatchStatus::Ambiguous, none};
    candidate = i;
  }
  if (candidate == none)
    return {MatchStatus::Missing, none};
  const auto &v = values[candidate];
  if (!v.visible || !std::isfinite(v.raw_seconds) || v.raw_seconds <= 0 ||
      v.acquired > v.actual || v.actual > v.callback)
    return {MatchStatus::Invalid, candidate};
  // Round unsigned nanoseconds without signed llround overflow. Use a strict
  // exclusive 2^64 bound because UINT64_MAX is not exactly representable.
  const long double ns =
      static_cast<long double>(v.raw_seconds) * 1000000000.0L;
  const long double rounded = std::floor(ns + 0.5L);
  if (!std::isfinite(rounded) || rounded < 1 ||
      rounded >= std::ldexp(1.0L, 64) ||
      distance(static_cast<uint64_t>(rounded), v.actual) > 2)
    return {MatchStatus::Invalid, candidate};
  return {MatchStatus::Matched, candidate};
}
// Timestamp equality is not identity: two distinct drawables can complete at
// the same genuine native time. Match the immutable acquisition scope first,
// then retain the existing native clock, visibility and rounding checks.
inline Match match_native_identity(const std::vector<Native>& values,
                                   uint64_t epoch, uint32_t id, uint64_t frame,
                                   uint64_t google_actual) {
  constexpr size_t none = std::numeric_limits<size_t>::max();
  if (!epoch || !id || !google_actual) return {MatchStatus::Invalid, none};
  size_t candidate = none;
  for (size_t i = 0; i < values.size(); ++i) {
    const auto& v = values[i];
    if (!v.identity_valid || v.epoch != epoch || v.present_id != id || v.frame != frame) continue;
    if (candidate != none) return {MatchStatus::Ambiguous, none};
    candidate = i;
  }
  if (candidate == none) return {MatchStatus::Missing, none};
  const auto& v = values[candidate];
  if (!v.actual || distance(v.actual, google_actual) > 2 || !v.visible ||
      !std::isfinite(v.raw_seconds) || v.raw_seconds <= 0 ||
      v.acquired > v.actual || v.actual > v.callback)
    return {MatchStatus::Invalid, candidate};
  const long double rounded = std::floor(static_cast<long double>(v.raw_seconds) * 1000000000.0L + 0.5L);
  if (!std::isfinite(rounded) || rounded < 1 || rounded >= std::ldexp(1.0L,64) ||
      distance(static_cast<uint64_t>(rounded),v.actual) > 2)
    return {MatchStatus::Invalid, candidate};
  return {MatchStatus::Matched, candidate};
}
class Anchor {
  bool set_ = false;
  uint64_t frame_ = 0, time_ = 0;

public:
  // First genuine joined post-origin presentation fixes phase forever.
  // Caller enforces scenario-origin exclusion; this policy never reanchors.
  bool observe(uint64_t frame, uint64_t actual) {
    if (!actual)
      return false;
    if (!set_) {
      frame_ = frame;
      time_ = actual;
      set_ = true;
      return true;
    }
    return frame >= frame_;
  }
  bool target(uint64_t frame, uint64_t period, uint64_t &out) const {
    if (!set_ || !period || frame < frame_)
      return false;
    const uint64_t delta = frame - frame_;
    const uint64_t max = std::numeric_limits<uint64_t>::max();
    if (delta > (max - time_) / period)
      return false;
    out = time_ + delta * period;
    return true;
  }
  bool set() const { return set_; }
  uint64_t frame() const { return frame_; }
  uint64_t time() const { return time_; }
};
} // namespace present_observer::core
