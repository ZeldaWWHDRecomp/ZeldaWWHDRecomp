#include "gfx/vulkan/background_cache_readiness.h"
#include "gfx/vulkan/background_checkpoint.h"
#include <atomic>
#include <cassert>
#include <future>
#include <mutex>
#include <string>
#include <thread>

int main() {
  gfxvk::BackgroundCacheReadiness readiness;
  std::mutex execution;
  std::string cachePath;
  int device = 0;
  unsigned cacheReads = 0;
  std::promise<void> firstChecked, initializedChecked, enterWorker, releaseWorker;
  auto entered = enterWorker.get_future();
  auto released = releaseWorker.get_future();
  gfxvk::BackgroundCheckpoint saver([&] {
    enterWorker.set_value();
    released.wait(); // simulate a request queued before shutdown acquires execution
    std::lock_guard lock(execution);
    if (!readiness.available()) return;
    assert(device == 7 && cachePath == "synthetic/cache");
    ++cacheReads;
  });
  std::atomic<bool> inspectInitialized{false};
  std::thread lifecycle([&] {
    // A Home event during initialization must not read the concurrently written payload.
    assert(!readiness.available());
    firstChecked.set_value();
    while (!inspectInitialized.load(std::memory_order_relaxed)) std::this_thread::yield();
    // Readiness acquire alone publishes the non-atomic cache/device state.
    while (!readiness.available()) std::this_thread::yield();
    assert(device == 7 && cachePath == "synthetic/cache");
    saver.request();
    initializedChecked.set_value();
  });
  firstChecked.get_future().wait();
  cachePath = "synthetic/cache";
  device = 7;
  readiness.publish();
  inspectInitialized.store(true, std::memory_order_relaxed);
  initializedChecked.get_future().wait();
  entered.wait();
  readiness.withdraw(); // reject stale queued work before joining, outside execution
  releaseWorker.set_value();
  saver.stop();
  lifecycle.join();
  assert(cacheReads == 0);
  { std::lock_guard lock(execution); device = 0; cachePath.clear(); }
  assert(!readiness.available());
}
