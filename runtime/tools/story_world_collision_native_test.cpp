#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
// Exercises guest-owned lifecycle against a deterministic native ABI stand-in.
#include "mods/story_world_collision_native.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {
using namespace story_world_collision;
constexpr uint32_t Arena=0x20000000,Bytes=0x100000,Actor=Arena+0x10000,Play=Arena+0x20000,Heap=Arena+0x1000,Parent=Arena+0x1800,Vtable=Arena+0x2000;
uint32_t cursor=Arena+0x40000,body=0,sets=0,registrations=0,releases=0,moves=0,allocations=0;
uint32_t heap_free=8*1024*1024,largest=8*1024*1024,parent_free=8*1024*1024,parent_largest=8*1024*1024;
bool solid_context=true,fail_register=false,fail_set=false;
void map(uint32_t address,uint32_t bytes){
#ifdef _WIN32
    assert(VirtualAlloc(mem::ptr(address),bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
    assert(mmap(mem::ptr(address),bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
uint32_t invoke(Cpu*,uint32_t fn,std::initializer_list<uint32_t> args){const std::vector<uint32_t>a(args);switch(fn){
    case 0x027EC230:return Heap;
    case 0x02000010:return a.at(0)==Parent?parent_free:heap_free;
    case 0x02000014:assert(a.at(1)==32);return a.at(0)==Parent?parent_largest:largest;
    case 0x0273AD10:{assert(solid_context);++allocations;const auto p=cursor;cursor=(cursor+a.at(0)+15)&~15u;return p;}
    case 0x024F23F4:assert(solid_context&&a.at(0)==0);body=cursor;cursor+=0xC0;st32(body,256);return body;
    case 0x0200A030:assert(solid_context&&a.at(0)==body&&a.at(2)==1&&a.at(3)==Actor+native::Matrix);assert(ld32(a.at(1)+0x30)==0x80000000);for(unsigned i=0;i<9;++i)assert(((ld32(ld32(a.at(1)+0x2C)+i*16+4)>>16)&31)==(i<5?31-i:i==8?29:26));assert(ld32(ld32(a.at(1)+0x2C)+6*16+12)==0xED);assert((ld32(ld32(a.at(1)+0x2C)+7*16+4)&0xF00)==0x100);assert(ld32(ld32(a.at(1)+0x2C)+8*16+12)==0x10);st32(body+0x94,a.at(1));++sets;return fail_set?1:0;
    case 0x025200D4:return Play;
    case 0x024EEA6C:{assert(!solid_context&&a.at(0)==Play+0x12A0&&a.at(1)==body&&a.at(2)==Actor);if(fail_register)return 1;st32(body,0);const uint32_t e=Play+0x12A0;st32(e,body);st32(e+4,1);st32(e+8,ld32(Actor+4));st32(e+12,Actor);++registrations;return 0;}
    case 0x020087EC:{assert(a.at(0)==Play+0x12A0&&a.at(1)==body);st32(Play+0x12A0+4,0);st32(body,256);++releases;return 0;}
    case 0x024F43DC:assert(a.at(0)==body);++moves;return 0;
    default:assert(!"unexpected native call");return 0;
}}
}
int main(){using namespace story_world_collision;map(Arena,Bytes);map(0x101D0000,0x10000);for(unsigned i=0;i<32;++i)st32(0x101D51CC+i*4,0xFFFFFFFF);{unsigned code=31;for(unsigned i=0;i<mesh::NativeResultIds.size();++i){bool seen=false;for(unsigned j=0;j<i;++j)seen|=mesh::NativeResultIds[j]==mesh::NativeResultIds[i];if(!seen)st32(0x101D51CC+(code--)*4,mesh::NativeResultIds[i]);}}
    st32(Heap+0xC,Vtable);st32(Parent+0xC,Vtable);st32(Vtable+0x74,0x02000010);st32(Vtable+0x7C,0x02000014);st32(Heap+0x24,Parent);st32(Actor+0xF4,Heap);st32(Actor+4,17);st8(Actor+0x326,12);for(unsigned r=0;r<3;++r)st32(Actor+0x56C+(r*4+r)*4,f32_as_u32(1));
    story_actors::identity::Fingerprint identity{};identity[0]=42;auto other=identity;other[0]=43;Cpu cpu{};mesh::Mesh geometry;std::string why;assert(mesh::make_shape("box",{100,100,100},0,0,0,geometry,why));
    heap_free=1;assert(!native::heap_create(&cpu,Actor,geometry,identity,invoke)&&allocations==0);heap_free=8*1024*1024;largest=1;assert(!native::heap_create(&cpu,Actor,geometry,identity,invoke)&&allocations==0);largest=8*1024*1024;
    st32(0x101D51CC+31*4,0xFFFFFFFF);assert(!native::heap_create(&cpu,Actor,geometry,identity,invoke)&&allocations==0);st32(0x101D51CC+31*4,mesh::NativeResultIds[0]);
    assert(native::heap_create(&cpu,Actor,geometry,identity,invoke)&&sets==1&&native::stored(Actor,identity));assert(!native::stored(Actor,other));
    // a body from an older full save state (the first layout's identity) is accepted only by that identity
    assert(&native::accepted(Actor,other,&identity)==&identity&&&native::accepted(Actor,identity,&other)==&identity&&&native::accepted(Actor,other,nullptr)==&other);{auto third=identity;third[1]=7;assert(&native::accepted(Actor,other,&third)==&other);}solid_context=false;
    parent_free=1;assert(!native::created(&cpu,Actor,identity,invoke)&&registrations==0);parent_free=8*1024*1024;parent_largest=1;assert(!native::created(&cpu,Actor,identity,invoke)&&registrations==0);parent_largest=8*1024*1024;
    fail_register=true;assert(!native::created(&cpu,Actor,identity,invoke)&&registrations==0);fail_register=false;
    for(unsigned i=0;i<240;++i)st32(Play+0x12A0+i*20+4,1);assert(!native::created(&cpu,Actor,identity,invoke)&&registrations==0);for(unsigned i=0;i<240;++i)st32(Play+0x12A0+i*20+4,0);
    assert(native::created(&cpu,Actor,identity,invoke)&&registrations==1);assert(native::created(&cpu,Actor,identity,invoke)&&registrations==1);assert(ld32(Actor+0x83C)==0&&ld32(Actor+0x39C)==0);assert(native::move(&cpu,Actor,identity,invoke)&&moves==1);assert(!native::move(&cpu,Actor,other,invoke)&&moves==1);
    const std::vector<uint8_t> snapshot(mem::ptr(Arena),mem::ptr(Arena)+Bytes);native::destroy(&cpu,Actor,invoke);assert(releases==1&&!native::stored(Actor,identity));native::destroy(&cpu,Actor,invoke);assert(releases==1);
    std::memcpy(mem::ptr(Arena),snapshot.data(),Bytes);assert(native::stored(Actor,identity)&&native::created(&cpu,Actor,identity,invoke)&&registrations==1);assert(native::move(&cpu,Actor,identity,invoke)&&moves==2);
    st32(Play+0x12A0+8,99);assert(!native::move(&cpu,Actor,identity,invoke));native::destroy(&cpu,Actor,invoke);assert(releases==1&&ld32(Play+0x12A0+4)==1); // foreign PID is never released
    std::memcpy(mem::ptr(Arena),snapshot.data(),Bytes);st32(Play+0x12A0+12,Actor+0x2000);native::destroy(&cpu,Actor,invoke);assert(releases==1); // foreign owner pointer
    std::memcpy(mem::ptr(Arena),snapshot.data(),Bytes);st32(Actor+4,18);native::destroy(&cpu,Actor,invoke);assert(releases==1); // recycled actor address
    std::memcpy(mem::ptr(Arena),snapshot.data(),Bytes);st32(Actor+native::State+4,0xFFFFFFFF);native::destroy(&cpu,Actor,invoke);assert(releases==1); // invalid body pointer
}
