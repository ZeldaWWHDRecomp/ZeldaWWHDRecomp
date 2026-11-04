#pragma once

#include "ppc.h"
#include <cstdint>
#include <cstring>

namespace guestmods {
// Clang's scalar PowerPC ABI preserves only PS0 of the nonvolatile FPRs. Native
// game callers can keep paired-single values in both lanes across a call.
// Keep this at native-to-module boundaries, rather than internal module calls.
inline void invoke_module(Cpu *cpu, PpcFunc function) {
  struct UpperLanes {
    Cpu *cpu;
    uint64_t bits[18];
    explicit UpperLanes(Cpu *c) : cpu(c) {
      for (unsigned i = 0; i < 18; ++i)
        std::memcpy(&bits[i], &cpu->f[14 + i].ps1, sizeof(bits[i]));
    }
    ~UpperLanes() {
      for (unsigned i = 0; i < 18; ++i)
        std::memcpy(&cpu->f[14 + i].ps1, &bits[i], sizeof(bits[i]));
    }
  } saved(cpu);
  function(cpu);
}
} // namespace guestmods
