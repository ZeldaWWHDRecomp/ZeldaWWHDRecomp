// Full save state load check: is a parked guest thread at the same place as in the save?
// (threads.cpp, threads_ss_check). Pure logic over two memory readers, so it can be tested alone
// (runtime/tools/thread_chain_test.cpp).
//
// A parked thread keeps its host call stack across a load: only its registers and guest memory are
// put back. So its guest call chain (stack pointer, return address, and the back chain of saved
// return addresses) must be the one the host stack is in. Two return addresses that differ are still
// the same place when they are a known pair of equivalent continuations: two call sites of the same
// callee inside one function whose code after the call is the same up to scratch registers, so the
// host continuing after one of them runs exactly what the saved one would have run (the callee's
// epilogue takes its return address from guest memory, which the host return ignores).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace thread_chain {

struct Diff {
    enum Field { None, Sp, Lr, BackChain, ReturnAddress } field = None;
    int depth = 0;       // 0: the thread's registers; n: the n-th caller frame
    uint32_t addr = 0;   // guest address of the differing stack word (BackChain / ReturnAddress)
    uint32_t now = 0, saved = 0;
    bool same() const { return field == None; }
};

// Equiv(a, b): return addresses a and b continue identically (symmetric; a != b)
template <class LdNow, class LdSaved, class Equiv>
Diff compare(uint32_t sp_now, uint32_t lr_now, uint32_t sp_saved, uint32_t lr_saved, LdNow ld_now, LdSaved ld_saved,
             Equiv equiv) {
    Diff d;
    if (sp_now != sp_saved) { d.field = Diff::Sp; d.now = sp_now; d.saved = sp_saved; return d; }
    if (lr_now != lr_saved && !equiv(lr_now, lr_saved)) { d.field = Diff::Lr; d.now = lr_now; d.saved = lr_saved; return d; }
    uint32_t a = sp_now;
    for (int i = 1; i <= 512; i++) {
        uint32_t na = ld_now(a), nb = ld_saved(a);
        if (na != nb) { d.field = Diff::BackChain; d.depth = i; d.addr = a; d.now = na; d.saved = nb; return d; }
        if (!na || na <= a || na - a > 0x100000) break;
        uint32_t ra = ld_now(na + 4), rb = ld_saved(na + 4);
        if (ra != rb && !equiv(ra, rb)) {
            d.field = Diff::ReturnAddress; d.depth = i; d.addr = na + 4; d.now = ra; d.saved = rb;
            return d;
        }
        a = na;
    }
    return d;
}

// "thread NAME is at a different place (...)": names the field that differs
inline std::string describe(const std::string& name, const Diff& d) {
    char buf[240];
    switch (d.field) {
    case Diff::Sp:
        snprintf(buf, sizeof buf, "thread %s is at a different place (stack pointer %08X, saved %08X)", name.c_str(), d.now, d.saved);
        break;
    case Diff::Lr:
        snprintf(buf, sizeof buf, "thread %s is at a different place (return address %08X, saved %08X)", name.c_str(), d.now, d.saved);
        break;
    case Diff::BackChain:
        snprintf(buf, sizeof buf, "thread %s is at a different place (caller frame %d: back chain at %08X is %08X, saved %08X)",
                 name.c_str(), d.depth, d.addr, d.now, d.saved);
        break;
    case Diff::ReturnAddress:
        snprintf(buf, sizeof buf, "thread %s is at a different place (caller frame %d: return address at %08X is %08X, saved %08X)",
                 name.c_str(), d.depth, d.addr, d.now, d.saved);
        break;
    default:
        snprintf(buf, sizeof buf, "thread %s is at the same place", name.c_str());
    }
    return buf;
}

}  // namespace thread_chain
