// Host ABI fixtures, not native gameplay. Observe inputs to the real runtime wrapper and model
// the native direction/BG-refinement contract without copying proprietary native code.
#include "mods/mods.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
namespace {
constexpr uint32_t link = 0x11000000, process = 0x11010000, camera = process+0x248;
bool focus=true, blocked=false, preview=false, throws=false, disable_inside=false;
unsigned calls=0;
uint16_t seen_yaw=0, seen_pitch=0;
void map(uint32_t a) {
#ifdef _WIN32
    auto p=VirtualAlloc(mem::ptr(a),65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    auto p=mmap(mem::ptr(a),65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
#endif
    assert(p==mem::ptr(a));
}
uint32_t type_style(unsigned type,unsigned mode) {return GD(0x10049350)+type*0x40+0x18+mode*2;}
uint32_t style_algorithm(unsigned style) {return GD(0x1004487C)+style*0x84+4;}
void fixture() {
    std::memset(mem::ptr(link),0,65536);std::memset(mem::ptr(process),0,65536);
    const auto styles=GD(0x1004487C)&~0xFFFFu, types=GD(0x10049350)&~0xFFFFu;
    std::memset(mem::ptr(styles),0,65536);if(types!=styles)std::memset(mem::ptr(types),0,65536);
    // Model only the native metadata entries needed here; no proprietary table/name copy.
    st32(GD(0x10049348),63);st32(GD(0x10044878),145);
    st16(type_style(7,0),83);st16(type_style(7,12),57);
    st16(type_style(60,0),86);st16(type_style(60,12),60);
    st32(style_algorithm(83),1);st32(style_algorithm(86),1);
    st32(style_algorithm(57),10);st32(style_algorithm(60),10);
    const auto play=GD(0x1046F0B0);st8(play+0x5292,0);st32(play+0x52E4,0);st32(play+0x5B00,0);
    st32(play+0x5AF8,process);st32(play+0x5B34,link);
    st8(camera+4,1);st32(camera+0x128,link);
    st32(camera+0x51C,7);st32(camera+0x520,7);st32(camera+0x524,7);
    st32(camera+0x514,57);st32(camera+0x528,0xFF);
    st32(camera+0x13C,12);st32(camera+0x140,12);
    for(const auto pair:{std::pair{0x8B0,7},std::pair{0x8B4,3},std::pair{0x8B8,62},
        std::pair{0x8C0,37},std::pair{0x8C4,38},std::pair{0x8C8,53},std::pair{0x8CC,2},std::pair{0x8D0,59}})
        st32(camera+pair.first,pair.second);
    // Gameplay looks right/up; render camera and ViewCache deliberately disagree.
    stf32(camera+0x10,100);stf32(camera+0x14,100);stf32(camera+0x18,0);
    stf32(process+0xDC,-100);stf32(process+0xE0,-100);st16(camera+0x42,0x8000);
    st32(link+0x65F0,0x92);st16(link+0x69B0,0x34);st32(link+0x834,0x20);
    st16(link+0x6984,10);st16(link+0x6986,123);st16(link+0x6988,456);
    st16(link+0x322,111);st16(link+0x32A,222);st16(link+0x3D0,333);
    focus=true;blocked=preview=throws=disable_inside=false;
}
void room_fixture() {
    fixture();st32(camera+0x51C,60);st32(camera+0x520,60);st32(camera+0x524,60);
    st32(camera+0x514,60);st32(camera+0x510,0x20);
}
}
namespace input { bool gameplay_focused(){return focus;} }
namespace overlay { bool blocks_input(){return blocked;} }
namespace true60 { bool preview(){return ::preview;} }
void log_msg(const char*,...) {}
extern "C" void f_024076CC_orig(Cpu* c) {
    ++calls;seen_yaw=ld16(c->r[3]+0x6986);seen_pitch=ld16(c->r[3]+0x6988);
    // The native BG check can refine pitch. It must remain effective during the call.
    st16(c->r[3]+0x6988,789);
    // Native lifetime/ammo/effects are not reset or rolled back by the wrapper.
    st16(c->r[3]+0x6984,ld16(c->r[3]+0x6984)-1);
    if(disable_inside)mods::set_camera_directed_items(false);
    if(throws)throw std::runtime_error("fixture unwind");
    c->r[3]=7;
}
extern "C" void hook_024076CC(Cpu*);
extern "C" void site_023E7A70(Cpu*);
void start(bool aligned,uint16_t yaw=0x4000,uint32_t actor_link=link) {
    Cpu c;std::memset(&c,0xA5,sizeof(c));c.r[30]=actor_link;
    Cpu expected;std::memcpy(&expected,&c,sizeof(c));
    unsigned char actor[65536];std::memcpy(actor,mem::ptr(link),sizeof(actor));
    if(aligned){actor[0x322]=actor[0x32A]=yaw>>8;actor[0x323]=actor[0x32B]=yaw&255;}
    site_023E7A70(&c);
    assert(!std::memcmp(&c,&expected,sizeof(c)));
    assert(!std::memcmp(mem::ptr(link),actor,sizeof(actor)));
}
void collision(bool directed) {
    Cpu c;std::memset(&c,0xA5,sizeof(c));c.r[3]=link;auto old_calls=calls;
    Cpu expected;std::memcpy(&expected,&c,sizeof(c));expected.r[3]=7;
    hook_024076CC(&c);assert(calls==old_calls+1&&!std::memcmp(&c,&expected,sizeof(c)));
    assert(ld16(link+0x322)==111&&ld16(link+0x32A)==222&&ld16(link+0x3D0)==333);
    if(directed) {
        assert(seen_yaw==0x4000&&int16_t(seen_pitch)==-0x2000);
        assert(ld16(link+0x6986)==123&&ld16(link+0x6988)==456);
    } else {
        assert(seen_yaw==123&&seen_pitch==456);
        assert(ld16(link+0x6988)==789); // Unmodified native behavior, including native writes.
    }
}
#include "camera_directed_items_actions_test.h"
int main(int argc,char**) {
    if(argc==2){assert(mods::camera_directed_items());return 0;}
    if(argc==3){assert(!mods::camera_directed_items());return 0;}
    assert(!mods::camera_directed_items());
    map(link);map(process);map(GD(0x1046F0B0)&~0xFFFFu);map((GD(0x1046F0B0)&~0xFFFFu)+65536);
    map(GD(0x1004487C)&~0xFFFFu);
    if((GD(0x10049350)&~0xFFFFu)!=(GD(0x1004487C)&~0xFFFFu))map(GD(0x10049350)&~0xFFFFu);
    fixture();collision(false);start(false);
    mods::set_camera_directed_items(true);fixture();collision(true);assert(ld16(link+0x6984)==9);
    // Actual native Forbidden Woods free-camera capture (no lock, active grounded gust):
    // room camera type 60 is not FIELD's named default 7, and ordinary flags include 0x20.
    // The original synthetic FIELD-only/zero-flags setup concealed this unreachable path.
    room_fixture();collision(true);
    room_fixture();start(true); // accepted native grounded use must face the sampled camera
    // Native camera-slot bit10 is present in ordinary free-camera item entries, not FP ownership.
    room_fixture();st32(GD(0x1046F0B0)+0x5B00,0x10);collision(true);start(true);
    fixture();st32(camera+0x510,0x20);collision(true); // ordinary mode marker, not event ownership
    room_fixture();st32(camera+0x510,0);collision(true); // custom type independently of flags
    // One gate at a time, then a valid context again: no stale queued steering or native pointers.
    for(int gate=0;gate<38;++gate){
        fixture();
        switch(gate){
        case 0:focus=false;break;case 1:blocked=true;break;case 2:preview=true;break;
        case 3:st8(camera+4,0);break;case 4:st8(camera+5,1);break;
        case 5:st32(camera+0x128,link+4);break;case 6:st32(camera+0x12C,link+4);break;
        case 7:st32(camera+0x134,link+4);break;case 8:st8(camera+0x19C,1);break;
        case 9:st32(camera+0x51C,5);break;case 10:st32(camera+0x520,5);break;
        case 11:st32(camera+0x510,0x20000000);break;case 12:st16(link+0x420,1);break;
        case 13:st8(GD(0x1046F0B0)+0x5292,1);break;
        case 14:st32(link+0x65F0,0x94);break;case 15:st16(link+0x69B0,0x27);break;
        case 16:stf32(camera+0x14,NAN);break;case 17:st32(camera+0x10,0);st32(camera+0x14,0);break;
        case 18:st32(link+0x65B0,process);break;case 19:st32(camera+0x138,1);break;
        case 20:st32(camera+0x140,2);break;case 21:st32(GD(0x1046F0B0)+0x5B34,link+4);break;
        case 22:st32(GD(0x1046F0B0)+0x5AF8,0);break;case 23:st32(link+0x834,0);break;
        case 24:st16(link+0x6984,0);break;case 25:st32(link+0x6A70,0x80);break;
        case 26:st16(link+0x6984,0xFFFF);break;
        case 27:st32(GD(0x1046F0B0)+0x52E4,1);break;
        case 28:st8(camera+0x518,1);break;case 29:st32(camera+0x528,60);break;
        case 30:st32(camera+0x514,145);break;case 31:st32(GD(0x10044878),0);break;
        case 32:st32(GD(0x10049348),0);break;
        case 33:st16(type_style(7,0),0xFFFF);break;
        case 34:st16(type_style(7,12),145);break;
        case 35:st32(camera+0x13C,13);st32(camera+0x140,13);break;
        case 36:st32(camera+0x51C,0xFFFFFFFF);st32(camera+0x520,0xFFFFFFFF);break;
        case 37:st32(camera+0x51C,63);st32(camera+0x520,63);st32(camera+0x524,63);break;
        }
        collision(false);
        // Initial use is accepted before a gust timer exists; all other guards are shared.
        start(gate==24||gate==26);fixture();collision(true);
    }
    // A second equivalent custom type prevents a numeric-60 whitelist regression.
    fixture();st32(camera+0x51C,8);st32(camera+0x520,8);st32(camera+0x524,8);
    st16(type_style(8,0),83);st16(type_style(8,12),57);collision(true);
    room_fixture();st32(camera+0x510,0x20000020);collision(false);
    room_fixture();st32(camera+0x520,53);collision(false);
    room_fixture();st32(camera+0x51C,53);collision(false);
    room_fixture();st32(link+0x65B0,process);collision(false);
    room_fixture();st32(camera+0x12C,link+4);collision(false);
    room_fixture();st32(camera+0x134,link+4);collision(false);
    room_fixture();st32(camera+0x138,1);collision(false);
    room_fixture();st8(camera+0x19C,1);collision(false);
    room_fixture();st32(link+0x6A70,0x80);collision(false);
    // Custom map-tool types still require FIELD's default and current-mode engine classes.
    room_fixture();st32(style_algorithm(86),11);collision(false); // custom event/default engine
    room_fixture();st32(style_algorithm(60),12);collision(false); // subject current engine
    room_fixture();st32(camera+0x514,57);collision(false); // stale style, even with same algorithm
    room_fixture();st32(camera+0x524,7);collision(false); // not the native room-selected type
    for(unsigned type:{3u,62u,37u,38u,53u,2u,59u}) {
        room_fixture();st32(camera+0x51C,type);st32(camera+0x520,type);st32(camera+0x524,type);
        st16(type_style(type,0),86);st16(type_style(type,12),60);collision(false);
    }
    for(bool room:{false,true}) {
        if(room)room_fixture();else fixture();st32(camera+0x13C,0);st32(camera+0x140,0);
        st32(camera+0x514,room?86:83);collision(true);start(true);
    }
    // Every other byte-sized inventory ID and player procedure is excluded, not a bow whitelist.
    for(unsigned item=0;item<256;++item){
        if(item==0x34)continue;fixture();st16(link+0x69B0,item);collision(false);start(false);
    }
    for(unsigned proc=0;proc<256;++proc){
        if(proc==0x92)continue;fixture();st32(link+0x65F0,proc);collision(false);start(false);
    }
    // Cardinal headings, signed wrap and downwards elevation use native angle units/sign.
    for(const auto x:{-100.f,0.f,100.f})for(const auto z:{-100.f,0.f,100.f}){
        if(x==0&&z==0)continue;fixture();stf32(camera+0x10,x);stf32(camera+0x14,-100);stf32(camera+0x18,z);
        Cpu c{};c.r[3]=link;hook_024076CC(&c);
        const double yaw=int16_t(seen_yaw)*3.141592653589793/32768;
        assert(std::fabs(std::sin(yaw)-x/std::hypot(x,z))<.001);
        assert(std::fabs(std::cos(yaw)-z/std::hypot(x,z))<.001);assert(int16_t(seen_pitch)>0);
        start(true,seen_yaw);
    }
    fixture();start(false,0,0);start(false,0,link+1);start(false,0,0xFFFF0000);
    // Native recoil and later camera motion are not continuously overwritten by collision.
    fixture();start(true);st16(link+0x322,0xC000);stf32(camera+0x10,-100);
    {Cpu c{};c.r[3]=link;hook_024076CC(&c);}
    assert(ld16(link+0x322)==0xC000&&ld16(link+0x32A)==0x4000&&seen_yaw==0xC000);
    fixture();disable_inside=true;collision(true);assert(!mods::camera_directed_items());start(false);
    mods::set_camera_directed_items(true);fixture();throws=true;
    Cpu c{};c.r[3]=link;try{hook_024076CC(&c);assert(false);}catch(const std::runtime_error&){}
    assert(ld16(link+0x6986)==123&&ld16(link+0x6988)==456);
    // A newly loaded native guest snapshot has no host pointer/token to clear.
    fixture();collision(true);start(true);mods::set_camera_directed_items(false);fixture();collision(false);start(false);
    action_tests();
}
