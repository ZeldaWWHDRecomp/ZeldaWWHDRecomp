// crash_addr::describe names the game function of an address in game code (set_game_functions).
#include "crash_addr.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <utility>

extern "C" __attribute__((noinline)) int fake_game_a(int x) { return x * 3 + 1; }
extern "C" __attribute__((noinline)) int fake_game_b(int x) { return x * 5 + 2; }

int main() {
    std::pair<uintptr_t, uint32_t> f[2] = {{(uintptr_t)&fake_game_a, 0x0200EDC8u}, {(uintptr_t)&fake_game_b, 0x0200EE00u}};
    std::sort(f, f + 2);
    static uintptr_t host[2];
    static uint32_t guest[2];
    for (int i = 0; i < 2; i++) host[i] = f[i].first, guest[i] = f[i].second;
    char buf[512];
    // before the table is set: module only
    int n = crash_addr::describe(buf, sizeof buf, host[0] + 4);
    assert(n > 0 && !strstr(buf, "game function"));
    crash_addr::set_game_functions(host, guest, 2);
    n = crash_addr::describe(buf, sizeof buf, host[0] + 4);
    char want[64];
    snprintf(want, sizeof want, "[game function %08X+0x4]", guest[0]);
    assert(n > 0 && strstr(buf, want));
    n = crash_addr::describe(buf, sizeof buf, host[1]);
    snprintf(want, sizeof want, "[game function %08X+0x0]", guest[1]);
    assert(strstr(buf, want));
    // far past the last game function: not game code
    n = crash_addr::describe(buf, sizeof buf, host[1] + 0x50000);
    assert(!strstr(buf, "game function"));
    puts("crash_addr_test: ok");
    return 0;
}
