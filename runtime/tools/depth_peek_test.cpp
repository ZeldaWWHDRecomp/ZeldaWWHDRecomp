#include "gfx/depth_peek.h"
#include <cassert>
#include <chrono>
#include <limits>
#include <atomic>
#include <thread>

int main() {
    using gfx::depth_peek::pixel;
    // The sun's legacy screen space and the HD viewport describe the same centre.
    assert(pixel(320, 1280, 640) == 640);
    assert(pixel(240, 720, 480) == 360);
    assert(pixel(640, 1280, 1280) == 640);
    assert(pixel(360, 720, 720) == 360);
    for (auto guest : {640u, 480u, 1280u, 720u}) {
        for (auto host : {1u, 720u, 1280u, 2560u}) {
            assert(pixel(0, host, guest) == 0);
            assert(pixel(uint32_t(-1), host, guest) == 0);
            assert(pixel(uint32_t(-32768), host, guest) == 0);
            assert(pixel(guest, host, guest) == int(host - 1));
            assert(pixel(guest * 2, host, guest) == int(host - 1));
            assert(pixel(guest - 1, host, guest) == int(uint64_t(guest - 1) * host / guest));
        }
    }
    using gfx::depth_peek::from_float;
    assert(from_float(0) == 0);
    assert(from_float(0.5f) == 0x7FFFFF);
    assert(from_float(1) == 0xFFFFFF);
    assert(from_float(-1) == 0);
    assert(from_float(2) == 0xFFFFFF);
    assert(from_float(std::numeric_limits<float>::quiet_NaN()) == 0);

    // AnswerGate: the game thread waits at its swap until the render thread handled every request
    using namespace std::chrono_literals;
    gfx::depth_peek::AnswerGate gate;
    assert(gate.wait(0ms));  // nothing issued: no wait
    gate.issue();
    gate.issue();
    gate.answer();
    assert(!gate.wait(10ms));  // one still open: the limit runs out ...
    assert(gate.wait(0ms));    // ... and the count is resynchronised
    gate.issue();
    std::thread render([&] { std::this_thread::sleep_for(20ms); gate.answer(); });
    const auto started = std::chrono::steady_clock::now();
    assert(gate.wait(5s));  // answered by the other thread
    assert(std::chrono::steady_clock::now() - started >= 15ms);
    render.join();
    gate.issue();
    gate.answer();
    assert(gate.wait(0ms));

    // the render thread retires copies only while the game thread waits (wanted), and is woken for it
    gfx::depth_peek::AnswerGate waited;
    static std::atomic<int> wakes{0};
    waited.wakeRender = [] { wakes++; };
    waited.issue();
    assert(waited.open() && !waited.wanted());  // not waiting yet: the render thread stays idle
    std::thread renderer([&] {
        while (!waited.wanted()) std::this_thread::yield();  // the game waits now
        waited.answer();
    });
    assert(waited.wait(5s) && wakes == 1 && !waited.wanted());
    renderer.join();
    // two steps (test mode 3): wait only for the earlier requests
    waited.issue();
    waited.issue();
    assert(waited.issued_count() == 3);
    assert(waited.wait_until(1, 0ms));       // the first request was handled long ago: no wait
    assert(!waited.wait_until(2, 10ms));     // the second is not: the limit runs out (no resync)
    waited.answer();
    assert(waited.wait_until(2, 0ms) && waited.open());
}
