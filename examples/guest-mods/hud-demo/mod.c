/* Original example; game names/layouts come from the public CC0 HD SDK. */
#include "wwhd_guest.h"
#include "wwhd/bindings.h"
#include "wwhd/link.h"
#include "wwhd/save.h"

static wwhd_hud_element element;
static char hearts[] = "Hearts: 00.00 / 00";
static u32 tile;
static unsigned long long epoch;

static void two_digits(char* dst,u32 value) {
    dst[0] = '0' + (value / 10) % 10;
    dst[1] = '0' + value % 10;
}
static void draw(u32 list) {
    unsigned long long current_epoch = wwhd_hud_epoch();
    if (epoch != current_epoch) { epoch = current_epoch; tile = 0; }
    if (!tile) tile = wwhd_hud_texture(WWHD_HUD_PACKAGE,"assets/tile.png");
    u16 life = 0, maximum = 0;
    if (WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)) {
        life = dComIfGs_player_status_a.life;
        maximum = dComIfGs_player_status_a.max_life;
    }
    if (maximum > 80 || life > maximum) return;
    two_digits(hearts+8,life/4);
    two_digits(hearts+11,(life%4)*25);
    two_digits(hearts+16,maximum/4);
    element = (wwhd_hud_element){.kind=WWHD_HUD_RECT,.anchor=WWHD_HUD_TOP_LEFT,
        .x=20,.y=96,.w=260,.h=56,.thickness=1,.u1=1,.v1=1,.rgba=0x101820E0};
    wwhd_hud_emit(list,&element);
    if (tile) {
        element.kind=WWHD_HUD_IMAGE;element.image=tile;element.rgba=0xFFFFFFFF;
        element.x=28;element.y=108;element.w=32;element.h=32;
        wwhd_hud_emit(list,&element);
    }
    element.kind=WWHD_HUD_TEXT;element.image=0;element.rgba=0xFFFFFFFF;element.x=72;element.y=113;
    element.size=18;element.text=hearts;element.text_bytes=sizeof hearts-1;
    wwhd_hud_emit(list,&element);
}
WWHD_HOOK(WWHD_ADDR_daPy_Execute,register_hud_demo,(void* link)) {
    (void)link;
    wwhd_hud_register(draw,WWHD_HUD_BOTH);
}
