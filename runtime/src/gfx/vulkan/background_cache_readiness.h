#pragma once
#include <atomic>

namespace gfxvk {
// Initialization runs before the renderer execution lock is in use. Publish only after cache
// state is initialized. Lifecycle threads may inspect this flag, never mutable renderer fields.
// Withdraw before draining the saver; cache access still needs the renderer execution lock.
class BackgroundCacheReadiness {
  std::atomic<bool> ready{false};
public:
  void publish() { ready.store(true, std::memory_order_release); }
  void withdraw() { ready.store(false, std::memory_order_release); }
  bool available() const { return ready.load(std::memory_order_acquire); }
};
}
