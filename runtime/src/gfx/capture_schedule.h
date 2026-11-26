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
// Test aid WWHD_DUMP_PASS (with WWHD_DUMP_FRAMES): the frame dumps take one pass per logic step, chosen on the
// game thread when the pass is swapped (gx2_core.cpp GX2SwapScanBuffers), and name it by its step:
// "record" (or "r") the step's record pass, the exact image of the step (with interpolation off its only pass),
// a number k the step's pass k (0 the logic pass, k hold pass k). The step is the scripted one with
// WWHD_TEST_STEP_CLOCK, else the logic step count; with WWHD_DUMP_PASS_ORIGIN=1 the step since the scenario origin
// (the same steps in every run, wherever a state load lands). Without it every pass of a listed frame writes
// frame_<n>.png (several per step with the step clock, whichever lands last). Returns -2 unset, -1 record, k.
inline int pass_select(const char* value) {
    if (!value || !*value) return -2;
    if (value[0] == 'r' || value[0] == 'R') return -1;
    int k = 0;
    auto [end, error] = std::from_chars(value, value + std::char_traits<char>::length(value), k);
    return error == std::errc{} && end != value && k >= 0 ? k : -1;
}
// the dump the swap command carries (render thread: set from the swap's payload before the backend's swap;
// 0 none, else the step + 1)
inline uint64_t g_pass_dump = 0;
inline std::string stem(uint64_t value, bool relative) {
    return std::string(relative ? "load_frame_" : "frame_") + std::to_string(value);
}
}  // namespace gfx::capture_schedule
