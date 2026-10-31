// gfx/vulkan/warm_handover.h: the render thread builds warm-up pipelines itself unless the hand-over is asked for.
#include "gfx/vulkan/warm_handover.h"

#include <cassert>
#include <cstdio>

using namespace gfxvk::warm_handover;

int main() {
  assert(use(nullptr) == Use::CacheOnly);  // default: cache only (issue #334)
  assert(use("") == Use::CacheOnly);
  assert(use("0") == Use::CacheOnly);
  assert(use("yes") == Use::CacheOnly);    // only the exact value 1 turns the old hand-over back on
  assert(use("1") == Use::Handover);
  std::puts("warm_handover_test: ok");
  return 0;
}
