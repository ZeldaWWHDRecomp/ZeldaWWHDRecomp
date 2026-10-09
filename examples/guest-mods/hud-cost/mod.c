/* Functional performance fixture: exactly 200 rectangles, or no registered HUD. */
#include "wwhd_guest.h"
#include "wwhd/bindings.h"
#include "wwhd/link.h"

static wwhd_hud_element element;
static void draw(u32 list) {
    element = (wwhd_hud_element){
        .kind=WWHD_HUD_RECT,.anchor=WWHD_HUD_TOP_LEFT,
        .w=16,.h=10,.thickness=1,.u1=1,.v1=1,.rgba=0x205080C0
    };
    for (u32 i=0;i<200;++i) {
        element.x=20+(i%20)*18;
        element.y=200+(i/20)*14;
        wwhd_hud_emit(list,&element);
    }
}
WWHD_HOOK(WWHD_ADDR_daPy_Execute,register_hud_cost,(void* link)) {
    (void)link;
    wwhd_hud_register(wwhd_config_bool("draw",0)?draw:0,WWHD_HUD_TV);
}
