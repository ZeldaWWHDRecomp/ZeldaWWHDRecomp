// Settings > Graphics > Detailed performance profiling (render_prof.h): off by default, switched at a
// swap without a restart, WWHD_PROFILE wins over the setting, and the performance report's line.
// Run as: render_prof_test (setting), render_prof_test env-on, render_prof_test env-off.
#include "render_prof.h"
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// portable environment edits (no setenv/unsetenv on Windows)
static void set_env(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value ? value : "");
#else
    if (value) setenv(key, value, 1); else unsetenv(key);
#endif
}

static std::vector<std::string> logs;
void log_msg(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    logs.push_back(buf);
}
namespace interp { uint64_t logic_steps() { return 0; } }

static bool logged(const char* text) {
    for (const auto& l : logs)
        if (l.find(text) != std::string::npos) return true;
    return false;
}
static bool has(const std::string& s, const char* text) { return s.find(text) != std::string::npos; }

// one frame of render-thread work as gx2_core runs it: a timed draw, then the swap
static void frame() {
    rprof::classify_draw();
    uint64_t t = rprof::op_begin(rprof::kOpDraw);
    rprof::op_end(rprof::kOpDraw, t);
    t = rprof::op_begin(rprof::kOpSwap);
    rprof::op_end(rprof::kOpSwap, t);
    rprof::frame_end(false);
}

static void setting() {
    set_env("WWHD_PROFILE", nullptr);
    // default: off (no saved value)
    rprof::start(false);
    assert(!rprof::enabled() && !rprof::requested() && !rprof::env_override());
    assert(logged("[prof] detailed performance profiling off (setting)"));
    for (int i = 0; i < 200; i++) frame();
    assert(rprof::latest_report().empty());
    std::string line = rprof::report_section();
    assert(has(line, "are off") && has(line, "Settings > Graphics > Detailed performance profiling"));
    assert(std::count(line.begin(), line.end(), '\n') == 1);
    assert(rprof::op_begin(rprof::kOpSwap) == 0);  // nothing is timed while off

    // turned on in Settings: in effect from the next swap, no restart
    logs.clear();
    rprof::set_requested(true);
    assert(rprof::requested() && !rprof::enabled());
    assert(has(rprof::report_section(), "Detailed profiling is on"));
    frame();
    assert(rprof::enabled() && logged("[prof] detailed performance profiling on (setting)"));
    for (int i = 0; i < 130; i++) frame();
    const std::string report = rprof::latest_report();
    assert(has(report, "[prof] frame") && has(report, "[prof] ops ms/frame") && has(report, "[prof] draw classes"));
    assert(rprof::report_section() == report);

    // turned off again: the last report is dropped, the one-line note is back
    logs.clear();
    rprof::set_requested(false);
    frame();
    assert(!rprof::enabled() && logged("[prof] detailed performance profiling off (setting)"));
    assert(rprof::latest_report().empty() && has(rprof::report_section(), "are off"));

    // the saved choice at start-up
    rprof::start(true);
    assert(rprof::enabled() && logged("[prof] detailed performance profiling on (setting)"));
    assert(!strcmp(rprof::kSettingKey, "detailedProfiling"));
}

static void env(bool on) {
    set_env("WWHD_PROFILE", on ? "1" : "0");
    rprof::start(!on);  // the saved setting says the opposite: WWHD_PROFILE wins
    assert(rprof::env_override() && rprof::enabled() == on && rprof::requested() == on);
    assert(logged(on ? "profiling on (WWHD_PROFILE=1)" : "profiling off (WWHD_PROFILE=0)"));
    rprof::set_requested(on ? false : true);
    for (int i = 0; i < 130; i++) frame();
    assert(rprof::enabled() == on);
    if (on) assert(has(rprof::report_section(), "[prof] frame"));
    else assert(has(rprof::report_section(), "WWHD_PROFILE=0"));
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "";
    if (!strcmp(mode, "env-on")) env(true);
    else if (!strcmp(mode, "env-off")) env(false);
    else setting();
    puts("render_prof: ok");
    return 0;
}
