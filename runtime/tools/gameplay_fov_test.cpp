#include "aspect.h"
#include "gfx/stereo.h"
#include "gx2/gx2_cmd.h"
#include "runtime.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

void log_msg(const char*, ...) {}
namespace framegen { bool requested() { return false; } }  // 60 fps (generated): off in this test
namespace hud_layout { bool marked() { return false; } }  // (no layout marker in this test)
namespace gx2 {
void emit(Op, const uint32_t*, uint32_t) {}
bool recording_display_list() { return false; }
}
namespace mods::cemu { float aspect_ratio() { return 0; } }
namespace mods { void play_context_pane(uint32_t) {} }  // the context buttons' pane observer (mods/play_context.cpp)
namespace mods { bool play_context_logs_panes() { return false; } }
namespace mods { float hud_fade_alpha_of(uint32_t) { return 1.f; } int hud_pane_hide(uint32_t) { return 0; } }  // HUD fades (mods/hud_fade.cpp)
namespace button_glyphs { void calculate_pane(Cpu* c,PpcFunc fn) { fn(c); } void draw_pane(Cpu* c,PpcFunc fn) { fn(c); } }
extern "C" {
void f_025A847C_orig(Cpu*) {}
void f_02704D2C_orig(Cpu*) {}
void f_0282D9C0_orig(Cpu*) {}
void f_02874038_orig(Cpu*) {}
void f_028766CC_orig(Cpu*) {}
void f_02877100_orig(Cpu*) {}
void f_028F8250_orig(Cpu*) {}
void site_024FFD60(Cpu*);
void site_0250007C(Cpu*);
void site_024F811C(Cpu*);
void site_024F8168(Cpu*);
void site_025ADB38(Cpu*);
}
static Cpu camera() {
    Cpu c{};c.f[1].ps0=c.f[1].ps1=45; c.f[2].ps0=c.f[2].ps1=16.f/9;
    c.f[3].ps0=1;c.f[4].ps0=10000;return c;
}
static bool close_enough(double a,double b) { return std::abs(a-b)<.0001; }
int main() {
    static_assert(aspect::kOriginal==0 && aspect::kWindow==1 && aspect::k16x10==2 &&
                  aspect::k21x9==3 && aspect::k32x9==4 && aspect::kCustom==5);
    static_assert(aspect::is_preset(aspect::k4x3) && !aspect::is_preset(aspect::kCustom));
    assert(std::strcmp(aspect::mode_name(aspect::k4x3), "4:3")==0);
    assert(close_enough(aspect::mode_value(aspect::k4x3),4.f/3));
    if(const char* ratio=std::getenv("WWHD_ASPECT"); ratio && std::strcmp(ratio,"4:3")==0) {
        assert(aspect::mode()==aspect::k4x3);
        assert(close_enough(aspect::requested(),4.f/3));
    }
    // Match window agrees with the exact presets, including integer-pixel 4:3 dimensions.
    aspect::set_mode(aspect::kWindow);
    for (auto dimensions : {std::pair{1280.f,960.f}, std::pair{1280.f,800.f},
                            std::pair{1920.f,1080.f}, std::pair{2560.f,1080.f}, std::pair{3840.f,1080.f}}) {
        const float ratio = dimensions.first / dimensions.second;
        aspect::set_window_aspect(ratio);
        assert(close_enough(aspect::requested(),ratio));
    }
    aspect::set_window_aspect(1281.f/960.f);assert(close_enough(aspect::requested(),4.f/3));
    aspect::set_window_aspect(1.431f);assert(close_enough(aspect::requested(),1.43f));
    // #490: presets within 1 % of the window take the window's shape; others stay exact.
    auto preset_in=[](int m,float w,float h){aspect::set_mode(m);aspect::set_window_aspect(w/h);return aspect::requested();};
    assert(close_enough(preset_in(aspect::k21x9,3440,1440),2.39f));  // stored window value (43:18 = 2.389)
    assert(close_enough(preset_in(aspect::k21x9,2560,1080),64.f/27));
    assert(close_enough(preset_in(aspect::k21x9,1920,1080),64.f/27));
    assert(close_enough(preset_in(aspect::k32x9,5120,1440),32.f/9));
    assert(close_enough(preset_in(aspect::k16x10,1920,1200),1.6f));
    assert(close_enough(preset_in(aspect::k4x3,1.34f,1),1.34f));
    assert(close_enough(preset_in(aspect::k4x3,1.35f,1),4.f/3));  // 1.25 % away: stays exact
    assert(close_enough(preset_in(aspect::kOriginal,1.77f*1.005f,1),16.f/9));
    assert(close_enough(preset_in(aspect::kOriginal,3440,1440),16.f/9));
    assert(close_enough(preset_in(aspect::kCustom,3440,1440),aspect::mode_value(aspect::kCustom)));
    assert(close_enough(preset_in(aspect::kWindow,3440,1440),2.39f));
    // HUD constraint: HUD panes follow the chosen shape, never wider than the picture itself.
    static_assert(aspect::kHudMatch==0 && aspect::kHud32x9==1 && aspect::kHud21x9==2 &&
                  aspect::kHud16x9==3 && aspect::kHudModes==4);
    assert(std::strcmp(aspect::hud_mode_name(aspect::kHudMatch), "Match display aspect ratio")==0);
    assert(aspect::hud_mode_value(aspect::kHudMatch)==0);
    assert(close_enough(aspect::hud_mode_value(aspect::kHud21x9), 64.f/27));
    // Graphics options wider than the picture are greyed out (and cannot be made effective).
    assert(aspect::hud_mode_fits(aspect::kHudMatch,16.f/9) && aspect::hud_mode_fits(aspect::kHud16x9,16.f/9));
    assert(!aspect::hud_mode_fits(aspect::kHud21x9,16.f/9) && !aspect::hud_mode_fits(aspect::kHud32x9,16.f/9));
    assert(aspect::hud_mode_fits(aspect::kHud21x9,64.f/27) && !aspect::hud_mode_fits(aspect::kHud32x9,64.f/27));
    assert(aspect::hud_mode_fits(aspect::kHud32x9,32.f/9) && !aspect::hud_mode_fits(aspect::kHud16x9,4.f/3));
    auto hud_at=[](int display,int hud,float& kx,float& ky) {
        aspect::set_mode(display);aspect::set_hud_mode(hud);aspect::on_swap();
        aspect::hud_factors(aspect::game(),kx,ky);
    };
    float hx,hy;
    hud_at(aspect::k32x9,aspect::kHudMatch,hx,hy);assert(close_enough(hx,2)&&close_enough(hy,1));
    hud_at(aspect::k32x9,aspect::kHud21x9,hx,hy);assert(close_enough(hx,4.f/3)&&close_enough(hy,1));
    hud_at(aspect::k32x9,aspect::kHud16x9,hx,hy);assert(close_enough(hx,1)&&close_enough(hy,1));
    hud_at(aspect::k21x9,aspect::kHud16x9,hx,hy);assert(close_enough(hx,1)&&close_enough(hy,1));
    // A saved choice wider than the current picture falls back to the picture.
    hud_at(aspect::kOriginal,aspect::kHud32x9,hx,hy);assert(close_enough(hx,1)&&close_enough(hy,1));
    hud_at(aspect::k4x3,aspect::kHud16x9,hx,hy);assert(close_enough(hx,1)&&close_enough(hy,16.f/9/(4.f/3)));
    hud_at(aspect::k4x3,aspect::kHudMatch,hx,hy);assert(hx==1.0f);  // Match is exactly the picture's factors
    // Mods read display.hud_aspect for layout->HUD math; display.aspect keeps reporting the picture.
    aspect::set_mode(aspect::k32x9);aspect::on_swap();
    aspect::set_hud_mode(aspect::kHudMatch);assert(close_enough(aspect::hud_shape(),32.f/9));
    aspect::set_hud_mode(aspect::kHud21x9);assert(close_enough(aspect::hud_shape(),64.f/27));
    aspect::set_hud_mode(aspect::kHud16x9);
    assert(close_enough(aspect::hud_shape(),16.f/9)&&close_enough(aspect::game(),32.f/9));
    aspect::set_mode(aspect::kOriginal);aspect::on_swap();
    aspect::set_hud_mode(aspect::kHud32x9);assert(close_enough(aspect::hud_shape(),16.f/9));  // wider than the picture
    // Mod HUD edge anchors use the same shape; match display keeps today's value.
    aspect::set_mode(aspect::k32x9);aspect::on_swap();
    aspect::set_hud_mode(aspect::kHudMatch);assert(close_enough(aspect::hud_anchor_extra(1280),640));
    aspect::set_hud_mode(aspect::kHud21x9);assert(close_enough(aspect::hud_anchor_extra(1280),640.f/3));
    aspect::set_hud_mode(aspect::kHud16x9);assert(close_enough(aspect::hud_anchor_extra(1280),0));
    aspect::set_hud_mode(aspect::kHudMatch);
    aspect::set_mode(aspect::kWindow);aspect::set_window_aspect(1.431f);
    for(uint32_t base:{0x101E0000u,0x10460000u}) {
        void* address=mem::ptr(base);
#ifdef _WIN32
        assert(VirtualAlloc(address,0x20000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==address);
#else
        assert(mmap(address,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==address);
#endif
    }
    const uint32_t play=0x1046F0B0;
    std::strcpy((char*)mem::ptr(play+0x5134),"sea");
    st32(play+0x5B34,0x15000000);st32(play+0x5B2C,0x15000000);
    for(int mode:{aspect::kOriginal,aspect::k4x3,aspect::k16x10,aspect::k21x9,aspect::k32x9}) {
        aspect::set_mode(mode);aspect::set_fov_scale(1);aspect::on_swap();
        auto original=camera();site_024FFD60(&original);
        aspect::set_fov_scale(1.5f);aspect::on_swap();
        for(int stereoMode=stereo::Off;stereoMode<stereo::Count;++stereoMode) {
            stereo::requestedMode=stereoMode;
            auto wide=camera();site_024FFD60(&wide);
            assert(wide.f[1].ps0>original.f[1].ps0 && close_enough(wide.f[2].ps0,original.f[2].ps0));
            const double d=3.141592653589793/360;
            assert(close_enough(std::tan(wide.f[1].ps0*d)/std::tan(original.f[1].ps0*d),1.5));
            auto hd=camera();hd.f[3].ps0=45*d*2;hd.f[4].ps0=16.f/9;
            site_0250007C(&hd);
            assert(close_enough(hd.f[3].ps0/d/2,wide.f[1].ps0));
            assert(close_enough(hd.f[4].ps0,wide.f[2].ps0));
            for(auto hook:{site_024F811C,site_024F8168}) {
                auto clip=camera();hook(&clip);
                assert(close_enough(clip.f[1].ps0,wide.f[1].ps0));
                assert(clip.f[2].ps0>=wide.f[2].ps0);
            }
            {   // #656: frame interpolation's in-between culling frustum is the view_setup one, not the authored FOV's
                auto blended=camera();aspect::clipper_setup_args(&blended);
                auto clip=camera();site_024F811C(&clip);
                assert(blended.f[1].ps0==clip.f[1].ps0 && blended.f[2].ps0==clip.f[2].ps0);
                assert(close_enough(blended.f[1].ps0,wide.f[1].ps0));
            }
            // All event modes, dialogue, menus and transitions preserve the authored view.
            for(auto address:{play+0x5292,play+0x5BB2,play+0x5BB3,play+0x5140+12,0x101EA069u}) {
                st8(address,1);auto cut=camera();site_024FFD60(&cut);
                assert(close_enough(cut.f[1].ps0,original.f[1].ps0));
                auto hdCut=camera();hdCut.f[3].ps0=45*d*2;hdCut.f[4].ps0=16.f/9;
                site_0250007C(&hdCut);assert(close_enough(hdCut.f[3].ps0/d/2,original.f[1].ps0));
                st8(address,0);
            }
            for(uint8_t event:{2,3,255}) {
                st8(play+0x5292,event);auto cut=camera();site_024FFD60(&cut);
                assert(close_enough(cut.f[1].ps0,original.f[1].ps0));
            }
            st8(play+0x5292,0);
            st32(0x101F36CC,1);auto wipe=camera();site_024FFD60(&wipe);
            assert(close_enough(wipe.f[1].ps0,original.f[1].ps0));st32(0x101F36CC,0);
            st32(play+0x5B2C,0);auto control=camera();site_024FFD60(&control);
            assert(close_enough(control.f[1].ps0,original.f[1].ps0));st32(play+0x5B2C,0x15000000);
            auto menu=camera();site_025ADB38(&menu);assert(close_enough(menu.f[1].ps0,original.f[1].ps0));
            auto resume=camera();site_024FFD60(&resume);assert(close_enough(resume.f[1].ps0,wide.f[1].ps0));
        }
    }
    aspect::set_fov_scale(1);aspect::on_swap();aspect::set_mode(aspect::kOriginal);aspect::on_swap();
    auto stock=camera();site_024FFD60(&stock);assert(stock.f[1].ps0==45);
    stereo::requestedMode=stereo::Off;auto stockClip=camera();aspect::clipper_setup_args(&stockClip);
    assert(stockClip.f[1].ps0==45 && stockClip.f[2].ps0==16.f/9);  // (16:9 at 100 %: the game's own frustum, untouched)
    aspect::set_fov_scale(std::numeric_limits<float>::quiet_NaN());assert(aspect::fov_scale()==1);
    aspect::set_fov_scale(100);assert(aspect::fov_scale()==2.5f);aspect::on_swap();
    for(int mode:{aspect::kOriginal,aspect::k4x3,aspect::k16x10,aspect::k21x9,aspect::k32x9}) {
        aspect::set_mode(mode);aspect::on_swap();
        for(float input:{30.f,45.f,60.f,90.f}) {
            auto wide=camera();wide.f[1].ps0=input;site_024FFD60(&wide);
            assert(wide.f[1].ps0<=110 && wide.f[1].ps0>=input);
            for(auto hook:{site_024F811C,site_024F8168}) {
                auto clip=camera();clip.f[1].ps0=input;hook(&clip);
                assert(close_enough(clip.f[1].ps0,wide.f[1].ps0));
                assert(clip.f[2].ps0>=wide.f[2].ps0);
            }
        }
    }
    puts("Gameplay FOV: camera/culling agree across aspects and stereo modes; events, dialogue, menus and transitions retain authored FOV; control return restores slider.");
}

namespace guestmods::maphud { bool hide_pane(uint32_t) { return false; } }  // map/HUD bridge (mods/map_hud.cpp)
