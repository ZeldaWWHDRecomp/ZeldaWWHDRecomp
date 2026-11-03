// SPDX-License-Identifier: MPL-2.0
// Host-runtime difference tests: shared runtime (rt.cpp).
#pragma once
#include <cstdint>
#include <vector>

#include "ppc.h"

namespace difftest {

// one call a function under test made outside its closure: an import (name) or a game function (addr)
struct Call {
    uint32_t addr;
    const char* import;
    uint32_t r[11];        // r0..r10 at the call (r1 as the offset below the test's entry SP)
    uint64_t f[9];         // f0..f8 (ps0 bits)
    uint32_t stack[4];     // words at SP+8 .. SP+0x14 (stack-passed arguments)
    uint32_t lr, back;     // LR at the call, back-chain word at SP
};
using CallLog = std::vector<Call>;
extern thread_local CallLog* t_log;

// models for imports / game functions beyond the built-in ones (memset, memcpy, __gh_set_errno)
extern bool (*import_hook)(Cpu*, const char*);
extern bool (*outside_hook)(Cpu*, uint32_t);

void load_image(const char* path);
// a fresh CPU state with a stack of its own per slot (thread)
void reset(Cpu* c, int slot);
inline uint32_t stack_top(int slot) { return 0xF0000000u + 0x00100000u * (uint32_t)(slot + 1) - 0x100u; }
// scratch guest memory per slot for buffers
inline uint32_t scratch(int slot) { return 0xE0000000u + 0x00100000u * (uint32_t)slot; }

inline uint64_t bits(double d) { return f64_as_u64(d); }

extern thread_local uint32_t t_entry_sp;  // the SP the test started the function with
Call make_call(Cpu* c, uint32_t addr, const char* import);

}  // namespace difftest
