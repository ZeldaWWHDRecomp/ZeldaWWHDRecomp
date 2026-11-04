/* Public CC0 daObjAuzu::Act_c::is_exist, d_a_obj_auzu.cpp.
 * Randomizer policy (MIT make_game_nonlinear.asm at 0231987C): bypass
 * the Endless Night query entirely, so the cave's whirlpool can exist before
 * that story event. The native create routine still owns resources, switches,
 * visibility, animation and cleanup.
 */
#pragma once
WWHD_REPLACE(0x02319870,u32,randomizer_jabun_whirlpool_exists,(u32 actor)) {
    (void)actor;
    return 1;
}
