#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace gfx::depth_peek {
inline uint32_t from_float(float z) {
    if (!std::isfinite(z)) return 0;
    return z >= 1.0f ? 0xFFFFFFu : uint32_t(std::clamp(z, 0.0f, 1.0f) * 16777215.0f);
}
inline int pixel(uint32_t coordinate, uint32_t extent, uint32_t guestExtent) {
    return int(std::clamp(double(int32_t(coordinate)) * extent / guestExtent, 0.0, double(extent - 1)));
}
uint64_t next_ticket();  // one per request, in request order: the renderers take one for every peek
int mode();
// tests only (renderer smoke): answer as each GPU frame completes (0) regardless of WWHD_PEEKZ_SYNC; -1 restores
void force_mode_for_test(int m);  // WWHD_PEEKZ_SYNC: 3 (default) answers two steps after the request; tests: 1 one step after
             // (as on the Wii U), 2 one step after with a synchronous copy, 0 the former timing
void publish(uint64_t ticket, const std::vector<uint32_t>& destinations, const std::vector<uint32_t>& depths);
// On the Wii U the game reads a peek's answers one step after its request: peekData reads the depth
// buffer on the CPU and writes the answers at once (peekz.cpp). Here the renderers submit the small
// copy at once and answer when it completes, and the answers are written at a fixed point: by
// default (3) at the game's swap after the next one, so the game reads them two steps after it
// asked. That needs no waiting in practice and gives the game the same answers whatever the timing
// (the glare reacts one step later than on the Wii U). Test value 1 writes them before the next
// step (exact): the game thread waits at its swap until that frame's answers are in, and the idle
// render thread retires the copy's submission meanwhile (render::peek_retire); 2 does the same with
// a synchronous copy; 0 is the former timing (whenever the GPU frame completes; not deterministic).
// Counts requests (game thread) against handled requests (render thread, in command order).
struct AnswerGate {
    std::mutex mutex;
    std::condition_variable wake;
    uint64_t issued = 0, answered = 0;
    uint64_t waitingFor = 0;  // the game thread waits until this many requests were handled (0: not waiting)
    void (*wakeRender)() = nullptr;  // wakes the idle render thread to retire the copies (gx2_core)
    void issue() { std::lock_guard lock(mutex); ++issued; }
    void answer() { { std::lock_guard lock(mutex); ++answered; } wake.notify_all(); }
    bool open() { std::lock_guard lock(mutex); return answered < issued; }
    void notify_render(std::unique_lock<std::mutex>& lock) {
        if (!wakeRender) return;
        lock.unlock();
        wakeRender();
        lock.lock();
    }
    // the game thread is blocked waiting for answers that are not in yet (the render thread then
    // retires the copies' submission instead of idling)
    bool wanted() { std::lock_guard lock(mutex); return waitingFor && answered < waitingFor; }
    uint64_t issued_count() { std::lock_guard lock(mutex); return issued; }
    // until the first target requests were handled (mode 3); false: the limit ran out (no resync)
    bool wait_until(uint64_t target, std::chrono::milliseconds limit) {
        std::unique_lock lock(mutex);
        if (answered >= target) return true;
        waitingFor = target;
        notify_render(lock);
        const bool ok = wake.wait_for(lock, limit, [&] { return answered >= target; });
        waitingFor = 0;
        return ok;
    }
    // true: every issued request was handled (at once or within limit); false: the limit ran out, the
    // count is resynchronised so that a lost request cannot stall later frames
    bool wait(std::chrono::milliseconds limit) {
        std::unique_lock lock(mutex);
        if (answered >= issued) return true;
        waitingFor = issued;
        notify_render(lock);
        const bool ok = wake.wait_for(lock, limit, [&] { return answered >= issued; });
        waitingFor = 0;
        if (!ok) answered = issued;
        return ok;
    }
};
bool synchronous();
void issued();                 // game thread: a peek request was sent to the renderer
void answered();               // render thread: that request was handled (answered, or no answer)
bool outstanding();            // a request has not been handled yet
bool wanted();                 // the game thread is waiting for an answer that is not in yet
void set_render_wake(void (*wake)());  // gx2_core: wakes the render thread's command wait
void wait_answered();          // game thread, at its swap: wait until every request was handled
// Renderers: answers a request exactly once when nothing took it over (an early return: no depth
// buffer, a format without depth readback, a withheld peek).
struct AnswerGuard {
    bool handed = false;
    ~AnswerGuard() { if (!handed) answered(); }
};
} // namespace gfx::depth_peek
