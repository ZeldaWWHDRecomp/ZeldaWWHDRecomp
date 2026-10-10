#include "gfx/vulkan/present_worker.h"
#include <cassert>
#include <chrono>
#include <future>
#include <stdexcept>
using namespace std::chrono_literals;
int main() {
    gfxvk::PresentWorker<int> worker;
    std::promise<void> entered,release;
    auto gate=release.get_future().share();
    worker.submit([&] { entered.set_value();gate.wait();return 42; });
    entered.get_future().wait();
    // A blocked presentation must not make a render-thread poll block.
    auto polling=std::async(std::launch::async,[&] { return worker.poll(); });
    const bool nonblocking=polling.wait_for(1s)==std::future_status::ready;
    release.set_value();
    assert(nonblocking && !polling.get());
    // The slot stays occupied even if the operation has finished: its result
    // must be consumed before another acquired image can be submitted.
    try { worker.submit([] { return 99; });assert(false); }
    catch(const std::logic_error&) { }
    assert(worker.wait()==42);
    assert(!worker.poll());
    worker.submit([]() -> int { throw std::runtime_error("authored failure"); });
    try { worker.wait();assert(false); }catch(const std::runtime_error&) { }
    for(int i=0;i<20;++i) {
        worker.submit([i] { return i; });
        assert(worker.wait()==i);
    }
}
