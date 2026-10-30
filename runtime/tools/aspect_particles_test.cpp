#include "aspect.h"
#include "savestate.h"
#include "runtime.h"
#include "gx2/gx2_cmd.h"
#include <cassert>
#include <cmath>
#include <cstring>
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
namespace button_glyphs { void calculate_pane(Cpu* c,PpcFunc fn) { fn(c); } void draw_pane(Cpu* c,PpcFunc fn) { fn(c); } }
static constexpr uint32_t base=0x20000000, pane=base+0x100, out=base+0x200,
    root=base+0xa00, emitter=base+0x400, projection=base+0x900;
static float drawn[16];
extern "C" void hook_028766CC(Cpu*);
extern "C" {
void f_02874038_orig(Cpu*) {}
void f_028766CC_orig(Cpu* c) {
    if(c->r[3]==root) {
        c->r[3]=pane;hook_028766CC(c);c->r[3]=root;
    } else {
        stf32(pane+0x54,ldf32(pane+0x1c));stf32(pane+0x64,ldf32(pane+0x20));
    }
}
void f_02877100_orig(Cpu*) {}
void f_028F8250_orig(Cpu*) {}
void f_025A847C_orig(Cpu* c) { c->r[3]=emitter; }
void f_02704D2C_orig(Cpu* c) {
    stf32(c->r[3],ldf32(c->r[4]+0x54));
    stf32(c->r[3]+4,ldf32(c->r[4]+0x64));
    stf32(c->r[3]+8,0);
}
void f_0282D9C0_orig(Cpu* c) {
    for(unsigned i=0;i<16;++i) drawn[i]=ldf32(c->r[5]+4*i);
}
void hook_025A847C(Cpu*);
void hook_02704D2C(Cpu*);
void hook_0282D9C0(Cpu*);
}
static void mode(int m) { aspect::set_mode(m);aspect::on_swap(); }
static void pane_at(float x,float y) {
    stf32(pane+0x1c,x);stf32(pane+0x20,y);
    Cpu c{};c.r[3]=root;hook_028766CC(&c);
}
static void capture() {
    Cpu c{};c.r[3]=out;c.r[4]=pane;hook_02704D2C(&c);
    stf32(emitter+0x22c,ldf32(out));stf32(emitter+0x230,-ldf32(out+4));
    c.r[4]=7;c.r[6]=out;hook_025A847C(&c);
}
static void draw(unsigned group) {
    st8(emitter+0x262,group);
    uint32_t before[16];for(unsigned i=0;i<16;++i)before[i]=ld32(projection+4*i);
    const uint32_t x=ld32(emitter+0x22c),y=ld32(emitter+0x230);
    Cpu c{};c.r[3]=emitter+0x9c;c.r[5]=projection;hook_0282D9C0(&c);
    for(unsigned i=0;i<16;++i)assert(before[i]==ld32(projection+4*i));
    assert(x==ld32(emitter+0x22c)&&y==ld32(emitter+0x230));
}
static bool close(float a,float b) { return std::abs(a-b)<0.00001f; }
int main() {
    void* address=mem::ptr(base);
#ifdef _WIN32
    assert(VirtualAlloc(address,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==address);
#else
    assert(mmap(address,0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==address);
#endif
    std::strcpy((char*)mem::ptr(pane+0x80),"L_Arrow_00");
    stf32(root+0x48,1);stf32(root+0x5c,1);
    st32(pane+0x0c,root);st32(pane+0x14,pane+0x14);
    stf32(pane+0x34,1);stf32(pane+0x38,1);stf32(pane+0x3c,16);stf32(pane+0x40,16);
    stf32(projection,1.f/640);stf32(projection+20,-1.f/360);
    stf32(projection+40,-1);stf32(projection+60,1);
    mode(aspect::kOriginal);pane_at(390,280);capture();draw(7);
    assert(close(drawn[0],1.f/640)&&drawn[3]==0&&drawn[7]==0);
    for(unsigned i=0;i<16;++i) {
        uint32_t bits;std::memcpy(&bits,&drawn[i],4);assert(bits==ld32(projection+4*i));
    }
    // Authored animation is not an aspect offset, and must stay unchanged.
    pane_at(430,280);draw(7);assert(drawn[3]==0&&drawn[7]==0);
    pane_at(390,280);
    // An emitter created at 16:9 follows the same pane after widening.
    const float kx=(64.f/27)/(16.f/9), dx=640*(kx-1);
    mode(aspect::k21x9);pane_at(390,280);draw(7);
    assert(close(drawn[0],1.f/(640*kx))&&close(drawn[3],dx/(640*kx)));
    // A later pane query at the same coordinates cannot rebind a live emitter.
    const uint32_t other=base+0xc00;
    st32(other+0x0c,pane);st32(other+0x14,other+0x14);
    std::strcpy((char*)mem::ptr(other+0x80),"other_effect");
    stf32(other+0x54,390);stf32(other+0x64,280);
    Cpu duplicate{};duplicate.r[3]=out;duplicate.r[4]=other;hook_02704D2C(&duplicate);
    draw(7);assert(close(drawn[3],dx/(640*kx)));
    // The reverse switch must also undo the position captured at creation.
    capture();mode(aspect::kOriginal);pane_at(390,280);draw(7);
    assert(close(drawn[0],1.f/640)&&close(drawn[3],-dx/640));
    // Vert+ keeps effects round and follows the upper/lower pane anchors.
    mode(aspect::k16x10);const float ky=(16.f/9)/(16.f/10),dy=360*(ky-1);
    pane_at(390,280);draw(8);
    assert(close(drawn[5],-1.f/(360*ky))&&close(drawn[7],dy/(360*ky)));
    // All TV 2D groups share projection scaling; world and GamePad groups do not.
    for(unsigned group:{0u,4u,6u,11u,12u}) {
        draw(group);assert(close(drawn[0],1.f/640)&&close(drawn[5],-1.f/360));
        assert(drawn[3]==0&&drawn[7]==0);
    }
    for(unsigned group:{7u,8u,9u,10u}) {draw(group);assert(close(drawn[5],-1.f/(360*ky)));}
    // The association survives a full-state load in a fresh host history.
    ss::Writer state;aspect::ss_save_particles(state);
    ss::Reader saved(state.b.data(),state.b.size());
    assert(aspect::ss_validate_particles(saved));
    for(size_t n=1;n<state.b.size();++n)
        assert(!aspect::ss_validate_particles(ss::Reader(state.b.data(),n)));
    aspect::ss_load_particles(ss::Reader());draw(7);assert(drawn[3]==0&&drawn[7]==0);
    aspect::ss_load_particles(saved);aspect::ss_reset();pane_at(390,280);draw(7);
    assert(close(drawn[7],dy/(360*ky)));
    // Wider and window-derived custom ratios use the same projection path.
    for(int m:{aspect::k32x9,aspect::kWindow}) {
        if(m==aspect::kWindow)aspect::set_window_aspect(2.1f);
        mode(m);pane_at(390,280);draw(7);
        const float wide=aspect::game()/(16.f/9);
        assert(close(drawn[0],1.f/(640*wide)));
    }
    mode(aspect::k16x10);pane_at(390,280);
    // A recycled pane address with another name must not move an old emitter.
    std::strcpy((char*)mem::ptr(pane+0x80),"reused");pane_at(-500,-300);draw(7);
    assert(drawn[3]==0&&drawn[7]==0);
}

namespace guestmods::maphud { bool hide_pane(uint32_t) { return false; } }  // map/HUD bridge (mods/map_hud.cpp)
