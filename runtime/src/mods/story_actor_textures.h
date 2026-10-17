// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <initializer_list>
#include <vector>
#include <span>
#include <string>
struct Cpu;
namespace story_actor_textures {
inline constexpr uint32_t kFtexBytes=0xA0,kMaxDimension=512;
// Allocation and ledger are snapshot-owned and retained until session teardown.
// Verifier must prove these exact allocations belong to this immutable definition.
struct Texture {uint32_t ftex=0,image=0,capacity=0,width=0,height=0,definition=0;};
struct Binding {
 uint32_t actor=0,pid=0,epoch=0,generation=0,model=0;
 uint32_t material=0,slot=0,resource=0,original_ftex=0;
 uint32_t private_begin=0,private_end=0;
 bool no_pattern_animation=false;
};
using Invoke=uint32_t(*)(Cpu*,uint32_t,std::initializer_list<uint32_t>);
using VerifyTexture=bool(*)(const Texture&);
using VerifyOwner=bool(*)(const Binding&);
bool configure(Invoke,VerifyTexture,VerifyOwner);
// Original decoded RGBA8 only; 64x8 multiples, <=512 each, single sRGB mip.
// No allocation/free or hooks. Caller must not rebuild an already published texture.
bool initialize(Cpu*,const Texture&,const std::vector<uint8_t>& rgba);
bool texture_valid(const Texture&);
// Exact owned per-instance slot only. No shared resource/material/FTEX mutation.
// Typed caller must verify model field, supported material/slot names and no BTP.
bool bind(const Binding&,const Texture&);
struct Pixels {uint32_t width=0,height=0;std::vector<uint8_t> rgba;};
bool decode_png(std::span<const uint8_t>,Pixels&,std::string& why);
inline constexpr uint32_t kMaxDefinitions=8,kCacheBytes=0x2000+kMaxDefinitions*0x102000;
// Startup declarations only. Identity includes mod/id, audited target mapping and PNG.
// No removal/reuse during a loaded session; caches are never eagerly freed.
uint32_t declare_png(const std::string& identity,std::span<const uint8_t>,std::string& why);
size_t checkpoint();
bool rollback(size_t) noexcept; // startup-only; removed tokens never reused
// Called once before native actors exist. Runtime bump allocation is snapshot-owned.
bool allocate_session();
bool declared();
uint32_t cache_address();
// Reads guest ledger every call; host readiness never survives a snapshot rewind.
bool cached(Cpu*,uint32_t definition,Texture&);
bool cached_ready(uint32_t definition,Texture&); // no allocation/native calls
// No eager unbind/free: native queued draws can retain descriptor/image references.
}
