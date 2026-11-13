// thread_chain.h: the full-state load check of a parked thread's guest call chain.
// The fixture is the case from a user report: JpegEncodeMgrThread (a sead worker thread) parked in
// MessageQueue::pop (lr 02760394) with the same stack pointer in the save and now, but called from
// sead::Thread::run's first pop in one and its loop pop in the other. The old check refused it with
// "is at a different place (lr 02760394 sp 3F102B48, saved lr 02760394 sp 3F102B48)", naming only the
// two values that were equal.
#include "thread_chain.h"

#include <cstdio>
#include <cstdlib>
#include <map>

static int g_fail = 0;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); g_fail++; } } while (0)

using Mem = std::map<uint32_t, uint32_t>;

static constexpr uint32_t kSp = 0x3F102B48, kRunFrame = 0x3F102B58, kEntryFrame = 0x3F102B78;
static constexpr uint32_t kPopLr = 0x02760394;                      // in MessageQueue::pop, after OSReceiveMessage
static constexpr uint32_t kFirstPop = 0x0275FFEC, kLoopPop = 0x02760020;  // in sead::Thread::run
static constexpr uint32_t kRunCaller = 0x0275F2A0;                   // run's return into the thread entry

// pop's frame -> run's frame (pop's saved LR at run+4) -> entry frame (run's saved LR) -> end of chain
static Mem stack(uint32_t pop_return) {
    return {{kSp, kRunFrame}, {kRunFrame, kEntryFrame}, {kRunFrame + 4, pop_return}, {kEntryFrame, 0}, {kEntryFrame + 4, kRunCaller}};
}

static bool equiv(uint32_t a, uint32_t b) {
    return (a == kFirstPop && b == kLoopPop) || (a == kLoopPop && b == kFirstPop);
}
static bool none(uint32_t, uint32_t) { return false; }

template <class E>
static thread_chain::Diff run(const Mem& now, const Mem& saved, E e, uint32_t sp_now = kSp, uint32_t lr_now = kPopLr,
                              uint32_t sp_saved = kSp, uint32_t lr_saved = kPopLr) {
    auto ld = [](const Mem& m) {
        return [&m](uint32_t a) { auto it = m.find(a); return it == m.end() ? 0u : it->second; };
    };
    return thread_chain::compare(sp_now, lr_now, sp_saved, lr_saved, ld(now), ld(saved), e);
}

int main() {
    const Mem first = stack(kFirstPop), loop = stack(kLoopPop);
    // same place
    CHECK(run(first, first, none).same());
    CHECK(run(loop, loop, none).same());

    // the reported case: same lr and sp, run called pop from the other site
    auto d = run(first, loop, none);
    CHECK(!d.same());
    CHECK(d.field == thread_chain::Diff::ReturnAddress && d.depth == 1 && d.addr == kRunFrame + 4);
    CHECK(d.now == kFirstPop && d.saved == kLoopPop);
    const std::string msg = thread_chain::describe("JpegEncodeMgrThread@3F102BF8", d);
    printf("%s\n", msg.c_str());
    CHECK(msg.find("caller frame 1") != std::string::npos && msg.find("3F102B5C") != std::string::npos &&
          msg.find("0275FFEC") != std::string::npos && msg.find("02760020") != std::string::npos);
    // ... equivalent continuations load, both ways
    CHECK(run(first, loop, equiv).same());
    CHECK(run(loop, first, equiv).same());

    // anything else still refused, and named
    Mem other = first;
    other[kEntryFrame + 4] = 0x0275F300;  // a different caller of run
    d = run(other, first, equiv);
    CHECK(d.field == thread_chain::Diff::ReturnAddress && d.depth == 2 && d.now == 0x0275F300 && d.saved == kRunCaller);
    Mem chain = first;
    chain[kRunFrame] = kEntryFrame + 0x10;  // a different frame layout
    d = run(chain, first, equiv);
    CHECK(d.field == thread_chain::Diff::BackChain && d.depth == 2 && d.addr == kRunFrame);
    d = run(first, first, equiv, kSp, kPopLr, kSp + 8, kPopLr);
    CHECK(d.field == thread_chain::Diff::Sp && thread_chain::describe("T", d).find("stack pointer") != std::string::npos);
    d = run(first, first, equiv, kSp, kPopLr, kSp, kPopLr + 4);
    CHECK(d.field == thread_chain::Diff::Lr);
    // an equivalent pair is accepted as the leaf return address too (a thread parked directly in run)
    CHECK(run(first, first, equiv, kSp, kFirstPop, kSp, kLoopPop).same());
    // a different place that happens to involve one of the pair's addresses is not equivalent
    d = run(stack(kFirstPop), stack(0x02760014), equiv);
    CHECK(d.field == thread_chain::Diff::ReturnAddress);

    if (g_fail) { fprintf(stderr, "%d failure(s)\n", g_fail); return 1; }
    printf("thread_chain_test: all passed\n");
    return 0;
}
