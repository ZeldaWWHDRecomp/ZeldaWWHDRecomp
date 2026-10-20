// SPDX-License-Identifier: MPL-2.0
// Mod demos, time changes (story_demo_light.cpp): while a mod demo runs, the game's time change
// (dKy_instant_timechg) only sets the time, even backwards past midnight, and the day change
// (dKankyo_DayProc: letters, daily counters, per-day event bits) never runs; the time and date from
// before the demo come back at its end. Without a mod demo both are the game's own.
#include "runtime.h"
#include "mods/story_demo_parts.h"

#include <cassert>
#include <cstdio>
#include <map>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

void log_msg(const char*, ...) {}
namespace {
constexpr uint32_t kSaveWord = 0x101F84DC, kTimeChange = 0x0256076C, kDayProc = 0x025542BC, kEnvLight = 0x02555D0C;
constexpr uint32_t Area = 0x15000000, Save = Area, Env = Area + 0x1000, Record = Area + 0x3000, Scratch = Area + 0x3800;
std::map<uint32_t, void (*)(Cpu*)> hooks;
std::map<uint32_t, unsigned> originals;
void map(uint32_t address, size_t size) {
#ifdef _WIN32
    auto p = VirtualAlloc(mem::ptr(address), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto p = mmap(mem::ptr(address), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
#endif
    assert(p == mem::ptr(address));
}
// the game's own: a backwards change counts a day and runs the day change
void original(Cpu* c, uint32_t fn) {
    ++originals[fn];
    if (fn == kTimeChange) {
        const float t = float(c->f[1].ps0);
        if (t < u32_as_f32(ld32(Save + 0x44))) { st16(Save + 0x48, uint16_t(ld16(Save + 0x48) + 1)); original(c, kDayProc); }
        st32(Save + 0x44, f32_as_u32(t));
    }
}
uint32_t call(Cpu*, uint32_t fn, std::initializer_list<uint32_t>) { return fn == kEnvLight ? Env : 0; }
void change(Cpu* c, float degrees) { c->f[1].ps0 = degrees; hooks.at(kTimeChange)(c); }
}  // namespace

int main() {
    map(0x101F4000, 0x8000);
    map(Area, 0x4000);
    st32(kSaveWord, Save);
    st32(Save + 0x44, f32_as_u32(22.f * 15));  // 22:00
    st16(Save + 0x48, 3);                      // day of week
    namespace L = story_demo_parts::light;
    const story_demo_parts::Context x{Record, Scratch, call, call};
    L::install(x, original, [](uint32_t) { return std::string(); }, [](uint32_t fn, void (*h)(Cpu*)) { hooks[fn] = h; }, false, true);
    assert(hooks.count(kTimeChange) && hooks.count(kDayProc) && !hooks.count(0x0255D128) && L::time_ready());
    Cpu c{};
    // no mod demo: the game's own (a retail event's 22:00 -> 06:00 counts the day)
    change(&c, 6.f * 15);
    assert(originals[kTimeChange] == 1 && originals[kDayProc] == 1 && ld16(Save + 0x48) == 4);
    st32(Save + 0x44, f32_as_u32(22.f * 15)); st16(Save + 0x48, 3); originals.clear();

    demo_scene::Demo d;
    d.frames = 60;
    d.time = {{0, 6.f}};
    L::begin(&c, x, d);
    assert(L::time_held());
    change(&c, 6.f * 15);  // the demo's LIGHT cut: 22:00 -> 06:00
    assert(u32_as_f32(ld32(Save + 0x44)) == 90.f && ld16(Save + 0x48) == 3 && originals.empty());
    hooks.at(kDayProc)(&c);  // any other caller of the day change during the demo
    assert(originals.empty());
    L::release(&c, x);  // the time and date from before the demo
    assert(u32_as_f32(ld32(Save + 0x44)) == 330.f && ld16(Save + 0x48) == 3 && !L::time_held());

    // keep_time: the demo's time stays, the date never moved
    d.keep_time = true;
    L::begin(&c, x, d);
    change(&c, 6.f * 15);
    L::release(&c, x);
    assert(u32_as_f32(ld32(Save + 0x44)) == 90.f && ld16(Save + 0x48) == 3 && originals.empty());

    // after the demo the game's own again
    hooks.at(kDayProc)(&c);
    assert(originals[kDayProc] == 1);
    std::puts("story_demo_light: ok");
    return 0;
}
