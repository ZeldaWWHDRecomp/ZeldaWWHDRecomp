#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>
// Synthetic guest memory tests the real W0 integration, not game physics.
#include "mods/story_world_collision_proof.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
using namespace story_world_collision::proof;
namespace {
constexpr uint32_t Arena=0x20000000,Bytes=0x100000,Actor=Arena+0x10000,Play=Arena+0x20000,Heap=Arena+0x1000,Vtable=Arena+0x2000;
uint32_t cursor=Arena+0x40000,body=0,sets=0,registrations=0,releases=0,moves=0;
bool solid_context=true;
uint32_t invoke(Cpu*c,uint32_t fn,std::initializer_list<uint32_t> args){
    const std::vector<uint32_t> a(args);
    switch(fn){
    case 0x027EC230:return Heap;
    case 0x02000010:return Arena+Bytes-cursor;
    case 0x0273AD10:{assert(solid_context);const auto out=cursor;cursor=(cursor+a.at(0)+15)&~15u;return out;}
    case 0x024F23F4:assert(solid_context&&a.at(0)==0);body=cursor;cursor+=0xC0;st32(body,256);return body;
    case 0x0200A030:assert(solid_context&&a.at(0)==body&&a.at(2)==1&&a.at(3)==Actor+Matrix);assert(ld32(a.at(1))==16&&ld32(a.at(1)+8)==20);st32(body+0x94,a.at(1));++sets;return 0;
    case 0x025200D4:return Play;
    case 0x024EEA6C:{assert(a.at(0)==Play+0x12A0&&a.at(1)==body&&a.at(2)==Actor);st32(body,0);const uint32_t e=Play+0x12A0;st32(e,body);st32(e+4,1);st32(e+8,ld32(Actor+4));st32(e+12,Actor);++registrations;return 0;}
    case 0x020087EC:{assert(a.at(0)==Play+0x12A0&&a.at(1)==body);st32(Play+0x12A0+4,0);st32(body,256);++releases;return 0;}
    case 0x024F43DC:assert(a.at(0)==body);++moves;return 0;
    case 0x0246BA0C:c->f[1].ps0=.01*c->f[1].ps0+.02*c->f[2].ps0;return 0;
    default:assert(!"unexpected native call");return 0;
    }
}
}
int main(int argc,char**argv){
    if(argc>1){std::ifstream source(argv[1]);assert(source);const std::string json((std::istreambuf_iterator<char>(source)),{});
        const auto manifest=mods::json::parse(json);std::string why;std::vector<story_notebook::ManifestModel> models;std::vector<story_notebook::ManifestActor> actors;
        assert(story_notebook::parse_models(manifest,models,why)&&models.size()==3);
        if(!story_notebook::parse_actors(manifest,actors,why)){std::fprintf(stderr,"%s\n",why.c_str());assert(false);}assert(actors.size()==3);
        for(const auto&a:actors)assert(a.stage=="sea"&&a.room==12&&a.kind=="mod"&&!a.combat_enabled&&!a.carry_enabled);
    }

#ifdef _WIN32
    assert(VirtualAlloc(mem::ptr(Arena),Bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
    assert(mmap(mem::ptr(Arena),Bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
    st32(Heap+0xC,Vtable);st32(Vtable+0x74,0x02000010);st32(Actor+4,17);st8(Actor+0x326,11);
    for(unsigned r=0;r<3;++r)st32(Actor+0x56C+(r*4+r)*4,f32_as_u32(1));
    st32(Actor+0x314,f32_as_u32(4000));st32(Actor+0x31C,f32_as_u32(-200000));
    Cpu cpu{};assert(heap_create(&cpu,Actor,Kind::None,invoke)&&sets==0);
    assert(heap_create(&cpu,Actor,Kind::Raft,invoke)&&sets==1&&stored(Actor));solid_context=false;
    assert(ld16(body+0xB8)==11&&ld8(body+0xBB)==11&&ld32(body+0xA8)==0x024EE708);
    assert(created(&cpu,Actor,invoke)&&registered(Play+0x12A0,body,Actor)&&registrations==1);
    assert(created(&cpu,Actor,invoke)&&registrations==1&&ld32(Actor+0x83C)==0);
    pose(&cpu,Actor,invoke);assert(std::fabs(u32_as_f32(ld32(Actor+0x318))+3915)<.01&&int16_t(ld16(Actor+0x328))<0&&int16_t(ld16(Actor+0x32C))>0);
    assert(move(&cpu,Actor,invoke)&&moves==1);
    const std::vector<uint8_t> snapshot(mem::ptr(Arena),mem::ptr(Arena)+Bytes);
    destroy(&cpu,Actor,invoke);assert(releases==1&&!stored(Actor)&&occupied(Play+0x12A0)==0);
    std::memcpy(mem::ptr(Arena),snapshot.data(),Bytes);assert(stored(Actor)&&move(&cpu,Actor,invoke)&&moves==2);
    assert(created(&cpu,Actor,invoke)&&registrations==1); // full-state restore needs no duplicate registration
    st32(Play+0x12A0+8,99);assert(!move(&cpu,Actor,invoke)&&moves==2);
    destroy(&cpu,Actor,invoke);assert(releases==1&&occupied(Play+0x12A0)==1); // never release another owner
    st32(Actor+State,Magic);st32(Actor+State+4,0xFFFFFFFF);assert(!stored(Actor));
}
