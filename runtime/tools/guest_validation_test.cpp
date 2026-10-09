// Synthetic module metadata only: no game image needed.
#include "mods/guest_validation.h"
#include <cassert>
#include <iostream>

static void fn(Cpu*) {}
static void other(Cpu*) {}
int main() {
    uint8_t image[4]{};
    WWHDGuestFunc funcs[] = {{0x7F000000, fn}};
    WWHDGuestHook hooks[] = {{WWHD_GUEST_REPLACE, 0x02000000, 0x7F000000, 0, fn},
                             {WWHD_GUEST_REPLACE, 0x02000000, 0x7F000000, 0, fn}};
    WWHDGuestModuleV1 m{sizeof(m), WWHD_GUEST_ABI_VERSION, "test", 0x7F000000, 4096,
                        image, sizeof(image), funcs, 1, hooks, 1};
    std::string error, prior;
    auto check = [&] {
        error.clear();
        return guestmods::validate_module(m, "second-mod", [](uint32_t a) { return a == 0x02000000; },
                                          [&](uint32_t) { return prior; }, error);
    };
    assert(check());
    prior = "first-mod";
    assert(!check());
    assert(error.find("first-mod and second-mod") != std::string::npos);
    prior.clear(); m.hook_count = 2;
    assert(!check());
    assert(error.find("second-mod and second-mod") != std::string::npos);
    hooks[1].kind = WWHD_GUEST_HOOK_ENTRY;
    assert(check()); // replacement plus entry/return hooks are allowed
    hooks[1].kind = WWHD_GUEST_HOOK_RETURN;
    assert(check());
    hooks[1].kind = 99; assert(!check());
    m.hook_count = 1;
    hooks[0].target += 4; assert(!check()); hooks[0].target -= 4;
    hooks[0].fn = other; assert(!check()); hooks[0].fn = fn;
    hooks[0].func += 4; assert(!check()); hooks[0].func -= 4;
    m.mem_base = 0x80000004; assert(!check()); // subtraction must not wrap
    m.mem_base = 0x7F000000; m.mem_size = 0; assert(!check());
    m.mem_size = 0x01000001; assert(!check()); m.mem_size = 4096;
    m.image_size = 4097; assert(!check()); m.image_size = 4;
    m.image = nullptr; assert(!check()); m.image = image;
    m.funcs = nullptr; assert(!check()); m.funcs = funcs;
    m.hooks = nullptr; assert(!check()); m.hooks = hooks;
    funcs[0].addr += 1; assert(!check()); funcs[0].addr -= 1;
    funcs[0].fn = nullptr; assert(!check()); funcs[0].fn = fn;
    assert(check());
    std::cout << "guest module validation passed\n";
}
