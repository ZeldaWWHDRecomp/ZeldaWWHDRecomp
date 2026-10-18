// W0-only integration for the original world-objects-w0 example.
#pragma once
#include "story_world_collision_proof_mesh.h"
#include "story_world_collision_budget.h"
#include "story_world_manifest.h"
#include "runtime.h"
#include "guest_addr.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
namespace story_world_collision::proof {
inline constexpr uint32_t State=0x850,Magic=0x5730434C,Matrix=State+16;
inline bool address(uint32_t p,uint32_t bytes){return p>=mem::kMem2Start&&bytes<=mem::kMem2End-mem::kMem2Start&&p<=mem::kMem2End-bytes;}
inline Kind selected(const std::string&mod,const story_notebook::ManifestActor&spec){
    const char*enable=std::getenv("WWHD_WORLD_OBJECT_PROOF");
    if(!enable||std::strcmp(enable,"1")||mod!="world-objects-w0"||spec.kind!="mod"||spec.combat_enabled||spec.carry_enabled||spec.scale.x!=1||spec.scale.y!=1||spec.scale.z!=1)return Kind::None;
    if(spec.id=="platform")return Kind::Platform;if(spec.id=="buoy")return Kind::Buoy;if(spec.id=="raft")return Kind::Raft;return Kind::None;
}
inline bool stored(uint32_t actor){return ld32(actor+State)==Magic&&ld32(actor+State+12)==ld32(actor+4)&&address(ld32(actor+State+4),0xBC);}
inline bool registered(uint32_t registry,uint32_t body,uint32_t actor){
    if(!address(registry,0x1404)||!address(body,0xBC))return false;
    const uint32_t slot=ld32(body);if(slot>=RegistrySlots)return false;
    const uint32_t entry=registry+slot*20;
    return (ld32(entry+4)&1)&&ld32(entry)==body&&ld32(entry+8)==ld32(actor+4)&&ld32(entry+12)==actor;
}
inline uint32_t occupied(uint32_t registry){uint32_t count=0;if(address(registry,0x1404))for(unsigned i=0;i<RegistrySlots;++i)count+=(ld32(registry+i*20+4)&1)!=0;return count;}
template<class Call> bool heap_create(Cpu*c,uint32_t actor,Kind kind,Call call){
    if(kind==Kind::None)return true;
    const auto geometry=mesh(kind);const auto size=encode(geometry,0,uint16_t(ld8(actor+0x326))).size();
    const uint32_t heap=call(c,GC(0x027EC230),{}),vt=address(heap,0x10)?ld32(heap+0xC):0;
    const uint32_t free_fn=address(vt,0x78)?ld32(vt+0x74):0,before=free_fn?call(c,free_fn,{heap}):0;
    const uint32_t data=call(c,GC(0x0273AD10),{uint32_t(size)});if(!address(data,uint32_t(size)))return false;
    const auto bytes=encode(geometry,data,uint16_t(ld8(actor+0x326)));std::memcpy(mem::ptr(data),bytes.data(),bytes.size());
    for(unsigned i=0;i<12;++i)st32(actor+Matrix+i*4,ld32(actor+0x56C+i*4));
    const uint32_t body=call(c,GC(0x024F23F4),{0});if(!address(body,0xBC))return false;
    if(call(c,GC(0x0200A030),{body,data,1,actor+Matrix}))return false;
    st32(body+0xA8,GC(0x024EE708));st16(body+0xB8,ld8(actor+0x326));st8(body+0xBB,ld8(actor+0x326));
    st32(actor+State,Magic);st32(actor+State+4,body);st32(actor+State+8,uint32_t(kind));st32(actor+State+12,ld32(actor+4));st32(actor+State+64,0);st32(actor+State+68,0);
    const uint32_t after=free_fn?call(c,free_fn,{heap}):0;
    LOG("[world_w0] heap actor=%08X pid=%u kind=%u vertices=%zu triangles=%zu source=%zu heap=%08X before=%u after=%u delta=%lld",actor,ld32(actor+4),unsigned(kind),geometry.vertices.size(),geometry.triangles.size(),size,heap,before,after,static_cast<long long>(before)-after);
    return true;
}
template<class Call> bool created(Cpu*c,uint32_t actor,Call call){
    if(!stored(actor))return true;
    const uint32_t play=call(c,GC(0x025200D4),{}),registry=play?play+0x12A0:0,body=ld32(actor+State+4);
    if(registered(registry,body,actor))return true;
    // A body that claims an occupied slot has uncertain provenance. Never steal it.
    if(!address(registry,0x1404)||ld32(body)<RegistrySlots||!slots_available(occupied(registry),1,16))return false;
    const uint32_t before=occupied(registry);if(call(c,GC(0x024EEA6C),{registry,body,actor})||!registered(registry,body,actor))return false;
    // Native heap entry has returned and shrunk the temporary construction heap.
    const uint32_t solid=ld32(actor+0xF4),parent=address(solid,0x28)?ld32(solid+0x24):0;
    if(address(parent,0x10)){const uint32_t vt=ld32(parent+0xC);if(address(vt,0x80)){
        const uint32_t free_fn=ld32(vt+0x74),largest_fn=ld32(vt+0x7C);
        const uint32_t free=free_fn?call(c,free_fn,{parent}):0,largest=largest_fn?call(c,largest_fn,{parent,32}):0;
        LOG("[world_w0] capacity actor=%08X solid=%08X parent=%08X free=%u largest_align32=%u",actor,solid,parent,free,largest);
    }}
    st32(actor+0x83C,0);st32(actor+0x39C,0); // Native background only, without a cylinder or dialogue prompt.
    LOG("[world_w0] register actor=%08X pid=%u body=%08X slot=%u occupied_before=%u occupied_after=%u room=%u",actor,ld32(actor+4),body,ld32(body),before,occupied(registry),unsigned(ld8(actor+0x326)));
    return true;
}
template<class Call> void pose(Cpu*c,uint32_t actor,Call call){
    if(!stored(actor)||ld32(actor+State+8)==uint32_t(Kind::Platform))return;
    const float x=u32_as_f32(ld32(actor+0x314)),z=u32_as_f32(ld32(actor+0x31C));
    auto wave=[&](float px,float pz){Cpu probe=*c;probe.f[1].ps0=px;probe.f[2].ps0=pz;call(&probe,GC(0x0246BA0C),{});return float(probe.f[1].ps0);};
    const float h=wave(x,z),left=wave(x-200,z),right=wave(x+200,z),front=wave(x,z-200),back=wave(x,z+200);
    if(!std::isfinite(h)||!std::isfinite(left)||!std::isfinite(right)||!std::isfinite(front)||!std::isfinite(back))return;
    constexpr float units=32768.f/3.14159265358979323846f;
    st32(actor+0x318,f32_as_u32(h+(ld32(actor+State+8)==uint32_t(Kind::Raft)?45.f:20.f)));
    st16(actor+0x328,uint16_t(int16_t(std::clamp(-std::atan2(back-front,400.f),-.15f,.15f)*units)));
    st16(actor+0x32C,uint16_t(int16_t(std::clamp(std::atan2(right-left,400.f),-.15f,.15f)*units)));
}
template<class Call> bool move(Cpu*c,uint32_t actor,Call call){
    if(!stored(actor))return true;
    const uint32_t play=call(c,GC(0x025200D4),{}),registry=play?play+0x12A0:0,body=ld32(actor+State+4);
    if(!registered(registry,body,actor)){LOG("[world_w0] rejected stale registration actor=%08X body=%08X",actor,body);return false;}
    for(unsigned i=0;i<12;++i)st32(actor+Matrix+i*4,ld32(actor+0x56C+i*4));
    call(c,GC(0x024F43DC),{body});const uint32_t tick=ld32(actor+State+64)+1;st32(actor+State+64,tick);
    if(tick%30==0){const uint32_t link=ld32(play+0x5B34);const bool riding=address(link,0x6600)&&(ld32(link+0x80C+0x28)&0x20)&&ld16(link+0x80C+0xD4+0x16)==ld32(body)&&ld32(link+0x80C+0xD4+0x18)==body;
        LOG("[world_w0] motion actor=%08X tick=%u body=%08X slot=%u occupied=%u y=%.3f pitch=%d roll=%d link=%08X riding=%u link_x=%.3f link_y=%.3f link_z=%.3f proc=%u",actor,tick,body,ld32(body),occupied(registry),double(u32_as_f32(ld32(actor+0x318))),int(int16_t(ld16(actor+0x328))),int(int16_t(ld16(actor+0x32C))),link,unsigned(riding),address(link,0x6600)?double(u32_as_f32(ld32(link+0x314))):0,address(link,0x6600)?double(u32_as_f32(ld32(link+0x318))):0,address(link,0x6600)?double(u32_as_f32(ld32(link+0x31C))):0,address(link,0x6600)?ld32(link+0x65F0):0xFFFFFFFFu);}
    return true;
}
template<class Call> void destroy(Cpu*c,uint32_t actor,Call call){
    if(!stored(actor))return;
    const uint32_t play=call(c,GC(0x025200D4),{}),registry=play?play+0x12A0:0,body=ld32(actor+State+4);
    if(registered(registry,body,actor)){
        const uint32_t before=occupied(registry),result=call(c,GC(0x020087EC),{registry,body});
        LOG("[world_w0] release actor=%08X body=%08X result=%u occupied_before=%u occupied_after=%u",actor,body,result,before,occupied(registry));
    }else LOG("[world_w0] release skipped foreign registration actor=%08X body=%08X",actor,body);
    st32(actor+State,0);
}
}
