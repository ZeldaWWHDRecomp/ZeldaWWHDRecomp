// Live renderer objects and the process's memory mappings, for crash logs (crash_context.cpp) and the
// overlay's performance report: a renderer that runs out of driver mappings shows it there. On Android
// every pipeline and shader module maps driver memory, and a process stops at vm.max_map_count
// (65530 by default): the GreenNaugahyde Android fork found its crash #48 that way. One line, e.g.
//   renderer objects: 572 shaders, 337 pipelines; memory mappings 23456 (limit 65530)
// Mappings are counted where the system lists them (/proc/self/maps: Linux, Android).
#pragma once
#include <cstdint>
#include <string>

namespace render_objects {
// renderers, when a count changes (any thread)
void set_shaders(uint64_t translatedShaders);
void set_pipelines(uint64_t pipelines);
// the line above; the mapping count is read again at most every `maxAgeSeconds` (reading
// /proc/self/maps takes a few milliseconds with tens of thousands of mappings). Not for signal handlers.
std::string line(double maxAgeSeconds = 10);
// the parts, for the tests: lines of a /proc/self/maps text, and the formatting
long count_lines(const char* text, size_t size);
std::string format(uint64_t shaders, uint64_t pipelines, long mappings, long limit);
}  // namespace render_objects
