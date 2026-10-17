#include "platform/present_observer_core.h"
#include <cassert>
#include <limits>
#include <thread>

int main() {
  using namespace present_observer::core;
  Native good{1, 7, 900000000, 1000000000, 1100000000, 1.0, true};
  std::vector<Native> values{good};
  auto match = match_native(values, 1000000002);
  assert(match.status == MatchStatus::Matched && match.index == 0);
  assert(match_native(values, 1000000003).status == MatchStatus::Missing);
  assert(match_native(values, 0).status == MatchStatus::Invalid);
  auto bad = good;
  bad.actual = 0;
  bad.raw_seconds = 0;
  assert(match_native({bad}, 1000000000).status == MatchStatus::Missing);
  values.push_back(good);
  assert(match_native(values, 1000000000).status == MatchStatus::Ambiguous);
  bad = good;
  bad.visible = false;
  assert(match_native({bad}, good.actual).status == MatchStatus::Invalid);
  bad = good;
  bad.acquired = good.actual + 1;
  assert(match_native({bad}, good.actual).status == MatchStatus::Invalid);
  bad = good;
  bad.callback = good.actual - 1;
  assert(match_native({bad}, good.actual).status == MatchStatus::Invalid);
  for (double raw : {0.0, -1.0, 2.0, std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::quiet_NaN()}) {
    bad = good;
    bad.raw_seconds = raw;
    assert(match_native({bad}, good.actual).status == MatchStatus::Invalid);
  }
  bad = good;
  bad.actual = UINT64_MAX;
  bad.callback = UINT64_MAX;
  bad.raw_seconds = std::ldexp(1.0, 64) / 1e9;
  assert(match_native({bad}, UINT64_MAX).status == MatchStatus::Invalid);
  // Genuine timestamp collisions retain distinct immutable identities. Callback
  // arrival order must not assign a frame; same-identity duplicates still fail.
  auto first = good;
  first.epoch = 2; first.present_id = 10; first.frame = 9; first.identity_valid = true;
  auto second = first; second.present_id = 11; second.frame = 10; second.drawable = 8;
  std::vector<Native> coalesced{second, first};
  assert(match_native_identity(coalesced,2,10,9,good.actual).index == 1);
  assert(match_native_identity(coalesced,2,11,10,good.actual).index == 0);
  assert(match_native_identity(coalesced,2,10,10,good.actual).status == MatchStatus::Missing);
  assert(match_native_identity({first,first},2,10,9,good.actual).status == MatchStatus::Ambiguous);
  assert(match_native_identity({first},2,10,9,good.actual+3).status == MatchStatus::Invalid);
  first.identity_valid = false;
  assert(match_native_identity({first},2,10,9,good.actual).status == MatchStatus::Missing);
  first.identity_valid = true; first.visible = false;
  assert(match_native_identity({first},2,10,9,good.actual).status == MatchStatus::Invalid);
  first.visible = true; first.acquired = good.actual + 1;
  assert(match_native_identity({first},2,10,9,good.actual).status == MatchStatus::Invalid);
  first.acquired = good.acquired; first.actual = 0;
  assert(match_native_identity({first},2,10,9,good.actual).status == MatchStatus::Invalid);
  // History may arrive first. Keep the collector open until a delayed native
  // handler publishes; a genuinely missing callback remains a bounded failure.
  std::vector<Native> delayed;
  std::mutex mutex;
  std::condition_variable ready;
  std::thread handler([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    std::lock_guard lock(mutex);
    delayed.push_back(good);
    ready.notify_all();
  });
  {
    std::unique_lock lock(mutex);
    assert(await_callbacks(ready, lock, delayed, 1,
                           std::chrono::milliseconds(500)));
    assert(!await_callbacks(ready, lock, delayed, 2,
                            std::chrono::milliseconds(10)));
    assert(delayed.size() == 1);
  }
  handler.join();
  Anchor anchor;
  uint64_t target = 42;
  assert(!anchor.target(0, 1, target) && target == 42);
  assert(!anchor.observe(10, 0) && !anchor.set());
  assert(anchor.observe(10, 1000));
  assert(anchor.observe(12, 999999));
  assert(anchor.frame() == 10 && anchor.time() == 1000);
  assert(anchor.target(12, 100, target) && target == 1200);
  assert(!anchor.observe(9, 1001));
  assert(!anchor.target(9, 100, target));
  assert(!anchor.target(10, 0, target));
  assert(!anchor.target(UINT64_MAX, UINT64_MAX, target));
  Anchor edge;
  assert(edge.observe(0, UINT64_MAX - 1));
  assert(edge.target(1, 1, target) && target == UINT64_MAX);
  assert(!edge.target(2, 1, target));
}
