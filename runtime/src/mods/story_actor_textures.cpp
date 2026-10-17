// SPDX-License-Identifier: MPL-2.0
#include "story_actor_textures.h"
#include "runtime.h"
#include "guest_addr.h"
#include "guest_png.h"
#include "mod_hash.h"
#include <cstring>
#include <algorithm>
namespace story_actor_textures {namespace {
Invoke invoke=nullptr;VerifyTexture verify_texture=nullptr;VerifyOwner verify_owner=nullptr;
struct Definition{std::string identity,digest;Pixels pixels;uint32_t token;};
std::vector<Definition> definitions;uint32_t arena=0,next_token=1;
constexpr uint32_t CacheMagic=0x54584331,SlotStride=0x102000;
size_t index_of(uint32_t token){for(size_t i=0;i<definitions.size();++i)if(definitions[i].token==token)return i;return definitions.size();}
Texture slot_texture(size_t index){const auto&d=definitions[index];uint32_t p=arena+0x2000+uint32_t(index)*SlotStride;return {p,p+0x2000,0x100000,d.pixels.width,d.pixels.height,d.token};}
bool cache_verified(const Texture&t){size_t i=index_of(t.definition);if(!arena||i==definitions.size())return false;Texture own=slot_texture(i);if(std::memcmp(&own,&t,sizeof t))return false;uint32_t row=arena+0x180+uint32_t(i)*0x80;return ld32(arena)==CacheMagic&&ld32(row)>=1&&ld32(row)<=2&&ld32(row+68)==t.definition&&std::memcmp(mem::ptr(row+4),definitions[i].digest.data(),64)==0;}
bool span(uint32_t p,uint32_t n){return n&&!(p&3)&&((p>=mem::kMem2Start&&uint64_t(p)+n<=mem::kMem2End)||(p>=mem::kRuntimeStart&&uint64_t(p)+n<=mem::kHostStart));}
bool overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m){return uint64_t(a)<uint64_t(b)+m&&uint64_t(b)<uint64_t(a)+n;}
bool storage(const Texture&t){return t.definition&&t.width&&t.height&&t.width<=kMaxDimension&&t.height<=kMaxDimension&&!(t.width%64)&&!(t.height%8)&&t.capacity>=t.width*t.height*4&&t.capacity<=kMaxDimension*kMaxDimension*4&&!(t.image&0x1FFF)&&span(t.ftex,kFtexBytes)&&span(t.image,t.capacity)&&!overlap(t.ftex,kFtexBytes,t.image,t.capacity);}
bool verified(const Texture&t){try{return storage(t)&&(cache_verified(t)||(verify_texture&&verify_texture(t)));}catch(...){return false;}}
struct Preserve{Cpu*c;Cpu old;Preserve(Cpu*p):c(p),old(*p){}~Preserve(){*c=old;}};
}
bool configure(Invoke i,VerifyTexture t,VerifyOwner o){if(!i||!t||!o)return false;if(invoke&&(invoke!=i||verify_texture!=t||verify_owner!=o))return false;invoke=i;verify_texture=t;verify_owner=o;return true;}
bool texture_valid(const Texture&t){if(!verified(t))return false;uint32_t s=t.ftex+4,alignment=ld32(s+0x38);for(unsigned i=0;i<13;++i)if(ld32(s+0x40+4*i))return false;return ld32(t.ftex)==0x46544558&&ld32(s)==1&&ld32(s+4)==t.width&&ld32(s+8)==t.height&&ld32(s+12)==1&&ld32(s+16)==1&&ld32(s+20)==0x41A&&!ld32(s+0x18)&&ld32(s+0x1C)==1&&ld32(s+0x20)==t.width*t.height*4&&ld32(s+0x24)==t.image&&!ld32(s+0x28)&&!ld32(s+0x2C)&&ld32(s+0x30)==1&&!ld32(s+0x34)&&alignment&&alignment<=0x2000&&!(alignment&(alignment-1))&&!(t.image&(alignment-1))&&ld32(s+0x3C)==t.width&&!ld32(s+0x74)&&ld32(s+0x78)==1&&!ld32(s+0x7C)&&ld32(s+0x80)==1&&ld32(s+0x84)==0x00010203;}
bool initialize(Cpu*c,const Texture&t,const std::vector<uint8_t>&rgba){
 if(!c||!invoke||!verified(t)||rgba.size()!=uint64_t(t.width)*t.height*4)return false;
 // A nonzero descriptor may be published or restored; never overwrite it.
 for(uint32_t i=0;i<kFtexBytes;i+=4)if(ld32(t.ftex+i))return false;
 Preserve cpu(c);uint32_t s=t.ftex+4;
 st32(s,1);st32(s+4,t.width);st32(s+8,t.height);st32(s+12,1);st32(s+16,1);st32(s+20,0x41A);st32(s+0x1C,1);st32(s+0x30,1);
 try{
  invoke(c,GC(0x027E14AC),{s});
  const uint32_t alignment=ld32(s+0x38);
  if(ld32(s+0x20)!=rgba.size()||ld32(s+0x3C)!=t.width||ld32(s+0x28)||ld32(s+0x30)!=1||!alignment||alignment>0x2000||(alignment&(alignment-1))||(t.image&(alignment-1)))throw 0;
  st32(s+0x24,t.image);st32(s+0x78,1);st32(s+0x80,1);st32(s+0x84,0x00010203);
  invoke(c,GC(0x027E14A4),{s});
  std::memcpy(mem::ptr(t.image),rgba.data(),rgba.size());
  invoke(c,GC(0x027E14C0),{s});
  st32(t.ftex,0x46544558);
 }catch(...){std::memset(mem::ptr(t.ftex),0,kFtexBytes);return false;}
 return texture_valid(t);
}
bool bind(const Binding&b,const Texture&t){
 if(!verify_owner||!b.no_pattern_animation||!b.pid||b.pid==0xFFFFFFFF||!b.epoch||!b.generation||!span(b.actor,0x334)||ld32(b.actor+4)!=b.pid||!span(b.model,0x144)||!span(b.resource,0x12)||!span(b.original_ftex,kFtexBytes)||!texture_valid(t))return false;
 try{if(!verify_owner(b))return false;}catch(...){return false;}
 const uint32_t count=ld16(b.model+0x2A),materials=ld32(b.model+0x34);
 if(!count||count>64||b.material>=count||!span(materials,count*0x3C)||b.private_begin>=b.private_end)return false;
 auto owned=[&](uint32_t p,uint32_t n){return p>=b.private_begin&&uint64_t(p)+n<=b.private_end;};
 const uint32_t private_size=b.private_end-b.private_begin;
 if(!span(b.private_begin,private_size)||!owned(materials,count*0x3C)||overlap(b.private_begin,private_size,b.resource,0x12)||overlap(b.private_begin,private_size,b.original_ftex,kFtexBytes)||overlap(b.private_begin,private_size,t.ftex,kFtexBytes)||overlap(b.private_begin,private_size,t.image,t.capacity))return false;
 const uint32_t m=materials+b.material*0x3C,refs=ld32(m+0x2C),slots=ld8(b.resource+0x11);
 if(ld32(m)!=b.resource||!slots||slots>32||b.slot>=slots||!span(refs,slots*4)||!owned(refs,slots*4)||overlap(refs,slots*4,materials,count*0x3C)||overlap(refs,slots*4,b.resource,0x12)||overlap(refs,slots*4,t.ftex,kFtexBytes)||overlap(refs,slots*4,t.image,t.capacity))return false;
 uint32_t prior=ld32(refs+b.slot*4);if(prior!=b.original_ftex&&prior!=t.ftex)return false;
 st32(refs+b.slot*4,t.ftex);return true;
}
bool decode_png(std::span<const uint8_t>b,Pixels&out,std::string&why){
 out={};why.clear();auto fail=[&](const char*s){why=s;return false;};
 constexpr uint8_t signature[]={137,80,78,71,13,10,26,10};
 if(b.size()<33||b.size()>2*1024*1024||std::memcmp(b.data(),signature,8))return fail("Texture must be a PNG of at most 2 MiB");
 auto be=[&](size_t i){return uint32_t(b[i])<<24|uint32_t(b[i+1])<<16|uint32_t(b[i+2])<<8|b[i+3];};
 bool header=false,data=false,end=false;uint32_t w=0,h=0;
 for(size_t p=8;p<b.size();){if(b.size()-p<12)return fail("Truncated PNG chunk");uint32_t n=be(p);if(uint64_t(n)+12>b.size()-p)return fail("Invalid PNG chunk size");
  uint32_t crc=~0u;for(size_t i=p+4;i<p+8+n;++i){crc^=b[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xEDB88320u&uint32_t(-int32_t(crc&1)));}if((crc^~0u)!=be(p+8+n))return fail("PNG chunk CRC mismatch");
  const uint32_t type=be(p+4);if(!header&&type!=0x49484452)return fail("PNG IHDR must be first");
  if(type==0x49484452){if(header||n!=13)return fail("Invalid PNG IHDR");header=true;w=be(p+8);h=be(p+12);if(!w||!h||w>512||h>512||w%64||h%8||b[p+16]!=8||b[p+17]!=6||b[p+18]||b[p+19]||b[p+20])return fail("PNG must be noninterlaced 8-bit RGBA, 64x8 multiples, at most 512x512");}
  else if(type==0x49444154){if(end)return fail("PNG data after end");data=true;}
  else if(type==0x49454E44){if(n||!data||p+12!=b.size())return fail("Invalid PNG end");end=true;}
  else if(type==0x6163544C||type==0x6663544C||type==0x66644154)return fail("Animated PNG is unsupported");
  else if(!(b[p+4]&32))return fail("Unsupported critical PNG chunk");
  p+=size_t(n)+12;
 }
 if(!end)return fail("PNG end missing");
 try{auto pixels=guestmods::hud::decode_png(b);if(pixels.width!=w||pixels.height!=h||pixels.rgba.size()!=uint64_t(w)*h*4)return fail("Decoded PNG dimensions mismatch");out={w,h,std::move(pixels.rgba)};return true;}catch(...){return fail("PNG decoding failed");}
}
uint32_t declare_png(const std::string&identity,std::span<const uint8_t>b,std::string&why){
 why.clear();if(arena||identity.empty()||identity.size()>512){why="Texture declarations are startup-only with bounded identity";return 0;}
 Pixels pixels;if(!decode_png(b,pixels,why))return 0;
 std::string digest=mods::hash::sha256_text(std::string("actor-texture-v1:")+std::to_string(identity.size())+":"+identity+std::string(reinterpret_cast<const char*>(b.data()),b.size()));
 for(size_t i=0;i<definitions.size();++i)if(definitions[i].identity==identity){if(definitions[i].digest==digest)return definitions[i].token;why="Texture identity has conflicting content";return 0;}
 if(definitions.size()>=kMaxDefinitions){why="Texture definition capacity exceeded";return 0;}
 if(!next_token){why="Texture token space exhausted";return 0;}uint32_t token=next_token++;
 definitions.push_back({identity,digest,std::move(pixels),token});return token;
}
size_t checkpoint(){return arena?size_t(-1):definitions.size();}
bool rollback(size_t n) noexcept {if(arena||n>definitions.size())return false;definitions.resize(n);return true;}
bool allocate_session(){if(arena)return true;if(definitions.empty())return false;uint32_t p=mem::runtime_alloc(kCacheBytes,0x2000);if(!p||p<mem::kRuntimeStart||uint64_t(p)+kCacheBytes>mem::kHostStart||p%0x2000)return false;std::memset(mem::ptr(p),0,kCacheBytes);arena=p;st32(arena,CacheMagic);return true;}
uint32_t cache_address(){return arena;}
bool declared(){return !definitions.empty();}
bool cached_ready(uint32_t token,Texture&out){out={};size_t i=index_of(token);if(!arena||!token||i==definitions.size()||ld32(arena)!=CacheMagic)return false;uint32_t row=arena+0x180+uint32_t(i)*0x80;Texture t=slot_texture(i);if(ld32(row)!=2||!cache_verified(t)||!texture_valid(t))return false;out=t;return true;}
bool cached(Cpu*c,uint32_t token,Texture&out){
 out={};size_t index=index_of(token);if(!c||!arena||!token||index==definitions.size()||ld32(arena)!=CacheMagic)return false;uint32_t row=arena+0x180+uint32_t(index)*0x80;Texture t=slot_texture(index);uint32_t state=ld32(row);
 if(state)return cached_ready(token,out);
 for(unsigned i=0;i<0x80;i+=4)if(ld32(row+i))return false;
 std::memcpy(mem::ptr(row+4),definitions[index].digest.data(),64);st32(row+68,token);st32(row,1);
 if(!initialize(c,t,definitions[index].pixels.rgba)){st32(row,3);return false;}
 st32(row,2);out=t;return true;
}
}
