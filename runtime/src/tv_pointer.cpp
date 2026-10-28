// The mouse on the TV picture for the port's own pages (tv_pointer.h).
#include "tv_pointer.h"

#include <mutex>

namespace tv_pointer {
namespace {
std::atomic<bool> g_wanted{false};
std::mutex g_mu;
State g_state;
}  // namespace

void set_wanted(bool on) {
    if (!g_wanted.exchange(on) || on) return;
    std::lock_guard lock(g_mu);
    g_state.down = false;  // no longer wanted: a held click ends
}
bool wanted() { return g_wanted.load(std::memory_order_relaxed); }
void set(bool down, float x, float y, float aspect) {
    std::lock_guard lock(g_mu);
    g_state = {down, x, y, aspect};
}
State read() {
    std::lock_guard lock(g_mu);
    return g_state;
}

}  // namespace tv_pointer
