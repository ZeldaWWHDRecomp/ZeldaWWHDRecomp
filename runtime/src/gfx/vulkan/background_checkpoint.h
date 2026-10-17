#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace gfxvk {
// Lifecycle callbacks only queue/coalesce requests. stop() must run before
// taking any renderer lock used by work, and before destroying the renderer.
class BackgroundCheckpoint {
  std::mutex mutex;
  std::condition_variable wake;
  std::thread worker;
  std::function<void()> work;
  bool pending = false, stopped = false;
  void run() {
    std::unique_lock lock(mutex);
    for (;;) {
      wake.wait(lock, [&] { return pending || stopped; });
      if (!pending) return;
      pending = false;
      lock.unlock();
      try { work(); } catch (...) { /* a cache failure must not end the app */ }
      lock.lock();
    }
  }
public:
  explicit BackgroundCheckpoint(std::function<void()> callback) : work(std::move(callback)) {}
  ~BackgroundCheckpoint() { stop(); }
  void request() {
    std::lock_guard lock(mutex);
    if (stopped) return;
    if (!worker.joinable()) worker = std::thread([this] { run(); });
    pending = true;
    wake.notify_one();
  }
  void stop() {
    { std::lock_guard lock(mutex); stopped = true; }
    wake.notify_one();
    if (worker.joinable()) worker.join();
  }
};
}
