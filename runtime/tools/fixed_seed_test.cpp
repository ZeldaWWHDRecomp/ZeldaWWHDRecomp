// WWHD_TEST_FIXED_SEED (runtime/src/fixed_seed.h): the switch's parsing, what apply() writes (the
// three cM_rnd seeds, the particle seed and g_Counter.mCounter0 = mTimer + offset, nothing else, wrapping like the
// game's u32 counters), the boot gate (once, at the first gameplay frame, not after a load) and the
// portable-load gate (loading frames begin, new stage's first frame, time-out) and the scripted-frame
// rebase (TV frames counted from the first gameplay frame).
#include "../src/fixed_seed.h"

#include <cstdint>
#include <cstdio>
#include <map>

static int g_failed = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                                   \
        }                                                                                 \
    } while (0)

namespace {
constexpr uint32_t kSeedsEa = 0x101FF9D4, kParticleSeedEa = 0x104B5730, kCounter0Ea = 0x101FF558, kTimerEa = 0x101FF560;

struct Memory {
    std::map<uint32_t, uint32_t> words;
    uint32_t load(uint32_t ea) { return words.count(ea) ? words[ea] : 0; }
    void store(uint32_t ea, uint32_t v) { words[ea] = v; }
};

fixed_seed::Applied run_apply(Memory& m) {
    return fixed_seed::apply([&](uint32_t ea) { return m.load(ea); }, [&](uint32_t ea, uint32_t v) { m.store(ea, v); }, kSeedsEa,
                             kParticleSeedEa, kCounter0Ea, kTimerEa);
}

void test_switch() {
    CHECK(!fixed_seed::enabled_value(nullptr));
    CHECK(!fixed_seed::enabled_value(""));
    CHECK(!fixed_seed::enabled_value("0"));
    CHECK(fixed_seed::enabled_value("1"));
    CHECK(fixed_seed::enabled_value("yes"));
    CHECK(fixed_seed::enabled_value("00"));  // only a lone "0" means off
}

void test_apply() {
    Memory m;
    m.store(kSeedsEa, 0x1234);
    m.store(kSeedsEa + 4, 0x5678);
    m.store(kSeedsEa + 8, 0x9ABC);
    m.store(kParticleSeedEa, 0xC0FFEE);
    m.store(kCounter0Ea, 5000);
    m.store(kTimerEa, 3091);
    m.store(kSeedsEa + 12, 0xDEAD);     // neighbours stay untouched
    m.store(kCounter0Ea + 4, 0xBEEF);   // g_Counter.mCounter1
    const auto a = run_apply(m);
    CHECK(a.seeds_before[0] == 0x1234 && a.seeds_before[1] == 0x5678 && a.seeds_before[2] == 0x9ABC);
    CHECK(a.counter0_before == 5000 && a.timer == 3091 && a.particle_seed_before == 0xC0FFEE);
    CHECK(m.load(kParticleSeedEa) == fixed_seed::kParticleSeed);
    for (int i = 0; i < 3; i++) CHECK(m.load(kSeedsEa + 4 * i) == fixed_seed::kSeeds[i]);
    CHECK(m.load(kCounter0Ea) == 3091 + fixed_seed::kCounterOffset);
    CHECK(a.counter0_after == m.load(kCounter0Ea));
    CHECK(m.load(kTimerEa) == 3091);
    CHECK(m.load(kSeedsEa + 12) == 0xDEAD);
    CHECK(m.load(kCounter0Ea + 4) == 0xBEEF);
    CHECK(m.words.size() == 8);

    // two runs whose boots took different numbers of frames end up in the same state
    Memory other = m;
    other.store(kSeedsEa, 77);
    other.store(kParticleSeedEa, 12345);
    other.store(kCounter0Ea, 4990);
    run_apply(other);
    CHECK(other.words == m.words);

    // u32 wrap
    Memory w;
    w.store(kTimerEa, 0xFFFFFFF0u);
    run_apply(w);
    CHECK(w.load(kCounter0Ea) == uint32_t(0xFFFFFFF0u + fixed_seed::kCounterOffset));
}

void test_boot_gate() {
    fixed_seed::BootGate g;
    CHECK(!g.due(0));  // still booting
    CHECK(!g.due(0));
    CHECK(g.due(1));   // first gameplay frame
    CHECK(!g.due(2));  // once
    fixed_seed::BootGate after_load;
    after_load.mark();  // a load applied it before gameplay
    CHECK(!after_load.due(1));
}
void test_load_gate() {
    using G = fixed_seed::LoadGate;
    G g;
    CHECK(g.step(10) == G::kNone);  // idle
    g.start(100);                   // applied at the boundary after gameplay frame 100
    for (uint32_t t = 101; t <= 128; t++) CHECK(g.step(t) == G::kNone);  // the old stage fades out
    CHECK(g.step(128) == G::kLoading);                                    // mTimer stopped: loading
    for (int i = 0; i < 35; i++) CHECK(g.step(128) == G::kNone);         // loading frames
    CHECK(g.step(129) == G::kArrived);                                    // the new stage's first frame
    CHECK(g.step(130) == G::kNone);                                       // once
    CHECK(g.step(130) == G::kNone);

    G stuck;  // no stage change: gives up
    stuck.start(5);
    G::Event e = G::kNone;
    uint32_t t = 5, n = 0;
    while (e == G::kNone && n < 10000) e = stuck.step(++t), n++;
    CHECK(e == G::kTimedOut);
    CHECK(n == G::kMaxFrames + 1);
    CHECK(stuck.step(t) == G::kNone);

    G long_load;  // a loading phase longer than the limit gives up too
    long_load.start(7);
    CHECK(long_load.step(7) == G::kLoading);
    e = G::kNone;
    n = 0;
    while (e == G::kNone && n < 10000) e = long_load.step(7), n++;
    CHECK(e == G::kTimedOut);
}
void test_rebase() {
    using fixed_seed::kBootFrame;
    using fixed_seed::rebase;
    CHECK(rebase(50, -1) == 50);      // first gameplay frame not seen: TV frames
    CHECK(rebase(5000, -1) == 5000);
    // boot took 87 TV frames in one run and 93 in another: scripted frames agree from there on
    CHECK(rebase(87, 87) == kBootFrame);
    CHECK(rebase(93, 93) == kBootFrame);
    CHECK(rebase(3400 - kBootFrame + 87, 87) == 3400);
    CHECK(rebase(3400 - kBootFrame + 93, 93) == 3400);
    CHECK(rebase(80, 87) == 80);      // before it: TV frames
    CHECK(rebase(120, 120) == kBootFrame);  // a long boot: the count steps back once, then rises
    CHECK(rebase(121, 120) == kBootFrame + 1);
}
}  // namespace

int main() {
    test_switch();
    test_apply();
    test_boot_gate();
    test_load_gate();
    test_rebase();
    if (g_failed) {
        std::fprintf(stderr, "fixed_seed_test: %d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("fixed_seed_test: ok\n");
    return 0;
}
