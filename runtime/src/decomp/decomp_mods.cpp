// Decomp build: code mods (guest mods, docs/mod-sdk-v2.md) and the in-app randomizer in the decomp version.
//
// Code mods need game code translated with recomp.py --mod-hooks: a hooked function's recompiled original starts
// with the mod's check (PPC_MOD_HOOK), and decomp::init keeps every function a mod hooks off its decompiled slot, so
// calls reach that original. The plain decomp build needs nothing more: its calls between decompiled functions check
// the slot (g_decomp_native_ok, the native entry table). Fast mode binds calls between decompiled functions when the
// build is made (wwhd_ne_ exports, in-unit native calls), so it must know the hooked functions then: setup passes the
// code mods' hooks to gen.py --guest-hooks, native_rules.py makes them callee=guest (their callers take the guest call)
// and hooks.cpp lists them (kGuestHooked). Here, once the guest mods are registered:
//  - a hooked decompiled function outside that list (a code mod switched on after the build): every game function runs
//    recompiled for this start (WWHD_DECOMP=none: correct, at the normal game's speed), the hooked set goes to
//    decomp-mod-hooks.txt in the install folder for setup's next decomp build, and the log says so;
//  - WWHD_DECOMP_MOD_HOOKS_PROBE=FILE (setup): writes the hooked game functions to FILE and exits;
//  - a hook installed after decomp::init on a function that is entered natively stops the game (decomp_mod_hooked):
//    the fast build cannot reroute calls bound by address, and the mod would silently not run.
#include "decomp/decomp.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>
#include "../runtime.h"
#include "recomp_table.h"

namespace decomp::mods {
namespace {
bool g_all_recompiled = false;
bool g_initialized = false;

bool listed(uint32_t addr) {
    return std::binary_search(kGuestHooked, kGuestHooked + kNumGuestHooked, addr);
}
// the install folder (mods::code::startup sets WWHD_INSTALL_DIR in every build)
std::filesystem::path install_dir() {
    const char* d = getenv("WWHD_INSTALL_DIR");
    return d && *d ? std::filesystem::path(d) : std::filesystem::path();
}
bool write_list(const std::filesystem::path& path, const std::vector<uint32_t>& addrs) {
    std::error_code ec;
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << "# the game functions the enabled code mods hook (the decomp version, tools/decomp_build/gen.py --guest-hooks)\n";
        char line[16];
        for (uint32_t a : addrs) { snprintf(line, sizeof line, "%08X\n", a); f << line; }
        if (!f) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}
}  // namespace

std::vector<uint32_t> hooked_functions() {
    std::vector<uint32_t> v;
    if (!g_mod_hook_flags) return v;
    for (unsigned i = 0; i < g_recomp_func_count; i++)
        if (g_mod_hook_flags[i]) v.push_back(g_recomp_funcs[i].addr);
    return v;
}

bool decide() {
    const std::vector<uint32_t> hooked = hooked_functions();
    if (const char* probe = getenv("WWHD_DECOMP_MOD_HOOKS_PROBE"); probe && *probe) {
        const bool ok = write_list(probe, hooked);
        LOG("[decomp] code mods: %zu hooked game functions written to %s%s", hooked.size(), probe, ok ? "" : " (failed)");
        fflush(stdout);
        fflush(stderr);
        std::_Exit(ok ? 0 : 1);
    }
#ifdef WWHD_DECOMP_FAST
    std::vector<uint32_t> uncovered;
    for (uint32_t a : hooked) {
        const Slot* s = std::lower_bound(g_slots, g_slots + kNumSlots, a, [](const Slot& x, uint32_t v) { return x.addr < v; });
        if (s != g_slots + kNumSlots && s->addr == a && !listed(a)) uncovered.push_back(a);
    }
    if (!uncovered.empty()) {
        g_all_recompiled = true;
        std::string some;
        for (size_t i = 0; i < uncovered.size() && i < 8; i++) {
            char b[12];
            snprintf(b, sizeof b, "%s%08X", i ? " " : "", uncovered[i]);
            some += b;
        }
        LOG("[decomp] code mods: %zu hooked functions were not hooked when this decomp version was built (%s%s): every "
            "game function runs recompiled this time; setup's next build (Repair, or a code-mod rebuild) includes them",
            uncovered.size(), some.c_str(), uncovered.size() > 8 ? " ..." : "");
        const auto dir = install_dir();
        if (!dir.empty() && !write_list(dir / "decomp-mod-hooks.txt", hooked))
            LOG("[decomp] code mods: cannot write %s", (dir / "decomp-mod-hooks.txt").string().c_str());
    } else if (!hooked.empty()) {
        LOG("[decomp] code mods: %zu hooked game functions, all known to this build (%u listed)", hooked.size(), kNumGuestHooked);
    }
#endif
    g_initialized = true;
    return g_all_recompiled;
}

bool all_recompiled() { return g_all_recompiled; }

void late_hook(uint32_t addr, bool native) {
#ifdef WWHD_DECOMP_FAST
    if (g_initialized && native && !g_all_recompiled && !listed(addr)) {
        fprintf(stderr, "[decomp] code mods: %08X was hooked after start-up, but this fast decomp version calls it "
                        "directly; the hook would not run. Start the normal game for this mod.\n", addr);
        fflush(stderr);
        abort();
    }
#else
    (void)addr;
    (void)native;
#endif
}

std::string randomizer_conflict() {
    // the in-app randomizer (tools/installer/randomizer_seeds.py): randomizer/seeds.json "playing"; the decomp version
    // is built from the game as it is and never plays a seed's code
    const auto dir = install_dir();
    if (dir.empty()) return {};
    std::ifstream f(dir / "randomizer" / "seeds.json");
    if (!f) return {};
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto k = text.find("\"playing\"");
    if (k == std::string::npos) return {};
    const auto q1 = text.find('"', text.find(':', k) + 1);
    const auto q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    const std::string playing = text.substr(q1 + 1, q2 - q1 - 1);
    return playing.empty() || playing == "vanilla" ? std::string() : playing;
}
}  // namespace decomp::mods
