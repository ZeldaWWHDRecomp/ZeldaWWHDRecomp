/* Original glyph art and overlay code. No game artwork is copied or modified. */
#include "wwhd_guest.h"
#include "wwhd/bindings.h"
#include "wwhd/link.h"

WWHD_GAME_FUNC(WWHD_ADDR_dScnPly_isPause,s32,button_scene_paused,(void));

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
/* Private pilot layout view, from the public port's docs/decomp-notes.md and
 * runtime/src/aspect.cpp; not an addition to the generated decomp SDK headers. */
static u8* panes[5];
static const char* const pane_names[] = {"P_A_00","P_B_00","P_X_00","P_Y_00","P_R_00"};
static double aspect;
static struct { f32 gx,gy,expansion;u32 alpha; } drawn[5];
static unsigned long long drawn_step;
static u32 have_drawn;
static void sync_epoch(void) {
    unsigned long long current=wwhd_hud_epoch();
    if(current==epoch)return;
    epoch=current;
    /* Full states restore this mod's guest snapshot too. Preserve it so a
     * paused captured TV picture can immediately recover its matching icons;
     * only host texture handles and live-pane discovery become invalid. */
    for(u32 i=0;i<5;++i){images[i]=0;panes[i]=0;}
}
static int pane_name(const u8* p,const char* expected) {
    const char* name=(const char*)(p+0x80);
    for(u32 i=0;i<24;++i) { if(name[i]!=expected[i])return 0;if(!name[i])return 1; }
    return 0;
}
static int pane_pointer(const u8* p) {
    u32 address=(u32)p;
    return address>=0x10000000u && address<=0x4FFFFF00u && !(address&3);
}
/* Only the TV CommandGuide cluster: its DRC copy is parked above the TV's
 * visible vertical range. The projected contextual A prompt has another root. */
static u8* tv_cluster(u8* p) {
    for(u32 depth=0;depth<12 && pane_pointer(p);++depth) {
        u8* parent=*(u8**)(p+0x0C);
        if(pane_name(p,"N_All_00") && pane_pointer(parent) && !*(u32*)(parent+0x0C)) {
            f32 gx=*(f32*)(p+0x54),gy=*(f32*)(p+0x64);
            if(gx>400 && gy>100 && gy<300)return p;
            return 0;
        }
        p=parent;
    }
    return 0;
}
/* Discover only a drawn root's bounded CommandGuide subtree. This avoids an
 * expensive hook on every pane matrix calculation. */
static void find_buttons(u8* p,u8** found,u32 depth,u32* budget) {
    if(depth==12 || !*budget || !pane_pointer(p))return;
    --*budget;
    for(u32 i=0;i<5;++i)if(pane_name(p,pane_names[i]))found[i]=p;
    u8* sentinel=p+0x14;u32 siblings=0;
    for(u8* child=*(u8**)sentinel; child!=sentinel && siblings++<256 && *budget && pane_pointer(child);child=*(u8**)child)
        find_buttons(child,found,depth+1,budget);
}
static int discover_buttons(u8* root) {
    u8* sentinel=root+0x14;
    u32 count=0;
    for(u8* child=*(u8**)sentinel;child!=sentinel && count++<128 && pane_pointer(child);child=*(u8**)child) {
        if(!pane_name(child,"N_All_00") || !tv_cluster(child))continue;
        u8* found[5]={0,0,0,0,0};u32 budget=256;
        find_buttons(child,found,0,&budget);
        for(u32 i=0;i<5;++i)if(!found[i])return 0;
        for(u32 i=0;i<5;++i)panes[i]=found[i];
        return 1;
    }
    return 0;
}
static u32 pane_alpha(u8* p) {
    u32 alpha=255;
    for(u32 depth=0;depth<12;++depth) {
        if(!pane_pointer(p) || !(p[0x44]&1))return 0;
        alpha=(alpha*p[0x45]+127)/255;
        u8* parent=*(u8**)(p+0x0C);
        if(!parent)return alpha;
        p=parent;
    }
    return 0;
}

/* Read the geometry when the layout is actually drawn. During Pause the game
 * can animate/reuse its matrices without drawing this HUD; stale matrices must
 * not put bright replacement icons on top of its blurred cached background. */
WWHD_HOOK(0x02877100,record_button_draw,(u8* pane,void* info)) {
    (void)info;
    if(!pane_pointer(pane) || *(u32*)(pane+0x0C))return;
    sync_epoch();
    if(button_scene_paused())return; /* Pause presents the last TV picture. */
    int belongs=0;
    for(u32 i=0;i<5;++i)if(pane_pointer(panes[i])) {
        u8* cluster=tv_cluster(panes[i]);
        if(cluster && *(u8**)(cluster+0x0C)==pane)belongs=1;
    }
    if(!belongs && !discover_buttons(pane))return;
    aspect=16.0/9.0;
    wwhd_setting_get("display.aspect",WWHD_SETTING_F64,&aspect,sizeof aspect);
    f32 expansion=aspect>16.0/9.0 ? (f32)((aspect*720.0-1280.0)*0.5) : 0;
    for(u32 i=0;i<5;++i) {
        u8* p=panes[i];
        drawn[i].alpha=0;
        if(!pane_pointer(p) || !pane_name(p,pane_names[i]) || !tv_cluster(p))continue;
        drawn[i].gx=*(f32*)(p+0x54);drawn[i].gy=*(f32*)(p+0x64);
        drawn[i].expansion=expansion;drawn[i].alpha=pane_alpha(p);
    }
    drawn_step=wwhd_logic_step();have_drawn=1;
}
static int labels_selected(void) {
    if (!wwhd_setting_get("input.face_layout",WWHD_SETTING_STRING,layout,sizeof layout)) return 0;
    return layout[0]=='l' && layout[1]=='a' && layout[2]=='b' && layout[3]=='e' &&
           layout[4]=='l' && layout[5]=='s' && layout[6]==0;
}
static void draw(u32 list) {
    sync_epoch();
    if (!labels_selected() || !have_drawn || (!button_scene_paused() && wwhd_logic_step()>drawn_step+1)) return; /* committing an empty list removes the previous icons */
    element=(wwhd_hud_element){.kind=WWHD_HUD_IMAGE,.anchor=WWHD_HUD_TOP_RIGHT,
        .w=32,.h=32,.thickness=1,.u1=1,.v1=1,.rgba=0xFFFFFFFF};
    for (u32 i=0;i<5;++i) {
        u32 alpha=drawn[i].alpha;
        if(!alpha)continue;
        if (!images[i]) images[i]=wwhd_hud_texture(WWHD_HUD_PACKAGE,paths[i]);
        if (!images[i]) continue;
        /* Matrix translations are in centred TV coordinates. The port already
         * adds its aspect expansion to the matrix: remove it before asking the
         * HUD service to apply the top-right anchor, so expansion occurs once. */
        f32 gx=drawn[i].gx,gy=drawn[i].gy;
        element.x=x[i]+gx-(i==0?541.0f:i==3?463.4f:i==4?567.4f:501.8f)-drawn[i].expansion;
        element.y=y[i]-gy+(i==0?209.048f:i==1?169.2f:i==2?248.4f:i==3?209.2f:271.6f);
        element.w=width[i];element.h=height[i];element.image=images[i];
        element.rgba=0xFFFFFF00u|alpha;
        wwhd_hud_emit(list,&element);
    }
}
WWHD_HOOK(WWHD_ADDR_daPy_Execute,register_button_icons,(void* link)) {
    (void)link;
    wwhd_hud_register(draw,WWHD_HUD_TV);
}
