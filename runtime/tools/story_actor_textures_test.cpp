// SPDX-License-Identifier: MPL-2.0
#include "mods/story_actor_textures.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <zlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace mem {uint32_t runtime_alloc(uint32_t n,uint32_t align){assert(n==story_actor_textures::kCacheBytes&&align==0x2000);return 0x60100000;}}
namespace T=story_actor_textures;
namespace {
constexpr uint32_t A=0x20000000,M=0x20001000,N=0x20002000,R=0x20003000,F=0x20004000,P=0x20005000,Q=0x20006000,Refs=0x20007000,OtherRefs=0x20008000;
T::Texture texture{0x60010000,0x60012000,2048,64,8,7};bool owner=true,allocation=true,fault=false,badpitch=false;unsigned calls=0;
bool verify_texture(const T::Texture&t){return allocation&&t.ftex==texture.ftex&&t.image==texture.image&&t.definition==7;}
bool verify_owner(const T::Binding&b){return owner&&b.actor==A&&b.model==M&&b.pid==12&&b.epoch==3&&b.generation==8&&b.private_begin==P&&b.private_end==0x20009000&&b.resource==R&&b.original_ftex==F;}
uint32_t call(Cpu*c,uint32_t f,std::initializer_list<uint32_t>a){++calls;c->pc=99;c->r[5]=88;const uint32_t s=*a.begin();if(fault)throw std::runtime_error("native");if(f==0x027E14AC){st32(s+0x20,2048);st32(s+0x3C,badpitch?128:64);st32(s+0x38,256);}else assert(f==0x027E14A4||f==0x027E14C0);return 0;}
void map(uint32_t p,uint32_t n){
#ifdef _WIN32
 assert(VirtualAlloc(mem::ptr(p),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
 assert(mmap(mem::ptr(p),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
std::vector<uint8_t> png(uint8_t value=0xA5){
 std::vector<uint8_t> out{137,80,78,71,13,10,26,10};auto be=[](std::vector<uint8_t>&v,uint32_t n){for(int s=24;s>=0;s-=8)v.push_back(uint8_t(n>>s));};
 auto chunk=[&](const char*type,std::vector<uint8_t> data){be(out,uint32_t(data.size()));size_t p=out.size();out.insert(out.end(),type,type+4);out.insert(out.end(),data.begin(),data.end());be(out,uint32_t(crc32(0,out.data()+p,uInt(data.size()+4))));};
 std::vector<uint8_t> ihdr;be(ihdr,64);be(ihdr,8);ihdr.insert(ihdr.end(),{8,6,0,0,0});chunk("IHDR",ihdr);
 std::vector<uint8_t> raw(8*(1+64*4),value);for(unsigned y=0;y<8;++y)raw[y*257]=0;uLongf size=compressBound(raw.size());std::vector<uint8_t> compressed(size);assert(compress(compressed.data(),&size,raw.data(),raw.size())==Z_OK);compressed.resize(size);chunk("IDAT",compressed);chunk("IEND",{});return out;
}
}
int main(){map(A,0x10000);map(0x60010000,0x10000);assert(!T::declared());assert(T::configure(call,verify_texture,verify_owner));
 Cpu c{};c.pc=123;c.r[5]=456;Cpu before=c;std::vector<uint8_t> rgba(2048,0xA5);
 badpitch=true;assert(!T::initialize(&c,texture,rgba));assert(!ld32(texture.ftex)&&std::memcmp(&c,&before,sizeof c)==0);badpitch=false;
 fault=true;assert(!T::initialize(&c,texture,rgba));fault=false;assert(T::initialize(&c,texture,rgba)&&T::texture_valid(texture));assert(std::memcmp(&c,&before,sizeof c)==0);assert(std::memcmp(mem::ptr(texture.image),rgba.data(),rgba.size())==0);
 unsigned old=calls;assert(!T::initialize(&c,texture,rgba)&&calls==old); // published immutable descriptor
 st32(A+4,12);st16(M+0x2A,1);st32(M+0x34,P);st16(N+0x2A,1);st32(N+0x34,Q);st32(P,R);st32(Q,R);st8(R+0x11,2);st32(P+0x2C,Refs);st32(Q+0x2C,OtherRefs);for(auto p:{Refs,OtherRefs}){st32(p,F);st32(p+4,F);}
 uint8_t shared[0x2000];std::memcpy(shared,mem::ptr(R),sizeof shared);
 T::Binding b{A,12,3,8,M,0,1,R,F,P,0x20009000,true};assert(T::bind(b,texture));assert(ld32(Refs)==F&&ld32(Refs+4)==texture.ftex&&ld32(OtherRefs+4)==F);assert(std::memcmp(shared,mem::ptr(R),sizeof shared)==0);assert(T::bind(b,texture));
 owner=false;assert(!T::bind(b,texture));owner=true;st32(A+4,13);assert(!T::bind(b,texture));st32(A+4,12);
 b.no_pattern_animation=false;assert(!T::bind(b,texture));b.no_pattern_animation=true;b.slot=2;assert(!T::bind(b,texture));b.slot=1;
 st32(P+0x2C,R);assert(!T::bind(b,texture));st32(P+0x2C,Refs);st32(Refs+4,F+4);assert(!T::bind(b,texture));st32(Refs+4,F);
 allocation=false;assert(!T::bind(b,texture));allocation=true;
 // Snapshot before allocation must not adopt host-only ready state.
 uint8_t descriptor[T::kFtexBytes];std::memcpy(descriptor,mem::ptr(texture.ftex),sizeof descriptor);std::memset(mem::ptr(texture.ftex),0,sizeof descriptor);assert(!T::bind(b,texture));std::memcpy(mem::ptr(texture.ftex),descriptor,sizeof descriptor);assert(T::bind(b,texture));
 auto invalid=texture;invalid.image=invalid.ftex;assert(!T::texture_valid(invalid));invalid=texture;invalid.width=513;assert(!T::texture_valid(invalid));
 auto bytes=png();T::Pixels pixels;std::string why;assert(T::decode_png(bytes,pixels,why)&&pixels.width==64&&pixels.height==8&&pixels.rgba==rgba);
 auto broken=bytes;broken[20]^=1;assert(!T::decode_png(broken,pixels,why)&&pixels.rgba.empty());broken=bytes;broken.pop_back();assert(!T::decode_png(broken,pixels,why));broken=bytes;broken.push_back(0);assert(!T::decode_png(broken,pixels,why));
 broken=bytes;broken[18]=2;broken[19]=1;uint32_t crc=uint32_t(crc32(0,broken.data()+12,17));for(unsigned i=0;i<4;++i)broken[29+i]=uint8_t(crc>>(24-8*i));assert(!T::decode_png(broken,pixels,why)&&why.find("512")!=std::string::npos);
 uint32_t token=T::declare_png("ownmod/ownactor/material/color/v1",bytes,why);assert(token==1&&T::declare_png("ownmod/ownactor/material/color/v1",bytes,why)==token);
 assert(T::declared());
 assert(!T::declare_png("ownmod/ownactor/material/color/v1",png(0x19),why)&&why.find("conflicting")!=std::string::npos);
 size_t mark=T::checkpoint();uint32_t removed=T::declare_png("provisional",bytes,why);assert(removed&&T::rollback(mark));uint32_t replacement=T::declare_png("replacement",bytes,why);assert(replacement>removed);assert(T::rollback(mark));
 map(0x60100000,T::kCacheBytes);assert(T::allocate_session()&&T::cache_address()==0x60100000);assert(!T::declare_png("late",bytes,why));
 assert(!T::rollback(0)&&T::checkpoint()==size_t(-1));
 T::Texture cached;old=calls;assert(!T::cached_ready(token,cached)&&calls==old);assert(T::cached(&c,token,cached)&&calls==old+3);assert(T::cached_ready(token,cached)&&calls==old+3);assert(T::cached(&c,token,cached)&&calls==old+3);
 assert(!T::cached(&c,removed,cached)&&!T::cached(&c,replacement,cached));
 const uint32_t row=T::cache_address()+0x180;uint8_t record[0x80];std::memcpy(record,mem::ptr(row),sizeof record);st32(row+4,0);assert(!T::cached(&c,token,cached)&&calls==old+3);std::memcpy(mem::ptr(row),record,sizeof record);
 // Rewind before first texture use: same reserved allocation, no stale host-ready cache.
 std::memset(mem::ptr(row),0,sizeof record);std::memset(mem::ptr(0x60102000),0,T::kFtexBytes);assert(T::cached(&c,token,cached)&&calls==old+6);
 st32(row,1);assert(!T::cached(&c,token,cached));st32(row,3);assert(!T::cached(&c,token,cached));
 assert(std::memcmp(&c,&before,sizeof c)==0);
}
