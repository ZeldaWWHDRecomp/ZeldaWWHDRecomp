// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_people_model_graph.h"
#include "guest_addr.h"
#include "ppc.h"
#include "mods/story_native_refresh_dispatch_audit.h"
#include <cassert>
#include <map>
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
#include <utility>
#include <functional>
#include <string_view>
#include <memory>
namespace G=story_native_people_model_graph;
// Test-only issuer; the production issuer remains solely in threads.cpp.
namespace threads {class NativeQuiescenceIssuer {public:static NativeQuiescence make(std::function<bool(const Cpu*)>c,bool readonly=false){return NativeQuiescence(std::move(c),[]{},readonly);}};}
namespace story_native_refresh_dispatch {struct Fixture {static Guard make(std::function<bool()>v){return Guard(std::move(v));}};}
void put(auto&b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
struct Fixture {
 Cpu cpu{};bool quiescent=true,quiescence_current=true,invalidate_quiescence_on_read=false,wrong_cpu=false;std::shared_ptr<threads::NativeQuiescence> quiescence;
 G::Expected e;std::map<uint32_t,std::vector<uint8_t>>memory;bool preclaim=true,pinned=true,dispatch=true,dispatch_current=true;std::shared_ptr<story_native_refresh_dispatch::Guard> guard;unsigned reads=0;bool unstable=false,invalidate_on_read=false;
 uint32_t control=0x20000000,info=0x20100000,ids=0x20100100,resource=0x49000000,data=0x49000200,rows=0x49000300,fmdl=0x4A000400,skeleton=0x4A000480,bones=0x4A001000,raw=0x4A000000;
 std::array<uint32_t,2>actor{0x47001000,0x47002000},heap{0x48000000,0x48010000},morf{},model{},ref{},anim{0x49000500,0x49000600};
 Fixture(){quiescence=std::make_shared<threads::NativeQuiescence>(threads::NativeQuiescenceIssuer::make([this](const Cpu*c){return c==&cpu&&quiescence_current;}));guard=std::make_shared<story_native_refresh_dispatch::Guard>(story_native_refresh_dispatch::Fixture::make([this]{return dispatch_current;}));e.identity={0x20010000,0x60000000,0x520,1,2,3,4};
  auto region=[&](uint32_t p,unsigned bytes){memory[p].resize(bytes);};
  region(GD(0x101C41D4),0x28);w(GD(0x101C41D4)+0x1C,0x432A0000);w(GD(0x101C41D4)+0x24,0x43160000);
  region(control,0x2130);region(info,0x8C);region(ids,48);region(resource,0x10000);region(raw,0x10000);
  region(GD(0x1018C7B0),4);w(GD(0x1018C7B0),GD(0x101FF5F0));region(GD(0x101FF5F0),0x30);
  region(GD(0x101F4F28),4);w(GD(0x101F4F28),control);region(GD(0x10058CC0),40);unsigned n=0;for(uint32_t v:{0u,0u,0u,GC(0x025EE1D8),0u,GC(0x027F3B28),0u,GC(0x028F036C),0u,GC(0x025E5C38)})w(GD(0x10058CC0)+4*n++,v);
  region(GD(0x1016C6C0),32);n=0;for(uint32_t v:{0xFFFFu,GC(0x027E4D20),0xFFFFu,GC(0x027E50A8),0xFFFFu,GC(0x027E565C),0xFFFFu,GC(0x027E5DA4)})w(GD(0x1016C6C0)+4*n++,v);
  region(GD(0x10145B38),0x70);w(GD(0x10145B38)+0x5C,GC(0x02755280));w(GD(0x10145B38)+0x6C,GC(0x02755298));
  auto init_heap=[&](uint32_t p){w(p+0xC,GD(0x10145B38));w(p+0x1C,p);w(p+0x20,0x10000);};init_heap(resource);
  w(control,GD(0x100E1A5C));w(control+0x14,control+0x28);w(control+0x1C,control+0x28);w(control+0x20,1);w(control+0x24,128);w(control+0x28+0x10,GD(0x100E1A6C));w(control+0x28+0x18,info);w(control+0x28+0x1C,control+0x14);
  w(info+0x10,info+0x1C);w(info+0x14,GD(0x100E19FC));w(info+0x18,64);w(info+0x1C,0x556F0000);w(info+0x5C,0x20100200);w(info+0x64,ids);w(info+0x80,resource);w(info+0x88,12);w(ids+40,data);w(ids+12,anim[0]);w(ids+16,anim[1]);
  region(GD(0x1016D820),32);n=0;for(uint32_t v:{0u,0u,0u,GC(0x028F036C),0u,GC(0x028F036C),0u,GC(0x027E0288)})w(GD(0x1016D820)+4*n++,v);
  region(GD(0x1016C6E0),64);n=0;for(uint32_t v:{0xFFFFu,GC(0x027E7164),0xFFFFu,GC(0x027E72CC),0xFFFFu,GC(0x027E7538),0xFFFFu,GC(0x027E7A14),0xFFFFu,GC(0x027E89FC),0xFFFFu,GC(0x027E8C4C),0xFFFFu,GC(0x027E8E54),0xFFFFu,GC(0x027E9020)})w(GD(0x1016C6E0)+4*n++,v);
  region(GD(0x1016C720),24);n=0;for(uint32_t v:{0xFFFFu,GC(0x027E92A8),0xFFFFu,GC(0x027E9378),0xFFFFu,GC(0x027E94D4)})w(GD(0x1016C720)+4*n++,v);
  region(GD(0x1016C738),168);n=0;for(uint32_t v:{0xFFFFu,GC(0x027E95E0),0xFFFFu,GC(0x027E96B0),0xFFFFu,GC(0x027E9780),0xFFFFu,GC(0x027E9850),0xFFFFu,GC(0x027E98F8),0xFFFFu,GC(0x027E99A0),0xFFFFu,GC(0x027E9A48),0xFFFFu,GC(0x027E9AE0),0xFFFFu,GC(0x027E9B90),0xFFFFu,GC(0x027E924C),0xFFFFu,GC(0x027E91F0),0xFFFFu,GC(0x027E9194),0xFFFFu,GC(0x027E9C38),0xFFFFu,GC(0x027E9C88),0xFFFFu,GC(0x027E9CD8),0xFFFFu,GC(0x027E1CF0),0xFFFFu,GC(0x027E1CF0),0xFFFFu,GC(0x027E1CF0),0xFFFFu,GC(0x027E1E9C),0xFFFFu,GC(0x027E1E9C),0xFFFFu,GC(0x027E1E9C)})w(GD(0x1016C738)+4*n++,v);
  w(data,fmdl);w(data+4,3);w(data+8,rows);w(fmdl+0xC,skeleton-fmdl-0xC);w(skeleton+8,3u<<16);w(skeleton+0x14,bones-skeleton-0x14);
  for(unsigned j=0;j<3;++j){w(rows+j*0x1C,bones+j*0x40);w(rows+j*0x1C+8,j?GC(0x022BE034):0);w(bones+j*0x40+4,j<<16);}
  w(rows+0x14,0xDEAD0000); // old retail shared binding: replaced before native use
  for(unsigned j=0;j<2;++j){region(actor[j],0x920);region(heap[j],0x10000);init_heap(heap[j]);morf[j]=heap[j]+0x100;model[j]=heap[j]+0x200;ref[j]=heap[j]+0x400;auto&a=e.actors[j];a.address=actor[j];a.pid=10+j;a.token=1+j;a.profile=0x175;a.subtype=1;a.fingerprint.fill(j+1);
   w(actor[j]+4,a.pid);w(actor[j]+0xC,0x00000175);w(actor[j]+0xF4,heap[j]);w(actor[j]+0x2DC,0x00010000);w(actor[j]+0x3B4,0x01020000);w(actor[j]+0x44C,morf[j]);w(actor[j]+0x8A0,GD(0x101C41D4));w(actor[j]+0x904,0x00010000);
   w(morf[j],GD(0x10058CC0));w(morf[j]+4,morf[j]+8);w(morf[j]+0x18,GC(0x027DAAA0));w(morf[j]+0x30,heap[j]+0x1000);w(morf[j]+0x5C,0xFFFF);w(morf[j]+0x64,0xFFFF);w(morf[j]+0x60,GC(0x027E6EA8));w(morf[j]+0x68,GC(0x027E7004));w(morf[j]+0x78,heap[j]+0x2000);w(morf[j]+0x7C,3);w(morf[j]+0x90,model[j]);w(morf[j]+0x94,anim[j]);w(morf[j]+0xA8,heap[j]+0x3000);w(morf[j]+0xAC,heap[j]+0x4000);
   w(model[j]+0x2C,ref[j]);w(model[j]+0xAC,data);w(model[j]+0xB8,actor[j]);w(ref[j],skeleton);w(ref[j]+8,bones);w(ref[j]+0xC,heap[j]+0x5000);w(ref[j]+0x10,heap[j]+0x6000);w(ref[j]+0x2C,3u<<16);w(ref[j]+0x30,0xDEAD0000);w(ref[j]+0x38,heap[j]+0x7000);w(anim[j]+4,GD(0x1016E95C));w(anim[j]+8,raw+0x800+j*0x100);
   const auto ad=raw+0x800+j*0x100,tracks=raw+0x1800+j*0x100,binding=raw+0x1A00+j*0x100;
   w(ad+0x14,3u<<16);w(ad+0x18,3);w(ad+0x20,tracks-ad-0x20);w(ad+0x24,skeleton-ad-0x24);w(ad+0x28,binding-ad-0x28);
   w(morf[j]+0x10,0x41F00000);w(morf[j]+0x20,heap[j]+0x4800);w(morf[j]+0x24,3);w(morf[j]+0x28,3);w(morf[j]+0x38,GD(0x1016D820));w(morf[j]+0x3C,heap[j]+0x4500);w(morf[j]+0x40,3);w(morf[j]+0x44,0x00030003);w(morf[j]+0x48,ad);w(morf[j]+0x4C,tracks);w(morf[j]+0x6C,skeleton);w(morf[j]+0x9C,0x41200000);
   for(unsigned k=0;k<3;++k){w(binding+k*2,(k<<16)|((k+1)%3));w(heap[j]+0x4500+k*4,k);w(heap[j]+0x4800+k*12+4,0x41F00000);const auto row=tracks+k*0x18,curve=raw+0x2000+j*0x1000+k*0x100,keys=raw+0x2400+j*0x1000+k*0x100,values=raw+0x2800+j*0x1000+k*0x100;
    w(row+8,0x00000100);w(row+0x10,curve-row-0x10);w(curve,2);w(curve+0xC,0x41F00000);w(curve+0x10,0x3F800000);w(curve+0x1C,keys-curve-0x1C);w(curve+0x20,values-curve-0x20);w(keys+4,0x41F00000);w(values,0x3F800000);w(values+16,0x3F800000);
   }

  }
  seed_cache();
 }
 void seed_cache(){const uint32_t loader=0x21000000,holder=0x21100000,res=0x4A009000,payload=raw;
  memory[GD(0x1015E184)].resize(0x40);w(GD(0x1015E184)+0x3C,GC(0x0275E208));w(GD(0x1015E184)+0x34,GC(0x0275CF28));
  memory[GD(0x101F4F7C)].resize(4);memory[loader].resize(0x1131C);memory[holder].resize(0x54);memory[res].resize(0x44);memory[payload].resize(0x10000);memory[0x20100200].resize(0x24);
  w(GD(0x101F4F7C),loader);w(loader,GD(0x100E222C));w(loader+0x14,loader+0x28);w(loader+0x1C,loader+0x28);w(loader+0x20,1);w(loader+0x24,640);
  const auto node=loader+0x28;w(node+0xC,node+0x20);w(node+0x10,GD(0x100E20A4));w(node+0x14,GD(0x100E231C));w(node+0x18,holder);w(node+0x1C,loader+0x14);w(node+0x20,0x556F0000);
  w(holder+0xC,GD(0x100E25D0));w(holder+0x10,holder+0x1C);w(holder+0x14,GD(0x100E2254));w(holder+0x18,0x30);w(holder+0x1C,0x556F0000);w(holder+0x4C,res);w(holder+0x50,payload);w(info+0x7C,payload);
  w(payload+0xC,GD(0x10145B38));w(payload+0x1C,payload);w(payload+0x20,0x10000);w(res+0x10,GD(0x1015E184));const auto base=payload+0x40,fres=payload+0x100;w(res+0x14,base);w(res+0x18,0x8000);w(res+0x1C,0x8000);w(res+0x28,base);w(res+0x2C,base+0x14);w(res+0x30,base+0x38);w(res+0x34,1);w(res+0x38,base+0x20);w(res+0x3C,base+0xC0);w(0x20100200+0x14,fres);
  w(base,0x53415243);w(base+4,0x0014FEFF);w(base+8,0x8000);w(base+0xC,0xC0);w(base+0x10,0x01000000);w(base+0x14,0x53464154);w(base+0x18,0x000C0001);w(base+0x1C,0x65);uint32_t hash=0;for(unsigned char ch:std::string_view("Uo.bfres"))hash=hash*0x65+ch;w(base+0x20,hash);w(base+0x24,0x01000000);w(base+0x28,0);w(base+0x2C,0x7F40);w(base+0x30,0x53464E54);w(base+0x34,0x00080000);w(base+0x38,0x556F2E62);w(base+0x3C,0x66726573);w(base+0x40,0);
  const auto models=payload+0x200,animations=payload+0x280;w(fres,0x46524553);w(fres+0x20,models-fres-0x20);w(fres+0x28,animations-fres-0x28);w(fres+0x50,0x00010000);w(fres+0x54,0x00020000);w(models+0x24,fmdl-models-0x24);w(animations+0x24,raw+0x800-animations-0x24);w(animations+0x34,raw+0x900-animations-0x34);
 }
 void w(uint32_t p,uint32_t value){for(auto&[a,b]:memory)if(p>=a&&uint64_t(p)+4<=uint64_t(a)+b.size()){put(b,p-a,value);return;}assert(false);}
 bool read(uint32_t p,std::span<uint8_t>out){++reads;if(invalidate_quiescence_on_read)quiescence_current=false;if(invalidate_on_read)dispatch_current=false;for(auto&[a,b]:memory)if(p>=a&&uint64_t(p)+out.size()<=uint64_t(a)+b.size()){std::copy_n(b.begin()+p-a,out.size(),out.begin());if(unstable&&p==morf[0]&&out.size()==0xC8)out[0x90]^=reads&1;return true;}return false;}
 G::Sources source(){return {[&](auto p,auto b){return read(p,b);},[&](const auto&a,const auto&i){if(i!=e.identity)return false;for(const auto&x:e.actors)if(x==a)return true;return false;},[&](G::Scope&s){s.expected=e;s.fenced=true;s.stage_live=true;s.resources_pinned=pinned;s.preclaim=preclaim;s.native_dispatch=dispatch?guard:nullptr;s.native_quiescence=quiescent?quiescence:nullptr;s.native_cpu=wrong_cpu?nullptr:&cpu;return true;}};}
};
int main(){
 for(unsigned fault=0;fault<4;++fault){Fixture f;if(fault==0)f.quiescent=false;if(fault==1)f.wrong_cpu=true;if(fault==2)f.quiescence_current=false;if(fault==3)f.invalidate_quiescence_on_read=true;G::Adapter a(f.e,f.source());assert(!a.prepare());assert(a.diagnose(0).parsed);}
 {Fixture f;G::Adapter a(f.e,f.source());assert(a.prepare());f.invalidate_quiescence_on_read=true;G::Capture c;assert(!a.capture(0,c));}

 {Fixture f;f.invalidate_on_read=true;G::Adapter a(f.e,f.source());assert(!a.prepare());assert(a.diagnose(0).parsed);}
 {Fixture f;G::Adapter a(f.e,f.source());assert(a.prepare());f.invalidate_on_read=true;G::Capture c;assert(!a.capture(0,c));}

 for(unsigned fault=0;fault<10;++fault){Fixture f;f.dispatch=false;G::Check expected{};
  switch(fault){
   case 0:f.w(GD(0x1018C7B0),f.heap[0]+0x1000);expected=G::Check::CalcMatrixPointer;break;
   case 1:f.w(f.ref[0]+4,3u<<24);f.w(f.ref[0]+0x14,f.heap[1]+0x8000);expected=G::Check::MatrixScale;break;
   case 2:f.w(f.skeleton+0x14,0);expected=G::Check::BoneRelative;break;
   case 3:f.w(f.ref[0]+8,f.bones+4);expected=G::Check::BonePointer;break;
   case 4:f.w(f.rows,f.bones+0x40);expected=G::Check::DescriptorBone;break;
   case 5:f.w(f.bones+4,1u<<16);expected=G::Check::BoneId;break;
   case 6:f.w(f.bones+0x40+4,(1u<<16)|0xFFFF);expected=G::Check::BoneParent;break;
   case 7:f.w(f.morf[0]+0x7C,4);expected=G::Check::MorfBoneCount;break;
   case 8:f.w(f.morf[0]+0x30,f.heap[1]+0x1000);expected=G::Check::MorfMatrix;break;
   case 9:f.w(f.morf[0]+0xAC,f.heap[1]+0x4000);expected=G::Check::MorfCache;break;
  }
  G::Adapter a(f.e,f.source());const auto d=a.diagnose(0);
  assert(!d.parsed&&d.refusal==G::Refusal::Skeleton&&d.check==expected&&d.checked_address);
 }
 {Fixture f;bool tampered=false;auto native=+[](Cpu*){};story_native_refresh_dispatch::detail::View v{true,true,true,true,[&](auto,auto&r){r={native,native,tampered,false};return true;}};f.guard=std::make_shared<story_native_refresh_dispatch::Guard>(story_native_refresh_dispatch::Fixture::make([&]{return story_native_refresh_dispatch::detail::audit(v);}));G::Adapter a(f.e,f.source());assert(a.prepare());tampered=true;G::Capture c;assert(!a.capture(0,c)&&!a.prepare());}

 {Fixture f;G::Adapter a(f.e,f.source());assert(a.prepare());f.dispatch_current=false;G::Capture c;assert(!a.capture(0,c));assert(!a.prepare());assert(a.diagnose(0).parsed);}
 {Fixture f;auto held=std::move(*f.guard);G::Adapter a(f.e,f.source());assert(!a.prepare());assert(held.current());}
 {Fixture f;G::Adapter a(f.e,f.source());assert(a.prepare());f.guard.reset();G::Capture c;assert(!a.capture(0,c));}

 // Passive diagnostics identify the first bounded skeleton/matrix refusal;
 // none of these observations grants refresh authority.
 for(unsigned fault=0;fault<8;++fault){Fixture f;f.dispatch=false;G::Check expected{};
  switch(fault){
   case 0:f.w(f.raw+0x200+0x24,f.fmdl+4-(f.raw+0x200)-0x24);expected=G::Check::ModelDictionary;break;
   case 1:f.w(f.fmdl+0xC,0);expected=G::Check::SkeletonRelative;break;
   case 2:f.w(f.fmdl+0xC,0x80000000);expected=G::Check::SkeletonRelative;break;
   case 3:f.w(f.skeleton+8,4u<<16);expected=G::Check::SkeletonCount;break;
   case 4:f.w(f.ref[0],f.skeleton+4);expected=G::Check::MatrixSkeleton;break;
   case 5:f.w(f.ref[0]+0xC,f.heap[1]+0x5000);expected=G::Check::MatrixRecords;break;
   case 6:f.w(f.ref[0]+0x10,f.heap[1]+0x6000);expected=G::Check::MatrixOutput;break;
   case 7:f.w(f.ref[0]+0x38,f.heap[1]+0x7000);expected=G::Check::MatrixPrevious;break;
  }
  G::Adapter a(f.e,f.source());const auto d=a.diagnose(0);
  assert(!d.parsed&&d.refusal==G::Refusal::Skeleton&&d.check==expected&&d.checked_address);
 }
 {Fixture f;G::Adapter a(f.e,f.source());assert(!a.publication_enabled()&&a.prepare());G::Capture p;assert(a.capture(0,p));assert(p.model.model_data==f.data&&p.model.morf==f.morf[0]);assert(a.refresh_admission_enabled()&&a.verify_model(f.e,0,p.model,p.transform));
  auto wrong_model=p.model;++wrong_model.model;assert(!a.verify_model(f.e,0,wrong_model,p.transform));
  auto wrong_transform=p.transform;wrong_transform.base[0]^=1;assert(!a.verify_model(f.e,0,p.model,wrong_transform));
  f.quiescence_current=false;assert(!a.verify_model(f.e,0,p.model,p.transform));f.quiescence_current=true;
  f.dispatch_current=false;assert(!a.verify_model(f.e,0,p.model,p.transform));f.dispatch_current=true;
  assert(a.verify_model(f.e,0,p.model,p.transform));f.preclaim=false;f.w(f.rows+0x14,f.morf[1]);f.w(f.ref[0]+0x30,f.morf[1]);assert(a.capture(0,p)&&p.root_calc==f.morf[1]&&p.mtx_calc==f.morf[1]);auto wrong=f.e;++wrong.identity.generation;assert(!a.verify_model(wrong,0,p.model,p.transform));}
 for(unsigned fault=0;fault<3;++fault){Fixture f;if(!fault)f.w(f.actor[0]+0x8A0,GD(0x101C41D4)+4);else f.w(GD(0x101C41D4)+(fault==1?0x18:0x24),1);G::Adapter a(f.e,f.source());const auto d=a.diagnose(0);assert(!d.parsed&&d.refusal==G::Refusal::Actor&&d.check==(fault?G::Check::NpcDatConstants:G::Check::NpcDatPointer));}
 // Map capacity is u16 at42; the adjacent u16 at40 contains binding flags.
 {Fixture f;for(unsigned n=0;n<2;++n)f.w(f.morf[n]+0x40,0x00010003);G::Adapter a(f.e,f.source());assert(a.prepare());}
 for(unsigned capacity:{0u,2u,129u}){Fixture f;f.w(f.morf[0]+0x40,0x00010000|capacity);G::Adapter a(f.e,f.source());const auto d=a.diagnose(0);assert(!d.parsed&&d.check==G::Check::AnimationMapCapacity&&d.checked_address==f.morf[0]+0x42);}
 // Animation and skeleton counts stay distinct; capacity bounds their maximum.
 {Fixture f;for(unsigned n=0;n<2;++n){f.w(f.raw+0x800+n*0x100+0x14,2u<<16);f.w(f.raw+0x800+n*0x100+0x18,2);f.w(f.morf[n]+0x28,2);f.w(f.morf[n]+0x44,0x00020003);}G::Adapter a(f.e,f.source());assert(a.prepare());}
 // The native constructor replaces intermediate E944 with final E95C.
 {Fixture f;f.w(f.anim[0]+4,GD(0x1016E944));G::Adapter a(f.e,f.source());const auto d=a.diagnose(0);assert(!d.parsed&&d.refusal==G::Refusal::AnimationResource&&d.check==G::Check::AnimationVtable&&d.observed==GD(0x1016E944)&&d.required==GD(0x1016E95C));}
 // Animation refusal diagnostics retain the exact first failed native field.
 for(unsigned fault=0;fault<5;++fault){Fixture f;G::Check expected{};
  switch(fault){
   case 0:f.w(f.anim[0]+8,0x40000000);expected=G::Check::AnimationPayload;break;
   case 1:f.w(f.raw+0x280+0x24,4);expected=G::Check::AnimationDictionary;break;
   case 2:f.w(f.morf[0]+0x5C,0);expected=G::Check::AnimationGetterKind;break;
   case 3:f.w(f.morf[0]+0x60,0);expected=G::Check::AnimationGetter;break;
   case 4:f.w(GD(0x1016D820)+12,0);expected=G::Check::DispatchWord;break;
  }
  G::Adapter a(f.e,f.source());const auto d=a.diagnose(0);
  assert(!d.parsed&&d.refusal==G::Refusal::AnimationPayload&&d.check==expected&&d.checked_address);
 }
 // FFFF is a root sentinel only, not permission for arbitrary parent indices.
 for(unsigned fault=0;fault<3;++fault){Fixture f;f.w(f.bones+4,0xFFFF);
  if(fault==0)f.w(f.bones+4,3); // non-sentinel root outside the bounded array
  if(fault==1)f.w(f.bones+0x40+4,(1u<<16)|0xFFFF);
  if(fault==2)f.w(f.bones+0x40+4,(1u<<16)|3);
  G::Adapter a(f.e,f.source());assert(!a.prepare());
 }
 for(unsigned fault=0;fault<4;++fault){Fixture f;const auto curve=f.raw+0x2000;G::Check expected{};
  if(fault==0){f.w(curve,(0x70u<<16)|2);expected=G::Check::SamplerLayout;}
  if(fault==1){f.w(curve+8,0x3F800000);expected=G::Check::SamplerFrameRange;}
  if(fault==2){f.w(f.raw+0x2400+4,0);expected=G::Check::SamplerKeyOrder;}
  if(fault==3){f.w(f.heap[0]+0x4800+8,2);expected=G::Check::SamplerCacheIndex;}
  G::Adapter a(f.e,f.source());const auto d=a.diagnose(0);assert(!d.parsed&&d.refusal==G::Refusal::Sampler&&d.check==expected&&d.checked_address);
 }
 // A shorter curve is safe only with the native clamp post-wrap policy.
 for(unsigned wrap=0;wrap<4;++wrap){Fixture f;for(unsigned n=0;n<2;++n)for(unsigned j=0;j<3;++j){const auto curve=f.raw+0x2000+n*0x1000+j*0x100;f.w(curve,((wrap<<12)<<16)|2);f.w(curve+0xC,0x41E80000);f.w(f.raw+0x2400+n*0x1000+j*0x100+4,0x41E80000);f.w(f.heap[n]+0x4800+j*12+4,0x41E80000);}G::Adapter a(f.e,f.source());if(!wrap)assert(a.prepare());else{const auto d=a.diagnose(0);assert(!d.parsed&&d.check==G::Check::SamplerFrameRange);}}
 // Invalidated caches reinitialize their index before any native key lookup.
 for(unsigned stale=0;stale<2;++stale){Fixture f;for(unsigned n=0;n<2;++n)for(unsigned j=0;j<3;++j){const auto at=f.heap[n]+0x4800+j*12;f.w(at,stale?0x40000000:0x7F800000);f.w(at+4,stale?0x3F800000:0x7FC00000);f.w(at+8,0x3F800000);}G::Adapter a(f.e,f.source());assert(a.prepare());}
 // All seven native sampler dispatch modes have distinct bounded payloads.
 for(unsigned mode=0;mode<=6;++mode){Fixture f;
  for(unsigned actor=0;actor<2;++actor)for(unsigned joint=0;joint<3;++joint){const auto curve=f.raw+0x2000+actor*0x1000+joint*0x100;f.w(curve,(mode<<20)|(mode==2?32:2));}
  G::Adapter a(f.e,f.source());assert(a.prepare());G::Capture c;assert(a.capture(0,c));
 }
 for(unsigned mode=0;mode<4;++mode){Fixture f;f.w(f.bones+4,0xFFFF);for(unsigned j=0;j<2;++j){f.w(f.ref[j]+4,mode<<24);f.w(f.ref[j]+0x14,f.heap[j]+0x8000);}G::Adapter a(f.e,f.source());assert(a.prepare());if(mode==3){f.w(f.ref[0]+0x14,f.heap[1]+0x8000);G::Capture c;assert(!a.capture(0,c));}}
 {Fixture f;f.w(GD(0x1018C7B0),f.heap[0]+0x1000);G::Adapter a(f.e,f.source());assert(!a.prepare());}
 {Fixture f;f.w(f.raw+0x2000+0xC,0x7F000000);G::Adapter a(f.e,f.source());assert(!a.prepare());}
 // Compressed key times are s16/32; byte payloads may be unaligned.
 {Fixture f;for(unsigned actor=0;actor<2;++actor)for(unsigned joint=0;joint<3;++joint){const auto curve=f.raw+0x2000+actor*0x1000+joint*0x100,keys=f.raw+0x2400+actor*0x1000+joint*0x100;f.w(curve,(0x39u<<16)|2);f.w(keys,960);f.w(curve+0x20,f.raw+0x2801+actor*0x1000+joint*0x100-curve-0x20);}G::Adapter a(f.e,f.source());assert(a.prepare());}
 // Constant transforms are read from the relative BoneAnim payload, not
 // trusted merely because the curve arrays themselves were bounded.
 {Fixture f;f.w(f.raw+0x1800,0x38);f.w(f.resource+0x1814,0x70000000);G::Adapter a(f.e,f.source());assert(!a.prepare());}
 {Fixture f;f.w(f.morf[0]+0xA8,f.heap[0]+0x1000);G::Adapter a(f.e,f.source());assert(!a.prepare());}
 {Fixture f;f.seed_cache();f.dispatch=false;G::Adapter a(f.e,f.source());assert(!a.prepare());const auto d=a.diagnose(0);assert(d.parsed&&d.refusal==G::Refusal::Complete&&d.cache_membership&&d.cache_buffer.address==0x4A000040&&d.metadata.address==f.resource&&d.payload_heap.address==0x4A000000);G::Capture c;assert(!a.capture(0,c));assert(!a.verify_model(f.e,0,{},{}));}
 {Fixture f;f.dispatch=false;f.pinned=false;G::Adapter a(f.e,f.source());assert(a.diagnose(0).refusal==G::Refusal::Scope);}
 {Fixture f;f.dispatch=false;f.w(f.raw+0x2000+4,0x30);G::Adapter a(f.e,f.source());const auto d=a.diagnose(0);assert(!d.parsed&&d.refusal==G::Refusal::Sampler&&d.bck==f.raw+0x800);}
 {Fixture f;f.seed_cache();f.dispatch=false;f.w(0x21100000+0x50,f.resource);G::Adapter a(f.e,f.source());const auto d=a.diagnose(0);assert(!d.parsed&&!d.cache_membership&&d.refusal==G::Refusal::CacheRegistry);}
 {Fixture f;f.dispatch=false;f.w(f.info+0x14,GD(0x100E19E4));G::Adapter a(f.e,f.source());const auto d=a.diagnose(0);assert(!d.parsed&&d.refusal==G::Refusal::ResourceRegistry&&d.check==G::Check::NameVtable&&d.checked_address==f.info+0x14&&d.observed==GD(0x100E19E4)&&d.required==GD(0x100E19FC));}
 {Fixture f;f.dispatch=false;f.w(f.control+0x24,129);G::Adapter a(f.e,f.source());const auto d=a.diagnose(0);assert(!d.parsed&&d.check==G::Check::PoolCapacity&&d.observed==129&&d.required==128);}
 // Raw assets live in a separate actual selected Uo.bfres file. A valid
 // metadata pointer or arbitrary raw-archive padding cannot replace membership.
 for(unsigned bad=0;bad<6;++bad){Fixture f;switch(bad){
 case 0:f.w(f.raw+0x200+0x24,f.fmdl+4-(f.raw+0x200+0x24));break;
 case 1:f.w(f.raw+0x280+0x24,f.raw+0x804-(f.raw+0x280+0x24));break;
 case 2:f.w(f.raw+0x40+0x28,4);break;
 case 3:f.w(f.raw+0x40+0x2C,0x200);break;
 case 4:f.w(f.raw+0x40+0x3C,0x66726574);break;
 case 5:f.w(f.morf[0]+0xA8,f.heap[0]+0x20B0);break; // only rounded clear overlaps
 }G::Adapter a(f.e,f.source());assert(!a.prepare());}
 for(unsigned bad=0;bad<25;++bad){Fixture f;G::Adapter a(f.e,f.source());assert(a.prepare());switch(bad){case 0:f.w(f.actor[1]+4,999);break;case 1:f.w(f.ids+40,f.data+4);break;case 2:f.w(f.rows+0x1C+8,GC(0x022BE038));break;case 3:f.w(f.rows+0x1C+0x14,f.morf[0]);break;case 4:f.w(f.morf[0]+0xC0,f.actor[1]);break;case 5:f.w(f.morf[0]+0x18,GC(0x027DAAA4));break;case 6:f.w(f.model[0]+0xB4,GC(0x022BE034));break;case 7:f.w(f.control+0x28,f.control+0x28);break;case 8:f.w(f.heap[0]+0x20,0x100);break;case 9:f.w(f.morf[0]+0xA8,f.heap[1]+0x3000);break;case 10:f.w(f.anim[0]+4,GD(0x1016E940));break;case 11:f.w(f.morf[0]+0x60,GC(0x027E6AA8));break;case 12:f.w(f.bones+4,3u<<16);break;case 13:f.pinned=false;break;case 14:f.w(f.control+0x24,129);break;case 15:f.w(f.raw+0x2000+4,0x30);break;case 16:f.w(f.raw+0x2000,(0x70u<<16)|2);break;case 17:f.w(f.raw+0x2400+4,0);break;case 18:f.w(f.heap[0]+0x4800+8,2);break;case 19:f.w(f.raw+0x2000+0x20,0x70000000);break;case 20:f.w(f.raw+0x800+0x24,0x100);break;case 21:f.w(f.raw+0x1A00,3u<<16);break;case 22:f.w(f.morf[0]+0x20,f.heap[1]+0x4800);break;case 23:f.w(f.morf[0]+0x38,GD(0x1016D824));break;case 24:f.w(f.rows+0x1C,f.bones+0x80);break;}G::Capture c;if(bad==0)assert(!a.capture(1,c));else assert(!a.capture(0,c));}
 {Fixture f;f.preclaim=false;G::Adapter a(f.e,f.source());assert(!a.prepare());G::Capture c;assert(!a.capture(0,c));}
 // Native metadata writes must not mutate dispatch/registry dependencies.
 for(unsigned alias=0;alias<4;++alias){Fixture f;switch(alias){
 case 0:{ // root.calc aliases ModelData.rows: genuine root binding would redirect traversal
  const auto data=f.rows+0xC;f.w(data,f.fmdl);f.w(data+4,3);f.w(data+8,f.rows);f.w(f.ids+40,data);for(auto model:f.model)f.w(model+0xAC,data);break;
 }
 case 1:{ // animation frame cache aliases a descriptor's otherwise unused padding
  const auto animation=f.rows+0xC;f.w(animation+4,GD(0x1016E95C));f.w(animation+8,f.raw+0x800);f.w(f.ids+12,animation);f.w(f.morf[0]+0x94,animation);break;
 }
 case 2:{ // animation frame cache aliases a genuine resource ID table entry
  const auto ids=f.resource+0x800;for(unsigned j=0;j<12;++j){uint32_t value=0;if(j==10)value=f.data;if(j==3)value=GD(0x1016E95C);if(j==4)value=f.raw+0x800;if(j==5)value=f.anim[1];if(j==11)value=ids+8;f.w(ids+j*4,value);}f.w(f.info+0x64,ids);f.w(f.morf[0]+0x94,ids+8);break;
 }
 case 3:f.w(f.heap[0]+0x20,0x1100000);break; // a claimed actorheap cannot encompass resource metadata
 }G::Adapter a(f.e,f.source());const auto d=a.diagnose(0);assert(!d.parsed&&(alias==3||d.refusal==G::Refusal::WorkSpans)&&d.check==(alias==3?G::Check::WorkHeapOverlap:G::Check::WorkImmutableAlias));assert(!a.prepare());}

 // Fixed native matrix/current-object globals must not alias live registries.
 for(uint32_t ids:{GD(0x1048D0CC),GD(0x101FF5F0),GD(0x104B4868),GD(0x104B4628),GD(0x104B462C)}){Fixture f;f.memory[ids].resize(48);for(unsigned j=0;j<12;++j)f.w(ids+j*4,j==10?f.data:j==3?f.anim[0]:j==4?f.anim[1]:0);f.w(f.info+0x64,ids);G::Adapter a(f.e,f.source());const auto d=a.diagnose(0);assert(!d.parsed&&d.refusal==G::Refusal::WorkSpans&&d.check==G::Check::WorkImmutableAlias);assert(!a.prepare());}

 // A fresh other-actor heap/header cannot expand this actor's work authority.
 {Fixture f;f.w(f.heap[0]+0x20,0x20000);G::Adapter a(f.e,f.source());const auto d=a.diagnose(0);assert(!d.parsed&&d.check==G::Check::WorkHeapOverlap);assert(!a.prepare());}
 {Fixture f;G::Adapter a(f.e,f.source());assert(a.prepare());f.w(f.actor[1]+0xF4,f.heap[0]);G::Capture v;assert(!a.capture(0,v));}

 // Saved-model trace is append-only parser data, not the filtered immutable
 // alias list: root/cache/scratch bytes and delegated SARC reads remain present.
 {Fixture f;G::Adapter a(f.e,f.source());story_native_model_saved_graph::Trace t;
  t.reads.push_back({1,1});assert(!a.trace(0,t)&&t.reads.empty()&&t.writes.empty());
  assert(a.prepare()&&a.trace(0,t));
  auto has=[](const auto&v,uint32_t p,uint32_t n){return std::any_of(v.begin(),v.end(),[&](auto r){return r.address<=p&&uint64_t(r.address)+r.bytes>=uint64_t(p)+n;});};
  assert(has(t.reads,f.rows,3*0x1C)&&has(t.reads,f.anim[0],12));
  assert(has(t.reads,GD(0x101FF5F0),0x30)); // removed from immutable alias list
  assert(has(t.reads,f.raw+0x40,0x14)); // delegated U::verify SARC header
  assert(has(t.reads,f.raw+0x78,9)); // delegated exact Uo.bfres name
  assert(has(t.reads,GD(0x1016C6C0),0x20)); // final native traversal dispatch
  assert(has(t.writes,f.rows+0x14,4)&&has(t.writes,f.anim[0],4));
  assert(has(t.writes,f.heap[0]+0x2000,0xC0)); // DCZeroRange count38 rounded64
  for(auto p:{GD(0x1048D0CC),GD(0x101FF5F0),GD(0x104B4868)})assert(has(t.writes,p,0x30));
  for(auto p:{GD(0x104B4628),GD(0x104B462C)})assert(has(t.writes,p,4));
  auto copy=t;assert(a.trace(0,t)&&t==copy);
  for(unsigned fault=0;fault<5;++fault){Fixture bad;G::Adapter b(bad.e,bad.source());assert(b.prepare());switch(fault){case 0:bad.quiescence_current=false;break;case 1:bad.dispatch_current=false;break;case 2:bad.invalidate_quiescence_on_read=true;break;case 3:bad.invalidate_on_read=true;break;case 4:bad.w(bad.rows+0x1C+8,GC(0x022BE038));break;}t=copy;assert(!b.trace(0,t)&&t.reads.empty()&&t.writes.empty());}
  t=copy;assert(!a.trace(2,t)&&t.reads.empty());
 }

 // A borrowed legacy read window may inspect existing host baselines, but
 // cannot authorize refresh even when its dispatch/data observations match.
 {Fixture f;G::Adapter a(f.e,f.source());assert(a.prepare());
  f.quiescence=std::make_shared<threads::NativeQuiescence>(threads::NativeQuiescenceIssuer::make([&](const Cpu*c){return c==&f.cpu&&f.quiescence_current;},true));
  G::Capture c;story_native_model_saved_graph::Trace t;assert(a.capture(0,c)&&a.trace(0,t));assert(!a.verify_model(f.e,0,c.model,c.transform));
 }

}
