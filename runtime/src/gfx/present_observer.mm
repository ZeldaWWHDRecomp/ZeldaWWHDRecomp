// Opt-in authored diagnostics only; no guest memory, input, global swizzle or
// driver changes.
#include "platform/present_observer.h"
#include "platform/present_deadline_core.h"
#include "platform/present_observer_core.h"
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/QuartzCore.h>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace present_observer {
namespace {
constexpr size_t Limit = 65536;
struct Present {
  uint64_t epoch, frame, enqueue;
  uint32_t id;
  bool visible;
};
struct History {
  uint64_t epoch, frame, desired, actual, earliest, margin;
  uint32_t id;
};
struct SwapFrame {
  uint64_t swap, frame;
};
struct ScheduledDue {
  uint64_t swap, tick, native_before, native_after;
  int64_t steady_due, steady_sample, shift_before, shift_after;
  uint32_t granule;
  bool shifted_clock, uncapped, translated;
  deadline::Interval native_due;
};
struct Topology {
  uint64_t epoch;
  uint32_t images, acquire_slots, submission_slots, present_mode;
  bool async;
};
struct State {
  std::mutex mutex;
  std::condition_variable ready;
  std::atomic<bool> visible{false};
  std::vector<core::Native> native;
  std::vector<Present> presents;
  std::vector<History> history;
  std::vector<std::string> errors;
  std::vector<SwapFrame> swap_frames;
  std::vector<ScheduledDue> scheduler_due;
  std::vector<Topology> topology;
  deadline::FrameJoin frame_join;
  bool stopped = false, has_origin = false;
  uint64_t origin_frame = 0, origin_step = 0, origin_ns = 0, dropped = 0;
  uint64_t period = 8333333;
  State() {
    native.reserve(Limit);
    presents.reserve(Limit);
    history.reserve(Limit);
    swap_frames.reserve(Limit);
    scheduler_due.reserve(Limit);
    if (const char *value = std::getenv("WWHD_PRESENT_OBSERVER_PERIOD_NS")) {
      const auto end = value + std::char_traits<char>::length(value);
      const auto parsed = std::from_chars(value, end, period);
      if (parsed.ec != std::errc{} || parsed.ptr != end || !period ||
          period > 1000000000)
        errors.emplace_back("invalid declared period");
    }
  }
};
std::shared_ptr<State> state() {
  static auto value = std::make_shared<State>();
  return value;
}
void error(State &s, const char *text) {
  if (s.errors.size() < 16)
    s.errors.emplace_back(text);
}
void quoted(FILE *file, const std::string &text) {
  std::fputc('"', file);
  for (unsigned char c : text) {
    if (c == '"' || c == '\\')
      std::fprintf(file, "\\%c", c);
    else if (c < 32)
      std::fprintf(file, "\\u%04x", unsigned(c));
    else
      std::fputc(c, file);
  }
  std::fputc('"', file);
}
using Key = std::pair<uint64_t, uint32_t>;
struct DrawableIdentity {
  uint64_t epoch = 0, frame = 0;
  uint32_t id = 0;
  unsigned acquisitions = 0;
  bool active = false, registered = false;
};
thread_local DrawableIdentity drawable_identity;
} // namespace
bool enabled() {
  static const bool value = [] {
    const char *p = std::getenv("WWHD_PRESENT_OBSERVER");
    return p && *p;
  }();
  return value;
}
uint64_t native_now_ns() {
  return uint64_t(std::llround(CACurrentMediaTime() * 1e9));
}
void fail(const char *text) {
  if (!enabled())
    return;
  auto s = state();
  std::lock_guard lock(s->mutex);
  if (!s->stopped)
    error(*s, text);
}
bool begin_drawable_identity(uint64_t epoch, uint32_t id, uint64_t frame) {
  if (!enabled()) return false;
  if (drawable_identity.active || !epoch || !id) {
    fail("nested or invalid drawable identity scope");
    return false;
  }
  drawable_identity = {epoch, frame, id, 0, true, false};
  return true;
}
void end_drawable_identity() {
  const auto identity = drawable_identity;
  drawable_identity = {};
  if (!identity.active || !identity.registered || identity.acquisitions != 1)
    fail("drawable identity scope missing, aborted, or not one-to-one");
}
void scenario_origin(uint64_t frame, uint64_t step) {
  if (!enabled())
    return;
  auto s = state();
  std::lock_guard lock(s->mutex);
  if (s->has_origin) {
    error(*s, "duplicate scenario origin");
    return;
  }
  s->has_origin = true;
  s->origin_frame = frame;
  s->origin_step = step;
  s->origin_ns = native_now_ns();
}
void render_swap(uint64_t swap, uint64_t frame) {
  if (!enabled())
    return;
  auto s = state();
  std::lock_guard lock(s->mutex);
  if (s->stopped)
    return;
  if (s->swap_frames.size() == Limit) {
    ++s->dropped;
    error(*s, "swap/frame capacity exceeded");
    return;
  }
  if (!s->frame_join.insert(swap, frame)) {
    error(*s, "duplicate/zero swap ID or duplicate renderer frame");
    return;
  }
  s->swap_frames.push_back({swap, frame});
}
void scheduler_due(uint64_t swap, int64_t steady_due_ns, uint64_t due_tick,
                   int64_t shift_before, int64_t shift_after, uint32_t granule,
                   bool shifted_clock, bool uncapped) {
  if (!enabled())
    return;
  ScheduledDue record{};
  record.swap = swap;
  record.tick = due_tick;
  record.steady_due = steady_due_ns;
  record.shift_before = shift_before;
  record.shift_after = shift_after;
  record.granule = granule;
  record.shifted_clock = shifted_clock;
  record.uncapped = uncapped;
  // Bracket an actual host-clock sample, not an assumed common clock epoch.
  record.native_before = native_now_ns();
  record.steady_sample =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();
  record.native_after = native_now_ns();
  record.translated =
      !shifted_clock && !uncapped &&
      deadline::translate(steady_due_ns, record.steady_sample,
                          record.native_before, record.native_after,
                          record.native_due);
  auto s = state();
  std::lock_guard lock(s->mutex);
  if (s->stopped)
    return;
  if (s->scheduler_due.size() == Limit) {
    ++s->dropped;
    error(*s, "scheduler deadline capacity exceeded");
    return;
  }
  // Invalid or wide deadline clocks remain raw/unclassifiable; they must not
  // discard independently valid CPU/RSS/frame observations.
  s->scheduler_due.push_back(record);
}
void presentation_topology(uint64_t epoch, uint32_t images,
                           uint32_t acquire_slots, uint32_t submission_slots,
                           uint32_t present_mode, bool async) {
  if (!enabled())
    return;
  auto s = state();
  std::lock_guard lock(s->mutex);
  if (s->stopped)
    return;
  if (s->topology.size() == 64) {
    ++s->dropped;
    error(*s, "topology capacity exceeded");
    return;
  }
  s->topology.push_back(
      {epoch, images, acquire_slots, submission_slots, present_mode, async});
}
void google_present(uint64_t epoch, uint32_t id, uint64_t frame) {
  if (!enabled())
    return;
  if (!drawable_identity.active || drawable_identity.epoch != epoch ||
      drawable_identity.id != id || drawable_identity.frame != frame ||
      drawable_identity.registered)
    fail("present request differs from drawable identity scope");
  drawable_identity.registered = true;
  auto s = state();
  std::lock_guard lock(s->mutex);
  if (s->stopped)
    return;
  if (s->presents.size() == Limit) {
    ++s->dropped;
    error(*s, "present capacity exceeded");
    return;
  }
  s->presents.push_back({epoch, frame, native_now_ns(), id, s->visible.load()});
}
void google_history(uint64_t epoch, uint32_t id, uint64_t frame,
                    uint64_t desired, uint64_t actual, uint64_t earliest,
                    uint64_t margin) {
  if (!enabled())
    return;
  auto s = state();
  std::lock_guard lock(s->mutex);
  if (s->stopped)
    return;
  if (s->history.size() == Limit) {
    ++s->dropped;
    error(*s, "history capacity exceeded");
    return;
  }
  s->history.push_back({epoch, frame, desired, actual, earliest, margin, id});
}
} // namespace present_observer

