#include "gfx/vulkan/background_checkpoint.h"
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
int main() {
  std::mutex mutex;
  std::condition_variable wake;
  bool release = false;
  unsigned calls = 0;
  const auto caller = std::this_thread::get_id();
  gfxvk::BackgroundCheckpoint saver([&] {
    assert(std::this_thread::get_id() != caller);
    std::unique_lock lock(mutex);
    ++calls; wake.notify_all();
    wake.wait(lock, [&] { return release; });
    if (calls == 1) throw std::runtime_error("synthetic disk failure");
  });
  saver.request(); // must return while the save is still blocked
  { std::unique_lock lock(mutex); wake.wait(lock, [&] { return calls == 1; }); }
  for (int i = 0; i < 42; ++i) saver.request(); // coalesce while a save runs
  { std::lock_guard lock(mutex); release = true; }
  wake.notify_all();
  saver.stop(); // drain the pending request, even after the first save failed
  assert(calls == 2);
  saver.request(); saver.stop(); // no work can be admitted after shutdown
  assert(calls == 2);
}
