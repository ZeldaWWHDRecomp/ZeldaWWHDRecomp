// Tests the actual mod/input state machine against synthetic guest fields, no game files.
#include "mods/fast_forward.h"
#include "game_clock.h"
#include "runtime.h"
#include <cassert>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
namespace { bool blocked = false; }
namespace overlay { bool blocks_input() { return blocked; } }
namespace timebase { void set_clock_rate(unsigned r) { game_clock::set_rate(r); } }
namespace mods { void trace(const char*, ...) {} }
namespace audio { void flush() {} }
void map(uint32_t address) {
#ifdef _WIN32
    auto p = VirtualAlloc(mem::ptr(address), 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto p = mmap(mem::ptr(address), 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(p == mem::ptr(address));
}
int main(int argc, char** argv) {
    if (argc == 6) {
        assert(mods::fast_forward() == (std::atoi(argv[2]) != 0));
        assert(mods::fast_forward_rate() == unsigned(std::atoi(argv[3])));
        assert(mods::fast_forward_button() == uint32_t(std::strtoul(argv[4], nullptr, 0)));
        assert(mods::fast_forward_mute() == (std::atoi(argv[5]) != 0));
        return 0;
    }
    map(0x10470000); map(0x101E0000); map(0x101F0000);
    constexpr uint32_t play = 0x1046F0B0;
    using namespace mods;
    assert(!fast_forward() && fast_forward_rate() == 2 && fast_forward_button() == 0x40 && fast_forward_mute());
    memcpy(mem::ptr(play + 0x5134), "sea", 4);
    st32(play + 0x5B34, 0x11000000);
    st8(play + 0x5292, 1);
    auto input = [](uint32_t b) { fast_forward_input(b); return b; };
    assert(input(0x8040) == 0x8040); fast_forward_service(); assert(!fast_forward_active());
    set_fast_forward(true);
    assert(input(0x8040) == 0x8000); fast_forward_service(); assert(fast_forward_active() && game_clock::rate() == 2);
    set_fast_forward_rate(4); fast_forward_service(); assert(game_clock::rate() == 4);
    assert(input(0x8000) == 0x8000); fast_forward_service(); assert(!fast_forward_active() && game_clock::rate() == 1);
    for (unsigned event = 0; event <= 4; ++event) {
        st8(play + 0x5292, event);
        input(0x40); fast_forward_service(); assert(fast_forward_active() == (event >= 1 && event <= 3));
    }
    st8(play + 0x5292, 2); input(0x40); fast_forward_service(); assert(fast_forward_active());
    fast_forward_reset(); assert(game_clock::rate() == 1);
    input(0x40); fast_forward_service(); assert(!fast_forward_active()); // save/load must not latch boost
    input(0); input(0x40); fast_forward_service(); assert(fast_forward_active());
    blocked = true; fast_forward_service(); assert(!fast_forward_active());
    blocked = false;
    for (const char* stage : {"sea_T", "Name", "ENDumi", ""}) {
        memset(mem::ptr(play + 0x5134), 0, 8); memcpy(mem::ptr(play + 0x5134), stage, strlen(stage));
        input(0x40); fast_forward_service(); assert(!fast_forward_active());
    }
    memcpy(mem::ptr(play + 0x5134), "sea", 4);
    st8(play + 0x5140 + 12, 1); input(0x40); fast_forward_service(); assert(!fast_forward_active());
    st8(play + 0x5140 + 12, 0);
    st32(0x101F36CC, 0x11000000); fast_forward_service(); assert(!fast_forward_active());
    st32(0x101F36CC, 0);
    st8(0x101EA069, 1); fast_forward_service(); assert(!fast_forward_active());
    st8(0x101EA069, 0); fast_forward_service(); assert(fast_forward_active());
    set_fast_forward_button(0x8000); assert(fast_forward_button() == 0x40);
    set_fast_forward_rate(0); set_fast_forward_rate(5); assert(fast_forward_rate() == 4);
    set_fast_forward(false); fast_forward_service(); assert(!fast_forward_active() && game_clock::rate() == 1);
    assert(input(0x8040) == 0x8040);
}
