// Submission pool resets off the render thread: pool_reset.h
#ifdef WWHD_NATIVE_DRAW
#include "pool_reset.h"
#include "backend.h"
#include "runtime.h"
#include "record_worker.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace gfxvk::pool_reset {
namespace {
uint64_t ns_since(std::chrono::steady_clock::time_point t0) {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
}
struct Stats { uint64_t inlineResets = 0, inlineNs = 0, async = 0, waits = 0, waitNs = 0; std::atomic<uint64_t> workerNs{0}; };
Stats stats;  // render thread, but workerNs (the reset thread adds to it)
class Worker {
 public:
  Worker() : thread_([this] { run(); }) {}
  uint64_t push(VkDevice device, VkFence fence, VkCommandPool commands, VkDescriptorPool descriptors) {
    std::lock_guard lock(mutex_);
    jobs_.push_back({device, fence, commands, descriptors, ++last_});
    changed_.notify_all();
    return last_;
  }
  // render thread: the reset of TICKET done; its results checked as the inline reset checks them
  void wait(uint64_t ticket) {
    std::unique_lock lock(mutex_);
    if (done_ < ticket) {
      const auto t0 = std::chrono::steady_clock::now();
      changed_.wait(lock, [&] { return done_ >= ticket; });
      ++stats.waits;
      stats.waitNs += ns_since(t0);
    }
    auto it = results_.find(ticket);
    const std::array<VkResult, 3> r = it->second;
    results_.erase(it);
    lock.unlock();
    vk_check(r[0], "submission fence (pool reset thread)");
    vk_check(r[1], "reset command pool");
    vk_check(r[2], "reset descriptor pool");
  }
 private:
  struct Job { VkDevice device; VkFence fence; VkCommandPool commands; VkDescriptorPool descriptors; uint64_t ticket; };
  void run() {
    std::unique_lock lock(mutex_);
    for (;;) {
      changed_.wait(lock, [&] { return !jobs_.empty(); });
      const Job j = jobs_.front();
      jobs_.pop_front();
      lock.unlock();
      std::array<VkResult, 3> r{VK_SUCCESS, VK_SUCCESS, VK_SUCCESS};
      r[0] = vkWaitForFences(j.device, 1, &j.fence, VK_TRUE, UINT64_MAX);
      const auto t0 = std::chrono::steady_clock::now();
      if (r[0] == VK_SUCCESS) {
        r[1] = vkResetCommandPool(j.device, j.commands, 0);
        r[2] = vkResetDescriptorPool(j.device, j.descriptors, 0);
      }
      stats.workerNs += ns_since(t0);
      lock.lock();
      results_[j.ticket] = r;
      done_ = j.ticket;
      changed_.notify_all();
    }
  }
  std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<Job> jobs_;
  std::unordered_map<uint64_t, std::array<VkResult, 3>> results_;
  uint64_t last_ = 0, done_ = 0;
  std::thread thread_;
};
Worker& worker() { static Worker* w = new Worker; return *w; }  // (never destroyed: no join at exit)
std::unordered_map<const void*, uint64_t> tickets;  // render thread: a slot's queued reset
}  // namespace
int mode() {
  static const int m = [] {
    const char* e = std::getenv("WWHD_VK_ASYNC_POOL_RESET");
    const int v = e && *e ? std::atoi(e) & 3 : 1;  // unset: the reset thread (pool_reset.h)
    LOG("[vulkan] submission pool resets: %s, %s (WWHD_VK_ASYNC_POOL_RESET=%d)", v & 1 ? "on a reset thread" : "on the render thread",
        v & 2 ? "transient command pools" : "resettable command buffers", v);
    return v;
  }();
  return m;
}
VkCommandPoolCreateFlags pool_flags(VkCommandPoolCreateFlags defaultFlags) {
  return (mode() & 2) && !record_worker_requested() ? VK_COMMAND_POOL_CREATE_TRANSIENT_BIT : defaultFlags;
}
void submitted(const void* slot, VkDevice device, VkFence fence, VkCommandPool commands, VkDescriptorPool descriptors) {
  if (!(mode() & 1)) return;
  tickets[slot] = worker().push(device, fence, commands, descriptors);
  ++stats.async;
}
bool queued(const void* slot) { return tickets.count(slot) != 0; }
void reset(const void* slot, VkDevice device, VkCommandPool commands, VkDescriptorPool descriptors) {
  if (auto it = tickets.find(slot); it != tickets.end()) {
    const uint64_t ticket = it->second;
    tickets.erase(it);
    worker().wait(ticket);
    return;
  }
  const auto t0 = std::chrono::steady_clock::now();
  vk_check(vkResetCommandPool(device, commands, 0), "reset command pool");
  vk_check(vkResetDescriptorPool(device, descriptors, 0), "reset descriptor pool");
  ++stats.inlineResets;
  stats.inlineNs += ns_since(t0);
}
void report(double frames) {
  const uint64_t workerNs = stats.workerNs.exchange(0);
  LOG("[vulkan pool reset] mode %d: inline %.2f/frame %.3f ms/frame; on the reset thread %.2f/frame (%.3f ms/frame "
      "there); render-thread waits for it %.2f/frame %.3f ms/frame",
      mode(), stats.inlineResets / frames, stats.inlineNs / 1e6 / frames, stats.async / frames, workerNs / 1e6 / frames,
      stats.waits / frames, stats.waitNs / 1e6 / frames);
  stats.inlineResets = stats.inlineNs = stats.async = stats.waits = stats.waitNs = 0;
}
}  // namespace gfxvk::pool_reset
#endif
