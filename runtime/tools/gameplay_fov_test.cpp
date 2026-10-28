#include "aspect.h"
#include "gfx/stereo.h"
#include "gx2/gx2_cmd.h"
#include "runtime.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <limits>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

void log_msg(const char*, ...) {}
namespace gx2 {
void emit(Op, const uint32_t*, uint32_t) {}
bool recording_display_list() { return false; }
}
namespace mods::cemu { float aspect_ratio() { return 0; } }
namespace mods { void play_context_pane(uint32_t) {} }  // the context buttons' pane observer (mods/play_context.cpp)
namespace mods { float hud_fade_alpha_of(uint32_t) { return 1.f; } int hud_pane_hide(uint32_t) { return 0; } }  // HUD fades (mods/hud_fade.cpp)
namespace button_glyphs { void calculate_pane(Cpu* c,PpcFunc fn) { fn(c); } }
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
    for(int mode:{aspect::kOriginal,aspect::k16x10,aspect::k21x9,aspect::k32x9}) {
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
    aspect::set_fov_scale(std::numeric_limits<float>::quiet_NaN());assert(aspect::fov_scale()==1);
    aspect::set_fov_scale(100);assert(aspect::fov_scale()==2.5f);aspect::on_swap();
    for(int mode:{aspect::kOriginal,aspect::k16x10,aspect::k21x9,aspect::k32x9}) {
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
