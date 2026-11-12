#include "exception_report.h"
#include <cassert>
#include <cstring>
#include <thread>
#ifndef _WIN32
#include <csignal>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

// A boundary that sees a library exception records "context: what" (or "context: unknown C++ exception") and
// aborts: run in a child, the recorded message comes back through a pipe and the child ends with SIGABRT.
static int g_pipe = -1;
static void to_pipe(const char* message) noexcept {
    const size_t n = std::strlen(message);
    if (write(g_pipe, message, n) != (ssize_t)n) _exit(3);
}
template <class F> static std::string dies_with(F&& f) {
    int fds[2];
    assert(pipe(fds) == 0);
    const pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(fds[0]);
        g_pipe = fds[1];
        std::signal(SIGABRT, SIG_DFL);
        exception_report::sink.store(to_pipe);
        f();
        _exit(0);  // not reached: the boundary aborts
    }
    close(fds[1]);
    std::string got;
    char buf[256];
    for (ssize_t n; (n = read(fds[0], buf, sizeof buf)) > 0;) got.append(buf, size_t(n));
    close(fds[0]);
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    return got;
}
#endif

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
#ifndef _WIN32
    struct Exit2 { int value; };
    assert(dies_with([] { exception_report::boundary<Exit2>("HLE coreinit.Test", [] { throw std::runtime_error("boom"); }); }) ==
           "HLE coreinit.Test: boom");
    assert(dies_with([] { exception_report::boundary("native game callback", [] { throw 42; }); }) ==
           "native game callback: unknown C++ exception");
    // a long what() is cut to the 4 KiB message, as before
    const std::string longer(5000, 'y');
    const std::string cut = dies_with([&] { exception_report::boundary("ctx", [&] { throw std::runtime_error(longer); }); });
    assert(cut.size() == sizeof(exception_report::last) - 1 && cut.rfind("ctx: yyy", 0) == 0);
#endif
}
