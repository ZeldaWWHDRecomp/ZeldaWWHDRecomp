// Jump anywhere (jump.cpp) against synthetic guest memory: the guest calls it makes. No game files.
#include "runtime.h"
#include "mods/mods.h"
#include "input.h"
#include <cassert>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*, ...) {}
namespace true60 { float test_dt = 1; float dt() { return test_dt; } }
namespace mods { static int moon = 0; bool infinite(int which) { return moon & which; } }
static std::vector<uint32_t> calls;
uint32_t guest_call(Cpu* cpu, uint32_t address, std::initializer_list<uint32_t> args) {
    const uint32_t link = *args.begin();
    calls.push_back(address);
    if (address == 0x023F7958) { st32(link + 0x65F0, 0x24); stf32(link + 0x340, 12.5f); }  // procAutoJump_init
    else if (address == 0x023F6564) st32(link + 0x65F0, 0x27);                               // procFall_init
    else assert(false);
    cpu->r[3] = 0xBAD;  // the mod must restore the caller's registers
    return 0;
}
static void map(uint32_t address, size_t size) {
#ifdef _WIN32
    auto p = VirtualAlloc(mem::ptr(address), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto p = mmap(mem::ptr(address), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(p == mem::ptr(address));
}
int main() {
    map(0x10470000, 0x10000);
    constexpr uint32_t link = 0x16000000;
    map(link, 0x10000);
    std::memcpy(mem::ptr(0x1046F0B0 + 0x5134), "sea\0\0\0\0", 8);
    Cpu cpu{}; cpu.r[3] = link;
    st32(link + 0x65F0, 0x04);  // standing
    st32(link + 0x834, 0xFF);
    // off: nothing happens
    mods::jump_input(input::kVirtJump);
    mods::jump_step(&cpu, link, true); mods::jump_step(&cpu, link, false);
    assert(calls.empty());
    mods::set_jump_anywhere(true);
    mods::jump_input(0);
    mods::jump_step(&cpu, link, true); mods::jump_step(&cpu, link, false);
    mods::jump_input(input::kVirtJump);
    true60::test_dt = .5f;  // a true 60 preview never starts a jump
    mods::jump_step(&cpu, link, true); mods::jump_step(&cpu, link, false);
    assert(calls.empty());
    true60::test_dt = 1;
    mods::jump_step(&cpu, link, true);
    assert(calls.size() == 1 && calls[0] == 0x023F7958 && cpu.r[3] == link);
    assert(ldf32(link + 0x340) == 16.f && (ld32(link + 0x834) & 0xE0) == 0);
    mods::jump_step(&cpu, link, false);
    assert(calls.size() == 1);  // our jump is never turned into a fall
    // the game's own ledge jump with "no automatic ledge jumps": a fall
    mods::set_no_auto_jump(true);
    mods::jump_input(0);
    st32(link + 0x65F0, 0x06);
    mods::jump_step(&cpu, link, true); mods::jump_step(&cpu, link, false);
    mods::jump_step(&cpu, link, true);
    st32(link + 0x65F0, 0x24);  // the game's ledge check started procAutoJump in this step
    stf32(link + 0x340, 20.f);
    mods::jump_step(&cpu, link, false);
    assert(calls.size() == 2 && calls[1] == 0x023F6564 && ld32(link + 0x65F0) == 0x27 && ldf32(link + 0x340) == 0.f);
    // within the grace window the button still jumps from the ground
    mods::jump_input(input::kVirtJump);
    mods::jump_step(&cpu, link, true);
    assert(calls.size() == 3 && calls[2] == 0x023F7958 && ldf32(link + 0x340) == 16.f);
    mods::jump_step(&cpu, link, false);
    // events block it; the moon jump on L wins
    mods::jump_input(0);
    st32(link + 0x65F0, 0x04);
    mods::jump_step(&cpu, link, true); mods::jump_step(&cpu, link, false);
    st8(0x1046F0B0 + 0x5292, 1);
    mods::jump_input(input::kVirtJump);
    mods::jump_step(&cpu, link, true); mods::jump_step(&cpu, link, false);
    assert(calls.size() == 3);
    st8(0x1046F0B0 + 0x5292, 0);
    // the moon jump held with it wins (one input for both: L1 by default)
    mods::moon = mods::kMoonJump;
    mods::jump_input(0); mods::jump_step(&cpu, link, true);
    mods::jump_input(input::kVirtJump | input::kVirtMoonJump); mods::jump_step(&cpu, link, true);
    assert(calls.size() == 3);
    // on another input both work; the game's L is never the jump any more
    mods::jump_input(0); mods::jump_step(&cpu, link, true);
    mods::jump_input(input::kVirtJump); mods::jump_step(&cpu, link, true);
    assert(calls.size() == 4);
    mods::jump_step(&cpu, link, false);
    // True 60: a press that begins on a half step (preview, dt < 1) and is let go before the next full
    // step still jumps there, once; one held through several full steps jumps once
    mods::moon = 0;
    st32(link + 0x65F0, 0x04);
    mods::jump_input(0); mods::jump_step(&cpu, link, true); mods::jump_step(&cpu, link, false);
    true60::test_dt = .5f;
    mods::jump_input(input::kVirtJump);  // the half step's read
    mods::jump_step(&cpu, link, true); mods::jump_step(&cpu, link, false);
    assert(calls.size() == 4);  // the preview starts nothing
    true60::test_dt = 1;
    mods::jump_input(0);  // let go before the full step's read
    mods::jump_step(&cpu, link, true);
    assert(calls.size() == 5 && calls[4] == 0x023F7958);
    mods::jump_step(&cpu, link, false);
    st32(link + 0x65F0, 0x04);
    for (int i = 0; i < 3; i++) {  // nothing more without a new press
        mods::jump_input(0); mods::jump_step(&cpu, link, true); mods::jump_step(&cpu, link, false);
    }
    assert(calls.size() == 5);
    true60::test_dt = .5f;
    mods::jump_input(input::kVirtJump);
    mods::jump_step(&cpu, link, true); mods::jump_step(&cpu, link, false);
    true60::test_dt = 1;
    for (int i = 0; i < 3; i++) {  // held on through three full steps (and their half steps): one jump
        mods::jump_input(input::kVirtJump); mods::jump_step(&cpu, link, true); mods::jump_step(&cpu, link, false);
        st32(link + 0x65F0, 0x04);
        true60::test_dt = .5f;
        mods::jump_input(input::kVirtJump); mods::jump_step(&cpu, link, true); mods::jump_step(&cpu, link, false);
        true60::test_dt = 1;
    }
    assert(calls.size() == 6);
    return 0;
}
