// Link's movement hooks for the mods (tools/recomp/hooks.txt; formerly in the retired True 60 code):
// the speed factor, moon jump and move effects at posMoveFromFootPos, faster sidling at
// setNormalSpeedF. With the mods off the game's own code runs bit for bit.
#include "runtime.h"
#include "mods/mods.h"

extern "C" void f_02416230_orig(Cpu* c);  // daPy_lk_c::setNormalSpeedF

// daPy_lk_c::posMoveFromFootPos, `current.pos += speed` (PSVECAdd(pos, speed, pos) at 023FD39C):
// the movement mods (speed factor, moon jump, start/trail/swim effects; mods.cpp) replace the vector
// argument. Off, the stock path runs bit for bit.
extern "C" void site_023FD39C(Cpu* c) {
    constexpr uint32_t kLinkSpeed = 0x33C;  // fopAc_ac_c::speed
    const uint32_t link = c->r[4] - kLinkSpeed;  // the vector argument is &Link->speed
    const float factor = mods::link_move_factor(link);
    mods::move_start_effect(c, link);
    mods::move_trail_effect(c, link);
    mods::move_swim_effect(c, link);
    float moon_x = 0, moon_z = 0;
    const bool moon = mods::moon_jump_move(link, moon_x, moon_z);
    if (factor == 1.f && !moon) return;
    uint32_t sp = c->r[4];
    float vx = u32_as_f32(ld32(sp)), vz = u32_as_f32(ld32(sp + 8));
    if (moon) { vx = moon_x; vz = moon_z; }
    uint32_t v = mem::fixed_slot(mem::kFixLinkScratch);
    st32(v, f32_as_u32(vx * factor));
    st32(v + 4, ld32(sp + 4));
    st32(v + 8, f32_as_u32(vz * factor));
    c->r[4] = v;
}

// daPy_lk_c::setNormalSpeedF: faster sidling (mods/traversal.cpp)
extern "C" void hook_02416230(Cpu* c) {
    const mods::SidleScope sidle = mods::sidle_scope_begin(c, true);
    f_02416230_orig(c);
    mods::sidle_scope_end(sidle);
}
