#pragma once
#include <cstdint>

namespace mods::targeting_letterbox {
struct Context {
    uint32_t size, mode;
    bool event, scripted, minigame, target, normal_write, free_target;
};
enum Mode { On = 0, OffWithEnemies = 1, AlwaysOff = 2 };
constexpr bool suppress(int mode, const Context& c) {
    const bool targeting = (c.mode == 2 && c.target) ||
                          (mode == AlwaysOff && c.mode == 1 && c.free_target);
    return mode != On && c.size == 1 && !c.event && !c.scripted && !c.minigame &&
           targeting && c.normal_write;
}
}
