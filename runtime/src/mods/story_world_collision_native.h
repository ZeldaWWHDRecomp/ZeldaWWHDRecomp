// Guest-owned native background collision for static mod world objects.
#pragma once
#include "story_world_collision_mesh.h"
#include "story_actor_identity.h"
#include "story_world_collision_proof.h"
namespace story_world_collision::native {
inline constexpr uint32_t State=0x1040,Magic=0x57434C31,Matrix=0x1050,Identity=0x1080;
inline bool stored(uint32_t actor,const story_actors::identity::Fingerprint& identity){
    return proof::address(actor,0x10C0)&&ld32(actor+State)==Magic&&ld32(actor+State+8)==ld32(actor+4)&&proof::address(ld32(actor+State+4),0xBC)&&story_actors::identity::matches(mem::ptr(actor+Identity),identity);
}
// The identity an actor's stored collision is checked against: the current one, or, when the actor holds a body
// built with the first native layout (a full save state made before water and the later surfaces), that layout's
// identity; such a body keeps working until the room loads again and builds it anew.
inline const story_actors::identity::Fingerprint& accepted(uint32_t actor,const story_actors::identity::Fingerprint& current,const story_actors::identity::Fingerprint* first_layout){
    if(!first_layout||stored(actor,current)||!stored(actor,*first_layout))return current;
    return *first_layout;
}
template<class Call> bool heap_create(Cpu*c,uint32_t actor,const mesh::Mesh& geometry,const story_actors::identity::Fingerprint& identity,Call call){
    std::array<uint8_t,mesh::NativeResultIds.size()> raw{};uint32_t conversion[32];
    for(unsigned code=0;code<32;++code)conversion[code]=ld32(GD(0x101D51CC)+code*4);
    if(!mesh::native_attributes(conversion,mesh::NativeResultIds,raw))return false;
    std::string why;std::vector<uint8_t> bytes;
    if(!mesh::build_native(geometry,raw,uint16_t(ld8(actor+0x326)),bytes,why))return false;
    const uint32_t heap=call(c,GC(0x027EC230),{});if(!proof::address(heap,0x10))return false;
    const uint32_t vt=ld32(heap+0xC);if(!proof::address(vt,0x80))return false;
    const uint32_t free_fn=ld32(vt+0x74),largest_fn=ld32(vt+0x7C);
    const uint64_t needed=mesh::allocation_bytes(geometry);
    const uint32_t free_before=free_fn?call(c,free_fn,{heap}):0;
    if(!free_fn||!largest_fn||free_before<needed||call(c,largest_fn,{heap,32})<bytes.size()+32)return false;
    const uint32_t data=call(c,GC(0x0273AD10),{uint32_t(bytes.size())});
    if(!proof::address(data,uint32_t(bytes.size()))||!mesh::relocate(bytes,data,why))return false;
    std::memcpy(mem::ptr(data),bytes.data(),bytes.size());
    for(unsigned i=0;i<12;++i)st32(actor+Matrix+i*4,ld32(actor+0x56C+i*4));
    const uint32_t body=call(c,GC(0x024F23F4),{0});if(!proof::address(body,0xBC)||call(c,GC(0x0200A030),{body,data,1,actor+Matrix}))return false;
    st32(body+0xA8,GC(0x024EE708));st16(body+0xB8,ld8(actor+0x326));st8(body+0xBB,ld8(actor+0x326));
    st32(actor+State,Magic);st32(actor+State+4,body);st32(actor+State+8,ld32(actor+4));st32(actor+State+12,0);
    const auto record=story_actors::identity::record(identity);std::memcpy(mem::ptr(actor+Identity),record.data(),record.size());
    const uint32_t free_after=call(c,free_fn,{heap});
    LOG("[world_collision] build actor=%08X vertices=%zu triangles=%zu source=%zu estimate=%llu heap_delta=%u",actor,geometry.vertices.size(),geometry.triangles.size(),bytes.size(),static_cast<unsigned long long>(needed),free_before>=free_after?free_before-free_after:0);return true;
}
template<class Call> bool created(Cpu*c,uint32_t actor,const story_actors::identity::Fingerprint& identity,Call call){
    if(!stored(actor,identity))return false;
    const uint32_t play=call(c,GC(0x025200D4),{}),registry=play?play+0x12A0:0,body=ld32(actor+State+4);
    if(proof::registered(registry,body,actor))return true;
    if(!proof::address(registry,0x1404)||ld32(body)<RegistrySlots||!slots_available(proof::occupied(registry),1,16))return false;
    const uint32_t solid=ld32(actor+0xF4),parent=proof::address(solid,0x28)?ld32(solid+0x24):0;
    if(!proof::address(parent,0x10))return false;const uint32_t vt=ld32(parent+0xC);if(!proof::address(vt,0x80))return false;
    const uint32_t free_fn=ld32(vt+0x74),largest_fn=ld32(vt+0x7C);
    if(!free_fn||!largest_fn||call(c,free_fn,{parent})<1024*1024||call(c,largest_fn,{parent,32})<256*1024)return false;
    if(call(c,GC(0x024EEA6C),{registry,body,actor})||!proof::registered(registry,body,actor))return false;
    st32(actor+State+12,1);st32(actor+0x83C,0);st32(actor+0x39C,0);
    LOG("[world_collision] register actor=%08X pid=%u body=%08X slot=%u occupied=%u",actor,ld32(actor+4),body,ld32(body),proof::occupied(registry));return true;
}
template<class Call> bool move(Cpu*c,uint32_t actor,const story_actors::identity::Fingerprint& identity,Call call){
    if(!stored(actor,identity))return false;const uint32_t play=call(c,GC(0x025200D4),{}),body=ld32(actor+State+4);
    if(!play||!proof::registered(play+0x12A0,body,actor))return false;
    for(unsigned i=0;i<12;++i)st32(actor+Matrix+i*4,ld32(actor+0x56C+i*4));call(c,GC(0x024F43DC),{body});
    const char*trace=std::getenv("WWHD_WORLD_COLLISION_TRACE");if(trace&&std::strcmp(trace,"1")==0){const uint32_t tick=ld32(actor+State+12)+1;st32(actor+State+12,tick);if(tick%30==0){const uint32_t link=ld32(play+0x5B34);if(proof::address(link,0x6B00)&&ld32(link+0x8F8)==body){const uint32_t attribute=call(c,GC(0x024EF0F4),{play+0x12A0,link+0x8F4});LOG("[world_collision] wall poly=%u front=%u floor=%.3f flags=%08X mode=%08X",unsigned(ld16(link+0x9E4)),unsigned(ld8(link+0x68D5)),double(u32_as_f32(ld32(link+0x6A2C))),ld32(link+0x3B8),ld32(link+0x6A70));LOG("[world_collision] ground tick=%u link=%08X body=%08X poly=%u attribute=%u x=%.3f y=%.3f z=%.3f",tick,link,body,unsigned(ld16(link+0x8F4)),attribute,double(u32_as_f32(ld32(link+0x314))),double(u32_as_f32(ld32(link+0x318))),double(u32_as_f32(ld32(link+0x31C))));}}}
    return true;
}
template<class Call> void destroy(Cpu*c,uint32_t actor,Call call){
    // Releasing an old declaration after a reload is allowed only by native owner/PID.
    if(!proof::address(actor,0x10C0)||ld32(actor+State)!=Magic||ld32(actor+State+8)!=ld32(actor+4))return;
    const uint32_t play=call(c,GC(0x025200D4),{}),body=ld32(actor+State+4);
    if(play&&proof::registered(play+0x12A0,body,actor)){call(c,GC(0x020087EC),{play+0x12A0,body});LOG("[world_collision] release actor=%08X body=%08X",actor,body);}st32(actor+State,0);
}
}
