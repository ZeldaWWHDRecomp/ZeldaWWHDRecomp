#include "gx2/vsync_pacing.h"
#include "platform/present_deadline_core.h"
#include <cassert>

int main() {
  using namespace present_observer::deadline;
  Interval converted;
  assert(translate(2600000100, 2600000000, 1900000000, 1900000020, converted));
  assert(converted.low == 1900000100 && converted.high == 1900000120);
  assert(translate(2599999900, 2600000000, 1900000000, 1900000020, converted));
  assert(converted.low == 1899999900 && converted.high == 1899999920);
  assert(!translate(1, 2, 100, 99, converted));
  assert(!translate(1, 2, 100, 200101, converted, 100000));
  assert(translate(1, 2, 100, 200101, converted));
  assert(!translate(-1, 2, 100, 101, converted));
  assert(!translate(0, INT64_MAX, 100, 101, converted));
  assert(!translate(INT64_MAX, 0, UINT64_MAX - 1, UINT64_MAX, converted));
  const uint64_t anchor = 1000000000;
  assert(classify(anchor + 8333333, {anchor + 8000000, anchor + 8000020},
                  anchor) == Status::OnTime);
  assert(classify(anchor + 16666667, {anchor + 8000000, anchor + 8000020},
                  anchor) == Status::Late);
  assert(classify(anchor + 16666667, {anchor + 8000000, anchor + 8000020},
                  anchor, 1) == Status::OnTime);
  // Clock uncertainty straddles a VBlank: do not force a midpoint decision.
  assert(classify(anchor + 12000000, {anchor + 8333330, anchor + 8333340},
                  anchor) == Status::Ambiguous);
  assert(classify(anchor, {anchor + 2, anchor + 1}, anchor) == Status::Invalid);
  assert(classify(UINT64_MAX, {anchor, anchor}, anchor) == Status::Invalid);
  FrameJoin joins;
  assert(joins.insert(55, 7));
  assert(!joins.insert(55, 8));
  assert(!joins.insert(56, 7));
  uint64_t frame = 0;
  assert(joins.find(55, frame) && frame == 7);
  assert(!joins.find(54, frame));
  // The runtime rephases after a hitch. Retain that flip's OLD target, but
  // the next flip's target must use the scheduler's NEW shift, not an anchor.
  const vsync_pacing::Clock clock{16683333, 4};
  int64_t shift = 0;
  const uint64_t due_tick = 4;
  const int64_t original_due = clock.time(due_tick, shift);
  const int64_t ready = original_due + 2 * clock.period;
  const uint64_t executed =
      vsync_pacing::flip_executed(clock, shift, due_tick, ready, 2);
  assert(shift != 0);
  assert(original_due < ready);
  const int64_t next_due = clock.time(executed + 2, shift);
  assert(next_due > ready && next_due - ready <= clock.period / 2 + 1);
  assert(next_due != original_due + clock.period / 2);
}
