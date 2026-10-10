// WWHD diagnostic extension. No formatting or allocation when disabled.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace wwhd_trace {
inline bool enabled() { static const bool on = [] { const char* v = std::getenv("WWHD_OVERLAY_TRACE"); return v && !std::strcmp(v, "1"); }(); return on; }
inline bool handoff() { static const bool on = [] { const char* v = std::getenv("WWHD_OVERLAY_MOUSE_HANDOFF"); return !v || std::strcmp(v, "0"); }(); return on; }
inline std::atomic<bool> open{false};
inline std::atomic<void (*)(const char*)> sink{nullptr}; // host/render threads install the normal-log sink
// Hard session budget and token bucket: noisy axes cannot flood the normal log.
inline void emit(const char* format, ...) {
    if (!enabled() || !open.load(std::memory_order_relaxed) || !sink) return;
    static std::atomic_flag lock = ATOMIC_FLAG_INIT;
    while (lock.test_and_set(std::memory_order_acquire)) {}
    static size_t bytes = 0;
    static double tokens = 80;
    static unsigned dropped = 0;
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    tokens += std::chrono::duration<double>(now-last).count()*40; last=now;
    if (tokens>80) tokens=80;
    if (bytes>=512*1024) { lock.clear(std::memory_order_release); return; }
    if (tokens<1) { ++dropped; lock.clear(std::memory_order_release); return; }
    if (dropped) { char note[96]; std::snprintf(note,sizeof note,"rate-limit: %u lines suppressed",dropped); sink.load()(note); bytes+=std::strlen(note)+32; dropped=0; }
    --tokens;
    char line[1024]; va_list args; va_start(args,format);
    std::vsnprintf(line,sizeof line,format,args); va_end(args);
    bytes += std::strlen(line)+32;
    sink.load()(line);
    if(bytes>=512*1024) sink.load()("trace budget reached; restart for a new capture");
    lock.clear(std::memory_order_release);
}
inline void scroll(const char* window, float old_y, float new_y, const char* path) {
    if (old_y != new_y) emit("scroll window=%s old=%.3f new=%.3f path=%s",window,old_y,new_y,path);
}
}
