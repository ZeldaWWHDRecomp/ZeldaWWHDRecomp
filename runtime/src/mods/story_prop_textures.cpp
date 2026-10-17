// SPDX-License-Identifier: MPL-2.0
#include "story_prop_textures.h"
#include "story_actor_textures.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cstring>
#include <array>
namespace story_prop_textures {namespace {
VerifyContext verify=nullptr;
struct Mapping{uint32_t type,material,count;const char* model;const char* material_name;const char* texture;};
constexpr Mapping mappings[]={{0,0,1,"obm_kotubo1","mkotubo1_v","Txm_tsubo02"},{3,0,2,"ktaru_01","lambert48","k_taru04"},{3,1,2,"ktaru_01","lambert49","k_taru05"},{15,0,1,"kkiba_00","lambert49","k_taru02"}};
const Mapping* mapping(uint32_t type,const std::string&texture){for(const auto&m:mappings)if(m.type==type&&texture==m.texture)return &m;return nullptr;}
bool span(uint32_t p,uint32_t n){return n&&!(p&3)&&p>=mem::kMem2Start&&uint64_t(p)+n<=mem::kMem2End;}
uint32_t relative(uint32_t p){if(!span(p,4))return 0;int32_t delta=int32_t(ld32(p));int64_t q=int64_t(p)+delta;if(!delta||q<mem::kMem2Start||q>=mem::kMem2End)return 0;return uint32_t(q);}
bool name(uint32_t p,const char*expected){size_t n=std::strlen(expected);if(!p||uint64_t(p)+n+1>mem::kMem2End)return false;return std::memcmp(mem::ptr(p),expected,n+1)==0;}
struct Pending{story_actor_textures::Binding binding;story_actor_textures::Texture texture;uint32_t entry,prior;};
}
bool configure(VerifyContext v){if(!v||(verify&&verify!=v))return false;verify=v;return true;}
bool supported(uint32_t type,const std::string&t){return mapping(type,t)!=nullptr;}
bool apply(Cpu*c,const Context&ctx,uint32_t phase,const std::vector<Replacement>&replacements,bool ready_only){
 if(!c||!verify||phase!=4||replacements.empty()||replacements.size()>2||!ctx.pid||ctx.pid==0xFFFFFFFF||!ctx.epoch||!ctx.generation||!span(ctx.actor,0x93C))return false;
 const uint32_t a=ctx.actor;if(ld32(a+4)!=ctx.pid||ld16(a+0xE)!=0x7FFD||ld32(a+0xB0)!=((ctx.type<<24)|0x007FFF3Fu)||ld32(a+0xB4)!=GD(0x10040BFC)||ld32(a+0x790)!=ctx.type||ld8(a+0x2DD)!=0xFF||ld32(a+0x3B8))return false;
 try{if(!verify(ctx))return false;}catch(...){return false;}
 const uint32_t model=ld32(a+0x3B4);if(!span(model,0x144))return false;const uint32_t fmdl=ld32(model+0x14);if(!span(fmdl,0x28)||ld32(fmdl)!=0x464D444C)return false;
 const uint32_t materials=ld32(model+0x34),count=ld16(model+0x2A);if(!count||count>2||!span(materials,count*0x3C))return false;
 std::array<Pending,2> pending{};size_t n=0;
 for(const auto&r:replacements){const Mapping*m=mapping(ctx.type,r.texture);if(!m||!r.definition||m->count!=count||!name(relative(fmdl+4),m->model))return false;for(size_t i=0;i<n;++i)if(pending[i].binding.material==m->material)return false;
  const uint32_t material=materials+m->material*0x3C,resource=ld32(material),refs=ld32(material+0x2C);
  if(!span(resource,0x2C)||ld32(resource)!=0x464D4154||!name(relative(resource+4),m->material_name)||ld8(resource+0x11)!=2||!span(refs,8))return false;
  const uint32_t names=relative(resource+0x28);if(!span(names,16)||!name(relative(names),m->texture)||!name(relative(names+8),"ZAtoon"))return false;
  // Native027F50F8/027F5180 updates all private references from the native
  // runtime texture table, including ZAtoon. Validate immutable source identity;
  // leave the independently managed live toon entry entirely untouched.
  const uint32_t original=relative(names+4),toon=relative(names+12);if(!span(original,0xA0)||ld32(original)!=0x46544558||!span(toon,0xA0)||ld32(toon)!=0x46544558)return false;
  auto&p=pending[n++];p.binding={a,ctx.pid,ctx.epoch,ctx.generation,model,m->material,0,resource,original,ctx.private_begin,ctx.private_end,true};p.entry=refs;p.prior=ld32(refs);
  if(ready_only?!story_actor_textures::cached_ready(r.definition,p.texture):!story_actor_textures::cached(c,r.definition,p.texture))return false;
 }
 // Core performs independent allocation/identity verification. Roll back any prior
 // own entry if a later guard refuses; no native calls occur during binding.
 for(size_t i=0;i<n;++i)if(!story_actor_textures::bind(pending[i].binding,pending[i].texture)){for(size_t j=0;j<i;++j)st32(pending[j].entry,pending[j].prior);return false;}
 return true;
}
}