@interface WWHDObservedMetalLayer : CAMetalLayer {
@public
  std::shared_ptr<present_observer::State> observation;
  NSMutableArray *notificationTokens;
}
@end
@implementation WWHDObservedMetalLayer
- (void)dealloc {
  for (id token in notificationTokens)
    [[NSNotificationCenter defaultCenter] removeObserver:token];
  observation.reset();
}
- (id<CAMetalDrawable>)nextDrawable {
  // Capture before the driver call and by value in the callback. Never infer
  // identity from callback order, current render frame, or timestamp equality.
  const auto identity = present_observer::drawable_identity;
  id<CAMetalDrawable> drawable = [super nextDrawable];
  if (!drawable || !observation)
    return drawable;
  auto& current = present_observer::drawable_identity;
  const bool inScope = identity.active && current.active && identity.epoch == current.epoch &&
                       identity.id == current.id && identity.frame == current.frame;
  const bool identityValid = inScope && ++current.acquisitions == 1;
  if (!inScope)
    present_observer::fail("unscoped, off-thread, or changed TV drawable acquisition");
  else if (!identityValid)
    present_observer::fail("multiple TV drawables in one identity scope");
  auto session = observation;
  const uint64_t drawableID = drawable.drawableID;
  const uint64_t acquired = present_observer::native_now_ns();
  [drawable addPresentedHandler:^(id<MTLDrawable> value) {
    const double raw = value.presentedTime;
    const uint64_t actual =
        std::isfinite(raw) && raw >= 0 && raw < double(INT64_MAX) / 1e9
            ? uint64_t(std::llround(raw * 1e9))
            : 0;
    const uint64_t callback = present_observer::native_now_ns();
    std::lock_guard lock(session->mutex);
    if (session->stopped)
      return;
    if (session->native.size() == present_observer::Limit) {
      ++session->dropped;
      present_observer::error(*session, "native callback capacity exceeded");
      return;
    }
    session->native.push_back({session->native.size() + 1, drawableID, acquired,
                               actual, callback, raw, session->visible.load(),
                               identity.epoch, identity.frame, identity.id,
                               identityValid});
    session->ready.notify_all();
  }];
  return drawable;
}
@end

