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

namespace guestmods {
struct ModuleStackBounds {
  uint32_t bottom = 0, top = 0;
};
enum class ModuleStackEntry {
  Aligned,
  Shadowed,
  UnknownBounds,
  InvalidFrame,
  InsufficientHeadroom
};
using ModuleStackDiagnostic = void (*)(ModuleStackEntry) noexcept;
// This is spare callee space, NOT the size of a guessed argument-copy window.
inline constexpr uint32_t kModuleStackHeadroom = 0x10000;

// Standard EABI callers keep outgoing stack arguments inside the current frame.
// Relocate that complete frame, retaining its parent backchain and original
// by-reference pointer values. Never copy it back over escaped caller locals.
// Missing receipts or inadequate space retain legacy behavior, with a
// diagnostic.
inline ModuleStackEntry
invoke_module(Cpu *cpu, PpcFunc function, ModuleStackBounds bounds,
              ModuleStackDiagnostic diagnostic = nullptr) {
  const uint32_t old_sp = cpu->r[1], old_lr = cpu->lr, old_ctr = cpu->ctr;
  ModuleStackEntry result = ModuleStackEntry::Aligned;
  uint32_t shadow = old_sp, frame_bytes = 0;
  if (old_sp & 15) {
    result = ModuleStackEntry::UnknownBounds;
    if (bounds.bottom && bounds.bottom < bounds.top &&
        old_sp >= bounds.bottom && uint64_t(old_sp) + 8 <= bounds.top) {
      result = ModuleStackEntry::InvalidFrame;
      if (!(old_sp & 7)) {
        const uint32_t parent = ld32(old_sp);
        if (parent > old_sp && parent <= bounds.top && !(parent & 7)) {
          frame_bytes = parent - old_sp;
          result = ModuleStackEntry::InsufficientHeadroom;
          if (uint64_t(old_sp) >= uint64_t(bounds.bottom) + frame_bytes) {
            shadow = (old_sp - frame_bytes) & ~uint32_t(15);
            if (shadow >= bounds.bottom &&
                shadow - bounds.bottom >= kModuleStackHeadroom)
              result = ModuleStackEntry::Shadowed;
          }
        }
      }
    }
  }
  if (result != ModuleStackEntry::Aligned &&
      result != ModuleStackEntry::Shadowed && diagnostic)
    diagnostic(result);
  if (result == ModuleStackEntry::Shadowed) {
    std::memcpy(ppc_ptr(shadow), ppc_ptr(old_sp), frame_bytes);
    cpu->r[1] = shadow;
  }
  // The emulated PPC epilogue cannot run when a host exception unwinds a
  // translated frame. Preserve its nonvolatile state explicitly on that path.
  uint32_t nonvolatile[20];
  nonvolatile[0] = cpu->r[2];
  for (unsigned i = 0; i < 19; ++i)
    nonvolatile[i + 1] = cpu->r[13 + i];
  uint8_t cr[12];
  std::memcpy(cr, cpu->cr + 8, sizeof(cr));
  uint64_t lower[18];
  for (unsigned i = 0; i < 18; ++i)
    std::memcpy(lower + i, &cpu->f[14 + i].ps0, 8);
  try {
    invoke_module(cpu, function);
  } catch (...) {
    cpu->r[2] = nonvolatile[0];
    for (unsigned i = 0; i < 19; ++i)
      cpu->r[13 + i] = nonvolatile[i + 1];
    std::memcpy(cpu->cr + 8, cr, sizeof(cr));
    for (unsigned i = 0; i < 18; ++i)
      std::memcpy(&cpu->f[14 + i].ps0, lower + i, 8);
    cpu->r[1] = old_sp;
    cpu->lr = old_lr;
    cpu->ctr = old_ctr;
    throw;
  }
  // A normal return keeps the callee's LR and CTR: they are architectural
  // outputs of the call (non-EABI helpers may return values there), exactly as
  // without this adapter. Only the stack pointer is ours to put back, since a
  // shadowed frame returns with r1 at the shadow copy.
  cpu->r[1] = old_sp;
  return result;
}
} // namespace guestmods
