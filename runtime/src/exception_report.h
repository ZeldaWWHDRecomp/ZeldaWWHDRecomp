// Preserve explicit runtime errors before the exception machinery touches generated game frames.
#pragma once
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace exception_report {
using Sink = void (*)(const char*) noexcept;
inline std::atomic<Sink> sink{nullptr};
// Per-thread and bounded: crash handlers must not allocate, lock, or inspect a C++ exception.
inline thread_local char last[4096]{};
inline void remember(const char* message) noexcept {
    size_t i = 0;
    if (!message) message = "unknown exception";
    for (; i < sizeof(last) - 1 && message[i]; ++i) last[i] = message[i];
    last[i] = 0;
    if (message[i]) {
        constexpr char tail[] = " [truncated]";
        for (size_t j = 0; j < sizeof(tail); ++j) last[sizeof(last) - sizeof(tail) + j] = tail[j];
    }
}
inline void record(const char* message) noexcept {
    remember(message);
    if (auto output = sink.load(std::memory_order_acquire)) output(last);
}
// Record BEFORE constructing/throwing the exception, not in an outer catch handler.
[[noreturn]] inline void raise(const char* message) {
    record(message);
    throw std::runtime_error(message);
}
[[noreturn]] inline void raise(const std::string& message) { raise(message.c_str()); }
inline void note(int fd, void (*out)(int, const char*, size_t)) noexcept {
    if (!last[0]) return;
    constexpr char title[] = "\nLast reported runtime error on this thread (may have been caught):\n";
    out(fd, title, sizeof(title) - 1);
    size_t n = 0;
    while (n < sizeof(last) && last[n]) ++n;
    out(fd, last, n);
    out(fd, "\n", 1);
}
struct NoPassThrough {};
// The report of an exception that reached a boundary: "context: what" ("context: unknown C++ exception" for
// what == nullptr), recorded, then abort. Out of line and cold: its 4 KiB message buffer is not in the frame of
// every boundary (each HLE import and port hook call), which kept a stack probe and a canary on every call.
[[noreturn, gnu::noinline, gnu::cold]] inline void boundary_failed(const char* context, const char* what) noexcept {
    char message[4096];
    if (what) std::snprintf(message, sizeof(message), "%s: %s", context, what);
    else std::snprintf(message, sizeof(message), "%s: unknown C++ exception", context);
    record(message);
    std::abort();
}
// Native boundaries catch unexpected library exceptions before they reach generated C frames.
// Guest thread exit is intentional control flow and must keep propagating to thread_main.
// WWHD_BOUNDARY_INLINE_REPORT=1 (compile time, A/B builds only) keeps the report inline as before.
template<class PassThrough = NoPassThrough, class F>
void boundary(const char* context, F&& fn) {
#if !defined(WWHD_BOUNDARY_INLINE_REPORT) || !WWHD_BOUNDARY_INLINE_REPORT
    try { std::forward<F>(fn)(); }
    catch (const PassThrough&) { throw; }
    catch (const std::exception& e) { boundary_failed(context, e.what()); }
    catch (...) { boundary_failed(context, nullptr); }
#else
    try { std::forward<F>(fn)(); }
    catch (const PassThrough&) { throw; }
    catch (const std::exception& e) {
        char message[4096];
        std::snprintf(message, sizeof(message), "%s: %s", context, e.what());
        record(message);
        std::abort();
    }
    catch (...) {
        char message[4096];
        std::snprintf(message, sizeof(message), "%s: unknown C++ exception", context);
        record(message);
        std::abort();
    }
#endif
}
} // namespace exception_report
