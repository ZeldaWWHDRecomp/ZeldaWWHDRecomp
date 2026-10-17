#include "platform/process_peak_rss.h"
#include <cassert>
#include <memory>

namespace {
unsigned calls = 0;
bool spy(process_peak_rss::Sample &) {
  ++calls;
  return false;
}
} // namespace
int main() {
  using namespace process_peak_rss;
  assert(write_endpoint_to(nullptr, 1, spy));
  assert(write_endpoint_to("", 1, spy));
  assert(calls == 0);
  uint64_t value = 77;
  assert(bytes({123, Unit::Bytes}, value) && value == 123);
  assert(bytes({123, Unit::KiB}, value) && value == 123 * 1024);
  assert(!bytes({-1, Unit::Bytes}, value));
  assert(!bytes({INT64_MAX, Unit::KiB}, value));
  assert(bytes({0, Unit::KiB}, value) && value == 0);
#if !defined(_WIN32)
  Sample before, during, after;
  assert(query(before));
  constexpr size_t size = 32 * 1024 * 1024;
  auto allocation = std::make_unique<unsigned char[]>(size);
  volatile unsigned char *pages = allocation.get();
  for (size_t i = 0; i < size; i += 4096)
    pages[i] = 1;
  pages[size - 1] = 1;
  assert(query(during));
  allocation.reset();
  assert(query(after));
  uint64_t b, d, a;
  assert(bytes(before, b) && bytes(during, d) && bytes(after, a));
  // Kernel lifetime high-water marks cannot decrease after freeing pages.
  // If an earlier runtime peak exceeds the allocation, equality is legitimate.
  assert(d >= b && a >= d);
  if (b < size)
    assert(d > b);
#else
  Sample unsupported;
  assert(!query(unsupported));
#endif
}
