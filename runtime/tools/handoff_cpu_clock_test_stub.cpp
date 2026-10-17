// Synthetic Vulkan fixtures do not link the full renderer profiler. CPU timing
// unavailable is a required diagnostic case; never substitute a wall clock.
#include <cstdint>
namespace rprof { uint64_t thread_cpu_ns() { return 0; } }
