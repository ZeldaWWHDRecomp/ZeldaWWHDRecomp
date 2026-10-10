// No renderer/game/SDL: exercise noisy-input rate limiting and the session budget.
#include "wwhd_trace.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

static size_t bytes = 0, lines = 0;
static bool rate_notice = false, budget_notice = false;
int main() {
    assert(wwhd_trace::enabled());
    wwhd_trace::open = true;
    wwhd_trace::sink = [](const char* line) {
        bytes += std::strlen(line) + 32;
        ++lines;
        rate_notice |= std::strstr(line,"rate-limit:") != nullptr;
        budget_notice |= std::strstr(line,"trace budget reached") != nullptr;
    };
    std::string noisy(1000, 'x');
    for (int i=0; i<10000; ++i) wwhd_trace::emit("%s",noisy.c_str());
    assert(lines < 100); // the initial burst is bounded
    for (int i=0; i<550 && !budget_notice; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(26));
        wwhd_trace::emit("%s",noisy.c_str());
    }
    assert(rate_notice && budget_notice);
    assert(bytes < 530*1024);
    const size_t stopped = lines;
    for (int i=0; i<10000; ++i) wwhd_trace::emit("%s",noisy.c_str());
    assert(lines == stopped);
    std::printf("overlay trace budget passed: %zu bytes, %zu lines\n",bytes,lines);
}
