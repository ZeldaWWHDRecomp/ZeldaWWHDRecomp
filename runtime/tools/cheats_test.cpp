// Run the production cheat service against synthetic guest memory. No game files.
#include "runtime.h"
#include "mods/mods.h"
#include <array>
#include <cassert>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*, ...) {}
namespace ss { void notice(const std::string&) {} }
static void map(uint32_t address, size_t size) {
#ifdef _WIN32
    auto p = VirtualAlloc(mem::ptr(address), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto p = mmap(mem::ptr(address), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(p == mem::ptr(address));
}
int main() {
    map(0x101F0000, 0x10000);
    map(0x10470000, 0x10000);
    constexpr uint32_t base = 0x15000000, player = base + 0x20;
    map(base, 0x10000);
    st32(0x101F84DC, base);
    std::memcpy(mem::ptr(0x104741E4), "Atorizk", 8);
    // Each earned sword phase, including before Medli and between the two temples.
    for (uint8_t sword : {1, 3, 7, 15}) {
        std::memset(mem::ptr(base), 0xA5, 0x2000);
        st16(player, 14); st16(player + 2, 14); st16(player + 4, 158);
        st8(player + 0x12, 0);
        std::memcpy(mem::ptr(player + 0x30), "sea\0\0\0\0\0", 8);
        st8(player + 0xB4, sword); st8(player + 0xB5, 1);
        std::array<uint8_t, 0x2000> expected;
        std::memcpy(expected.data(), mem::ptr(base), expected.size());
        expected[0x2E] = 0x3E; expected[0x2F] = 0x3C;
        mods::request_cheat(mods::kCheatSword);
        mods::cheats_service();
        // Includes all story flags, ownership bits, adjacent memory, and the save header.
        assert(!std::memcmp(expected.data(), mem::ptr(base), expected.size()));
        mods::cheats_service();
        assert(!std::memcmp(expected.data(), mem::ptr(base), expected.size()));
    }
}
