/* Example guest mod: hooks Link's per-step function and lets his hearts tick down a quarter heart
 * every `every` logic steps, from full to half, then refills them (never below half: harmless).
 * Written for this example; names and layouts come from the generated public HD SDK. */
#include "wwhd_guest.h"
#include "game/bindings.h"
#include "game/link.h"
#include "game/save.h"

static u32 steps, returns;
static f32 smooth;

WWHD_HOOK(WWHD_ADDR_daPy_Execute, on_link_execute, (void* link)) {
    int every = wwhd_config_int("every", 30);
    if (every < 1) every = 1;
    if (++steps % (u32)every) return;
    u16 max_life = dComIfGs_player_status_a.max_life;
    u16 life = dComIfGs_player_status_a.life;
    if (max_life < 12 || max_life > 80 || life > max_life) return;  /* no save data loaded */
    u16 next = life > max_life / 2 ? life - 1 : max_life;
    dComIfGs_player_status_a.life = next;
    wwhd_cLib_addCalc2_hd(&smooth, (f32)next, 0.5f, 2.0f);
    if (steps % (u32)(every * 4) == 0) {
        wwhd_log_int("heart-ticker: life (quarter hearts)", next);
        wwhd_log_float("heart-ticker: smoothed by the game's cLib_addCalc2", smooth);
    }
}

WWHD_HOOK_RETURN(WWHD_ADDR_daPy_Execute, after_link_execute, (void* link)) {
    if (++returns == 1) wwhd_log_hex("heart-ticker: first return hook, Link at", (u32)link);
}
