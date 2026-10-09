#include "exception_report.h"
#include <cassert>
#include <cstring>
#include <thread>

static bool unwound = false, saw_before_unwind = false;
static void capture(const char* message) noexcept {
    saw_before_unwind = !unwound && !std::strcmp(message, "original error");
}
int main() {
    exception_report::sink.store(capture);
    try {
        struct Cleanup { ~Cleanup() { unwound = true; } } cleanup;
        exception_report::raise("original error");
    } catch (const std::runtime_error& e) {
        assert(!std::strcmp(e.what(), "original error"));
    }
    assert(unwound && saw_before_unwind);
    exception_report::sink.store(nullptr);
    exception_report::remember(std::string(5000, 'x').c_str());
    assert(std::strlen(exception_report::last) == sizeof(exception_report::last) - 1);
    assert(std::strstr(exception_report::last, "[truncated]"));
    exception_report::remember("main thread");
    std::thread worker([] {
        assert(!exception_report::last[0]);
        exception_report::remember("worker thread");
        assert(!std::strcmp(exception_report::last, "worker thread"));
    });
    worker.join();
    assert(!std::strcmp(exception_report::last, "main thread"));
    struct Exit { int value; };
    bool exited = false;
    try {
        exception_report::boundary<Exit>("intentional exit", [] { throw Exit{7}; });
    } catch (const Exit& e) { exited = e.value == 7; }
    assert(exited);
    assert(!std::strcmp(exception_report::last, "main thread"));
}
