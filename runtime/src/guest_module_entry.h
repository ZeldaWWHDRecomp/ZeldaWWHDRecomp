#pragma once
#include "guest_module_call.h"
#include "runtime.h"
#include <atomic>

namespace guestmods {
inline void invoke_module_entry(Cpu *cpu, PpcFunc function) {
  ModuleStackBounds bounds;
  (void)threads::current_stack_bounds(cpu, bounds.bottom, bounds.top);
  invoke_module(cpu, function, bounds, [](ModuleStackEntry reason) noexcept {
    static std::atomic_flag warned = ATOMIC_FLAG_INIT;
    if (warned.test_and_set(std::memory_order_relaxed))
      return;
    try {
      LOG("[guestmods] Stack realignment unavailable (%u); keeping the "
          "original call. Rebuild this mod with -mstackrealign. Old flagless "
          "mods are protected only with verified stack bounds, a valid caller "
          "backchain and 64 KiB spare callee stack.",
          unsigned(reason));
    } catch (...) {
    } // Diagnostics must never turn a legacy call into a failure.
  });
}
} // namespace guestmods
