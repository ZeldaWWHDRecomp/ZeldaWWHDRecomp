// Execute the real hook sites against synthetic guest fields, without any game files.
#include "mods/mods.h"
#include "runtime.h"
#include "input.h"
#include "guest_addr.h"
#include <vector>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
extern "C" void site_024EB4A8(Cpu*);
extern "C" void site_024ECC70(Cpu*);
extern "C" void site_024EB868(Cpu*);
extern "C" void site_024EE574(Cpu*);
extern "C" void hook_024ECCFC(Cpu*);
// the decomp build's port points (port_hooks.h PORT_LOCK_ON / PORT_LOCK_ON_DISTANCE, mods/lock_on_points.cpp)
extern "C" double wwhd_lock_on(Cpu*, unsigned, unsigned, unsigned, unsigned, double*);
extern "C" void ppc_host_call(Cpu* c, PpcFunc fn) { fn(c); }
namespace {
uint64_t now = 100;
bool blocked = false;
unsigned rebuilds = 0, native_calls = 0;
struct Candidate { uint32_t pid, category; bool alive = true; };
std::vector<Candidate> candidates;
uint32_t fake_count = 0;

constexpr uint32_t actor = 0x11000000, attention = 0x11001000, rule = 0x11002000;
void map(uint32_t address) {
#ifdef _WIN32
    auto p = VirtualAlloc(mem::ptr(address), 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto p = mmap(mem::ptr(address), 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(p == mem::ptr(address));
}
Cpu arguments() {
    Cpu c{};
    c.r[25] = actor; c.r[31] = rule;
    c.f[1].ps0 = 600; c.f[2].ps0 = 100;
    c.f[3].ps0 = 200; c.f[4].ps0 = -300;
    c.r[4] = 1234; // native angle remains unchanged
    return c;
}
}
namespace mods { uint64_t step() { return now; } void trace(const char*, ...) {} }
namespace overlay { bool blocks_input() { return blocked; } }
extern "C" void f_024ECCFC_orig(Cpu* c) {
    ++native_calls;
    c->r[3] = 0x12345678; c->r[4] = 0xDEADBEEF; c->f[1].ps0 = 17;
}
uint32_t guest_call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args) {
    const std::vector<uint32_t> a(args);
    assert(!a.empty());
    if (fn == GC(0x024EC6A8)) {
        assert(a.size() == 2 && a[0] == attention && a[1] == 7);
        ++rebuilds;
        std::memset(mem::ptr(attention + 0x54), 0, 0xD0);
    } else if (fn == GC(0x024EC72C)) {
        assert(a[0] == attention && candidates.size() <= 8);
        st32(attention + 0xD4, fake_count ? fake_count : candidates.size());
        for (unsigned i = 0; i < candidates.size(); ++i) {
            st32(attention + 0x54 + i*16 + 8, candidates[i].category);
            st32(attention + 0x54 + i*16 + 12, candidates[i].pid);
        }
    } else if (fn == GC(0x024EC770)) {
        assert(a[0] == attention); // fixture is already in native priority order
    } else {
        assert(fn == GC(0x024EBA14));
        const auto i = (a[0] - attention - 0x54) / 16;
        assert(i < candidates.size());
        if (!candidates[i].alive) return 0;
        const auto ptr = actor + 0x3000 + i*0x400;
        st32(ptr + 0x39C, candidates[i].category == 2 ? 4 : 1);
        return ptr;
    }
    c->r[3] = 0xBAD; c->r[4] = 0; c->f[1].ps0 = 999; // helper register clobbers
    return c->r[3];
}
int main(int argc, char** argv) {
    if (argc == 5) {
        assert(mods::extended_lock_on() == (std::atoi(argv[2]) != 0));
        assert(mods::lock_on_range() == std::strtof(argv[3], nullptr));
        assert(mods::lock_on_cycling() == (std::atoi(argv[4]) != 0));
        return 0;
    }
#ifdef TEST_EU
    assert(GC(0x024EC6A8) == 0x024EC6AC && GC(0x024EBA14) == 0x024EBA18);
#endif
    using namespace mods;
    assert(!extended_lock_on() && lock_on_range() == 1.f && lock_on_cycling());
    // Default-off wrapper must call native judgement exactly once and not read even
    // an unmapped attention object (nor consult overlay/step/input state).
    Cpu stock{}; stock.r[3] = attention;
    Cpu expected = stock;
    f_024ECCFC_orig(&expected);
    const auto native_before = native_calls;
    hook_024ECCFC(&stock);
    assert(native_calls == native_before + 1 && rebuilds == 0);
    assert(!std::memcmp(&stock, &expected, sizeof stock));
    map(actor); map(0x10470000); map(0x101E0000); map(0x101F0000);
    st16(rule, 2); st32(actor + 0x39C, 4);
    auto c = arguments();
    set_lock_on_range(2.f); site_024EB4A8(&c);
    assert(c.f[1].ps0 == 600 && c.f[2].ps0 == 100); // off really is vanilla
    set_extended_lock_on(true); site_024EB4A8(&c);
    assert(c.f[1].ps0 == 1200 && c.f[2].ps0 == 200);
    assert(c.f[3].ps0 == 200 && c.f[4].ps0 == -300 && c.r[4] == 1234);
    // NPCs, objects and action channels keep their native interaction radius.
    for (unsigned channel : {0u, 1u, 3u, 0xFFFFu}) {
        st16(rule, channel); c = arguments(); site_024EB4A8(&c);
        assert(c.f[1].ps0 == 600 && c.f[2].ps0 == 100);
    }
    st16(rule, 2); st32(actor + 0x39C, 0);
    c = arguments(); site_024EB4A8(&c); assert(c.f[1].ps0 == 600);
    st32(actor + 0x39C, 4);
    c = arguments(); c.r[28] = attention + 0x54; c.r[30] = actor + 0x388;
    st32(c.r[28] + 8, 2); site_024ECC70(&c);
    assert(c.f[1].ps0 == 1200 && c.f[2].ps0 == 200);
    st32(c.r[28] + 8, 1); c.f[1].ps0 = 600; site_024ECC70(&c);
    assert(c.f[1].ps0 == 600);
    c = {}; c.r[31] = actor; c.f[1].ps0 = 900; c.f[13].ps0 = 700;
    site_024EB868(&c); assert(c.f[1].ps0 == 900 && c.f[13].ps0 == 1400);
    st32(actor + 0x39C, 0); c.f[13].ps0 = 700; site_024EB868(&c);
    assert(c.f[13].ps0 == 700);
    c = {}; c.r[31] = attention; c.f[1].ps0 = 700;
    st32(attention + 0xD8, 7); st32(attention + 0x54 + 7*16 + 8, 2);
    site_024EE574(&c); assert(c.f[1].ps0 == 1400);
    st32(attention + 0xD8, 8); c.f[1].ps0 = 700;
    site_024EE574(&c); assert(c.f[1].ps0 == 700); // corrupted/out-of-range index
    st32(attention + 0xD8, 7); st32(attention + 0x54 + 7*16 + 8, 0);
    site_024EE574(&c); assert(c.f[1].ps0 == 700);
    // The points: the same scaling as each site, for the values and registers the decompiled code passes, and the
    // Cpu comes back as it was (r1 included)
    {
        Cpu p{}; p.r[1] = 0x1234560; p.r[31] = 0xAAAA; p.r[25] = 0xBBBB; p.f[1].ps0 = 5; const Cpu before = p;
        st16(rule, 2); st32(actor + 0x39C, 4);
        double v[2] = {600, 100};
        assert(wwhd_lock_on(&p, 0x1234000, 0x024EB4A8, rule, actor, v) == 1200 && v[1] == 200);
        assert(!std::memcmp(&p, &before, sizeof p));
        st16(rule, 1); v[0] = 600; v[1] = 100;
        wwhd_lock_on(&p, 0x1234000, 0x024EB4A8, rule, actor, v); assert(v[0] == 600 && v[1] == 100);
        st16(rule, 2);
        st32(attention + 0x54 + 8, 2); v[0] = 600; v[1] = 100;
        wwhd_lock_on(&p, 0x1234000, 0x024ECC70, attention + 0x54, actor, v); assert(v[0] == 1200 && v[1] == 200);
        st32(attention + 0x54 + 8, 1); v[0] = 600; v[1] = 100;
        wwhd_lock_on(&p, 0x1234000, 0x024ECC70, attention + 0x54, actor, v); assert(v[0] == 600 && v[1] == 100);
        v[0] = 700; assert(wwhd_lock_on(&p, 0x1234000, 0x024EB868, actor, 0, v) == 1400);
        st32(actor + 0x39C, 0); v[0] = 700; assert(wwhd_lock_on(&p, 0x1234000, 0x024EB868, actor, 0, v) == 700);
        st32(actor + 0x39C, 4);
        st32(attention + 0xD8, 7); st32(attention + 0x54 + 7*16 + 8, 2);
        v[0] = 700; assert(wwhd_lock_on(&p, 0x1234000, 0x024EE574, attention, 0, v) == 1400);
        st32(attention + 0x54 + 7*16 + 8, 0);
        v[0] = 700; assert(wwhd_lock_on(&p, 0x1234000, 0x024EE574, attention, 0, v) == 700);
        assert(!std::memcmp(&p, &before, sizeof p));
    }
    for (float value : {0.f, -1.f, 0.5f}) { set_lock_on_range(value); assert(lock_on_range() == 1.f); }
    set_lock_on_range(99.f); assert(lock_on_range() == 3.f);
    for (float value : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
        set_lock_on_range(value); assert(lock_on_range() == 1.f);
    }
    set_lock_on_range(1.5f); assert(lock_on_range() == 1.5f);
    set_lock_on_cycling(false); assert(!lock_on_cycling());
    set_extended_lock_on(false); c = arguments(); site_024EB4A8(&c); assert(c.f[1].ps0 == 600);

    // Native-cycle fixture: three enemies interspersed with an NPC, never raw actor pointers.
    set_extended_lock_on(true); set_lock_on_cycling(true);
    candidates = {{10, 2}, {99, 0}, {20, 2}, {30, 2}};
    st8(attention + 0x18, 1); st8(attention + 0x1A, 2);
    st32(attention + 0x10, 7); st32(attention + 0xD4, 1); st32(attention + 0xD8, 0);
    st32(attention + 0x54 + 8, 2); st32(attention + 0x54 + 12, 10);
    auto judgement = [&] {
        Cpu cpu{}; cpu.r[3] = attention; cpu.r[1] = actor + 0xF000;
        Cpu expected = cpu;
        f_024ECCFC_orig(&expected);
        const auto calls = native_calls;
        hook_024ECCFC(&cpu);
        assert(native_calls == calls + 1);
        assert(!std::memcmp(&cpu, &expected, sizeof cpu)); // complete native register ABI
    };
    auto pad = [&](uint32_t buttons) {
        input::PadState s{}; s.buttons = buttons; s.rx = .7f;
        lock_on_filter_pad(s); assert(s.rx == .7f); return s.buttons;
    };
    auto selected = [&] { return ld32(attention + 0x54 + ld32(attention + 0xD8)*16 + 12); };
    pad(0); judgement();
    assert(pad(input::kStickR | input::kA) == input::kA);
    judgement(); assert(selected() == 20 && rebuilds == 1 && (ld32(attention + 0x20) & 8));
    judgement(); pad(input::kStickR); judgement(); assert(rebuilds == 1); // a hold is one cycle
    pad(0); pad(input::kStickR); judgement(); assert(selected() == 30 && rebuilds == 2);
    pad(0); pad(input::kStickR); judgement(); assert(selected() == 10 && rebuilds == 3); // wrap
    // No live alternate: retain both lists/index exactly, including a release-range-only target.
    unsigned char before[0xD0]; std::memcpy(before, mem::ptr(attention + 0x54), sizeof before);
    candidates = {{10, 2}, {99, 0}, {UINT32_MAX, 2}, {20, 2, false}};
    pad(0); pad(input::kStickR); judgement();
    assert(!std::memcmp(before, mem::ptr(attention + 0x54), sizeof before));
    // Missing old target in a rebuilt list selects the first remaining eligible live enemy.
    candidates = {{99, 1}, {20, 2}, {30, 2}};
    pad(0); pad(input::kStickR); judgement(); assert(selected() == 20);
    // Corrupt/unexpected native counts must not permit an out-of-bounds scan.
    std::memcpy(before, mem::ptr(attention + 0x54), sizeof before);
    fake_count = 9; pad(0); pad(input::kStickR); judgement(); fake_count = 0;
    assert(!std::memcmp(before, mem::ptr(attention + 0x54), sizeof before));
    const auto count_before = rebuilds;
    // A simultaneous Z-target edge, pause, disabling cycling or reset does not replay the request.
    st8(attention + 0x1A, 1); pad(0); pad(input::kStickR); judgement();
    assert(rebuilds == count_before); st8(attention + 0x1A, 2);
    pad(0); blocked = true; assert(pad(input::kStickR) == input::kStickR); judgement();
    blocked = false; pad(0); judgement();
    pad(input::kStickR); lock_on_reset(); judgement(); assert(rebuilds == count_before);
    pad(input::kStickR); judgement(); assert(rebuilds == count_before);
    pad(0); judgement(); pad(input::kStickR); judgement(); assert(rebuilds == count_before + 1);
    set_lock_on_cycling(false); pad(0); judgement();
    assert(pad(input::kStickR) == input::kStickR);
    set_lock_on_cycling(true); pad(0); judgement();
    now += 3; assert(pad(input::kStickR) == input::kStickR); judgement(); // stale context expires
    const auto stale_count = rebuilds;
    pad(0); judgement(); pad(input::kStickR); now += 3; judgement(); // stale press expires too
    assert(rebuilds == stale_count);
    // NPC locks, released locks and invalid indices leave the original first-person R3 alone.
    for (unsigned state : {0u, 2u}) {
        st8(attention + 0x18, state); pad(0); judgement();
        assert(pad(input::kStickR) == input::kStickR);
    }
    st8(attention + 0x18, 1); st32(attention + 0xD8, 8); pad(0); judgement();
    assert(pad(input::kStickR) == input::kStickR);
    st32(attention + 0xD8, 0); st32(attention + 0x54 + 8, 0); pad(0); judgement();
    assert(pad(input::kStickR) == input::kStickR);
    // Exercise all eight slots, not just the first pair, then wrap to the first enemy again.
    candidates.clear(); for (unsigned i = 0; i < 8; ++i) candidates.push_back({100 + i, 2});
    st32(attention + 0xD4, 1); st32(attention + 0xD8, 0);
    st32(attention + 0x54 + 8, 2); st32(attention + 0x54 + 12, 100);
    pad(0); judgement();
    for (unsigned i = 1; i <= 8; ++i) {
        pad(0); pad(input::kStickR); judgement(); assert(selected() == 100 + i % 8);
    }
    // An overlay that opens after the press cancels it, including on the resume frame.
    auto last_rebuild = rebuilds;
    pad(0); pad(input::kStickR); blocked = true; judgement(); blocked = false; judgement();
    assert(rebuilds == last_rebuild);
    // Native event, pause and transition gates cancel a queued press even if logic time freezes.
    const uint32_t play = GD(0x1046F0B0);
    for (auto address : {play + 0x5292, play + 0x514C, GD(0x101EA069)}) {
        pad(0); judgement(); pad(input::kStickR); st8(address, 1);
        pad(input::kStickR); st8(address, 0); judgement(); assert(rebuilds == last_rebuild);
    }
    pad(0); judgement(); pad(input::kStickR); st32(GD(0x101F36CC), 1);
    pad(input::kStickR); st32(GD(0x101F36CC), 0); judgement(); assert(rebuilds == last_rebuild);
    // A pending press is bound to the attention object that published it (scene changes).
    pad(0); judgement(); pad(input::kStickR);
    std::memcpy(mem::ptr(attention + 0x500), mem::ptr(attention), 0x124);
    Cpu other{}; other.r[3] = attention + 0x500; hook_024ECCFC(&other);
    judgement(); assert(rebuilds == last_rebuild);
    // A save/load reset disarms a queued press; release is required for the next one.
    pad(0); judgement(); pad(input::kStickR); lock_on_reset(); judgement();
    assert(rebuilds == last_rebuild);
    pad(input::kStickR); judgement(); assert(rebuilds == last_rebuild);
    pad(0); judgement(); pad(input::kStickR); judgement(); assert(rebuilds == last_rebuild + 1);
    // Disabling with a queued press cancels it; a held R3 cannot replay on re-enable.
    last_rebuild = rebuilds;
    pad(0); judgement(); pad(input::kStickR);
    set_extended_lock_on(false); judgement();
    assert(rebuilds == last_rebuild);
    set_extended_lock_on(true);
    pad(input::kStickR); judgement(); assert(rebuilds == last_rebuild);
    pad(0); judgement(); pad(input::kStickR); judgement(); assert(rebuilds == last_rebuild + 1);
    // The default 1x path is numerically identical even with the assistance enabled.
    st32(actor + 0x39C, 4); set_lock_on_range(1.f); c = arguments(); site_024EB4A8(&c);
    assert(c.f[1].ps0 == 600 && c.f[2].ps0 == 100);
}
