// OSRendezvous arrival state (coreinit OSInitRendezvous / OSWaitRendezvous[WithTimeout]).
//
// Cafe OS keeps one arrival flag per core in the rendezvous: a waiter sets the flag of the core it
// runs on (OSGetCoreId), then waits until the flag of every core in its mask (bits 0-2) is set.
// Arrivals are per core, not a count: two threads on the same core are one arrival, and a caller
// whose own core is not in the mask still marks its core. Nothing clears the flags after a wait
// (also not on timeout); only OSInitRendezvous does.
//
// threads.cpp holds this behind the HLE's mutex and parks waiters on a condition variable; the
// logic is here so runtime/tools/rendezvous_test.cpp can check it without the guest runtime.
#pragma once
#include <cstdint>

namespace threads {
struct RendezvousCores {
    static constexpr uint32_t kCoreMask = 7;  // Espresso cores 0-2
    uint32_t arrived = 0;                     // bit n: core n has called OSWaitRendezvous since init

    void reset() { arrived = 0; }
    void arrive(uint32_t core) {
        if (core <= 2) arrived |= 1u << core;
    }
    bool complete(uint32_t mask) const { return (arrived & (mask & kCoreMask)) == (mask & kCoreMask); }
};
}  // namespace threads
