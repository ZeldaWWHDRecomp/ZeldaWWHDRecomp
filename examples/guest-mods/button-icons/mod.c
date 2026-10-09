/* Original glyph art and overlay code. No game artwork is copied or modified. */
#include "wwhd_guest.h"
#include "wwhd/bindings.h"
#include "wwhd/link.h"

static const char* const paths[] = {
    "assets/a.png","assets/b.png","assets/x.png","assets/y.png","assets/r.png"
};
/* TV coordinates; top-right anchoring follows the port's wider HUD canvas.
 * Placement is deliberately kept together for frame-dump calibration. */
static const f32 x[] = {1155,1122,1122,1084,1178};
static const f32 y[] = {125,171,91,131,72};
static const f32 width[] = {50,38,38,38,56};
static const f32 height[] = {50,38,38,38,32};
static u32 images[5];
static unsigned long long epoch;
static char layout[16];
static wwhd_hud_element element;

static int labels_selected(void) {
    if (!wwhd_setting_get("input.face_layout",WWHD_SETTING_STRING,layout,sizeof layout)) return 0;
    return layout[0]=='l' && layout[1]=='a' && layout[2]=='b' && layout[3]=='e' &&
           layout[4]=='l' && layout[5]=='s' && layout[6]==0;
}
static void draw(u32 list) {
    unsigned long long current = wwhd_hud_epoch();
    if (current != epoch) {
        epoch=current;
        for (u32 i=0;i<5;++i) images[i]=0;
    }
    if (!labels_selected()) return; /* committing an empty list removes the previous icons */
    element=(wwhd_hud_element){.kind=WWHD_HUD_IMAGE,.anchor=WWHD_HUD_TOP_RIGHT,
        .w=32,.h=32,.thickness=1,.u1=1,.v1=1,.rgba=0xFFFFFFFF};
    for (u32 i=0;i<5;++i) {
        if (!images[i]) images[i]=wwhd_hud_texture(WWHD_HUD_PACKAGE,paths[i]);
        if (!images[i]) continue;
        element.x=x[i];element.y=y[i];element.w=width[i];element.h=height[i];element.image=images[i];
        wwhd_hud_emit(list,&element);
    }
}
WWHD_HOOK(WWHD_ADDR_daPy_Execute,register_button_icons,(void* link)) {
    (void)link;
    wwhd_hud_register(draw,WWHD_HUD_TV);
}
