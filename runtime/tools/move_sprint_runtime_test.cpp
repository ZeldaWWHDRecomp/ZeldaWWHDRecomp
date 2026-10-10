// Exercises the actual boost state and joint hook. Native callbacks are modelled at their guest ABI:
// jointBeforeCB optionally saves a quaternion, jointAfterCB restores it from Link's per-joint slot.
#include "mods/mods.h"
#include "input.h"
#include "runtime.h"
#include <cassert>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace {
constexpr uint32_t link = 0x11000000, quat = 0x11009000, table = 0x100366A0;
float dt = 1.f;
bool native_adjust = false;
unsigned calls = 0;
unsigned dust_calls = 0;
void map(uint32_t address) {
#ifdef _WIN32
    auto p = VirtualAlloc(mem::ptr(address), 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto p = mmap(mem::ptr(address), 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(p == mem::ptr(address));
}
void identity() { for(unsigned i=0;i<4;++i) st32(quat+i*4,f32_as_u32(i==3?1.f:0.f)); }
bool is_identity() { return ld32(quat)==0 && ld32(quat+4)==0 && ld32(quat+8)==0 && ld32(quat+12)==f32_as_u32(1.f); }
// This is the game's restore contract, independent of the mod's enable state at callback exit.
void after(unsigned joint) {
    auto flags = ld8(link+0x68E2+joint);
    if(flags&1) for(unsigned i=0;i<4;++i) st32(quat+i*4,ld32(link+0x6AB0+joint*16+i*4));
    st8(link+0x68E2+joint,0);
}
}
namespace interp { uint64_t logic_steps() { return 0; } }
namespace true60 { float dt() { return ::dt; } }
namespace mods { void mouse_release() {} }
void log_msg(const char*, ...) {}
uint32_t guest_call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args) {
    assert(fn==0x025A87C0);
    const std::vector<uint32_t> values(args);
    assert(values.size()==7 && values[0]==0x1100A000 && values[2]==link+0x314 && values[4]==link+0x110);
    assert(values[6]&1); // the native helper's dry-land smoke flag
    assert(c->f[1].ps0>1.f);
    ++dust_calls; c->r[8]=0xDEADBEEF; c->f[1].ps0=0;
    return 0x11009800;
}
extern "C" void f_023DE788_orig(Cpu*) {}
extern "C" void f_023E048C_orig(Cpu*) {}
extern "C" void f_023D6B30_orig(Cpu* c) {
    ++calls;
    if(native_adjust) {
        unsigned joint=c->r[4];
        for(unsigned i=0;i<4;++i) st32(link+0x6AB0+joint*16+i*4,ld32(c->r[6]+i*4));
        st8(link+0x68E2+joint,ld8(link+0x68E2+joint)|1);
        st32(c->r[6]+8,f32_as_u32(.1f));
        st32(c->r[6]+12,f32_as_u32(std::sqrt(.99f)));
    }
    c->r[3]=1; c->r[4]=0; c->r[6]=0; // volatile registers need not retain the arguments
}
extern "C" void hook_023D6B30(Cpu*);
void before(unsigned joint) {
    Cpu c{}; c.r[3]=link; c.r[4]=joint; c.r[6]=quat;
    hook_023D6B30(&c);
    assert(c.r[3]==1);
}
int main(int argc, char**) {
    if(argc==2) { assert(mods::move_speed_anim()==mods::MoveAnim::kSprint); return 0; }
    map(link); map(0x10030000); map(0x10470000); map(mem::kFixedStart);
    st32(link+0x65F0,mods::kProcMove);
    // Captured during ordinary running: free movement uses DIR_NONE (4), not DIR_FORWARD (0).
    st8(link+0x68D4,4); st32(link+0x6A70,0x211C4);
    st32(link+0x6A14,f32_as_u32(8.f)); st32(link+0x3C4,f32_as_u32(8.f));
    st16(table+8,0x120); st16(table+16,0x121);
    st16(link+0x5858,0x121); // currently playing the native run resource
    st16(link+0x5888,0xFFFF); st16(link+0x69B0,0xFF);
    st16(link+0x58A8+8,0); st16(link+0x58A8+10,24); st16(link+0x58A8+12,0);
    st32(link+0x58A8+4,f32_as_u32(6.f));
    mods::set_move_speed(true); mods::set_move_speed_anim(mods::MoveAnim::kSprint);
    mods::set_move_speed_stamina_seconds(0.f); mods::set_move_speed_land_factor(1.5f);
    mods::move_speed_input(input::kStickL);
    for(int i=0;i<30;++i) mods::link_move_factor(link);
    identity(); before(3);
    assert(!is_identity()); // the actual runtime hook must visibly change the torso pose
    assert(ld8(link+0x68E2+3)&1);
    after(3); assert(is_identity()); // no pose accumulation between model calculations
    // Repeated draws and true60 previews must not compound the additive rotation.
    for(int i=0;i<100;++i) {
        dt=.5f; mods::link_move_factor(link); before(3);
        const float z=u32_as_f32(ld32(quat+8)), w=u32_as_f32(ld32(quat+12));
        assert(z>0.f && std::abs(z*z+w*w-1.f)<1e-5f);
        after(3); assert(is_identity());
    }
    dt=1.f;
    // A native look/turn correction may already own the restore slot. Do not overwrite it.
    native_adjust=true;
    before(15); after(15); assert(is_identity());
    native_adjust=false;
    // Restoring the authored pose remains the native callback's job even after a live mode change.
    before(3); mods::set_move_speed_anim(mods::MoveAnim::kNative);
    after(3); assert(is_identity()); before(3); assert(is_identity());
    mods::set_move_speed_anim(mods::MoveAnim::kSprint);
    // The arm drive follows the actual run cycle and exchanges sides after half a stride.
    before(6); const auto left=ld32(quat+8); after(6);
    before(10); const auto right=ld32(quat+8); after(10);
    assert(left!=right);
    st32(link+0x58A8+4,f32_as_u32(18.f));
    before(6); assert(ld32(quat+8)==right); after(6);
    before(10); assert(ld32(quat+8)==left); after(10);
    // Neither the root nor the leg chain is rotated: foot contacts remain the game's own.
    for(unsigned joint:{0u,1u,2u,29u,30u,32u,34u}) { before(joint); assert(is_identity()); }
    // The native callback still runs, but no sprint layer leaks into incompatible actions.
    st32(link+0x65F0,mods::kProcSwimMove); before(3); assert(is_identity());
    st32(link+0x65F0,0); before(3); assert(is_identity());
    st32(link+0x65F0,mods::kProcMove);
    for(unsigned direction:{1u,2u,3u}) { st8(link+0x68D4,direction); before(3); assert(is_identity()); }
    st8(link+0x68D4,0); before(3); assert(!is_identity()); after(3);
    st8(link+0x68D4,4); before(3); assert(!is_identity()); after(3);
    st32(link+0x6A70,1); before(3); assert(is_identity()); st32(link+0x6A70,0x211C4);
    st16(link+0x5888,0x35); before(3); assert(is_identity()); st16(link+0x5888,0xFFFF);
    st16(link+0x5858,0x999); before(3); assert(is_identity()); st16(link+0x5858,0x121);
    st8(0x1046F0B0+0x5292,1); before(3); assert(is_identity()); st8(0x1046F0B0+0x5292,0);
    st32(link+0x6A14,0); before(3); assert(is_identity()); st32(link+0x6A14,f32_as_u32(8.f));
    // Releasing the boost blends towards the authored pose rather than leaving a frozen lean.
    before(3); float previous=u32_as_f32(ld32(quat+8)); after(3);
    mods::move_speed_input(0);
    for(int i=0;i<60;++i) {
        mods::link_move_factor(link); before(3);
        const float next=u32_as_f32(ld32(quat+8));
        assert(next>=0.f && next<=previous); previous=next;
        after(3); assert(is_identity());
    }
    assert(previous==0.f);
    mods::move_speed_input(input::kStickL);
    for(int i=0;i<30;++i) mods::link_move_factor(link);
    mods::set_move_speed(false); before(3); assert(is_identity());
    // A toggled sprint survives an ordinary forward roll, without boosting the roll itself.
    mods::link_move_factor(link);
    mods::set_move_speed(true); mods::set_move_speed_mode(mods::MoveMode::kToggle);
    mods::set_move_speed_stamina_seconds(5.f);
    mods::move_speed_input(input::kStickL); mods::link_move_factor(link);
    mods::move_speed_input(0); for(int i=0;i<15;++i) mods::link_move_factor(link);
    const float stamina_before_roll=mods::move_hud().stamina;
    st32(link+0x65F0,0x1E); // native procFrontRoll
    for(int i=0;i<15;++i) assert(mods::link_move_factor(link)==1.f);
    assert(mods::move_hud().stamina<stamina_before_roll);
    st32(link+0x65F0,mods::kProcMove);
    assert(mods::link_move_factor(link)>1.f);
    // One dust burst when a new grounded sprint starts, with the caller's CPU state preserved.
    mods::set_move_speed(false); mods::link_move_factor(link); mods::move_speed_input(0);
    mods::set_move_speed(true); mods::set_move_speed_mode(mods::MoveMode::kToggle);
    st32(link+0x834,0x20); st32(0x1046F0B0+0x5AB0,0x1100A000);
    Cpu fx{}; fx.r[8]=123; fx.f[1].ps0=456; const Cpu saved_fx=fx;
    mods::move_speed_input(input::kStickL); mods::link_move_factor(link);
    mods::move_start_effect(&fx,link);
    assert(dust_calls==1 && !memcmp(&fx,&saved_fx,sizeof fx));
    mods::move_speed_input(0);
    for(int i=0;i<5;++i) { mods::link_move_factor(link); mods::move_start_effect(&fx,link); }
    st32(link+0x65F0,mods::kProcFrontRoll);
    for(int i=0;i<10;++i) {
        assert(mods::link_move_factor(link)==1.f); mods::move_start_effect(&fx,link);
        assert(mods::move_hud().boosted);
        before(3); assert(is_identity()); // the sprint pose does not deform the roll animation
    }
    st32(link+0x65F0,mods::kProcMove);
    assert(mods::link_move_factor(link)>1.f); mods::move_start_effect(&fx,link);
    assert(dust_calls==1); // same cycle after the roll, no second burst
    // A real stop cancels toggle, and only a new press starts another dusty sprint.
    st32(link+0x65F0,4); mods::link_move_factor(link);
    st32(link+0x65F0,mods::kProcMove); assert(mods::link_move_factor(link)==1.f);
    mods::move_speed_input(input::kStickL); mods::link_move_factor(link); mods::move_start_effect(&fx,link);
    assert(dust_calls==2);
    // Cancellation during a roll must not silently resume at its end.
    mods::move_speed_input(0); mods::link_move_factor(link);
    st32(link+0x65F0,mods::kProcFrontRoll); mods::link_move_factor(link);
    mods::move_speed_input(input::kStickL); mods::link_move_factor(link);
    mods::move_speed_input(0); st32(link+0x65F0,mods::kProcMove);
    assert(mods::link_move_factor(link)==1.f);
    // Exhausting the bar during the roll ends the cycle, even if it starts refilling before exit.
    mods::set_move_speed(false); mods::link_move_factor(link); mods::set_move_speed(true);
    mods::set_move_speed_stamina_seconds(.2f);
    mods::move_speed_input(input::kStickL); mods::link_move_factor(link);
    mods::move_speed_input(0); st32(link+0x65F0,mods::kProcFrontRoll);
    for(int i=0;i<10;++i) mods::link_move_factor(link);
    assert(mods::move_hud().exhausted);
    st32(link+0x65F0,mods::kProcMove); assert(mods::link_move_factor(link)==1.f);
    // Start effects are never emitted from preview passes, water or off the ground.
    for(unsigned condition:{0u,1u,2u}) {
        mods::set_move_speed(false); mods::link_move_factor(link); mods::move_speed_input(0);
        mods::set_move_speed(true); mods::set_move_speed_stamina_seconds(5.f);
        st32(link+0x834,condition==1?0:0x20); st32(link+0x69D4,condition==2?0x13:0);
        mods::move_speed_input(input::kStickL); mods::link_move_factor(link);
        const auto count=dust_calls;
        if(condition==0) {
            dt=.5f; mods::move_start_effect(&fx,link); assert(dust_calls==count); dt=1.f;
        }
        mods::move_start_effect(&fx,link);
        assert(dust_calls==count+(condition==0?1:0));
        if(condition!=0) {
            st32(link+0x834,0x20); st32(link+0x69D4,0);
            if(condition==1) {
                mods::set_move_speed_land_factor(1.f); mods::link_move_factor(link); mods::move_start_effect(&fx,link);
                assert(dust_calls==count); // disabling the running multiplier also cancels pending dust
                mods::set_move_speed_land_factor(1.5f);
            }
            mods::link_move_factor(link); mods::move_start_effect(&fx,link);
            assert(dust_calls==count+1); // the first eligible grounded frame still gets its burst
        }
    }
    mods::set_move_speed(false); mods::link_move_factor(link); mods::move_speed_input(0);
    mods::set_move_speed(true); mods::set_move_speed_mode(mods::MoveMode::kHold);
    st32(link+0x65F0,mods::kProcFrontRoll); mods::move_speed_input(input::kStickL);
    mods::link_move_factor(link);
    assert(mods::move_hud().stamina==1.f && !mods::move_hud().boosted); // only an existing run carries
    assert(calls>100);
    return 0;
}