namespace present_observer {
CAMetalLayer *make_layer(NSWindow *window) {
  if (!enabled())
    return [CAMetalLayer layer];
  auto s = state();
  WWHDObservedMetalLayer *layer = [WWHDObservedMetalLayer layer];
  layer->observation = s;
  __weak NSWindow *weakWindow = window;
  const auto visible = ^{
    NSWindow *ownedWindow = weakWindow;
    s->visible.store(
        ownedWindow && ownedWindow.visible && !ownedWindow.miniaturized &&
        (ownedWindow.occlusionState & NSWindowOcclusionStateVisible));
  };
  visible();
  layer->notificationTokens = [NSMutableArray array];
  for (NSNotificationName name in @[
         NSWindowDidChangeOcclusionStateNotification,
         NSWindowDidMiniaturizeNotification,
         NSWindowDidDeminiaturizeNotification
       ]) {
    id token = [[NSNotificationCenter defaultCenter]
        addObserverForName:name
                    object:window
                     queue:nil
                usingBlock:^(NSNotification *) {
                  visible();
                }];
    [layer->notificationTokens addObject:token];
  }
  return layer;
}
void flush() {
  if (!enabled())
    return;
  auto s = state();
  std::vector<core::Native> native;
  std::vector<Present> presents;
  std::vector<History> histories;
  std::vector<std::string> errors;
  std::vector<SwapFrame> swap_frames;
  std::vector<ScheduledDue> scheduled_due;
  std::vector<Topology> topology;
  uint64_t origin_frame, origin_step, origin_ns, period, dropped;
  bool has_origin;
  {
    std::unique_lock lock(s->mutex);
    if (s->stopped)
      return;
    if (!core::await_callbacks(s->ready, lock, s->native, s->history.size(),
                               std::chrono::milliseconds(2000)))
      error(*s, "native callback readiness timeout");
    s->stopped = true;
    native.swap(s->native);
    presents.swap(s->presents);
    histories.swap(s->history);
    errors.swap(s->errors);
    swap_frames.swap(s->swap_frames);
    scheduled_due.swap(s->scheduler_due);
    topology.swap(s->topology);
    origin_frame = s->origin_frame;
    origin_step = s->origin_step;
    origin_ns = s->origin_ns;
    period = s->period;
    dropped = s->dropped;
    has_origin = s->has_origin;
  }
  // The raw records remain authoritative. Anchor only a genuine joined TV
  // present after scripted origin, outside the later accepted window; never
  // re-anchor.
  core::Anchor anchor;
  std::map<Key, const History *> by_id;
  for (const auto &h : histories)
    if (!by_id.emplace(Key{h.epoch, h.id}, &h).second)
      errors.emplace_back("duplicate history ID");
  std::map<Key, bool> requests;
  for (const auto &p : presents) {
    if (!requests.emplace(Key{p.epoch, p.id}, true).second)
      errors.emplace_back("duplicate present ID");
    auto h = by_id.find({p.epoch, p.id});
    if (h == by_id.end()) {
      errors.emplace_back("missing history ID");
      continue;
    }
    if (h->second->frame != p.frame || h->second->desired != 0) {
      errors.emplace_back("history metadata mismatch");
      continue;
    }
    if (!anchor.set() && has_origin && p.frame >= origin_frame &&
        p.enqueue >= origin_ns) {
      const auto match = core::match_native_identity(native, p.epoch, p.id, p.frame, h->second->actual);
      if (match.status == core::MatchStatus::Matched) {
        const auto &n = native[match.index];
        if (p.visible && n.actual >= p.enqueue && n.callback >= n.actual)
          anchor.observe(p.frame, n.actual);
      }
    }
  }
  for (const auto &h : histories)
    if (!requests.count({h.epoch, h.id}))
      errors.emplace_back("unknown history ID");
  if (!has_origin)
    errors.emplace_back("scenario origin missing");
  if (!anchor.set())
    errors.emplace_back("genuine post-origin anchor missing");
  if (dropped)
    errors.emplace_back("records dropped");
  const char *path = std::getenv("WWHD_PRESENT_OBSERVER");
  FILE *file = std::fopen(path, "wb");
  if (!file) {
    std::fprintf(stderr, "[present observer] write failed\n");
    return;
  }
  std::fprintf(
      file,
      "{\"native_identity_protocol\":\"scoped_epoch_present_frame_v1\","
      "\"clock\":\"CACurrentMediaTime\",\"screen\":\"TV\",\"period_ns\":%llu,"
      "\"tolerance_ns\":1000000,\"origin\":{\"render_frame\":%llu,\"logic_"
      "step\":%llu,\"native_ns\":%llu},\"anchor\":{\"render_frame\":%llu,"
      "\"native_ns\":%llu},\"dropped\":%llu,\"errors\":[",
      (unsigned long long)period, (unsigned long long)origin_frame,
      (unsigned long long)origin_step, (unsigned long long)origin_ns,
      (unsigned long long)anchor.frame(), (unsigned long long)anchor.time(),
      (unsigned long long)dropped);
  for (size_t i = 0; i < errors.size(); ++i) {
    if (i)
      std::fputc(',', file);
    quoted(file, errors[i]);
  }
  std::fputs("],\"presents\":[", file);
  for (size_t i = 0; i < presents.size(); ++i) {
    const auto &p = presents[i];
    if (i)
      std::fputc(',', file);
    std::fprintf(file,
                 "{\"epoch\":%llu,\"present_id\":%u,\"render_frame\":%llu,"
                 "\"enqueue_ns\":%llu,\"visible\":%s}",
                 (unsigned long long)p.epoch, p.id, (unsigned long long)p.frame,
                 (unsigned long long)p.enqueue, p.visible ? "true" : "false");
  }
  std::fputs("],\"google_history\":[", file);
  for (size_t i = 0; i < histories.size(); ++i) {
    const auto &h = histories[i];
    if (i)
      std::fputc(',', file);
    std::fprintf(
        file,
        "{\"epoch\":%llu,\"present_id\":%u,\"render_frame\":%llu,\"desired_"
        "ns\":%llu,\"actual_ns\":%llu,\"earliest_ns\":%llu,\"margin_ns\":%llu}",
        (unsigned long long)h.epoch, h.id, (unsigned long long)h.frame,
        (unsigned long long)h.desired, (unsigned long long)h.actual,
        (unsigned long long)h.earliest, (unsigned long long)h.margin);
  }
  std::fputs("],\"native_callbacks\":[", file);
  for (size_t i = 0; i < native.size(); ++i) {
    const auto &n = native[i];
    if (i)
      std::fputc(',', file);
    std::fprintf(file,
                 "{\"sequence\":%llu,\"drawable_id\":%llu,\"acquired_ns\":%llu,"
                 "\"presented_time_seconds\":",
                 (unsigned long long)n.sequence, (unsigned long long)n.drawable,
                 (unsigned long long)n.acquired);
    if (std::isfinite(n.raw_seconds))
      std::fprintf(file, "%.17g", n.raw_seconds);
    else
      std::fputs("null", file);
    std::fprintf(file,
                 ",\"actual_ns\":%llu,\"callback_ns\":%llu,\"visible\":%s,"
                 "\"epoch\":%llu,\"present_id\":%u,\"render_frame\":%llu,\"identity_valid\":%s}",
                 (unsigned long long)n.actual, (unsigned long long)n.callback,
                 n.visible ? "true" : "false", (unsigned long long)n.epoch,
                 n.present_id, (unsigned long long)n.frame,
                 n.identity_valid ? "true" : "false");
  }
  std::fputs(
      "],\"scheduler_deadline_policy\":{\"target\":\"actual_pre_rephase_GX2_"
      "due\","
      "\"clock_translation\":\"native_before_steady_sample_native_after\","
      "\"clock_bracket_preferred_ns\":100000,\"wide_brackets_retained\":true,"
      "\"primary_queue_allowance_slots\":0,"
      "\"sensitivity_queue_allowance_slots\":1,\"allowance_is_queue_"
      "guarantee\":false,"
      "\"classification\":\"definite_on_time_definite_late_ambiguous_or_"
      "unclassifiable_all_frames_retained\"},"
      "\"swap_frames\":[",
      file);
  for (size_t i = 0; i < swap_frames.size(); ++i) {
    if (i)
      std::fputc(',', file);
    const auto &v = swap_frames[i];
    std::fprintf(file, "{\"swap_id\":%llu,\"render_frame\":%llu}",
                 (unsigned long long)v.swap, (unsigned long long)v.frame);
  }
  std::fputs("],\"scheduler_deadlines\":[", file);
  for (size_t i = 0; i < scheduled_due.size(); ++i) {
    if (i)
      std::fputc(',', file);
    const auto &v = scheduled_due[i];
    std::fprintf(
        file,
        "{\"swap_id\":%llu,\"due_tick\":%llu,\"steady_due_ns\":%lld,"
        "\"shift_before_ns\":%lld,\"shift_after_ns\":%lld,\"granule\":%u,"
        "\"shifted_clock\":%s,\"uncapped\":%s,\"native_before_ns\":%llu,"
        "\"steady_sample_ns\":%lld,\"native_after_ns\":%llu,\"translation_"
        "valid\":%s,"
        "\"native_due_low_ns\":%llu,\"native_due_high_ns\":%llu}",
        (unsigned long long)v.swap, (unsigned long long)v.tick,
        (long long)v.steady_due, (long long)v.shift_before,
        (long long)v.shift_after, v.granule, v.shifted_clock ? "true" : "false",
        v.uncapped ? "true" : "false", (unsigned long long)v.native_before,
        (long long)v.steady_sample, (unsigned long long)v.native_after,
        v.translated ? "true" : "false", (unsigned long long)v.native_due.low,
        (unsigned long long)v.native_due.high);
  }
  std::fputs("],\"presentation_topology\":[", file);
  for (size_t i = 0; i < topology.size(); ++i) {
    if (i)
      std::fputc(',', file);
    const auto &v = topology[i];
    std::fprintf(
        file,
        "{\"epoch\":%llu,\"swapchain_images\":%u,\"acquire_slots\":%u,"
        "\"submission_slots\":%u,\"present_mode\":%u,\"async_present\":%s,\"scope\":\"configured_non_capture_path_capture_forces_synchronous\"}",
        (unsigned long long)v.epoch, v.images, v.acquire_slots,
        v.submission_slots, v.present_mode, v.async ? "true" : "false");
  }
  std::fputs("]}\n", file);
  const bool failed = std::ferror(file);
  const int closed = std::fclose(file);
  std::fprintf(stderr,
               "[present observer] %zu presents, %zu histories, %zu callbacks; "
               "%llu dropped; write %s\n",
               presents.size(), histories.size(), native.size(),
               (unsigned long long)dropped,
               failed || closed ? "failed" : "complete");
}
} // namespace present_observer
