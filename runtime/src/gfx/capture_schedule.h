#pragma once
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Pure diagnostic scheduling, shared by both renderers. Invalid input stops
// parsing rather than stalling the render thread; completed entries remain valid.
namespace gfx::capture_schedule {
inline std::vector<uint64_t> parse(const char* value) {
    std::vector<uint64_t> result;
    if (!value) return result;
    std::string_view text(value);
    while (!text.empty()) {
        uint64_t n = 0;
        auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), n);
        if (error != std::errc{} || end == text.data()) break;
        result.push_back(n);
        text.remove_prefix(end - text.data());
        while (!text.empty() && text.front() == ',') text.remove_prefix(1);
    }
    return result;
}
inline bool contains(const std::vector<uint64_t>& values, uint64_t value) {
    for (auto n : values) if (n == value) return true;
    return false;
}
inline bool relative_due(const std::vector<uint64_t>& offsets, uint64_t frame, uint64_t loaded) {
    return loaded != 0 && frame >= loaded && contains(offsets, frame - loaded);
}
inline std::string stem(uint64_t value, bool relative) {
    return std::string(relative ? "load_frame_" : "frame_") + std::to_string(value);
}
}  // namespace gfx::capture_schedule
