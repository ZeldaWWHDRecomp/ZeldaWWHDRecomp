#include "aspect_panes.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace aspect::panes;
static void check(bool ok) { if (!ok) { std::fprintf(stderr,"pane policy check failed\n"); std::exit(1); } }
struct Pane { const char* name; float x,y,w,h; std::vector<Pane> children; };
static Transform apply(const Pane& p, float kx, float ky) {
    Role role=fill(p.name,p.children.empty(),p.x,p.y,p.w,p.h)?Role::Fill:Role::Content;
    return transform(role,p.x,p.y,1,1,kx,ky);
}
int main() {
    Pane tree{"RootPane",0,0,1280,720,{
        {"P_BG_00",0,0,1280,720,{}},
        {"PF_PauseTV_00",0,0,1280,720,{}},
        {"W_Base_00",640,0,2480,700,{{"W_ItemBase_00",347,-92,478,414,{}}}},
        {"P_ItemMask_00",0,0,1290,730,{}},
        {"B_Slide_00",0,0,1280,720,{}}
    }};
    for(float a : {16.f/9,64.f/27,32.f/9,16.f/10,4.f/3}) {
        float kx=std::max(1.f,a/(16.f/9)),ky=std::max(1.f,(16.f/9)/a);
        check(apply(tree,kx,ky).sx==1);
        for(int i=0;i<2;i++) {auto t=apply(tree.children[i],kx,ky);check(t.sx==kx && t.sy==ky);}
        for(int i=2;i<5;i++) {auto t=apply(tree.children[i],kx,ky);check(t.sx==1 && t.sy==1 && t.x==tree.children[i].x);}
        auto map=transform(Role::Content,224,-26,1,1,kx,ky);check(map.x==224 && map.y==-26);
        auto box=apply(tree.children[2].children[0],kx,ky);check(box.x==347 && box.y==-92);
        auto cursor=transform(Role::Content,347,-92,1,1,kx,ky);check(cursor.x==box.x && cursor.y==box.y);
        auto hud=transform(Role::Hud,-601,338,1,1,kx,ky);check(hud.x==-601-640*(kx-1) && hud.y==338+360*(ky-1));
        auto world=transform(Role::Projected,200,100,1,1,kx,ky);check(world.x==200*kx && world.y==100*ky);
        // Exact native scissor on integral-size targets, including tall pictures and 2x resolution.
        for (uint32_t scale : {1u,2u,3u}) {
            uint32_t w=uint32_t(std::lround(1280*scale*kx)),h=uint32_t(std::lround(720*scale*ky));
            uint32_t x=0,y=0,ex=w,ey=h;clip(w,h,kx,ky,x,y,ex,ey);
            check(ex-x<=1280*scale && ey-y<=720*scale && ex-x>=1280*scale-1 && ey-y>=720*scale-1);
            if (a != 64.f/27) check(ex-x==1280*scale && ey-y==720*scale);
            uint32_t ox=x,oy=y;clip(w,h,kx,ky,x,y,ex,ey);check(x==ox&&y==oy);
        }
    }
    check(parked_hud(700,0) && parked_hud(0,400) && !parked_hud(640,360));
    check(!projected_root_name("W_Cursor_00") && !projected_root_name("N_FloorCenter_00") && projected_root_name("N_EnemyHP_00"));
    check(!fill("P_BG_00",false,0,0,1280,720));
    check(!fill("P_BG_00",true,1280,0,1280,720));
    check(!fill("P_BG_00",true,0,0,300,200));
    uint32_t x=0,y=0,ex=1280,ey=720;clip(1280,720,1,1,x,y,ex,ey);check(x==0&&y==0&&ex==1280&&ey==720);
    std::puts("pane rules: five aspects, filters, frame trees, cursor alignment, HUD and clipping passed");
}
