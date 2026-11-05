// Exercise the production hook with synthetic guest memory. No game data required.
#include "mods/mods.h"
#include "mods/targeting_letterbox.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
extern "C" void site_024FC080(Cpu*);
extern "C" void site_024FF980(Cpu*);
extern "C" void hook_02515280(Cpu*);
extern "C" void hook_024FBF9C(Cpu*);
extern "C" void site_024FF4C0(Cpu*);
extern "C" void site_024FF4E4(Cpu*);
namespace {
uint64_t now = 10;
constexpr uint32_t camera = 0x11000000;
bool table_write = true;
void map(uint32_t address) {
    address &= ~65535u;
#ifdef _WIN32
    auto p = VirtualAlloc(mem::ptr(address), 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto p = mmap(mem::ptr(address), 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(p == mem::ptr(address));
}
}
namespace mods {
uint64_t step() { return now; }
double game_time() { return now / 30.0; }
bool trace_on() { return false; }
void trace(const char*, ...) {}
}
extern "C" void f_02515280_orig(Cpu* c) { st32(c->r[3] + 0x600, c->r[4]); }
extern "C" void f_024FBF9C_orig(Cpu* c) {
    if (table_write) { c->r[31] = c->r[3]; c->r[3] = 1; site_024FC080(c); }
}
int main(int argc, char** argv) {
    if (argc > 1) { assert(mods::targeting_letterbox_mode() == std::atoi(argv[1])); return 0; }
    assert(mods::letterbox_while_targeting());
    map(camera); map(GD(0x1046F0B0) + 0x5292);
    const uint32_t play = GD(0x1046F0B0);
    st32(camera + 0x604, ~0u);
    st32(camera + 0x12C, camera + 0x1000);
    st8(play + 0x5804 + 0x18, 1);
    st32(play + 0x5804 + 0xD4, 1);
    st32(play + 0x5804 + 0x54 + 12, 42);
    st32(camera + 0x1000 + 4, 42);
    for (int policy : {0, 1, 2}) for (unsigned size = 0; size <= 4; ++size)
    for (unsigned mode = 0; mode <= 19; ++mode) for (unsigned flags = 0; flags < 64; ++flags) {
        const bool event = flags & 1, scripted = flags & 2, minigame = flags & 4;
        const bool target = flags & 8, provenance = flags & 16, attention = flags & 32;
        st32(play + 0x5824, attention ? 0x20000000u : 0);
        st32(camera + 0x600, size); st32(camera + 0x13C, mode);
        st8(play + 0x5292, event); st32(play + 0x52E4, scripted);
        st8(play + 0x5CEA, minigame); st32(camera + 0x12C, target ? camera + 0x1000 : 0);
        mods::set_targeting_letterbox_mode(policy);
        Cpu c{}; c.r[3] = camera; table_write = provenance;
        hook_024FBF9C(&c);
        c.r[31] = camera; c.f[11].ps0 = c.f[11].ps1 = 48;
        Cpu before = c;
        site_024FF980(&c);
        const bool enemy = mode == 2 && target;
        const bool free = mode == 1 && !target && attention;
        const bool suppress = policy != 0 && size == 1 && !event && !scripted && !minigame && provenance &&
                              (enemy || (policy == 2 && free));
        if (suppress) before.f[11].ps0 = before.f[11].ps1 = 0;
        assert(std::memcmp(&before, &c, sizeof(c)) == 0);
        assert(ld32(camera + 0x600) == size);
        // Native size-to-height mapping, including the existing event suppression.
        const float old = 32, cinema = 80;
        auto native = [&](float vista) {
            if (size == 4) return 0.f;
            if (event || size == 0) return old * .75f;
            if (size == 3) return cinema;
            const float goal = size == 1 ? vista : cinema;
            return std::fma(goal - old, .25f, old);
        };
        const float actual = native(float(c.f[11].ps0));
        assert(actual == (suppress ? old * .75f : native(48)));
    }
    // Explicit trim overwrites and advancing a logic step invalidate provenance.
    st32(camera + 0x600, 1); st32(camera + 0x13C, 2);
    st8(play + 0x5292, 0); st32(play + 0x52E4, 0); st8(play + 0x5CEA, 0);
    st32(camera + 0x12C, camera + 0x1000); mods::set_letterbox_while_targeting(false);
    for (bool explicit_write : {false, true}) {
        Cpu c{}; c.r[3] = camera; table_write = true; hook_024FBF9C(&c);
        if (explicit_write) { c.r[3] = camera; c.r[4] = 1; hook_02515280(&c); }
        else ++now;
        c.r[31] = camera; c.f[11].ps0 = 48; site_024FF980(&c); assert(c.f[11].ps0 == 48);
    }
    for (auto event_write : {site_024FF4C0, site_024FF4E4}) {
        Cpu c{}; c.r[3] = camera; hook_024FBF9C(&c);
        c.r[5] = 1; event_write(&c);
        c.f[11].ps0 = 48; site_024FF980(&c); assert(c.f[11].ps0 == 48);
    }
    for (bool scripted : {false, true}) {
        Cpu c{}; c.r[3] = camera; hook_024FBF9C(&c);
        st8(play + 0x581C, scripted ? 1 : 0);
        st32(camera + 0x510, scripted ? 0x20000000 : 0);
        c.f[11].ps0 = 48; site_024FF980(&c); assert(c.f[11].ps0 == 48);
    }
    st8(play + 0x581C, 1); st32(camera + 0x510, 0);
    // A returning boomerang/forced target is not the attention system's selected actor.
    for (unsigned state : {1u, 2u}) {
        st8(play + 0x581C, state);
        st32(play + 0x5804 + 0x54 + 12, 99);
        Cpu c{}; c.r[3] = camera; hook_024FBF9C(&c);
        c.f[11].ps0 = 48; site_024FF980(&c); assert(c.f[11].ps0 == 48);
    }
    st8(play + 0x581C, 1); st32(play + 0x5804 + 0x54 + 12, 42);
    // A live toggle changes the next height result while the size stays at 1.
    float height = 48;
    for (unsigned i = 0; i < 8; ++i) {
        mods::set_letterbox_while_targeting(i < 2 || i >= 6);
        Cpu c{}; c.r[3] = camera; hook_024FBF9C(&c);
        c.f[11].ps0 = 48; site_024FF980(&c);
        const float previous = height;
        height = std::fma(float(c.f[11].ps0) - height, .25f, height);
        assert(height == ((i >= 2 && i < 6) ? previous * .75f : std::fma(48 - previous, .25f, previous)));
        assert(ld32(camera + 0x600) == 1);
    }
    return 0;
}
