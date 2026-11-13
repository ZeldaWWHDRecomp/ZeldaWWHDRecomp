// Fix original game bugs (built-in setting, on by default): fixes of bugs that are in the original game's own
// code, not in the port. Off, the game behaves exactly as the original. Each fix asks fix_game_bugs() at the point
// where it acts:
//   issue #336  the map tab's chart grid takes the pause menu's focus after a scene change; the item menu then has
//               no selection cursor (hud_gate.cpp: the grid's focus rule and the safety net that gives it back)
// Start-up / test switch: WWHD_MOD_FIX_GAME_BUGS=0 (off) or 1 (on); it wins over the saved setting.
#include "mods.h"
#include "runtime.h"

#include <atomic>
#include <cstdlib>

namespace mods {
namespace {
std::atomic<bool> g_fix{[] {
    const char* e = getenv("WWHD_MOD_FIX_GAME_BUGS");
    return !e || atoi(e) != 0;
}()};
}  // namespace

bool fix_game_bugs() { return g_fix.load(std::memory_order_relaxed); }
void set_fix_game_bugs(bool on) {
    if (g_fix.exchange(on, std::memory_order_relaxed) != on) LOG("[mods] fix original game bugs %s", on ? "on" : "off");
}
}  // namespace mods
