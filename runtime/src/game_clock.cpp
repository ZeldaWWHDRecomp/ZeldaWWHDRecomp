// SPDX-License-Identifier: MPL-2.0
#include "game_clock.h"
#include <chrono>
#include <atomic>
#include <mutex>
namespace game_clock {
namespace {
std::mutex mutex;
Clock clock;
std::atomic<bool> has_shift{false};
int64_t host_now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
Sample sample() {
    std::lock_guard<std::mutex> lock(mutex);
    const auto host = host_now();
    return {host, clock.now(host), clock.rate()};
}
void set_rate(unsigned rate) {
    std::lock_guard<std::mutex> lock(mutex);
    clock.set_rate(host_now(), rate);
    if (rate > 1 && rate <= 4) has_shift = true;
}
unsigned rate() { return has_shift.load(std::memory_order_relaxed) ? sample().rate : 1; }
bool shifted() { return has_shift.load(std::memory_order_relaxed); }
}
