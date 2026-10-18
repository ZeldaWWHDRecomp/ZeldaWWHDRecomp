// SPDX-License-Identifier: MPL-2.0
#include "story_native_people_model_graph.h"
#include "guest_addr.h"
#include "story_native_uo_bfres_extent.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
namespace story_native_people_model_graph {
namespace {constexpr uint32_t kMaximumHeapBytes=32u*1024*1024;}
namespace {
uint32_t w(std::span<const uint8_t>b,unsigned p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
unsigned h(std::span<const uint8_t>b,unsigned p=0){return unsigned(b[p])<<8|b[p+1];}
bool bytes(uint32_t a,size_t n){return n&&a>=0x10000000&&uint64_t(a)+n<=0x50000000;}
bool mem(uint32_t a,size_t n){return !(a&3)&&bytes(a,n);}
float fp(std::span<const uint8_t>b,unsigned p){const uint32_t bits=w(b,p);float v;std::memcpy(&v,&bits,4);return v;}
bool within(uint32_t a,size_t n,uint32_t start,uint32_t size){return bytes(a,n)&&a>=start&&uint64_t(a)+n<=uint64_t(start)+size;}
bool same(const Capture&a,const Capture&b){return a.model==b.model&&a.transform==b.transform&&a.animation.address==b.animation.address&&a.animation.frame_control==b.animation.frame_control&&a.animation.morph==b.animation.morph&&a.animation.cached_frame==b.animation.cached_frame&&a.root_calc==b.root_calc&&a.mtx_calc==b.mtx_calc;}
}
Adapter::Adapter(Expected e,Sources s):expected_(std::move(e)),sources_(std::move(s)){}
bool Adapter::sample(unsigned n,Graph&out,Diagnostic*diag,bool invocation)const{
 Diagnostic passive;if(!diag)diag=&passive;
 auto phase=[&](Refusal value){if(diag)diag->refusal=value;};
 phase(Refusal::Scope);
 if(n>=2||!sources_.read||!sources_.resident||!sources_.scope)return false;
 Scope scope;if(!sources_.scope(scope)||scope.expected!=expected_||!scope.fenced||!scope.stage_live||!scope.resources_pinned||(invocation&&(!scope.native_dispatch||!scope.native_dispatch->current()||!scope.native_cpu||!scope.native_quiescence||!scope.native_quiescence->current(scope.native_cpu)))||(!invocation&&!scope.preclaim))return false;
 phase(Refusal::Actor);const auto&a=expected_.actors[n];if(diag)diag->actor=a.address;if(a.profile!=0x175||a.subtype!=1||!sources_.resident(a,expected_.identity))return false;
 auto check=[&](bool ok,Check why,uint32_t address,uint32_t observed,uint32_t required){if(!ok&&diag){diag->check=why;diag->checked_address=address;diag->observed=observed;diag->required=required;}return ok;};
 struct ReadSpan{uint32_t p;size_t n;bool forced=false;};std::vector<ReadSpan>immutable;
 auto recorded=[&](uint32_t p,std::span<uint8_t>b){const bool ok=bytes(p,b.size())&&sources_.read(p,b);if(ok){immutable.push_back({p,b.size()});out.trace.reads.push_back({p,uint32_t(b.size())});}return ok;};
 auto read=[&](uint32_t p,std::span<uint8_t>b){const bool ok=mem(p,b.size())&&recorded(p,b);if(!ok&&diag){diag->check=Check::Read;diag->checked_address=p;diag->read_bytes=uint32_t(b.size());}return ok;};
 auto scalar=[&](uint32_t p,uint32_t&v){std::array<uint8_t,4>b{};if(!read(p,b))return false;v=w(b);return true;};
 auto words=[&](uint32_t p,std::span<const uint32_t>expected){std::vector<uint8_t>b(expected.size()*4);if(!read(p,b))return false;for(unsigned j=0;j<expected.size();++j)if(!check(w(b,j*4)==expected[j],Check::DispatchWord,p+j*4,w(b,j*4),expected[j]))return false;return true;};
 auto heap=[&](uint32_t p,uint32_t&start,uint32_t&size){std::array<uint8_t,0x28>b{};if(!read(p,b))return false;immutable.push_back({p,b.size(),true});const auto vt=w(b,0xC);if(vt!=GD(0x10145B38))return false;uint32_t getter{},sizer{};if(!scalar(vt+0x5C,getter)||!scalar(vt+0x6C,sizer)||getter!=GC(0x02755280)||sizer!=GC(0x02755298))return false;start=w(b,0x1C);size=w(b,0x20);if(!mem(start,size)||size>kMaximumHeapBytes||!within(p,0x28,start,size))return false;out.identity.insert(out.identity.end(),{p,vt,start,size,w(b,0x24)});return true;};
 std::array<uint8_t,0x920>actor{};if(!read(a.address,actor)||w(actor,4)!=a.pid||actor[0xC]==3||actor[0x5C]||h(actor,0xE)!=0x175||actor[0x2DD]!=1||actor[0x905]!=1)return false;
 // The native joint callback reads canonical Uo2 NpcDat; the actor pointer
 // alone must not authorize a different mutable callback data source.
 std::array<uint8_t,0x28>npc_data{};
 if(!check(w(actor,0x8A0)==GD(0x101C41D4),Check::NpcDatPointer,a.address+0x8A0,w(actor,0x8A0),GD(0x101C41D4))||!read(GD(0x101C41D4),npc_data)||!check(w(npc_data,0x18)==0&&w(npc_data,0x1C)==0x432A0000&&w(npc_data,0x20)==0&&w(npc_data,0x24)==0x43160000,Check::NpcDatConstants,GD(0x101C41D4)+0x18,w(npc_data,0x1C),0x432A0000))return false;
 phase(Refusal::ActorHeap);uint32_t start{},size{};if(!heap(w(actor,0xF4),start,size))return false;
 if(diag)diag->actor_heap={start,size};
 // Both independently resident actor allocations must remain disjoint. One
 // malformed solid-heap header cannot make another SDK actor writable work.
 const auto&other=expected_.actors[1-n];std::array<uint8_t,0x920>other_actor{};
 uint32_t other_start{},other_size{};
 if(!sources_.resident(other,expected_.identity)||!read(other.address,other_actor)||w(other_actor,4)!=other.pid||other_actor[0xC]==3||other_actor[0x5C]||h(other_actor,0xE)!=0x175||other_actor[0x2DD]!=1||!heap(w(other_actor,0xF4),other_start,other_size))return false;
 if(!check(uint64_t(start)+size<=other_start||uint64_t(other_start)+other_size<=start,Check::WorkHeapOverlap,start,size,other_start))return false;
 auto owned=[&](uint32_t p,size_t bytes){return !(p&3)&&within(p,bytes,start,size);};
 phase(Refusal::Morf);auto&c=out.capture;auto&m=c.model;m.morf=w(actor,0x44C);if(!owned(m.morf,0xC8))return false;
 if(diag)diag->morf=m.morf;
 std::array<uint8_t,0xC8>morf{};if(!read(m.morf,morf)||w(morf)!=GD(0x10058CC0)||w(morf,4)!=m.morf+8||w(morf,0xC0)||w(morf,0xC4))return false;
 const std::array<uint32_t,10>vt{0,0,0,GC(0x025EE1D8),0,GC(0x027F3B28),0,GC(0x028F036C),0,GC(0x025E5C38)};if(!words(GD(0x10058CC0),vt))return false;
 if((w(morf,0x18)!=GC(0x027DAAA0)&&w(morf,0x18)!=GC(0x027DA9E8))||w(morf,0x1C))return false;
 phase(Refusal::Model);m.model=w(morf,0x90);if(diag)diag->model=m.model;if(!owned(m.model,0x170))return false;
 std::array<uint8_t,0xF8>model{};if(!read(m.model,model)||w(model,0xB8)!=a.address||w(model,0xB4))return false;
 m.user_area=a.address;m.model_data=w(model,0xAC);m.material_table=m.model_data;m.joint_tree=w(model,0x2C);m.callback=0;
 if(diag){diag->model_data=m.model_data;diag->animation=w(morf,0x94);uint32_t p{};if(scalar(m.model_data,p))diag->fmdl=p;if(scalar(diag->animation+8,p))diag->bck=p;}
 if(!owned(m.joint_tree,0x3C))return false;
 // Independent dRes getIDRes provenance: actual singleton, inline pool and Uo
 // archive ID10. No model pointer supplies or creates its own resource identity.
 phase(Refusal::ResourceRegistry);uint32_t control{};if(!scalar(GD(0x101F4F28),control))return false;std::array<uint8_t,0x28>ctl{};if(!read(control,ctl)||!check(w(ctl)==GD(0x100E1A5C),Check::ControlVtable,control,w(ctl),GD(0x100E1A5C))||!check(w(ctl,0x1C)==control+0x28,Check::PoolStorage,control+0x1C,w(ctl,0x1C),control+0x28)||!check(w(ctl,0x24)==128,Check::PoolCapacity,control+0x24,w(ctl,0x24),128)||!check(w(ctl,0x20)&&w(ctl,0x20)<=128,Check::PoolCount,control+0x20,w(ctl,0x20),128))return false;
 const auto pool=control+0x14;std::vector<uint32_t>todo{w(ctl,0x14)},seen;uint32_t info=0;
 while(!todo.empty()){const auto node=todo.back();todo.pop_back();if(!node)continue;if(!check(seen.size()<128&&within(node,0x20,control+0x28,128*0x20)&&((node-control-0x28)%0x20)==0,Check::NodeRange,node,node,control+0x28)||!check(std::find(seen.begin(),seen.end(),node)==seen.end(),Check::NodeCycle,node,node,0))return false;seen.push_back(node);std::array<uint8_t,0x20>b{};if(!read(node,b)||!check(w(b,0x10)==GD(0x100E1A6C),Check::NodeVtable,node+0x10,w(b,0x10),GD(0x100E1A6C))||!check(w(b,0x1C)==pool,Check::NodeOwner,node+0x1C,w(b,0x1C),pool))return false;todo.push_back(w(b));todo.push_back(w(b,4));
  const auto candidate=w(b,0x18);std::array<uint8_t,0x5C>name{};if(!read(candidate,name)||!check(w(name,0x10)==candidate+0x1C,Check::NamePointer,candidate+0x10,w(name,0x10),candidate+0x1C)||!check(w(name,0x14)==GD(0x100E19FC),Check::NameVtable,candidate+0x14,w(name,0x14),GD(0x100E19FC))||!check(w(name,0x18)==64,Check::NameCapacity,candidate+0x18,w(name,0x18),64))return false;
  if(name[0x1C]=='U'&&name[0x1D]=='o'&&!name[0x1E]){if(!check(!info,Check::DuplicateArchive,node,candidate,info))return false;info=candidate;out.identity.insert(out.identity.end(),{node,w(b,0xC),w(b,0x14)});}
 }
 if(!check(seen.size()==w(ctl,0x20),Check::ArchiveCount,control+0x20,uint32_t(seen.size()),w(ctl,0x20))||!check(bool(info),Check::ArchiveMissing,pool,0,1))return false;
 std::array<uint8_t,0x8C>archive{};if(!read(info,archive)||!check(w(archive,0x88)>10&&w(archive,0x88)<=4096,Check::IdCount,info+0x88,w(archive,0x88),4096))return false;
 if(diag){diag->archive_info=info;diag->archive=w(archive,0x5C);diag->parent_heap=w(archive,0x7C);diag->data_heap=w(archive,0x80);uint32_t header{};if(scalar(diag->archive+0x14,header))diag->fres=header;}
 uint32_t body{};if(!scalar(w(archive,0x64)+40,body)||!check(body==m.model_data,Check::BodyIdentity,w(archive,0x64)+40,body,m.model_data))return false;
 phase(Refusal::ResourceHeap);uint32_t resource_start{},resource_size{};if(!heap(w(archive,0x80),resource_start,resource_size))return false;
 if(diag)diag->metadata={resource_start,resource_size};
 // Independent native cache registry binds an exact selected Uo.bfres file.
 namespace U=story_native_uo_bfres_extent;U::Extent file;
 if(diag){auto cache=[&]()->bool{
  uint32_t loader{};if(!scalar(GD(0x101F4F7C),loader))return false;
  std::array<uint8_t,0x28>l{};if(!read(loader,l)||w(l)!=GD(0x100E222C)||w(l,0x1C)!=loader+0x28||w(l,0x24)!=640||w(l,0x20)>640)return false;
  std::vector<uint32_t>pending{w(l,0x14)},visited;uint32_t holder{};
  while(!pending.empty()){const auto node=pending.back();pending.pop_back();if(!node)continue;
   if(visited.size()>=640||!within(node,0x54,loader+0x28,640*0x54)||(node-loader-0x28)%0x54||std::find(visited.begin(),visited.end(),node)!=visited.end())return false;
   visited.push_back(node);std::array<uint8_t,0x54>b{};if(!read(node,b)||w(b,0xC)!=node+0x20||w(b,0x10)!=GD(0x100E20A4)||w(b,0x14)!=GD(0x100E231C)||w(b,0x1C)!=loader+0x14||std::find(b.begin()+0x20,b.begin()+0x50,0)==b.begin()+0x50)return false;
   pending.push_back(w(b));pending.push_back(w(b,4));if(b[0x20]=='U'&&b[0x21]=='o'&&!b[0x22]){if(holder)return false;holder=w(b,0x18);}
  }
  if(!holder||visited.size()!=w(l,0x20))return false;diag->cache_holder=holder;
  std::array<uint8_t,0x54>hld{};if(!read(holder,hld)||w(hld,0xC)!=GD(0x100E25D0)||w(hld,0x10)!=holder+0x1C||w(hld,0x14)!=GD(0x100E2254)||w(hld,0x18)!=0x30||hld[0x1C]!='U'||hld[0x1D]!='o'||hld[0x1E])return false;
  diag->cache_resource=w(hld,0x4C);std::array<uint8_t,0x44>res{};if(!read(diag->cache_resource,res)||w(res,0x10)!=GD(0x1015E184))return false;
  uint32_t get_file{},set_buffer{};if(!scalar(GD(0x1015E184)+0x3C,get_file)||get_file!=GC(0x0275E208)||!scalar(GD(0x1015E184)+0x34,set_buffer)||set_buffer!=GC(0x0275CF28))return false;
  diag->cache_buffer={w(res,0x14),w(res,0x1C)};diag->cache_size=w(res,0x18);
  if(w(hld,0x50)!=diag->parent_heap||!diag->cache_size||diag->cache_size>diag->cache_buffer.bytes)return false;
  uint32_t payload_start{},payload_size{};if(!heap(w(hld,0x50),payload_start,payload_size))return false;
  diag->payload_heap={payload_start,payload_size};
  if(!within(diag->cache_buffer.address,diag->cache_buffer.bytes,payload_start,payload_size)||!within(diag->fres,0x54,diag->cache_buffer.address,diag->cache_size))return false;
  U::Input input;input.parent_allocation={payload_start,payload_size};input.parent_heap=w(hld,0x50);input.archive_parent_heap=w(archive,0x7C);input.holder_heap=w(hld,0x50);input.resource_address=diag->cache_resource;input.archive_file=diag->fres;input.resource=res;
  if(!read(input.parent_heap,input.heap)||!U::verify(recorded,input,file))return false;
  out.identity.insert(out.identity.end(),{loader,holder,input.resource_address,input.parent_heap,file.base,file.bytes,file.archive_base,file.archive_bytes,file.allocation_bytes});
  return true;
 };diag->cache_membership=cache();if(!diag->cache_membership){phase(Refusal::CacheRegistry);return false;}}
 auto resource=[&](uint32_t p,size_t bytes){return within(p,bytes,resource_start,resource_size);};
 auto payload=[&](uint32_t p,size_t bytes){return within(p,bytes,file.base,file.bytes);};
 phase(Refusal::CacheRegistry);std::array<uint8_t,0x58>fres{};if(!payload(file.base,fres.size())||!read(file.base,fres)||w(fres)!=0x46524553)return false;
 out.animation_payload.insert(out.animation_payload.end(),fres.begin(),fres.end());
 auto dictionary_member=[&](unsigned field,unsigned count_field,uint32_t target){
  const unsigned entries=h(fres,count_field);if(!entries||entries>4096||!w(fres,field))return false;
  const int64_t base=int64_t(file.base)+field+int32_t(w(fres,field));if(base<0||base>0xFFFFFFFF)return false;
  const auto dict=uint32_t(base);std::vector<uint8_t>rows(0x18+entries*0x10);if(!payload(dict,rows.size())||!read(dict,rows))return false;
  unsigned matches=0;for(unsigned k=0;k<entries;++k){const unsigned offset=0x24+k*0x10;const auto rel=w(rows,offset);const int64_t at=int64_t(dict)+offset+int32_t(rel);if(!rel||at<0||at>0xFFFFFFFF||!payload(uint32_t(at),4))return false;if(uint32_t(at)==target)++matches;}
  out.animation_payload.insert(out.animation_payload.end(),rows.begin(),rows.end());out.identity.insert(out.identity.end(),{dict,entries});return matches==1;
 };
 phase(Refusal::JointDescriptors);std::array<uint8_t,0xC>data{};if(!resource(body,data.size())||!read(body,data))return false;const auto count=w(data,4),rows=w(data,8);if(!count||count>128||!resource(rows,count*0x1C))return false;
 const unsigned head=actor[0x3B4],back=actor[0x3B5];if(head>=count||back>=count||head==back)return false;
 out.descriptors.resize(count*0x1C);if(!read(rows,out.descriptors))return false;
 // Only descriptor0.calc is overwritten by genuine Morf.calc. Preserve every
 // other descriptor byte as an immutable native dispatch dependency.
 immutable.pop_back();immutable.push_back({rows,0x14});immutable.push_back({rows+0x18,out.descriptors.size()-0x18});
 for(unsigned j=0;j<count;++j){const auto at=j*0x1C;const auto cb=w(out.descriptors,at+8);if(cb!=((j==head||j==back)?GC(0x022BE034):0))return false;if(j&&w(out.descriptors,at+0x14))return false;}
 c.root_calc=w(out.descriptors,0x14);std::fill_n(out.descriptors.begin()+0x14,4,0); // overwritten before any native dispatch
 std::array<uint8_t,0x3C>ref{};if(!read(m.joint_tree,ref)||h(ref,0x2C)!=count)return false;
 phase(Refusal::Skeleton);diag->fmdl=w(data);
 if(!check(payload(w(data),0x10),Check::ModelPayload,w(data),w(data),0x10)||
    !check(dictionary_member(0x20,0x50,w(data)),Check::ModelDictionary,w(data),w(data),file.base))return false;
 uint32_t skeleton_relative{};if(!scalar(w(data)+0xC,skeleton_relative))return false;
 const int64_t skeleton_address=int64_t(w(data))+0xC+int32_t(skeleton_relative);
 if(!check(skeleton_relative&&skeleton_address>0&&skeleton_address<=0xFFFFFFFFll,Check::SkeletonRelative,w(data)+0xC,skeleton_relative,0))return false;
 const auto skeleton=uint32_t(skeleton_address);diag->skeleton=skeleton;
 std::array<uint8_t,0x28>skel{};
 if(!check(payload(skeleton,skel.size()),Check::SkeletonPayload,skeleton,skeleton,uint32_t(skel.size()))||!read(skeleton,skel)||
    !check(h(skel,8)==count,Check::SkeletonCount,skeleton+8,h(skel,8),count)||
    !check(w(ref)==skeleton,Check::MatrixSkeleton,m.joint_tree,w(ref),skeleton))return false;
 // Per-joint descriptors contain only the two fixed People callbacks. Native
 // matrix records and matrices must stay inside the actual actor solid heap.
 if(!check(owned(w(ref,0xC),count*0x40),Check::MatrixRecords,m.joint_tree+0xC,w(ref,0xC),count*0x40)||
    !check(owned(w(ref,0x10),count*0x30),Check::MatrixOutput,m.joint_tree+0x10,w(ref,0x10),count*0x30)||
    !check(owned(w(ref,0x38),count*0x30),Check::MatrixPrevious,m.joint_tree+0x38,w(ref,0x38),count*0x30))return false;
 c.mtx_calc=w(ref,0x30);
 const unsigned matrix_mode=(h(ref,4)>>8)&3;
 // Modes1-3 prefetch the next record flags word at the loop tail.
 if(matrix_mode&&!check(owned(w(ref,0xC),count*0x40+4),Check::MatrixPrefetch,m.joint_tree+0xC,w(ref,0xC),count*0x40+4))return false;
 // People joint callbacks copy through the native calc_mtx pointer slot.
 // Its RPX ADDR32 relocation is the fixed 3x4 matrix at101FF5F0.
 uint32_t calc_matrix{};std::array<uint8_t,0x30>calc_bytes{};
 if(!scalar(GD(0x1018C7B0),calc_matrix)||!check(calc_matrix==GD(0x101FF5F0),Check::CalcMatrixPointer,GD(0x1018C7B0),calc_matrix,GD(0x101FF5F0))||!read(calc_matrix,calc_bytes))return false;
 immutable.pop_back(); // fixed native matrix scratch, not an immutable asset
 // Mode3 also reads/writes its per-joint scale vector span (027E5DA4).
 if(matrix_mode==3&&!check(owned(w(ref,0x14),count*12),Check::MatrixScale,m.joint_tree+0x14,w(ref,0x14),count*12))return false;
 const int64_t bone_target=int64_t(skeleton)+0x14+int32_t(w(skel,0x14));
 if(!check(w(skel,0x14)&&bone_target>=0&&bone_target<=0xFFFFFFFF,Check::BoneRelative,skeleton+0x14,w(skel,0x14),0)||
    !check(w(ref,8)==uint32_t(bone_target),Check::BonePointer,m.joint_tree+8,w(ref,8),uint32_t(bone_target))||
    !check(payload(w(ref,8),count*0x40),Check::BonePayload,w(ref,8),w(ref,8),count*0x40))return false;
 out.bones.resize(count*0x40);if(!read(w(ref,8),out.bones))return false;
 // Native027F6484..64F4 binds each descriptor through027F7870 to
 // this exact resource bone; the callback follows descriptor+0 before ID+4.
 for(unsigned j=0;j<count;++j){
  if(!check(w(out.descriptors,j*0x1C)==w(ref,8)+j*0x40,Check::DescriptorBone,rows+j*0x1C,w(out.descriptors,j*0x1C),w(ref,8)+j*0x40)||
     !check(h(out.bones,j*0x40+4)==j,Check::BoneId,w(ref,8)+j*0x40+4,h(out.bones,j*0x40+4),j)||
     // All four native matrix modes handle bone0 separately and increment
     // before indexing a parent (027E5084/5588/5CD0/633C). The root's FFFF
     // sentinel is never used as an index. Every other parent remains bounded.
     !check(h(out.bones,j*0x40+6)<count||(!j&&h(out.bones,6)==0xFFFF),Check::BoneParent,w(ref,8)+j*0x40+6,h(out.bones,j*0x40+6),count))return false;
 }
 if(!check(h(morf,0x7E)==count,Check::MorfBoneCount,m.morf+0x7E,h(morf,0x7E),count)||
    !check(owned(w(morf,0x30),count*0x30),Check::MorfMatrix,m.morf+0x30,w(morf,0x30),count*0x30)||
    !check(owned(w(morf,0x78),count*0x38),Check::MorfWork,m.morf+0x78,w(morf,0x78),count*0x38)||
    !check(owned(w(morf,0xA8),count*0x20),Check::MorfOutput,m.morf+0xA8,w(morf,0xA8),count*0x20)||
    !check(owned(w(morf,0xAC),count*0x10),Check::MorfCache,m.morf+0xAC,w(morf,0xAC),count*0x10))return false;
 phase(Refusal::AnimationResource);c.animation.address=w(morf,0x94);if(diag)diag->animation=c.animation.address;if(!check(bool(c.animation.address),Check::AnimationAddress,m.morf+0x94,c.animation.address,1))return false;
 // The animation wrapper must independently occur in the same actual native
 // archive ID table, rather than being accepted from Morf's pointer alone.
 bool animation_resource=false;for(unsigned j=0;j<w(archive,0x88);++j){uint32_t v{};if(!scalar(w(archive,0x64)+j*4,v))return false;if(v==c.animation.address)animation_resource=true;}
 std::array<uint8_t,12>animation{};if(!check(animation_resource,Check::AnimationRegistry,c.animation.address,c.animation.address,w(archive,0x64))||!check(resource(c.animation.address,0x38),Check::AnimationWrapperRange,c.animation.address,c.animation.address,0x38)||!read(c.animation.address,animation)||!check(w(animation,4)==GD(0x1016E95C),Check::AnimationVtable,c.animation.address+4,w(animation,4),GD(0x1016E95C)))return false;
 // The wrapper frame word is an intentional native cache write; its vtable
 // and resource pointer remain immutable, including under aliasing layouts.
 immutable.pop_back();immutable.push_back({c.animation.address+4,8});
 phase(Refusal::AnimationPayload);const auto animation_data=w(animation,8);if(diag)diag->bck=animation_data;std::array<uint8_t,0x2C>anim_data{};if(!check(payload(animation_data,anim_data.size()),Check::AnimationPayload,animation_data,animation_data,uint32_t(anim_data.size()))||!check(dictionary_member(0x28,0x54,animation_data),Check::AnimationDictionary,animation_data,animation_data,file.base)||!read(animation_data,anim_data))return false;
 const bool kind=(w(anim_data,0xC)&0x7000)==0x1000;const auto getter=kind?GC(0x027E6AA8):GC(0x027E6EA8),entry=kind?GC(0x027E6C04):GC(0x027E7004);
 if(!check(w(morf,0x5C)==0xFFFF,Check::AnimationGetterKind,m.morf+0x5C,w(morf,0x5C),0xFFFF)||!check(w(morf,0x64)==0xFFFF,Check::AnimationEntryKind,m.morf+0x64,w(morf,0x64),0xFFFF)||!check(w(morf,0x60)==getter,Check::AnimationGetter,m.morf+0x60,w(morf,0x60),getter)||!check(w(morf,0x68)==entry,Check::AnimationEntry,m.morf+0x68,w(morf,0x68),entry))return false;
 // Canonical nested animation-buffer virtual and direct dispatch tables.
 const std::array<uint32_t,8>secondary{0,0,0,GC(0x028F036C),0,GC(0x028F036C),0,GC(0x027E0288)};
 if(!check(w(morf,0x38)==GD(0x1016D820),Check::AnimationSecondary,m.morf+0x38,w(morf,0x38),GD(0x1016D820))||!words(GD(0x1016D820),secondary))return false;
 const std::array<uint32_t,16>blend{0xFFFF,GC(0x027E7164),0xFFFF,GC(0x027E72CC),0xFFFF,GC(0x027E7538),0xFFFF,GC(0x027E7A14),0xFFFF,GC(0x027E89FC),0xFFFF,GC(0x027E8C4C),0xFFFF,GC(0x027E8E54),0xFFFF,GC(0x027E9020)};
 if(!words(GD(0x1016C6E0),blend))return false;
 const std::array<uint32_t,6>key_dispatch{0xFFFF,GC(0x027E92A8),0xFFFF,GC(0x027E9378),0xFFFF,GC(0x027E94D4)};
 const std::array<uint32_t,42>curve_dispatch{0xFFFF,GC(0x027E95E0),0xFFFF,GC(0x027E96B0),0xFFFF,GC(0x027E9780),0xFFFF,GC(0x027E9850),0xFFFF,GC(0x027E98F8),0xFFFF,GC(0x027E99A0),0xFFFF,GC(0x027E9A48),0xFFFF,GC(0x027E9AE0),0xFFFF,GC(0x027E9B90),0xFFFF,GC(0x027E924C),0xFFFF,GC(0x027E91F0),0xFFFF,GC(0x027E9194),0xFFFF,GC(0x027E9C38),0xFFFF,GC(0x027E9C88),0xFFFF,GC(0x027E9CD8),0xFFFF,GC(0x027E1CF0),0xFFFF,GC(0x027E1CF0),0xFFFF,GC(0x027E1CF0),0xFFFF,GC(0x027E1E9C),0xFFFF,GC(0x027E1E9C),0xFFFF,GC(0x027E1E9C)};
 if(!words(GD(0x1016C720),key_dispatch)||!words(GD(0x1016C738),curve_dispatch))return false;
 auto relative=[&](uint32_t field,uint32_t&target){uint32_t v{};if(!scalar(field,v)||!v)return false;const int64_t sum=int64_t(field)+int32_t(v);if(sum<0||sum>0xFFFFFFFF)return false;target=uint32_t(sum);return true;};
 auto asset=[&](uint32_t p,unsigned len,std::vector<uint8_t>&b){if(!payload(p,len)||len>0x100000)return false;b.resize(len);if(!recorded(p,b))return false;out.animation_payload.insert(out.animation_payload.end(),b.begin(),b.end());out.identity.insert(out.identity.end(),{p,len});return true;};
 auto scratch=[&](uint32_t p,unsigned len,std::vector<uint8_t>&b){if(!owned(p,len))return false;b.resize(len);return recorded(p,b);};
 const auto rotation=w(anim_data,0xC)&0x7000;if(!check(rotation==0||rotation==0x1000,Check::AnimationRotation,animation_data+0xC,rotation,0x1000))return false;
 const auto bone_count=h(anim_data,0x14),curve_count=w(anim_data,0x18);if(!check(bone_count&&bone_count<=count,Check::AnimationBoneCount,animation_data+0x14,bone_count,count)||!check(curve_count<=count*12,Check::AnimationCurveCount,animation_data+0x18,curve_count,count*12))return false;
 uint32_t track_rows{},bound_skeleton{},binding{};
 if(!check(relative(animation_data+0x20,track_rows),Check::AnimationTracks,animation_data+0x20,track_rows,1)||!check(relative(animation_data+0x24,bound_skeleton)&&bound_skeleton==skeleton,Check::AnimationSkeleton,animation_data+0x24,bound_skeleton,skeleton)||!check(w(morf,0x6C)==skeleton,Check::AnimationMorfSkeleton,m.morf+0x6C,w(morf,0x6C),skeleton)||!check(relative(animation_data+0x28,binding),Check::AnimationBinding,animation_data+0x28,binding,1))return false;
 // Force the source-proven direct skeleton binding. Name-remapping and its
 // additional graph are deliberately unsupported.
 if(!check(w(morf,0x48)==animation_data,Check::AnimationDirectBinding,m.morf+0x48,w(morf,0x48),animation_data)||!check(w(morf,0x4C)==track_rows,Check::AnimationTrackPointer,m.morf+0x4C,w(morf,0x4C),track_rows)||!check(h(morf,0x44)==bone_count,Check::AnimationTrackCount,m.morf+0x44,h(morf,0x44),bone_count)||!check(h(morf,0x46)==count,Check::AnimationModelCount,m.morf+0x46,h(morf,0x46),count)||!check(h(morf,0x42)>=std::max(bone_count,count)&&h(morf,0x42)<=128,Check::AnimationMapCapacity,m.morf+0x42,h(morf,0x42),std::max(bone_count,count)))return false;
 std::vector<uint8_t>tracks,map,bind;
 if(!check(asset(track_rows,bone_count*0x18,tracks)&&asset(binding,bone_count*2,bind)&&scratch(w(morf,0x3C),h(morf,0x42)*4,map),Check::AnimationAssets,track_rows,binding,w(morf,0x3C)))return false;
 for(unsigned j=0;j<bone_count;++j){const auto index=h(bind,j*2);if(!check(index==0xFFFF||index<count,Check::AnimationBindIndex,binding+j*2,index,count))return false;const auto mapped=w(map,j*4);if(!check((mapped&0x80000000)||(mapped&0x7FFF)<count,Check::AnimationMapIndex,w(morf,0x3C)+j*4,mapped,count))return false;}
 const float frame=fp(morf,0x9C),start_frame=fp(morf,0xC),end_frame=fp(morf,0x10);
 if(!check(std::isfinite(frame)&&std::isfinite(start_frame)&&std::isfinite(end_frame)&&start_frame==0&&end_frame>0&&end_frame<=32767&&frame>=0&&frame<=end_frame,Check::AnimationFrame,m.morf+0x9C,w(morf,0x9C),w(morf,0x10)))return false;
 const auto cache_capacity=w(morf,0x24);if(!check(cache_capacity>=curve_count&&cache_capacity<=4096&&w(morf,0x28)==curve_count,Check::AnimationCache,m.morf+0x24,cache_capacity,curve_count))return false;
 std::vector<uint8_t>cache;if(curve_count&&!check(scratch(w(morf,0x20),cache_capacity*12,cache),Check::AnimationCacheSpan,w(morf,0x20),cache_capacity*12,1))return false;
 phase(Refusal::Sampler);unsigned curve_serial=0;
 for(unsigned j=0;j<bone_count;++j){const unsigned channels=tracks[j*0x18+0xA];if(!check(channels<=12&&curve_serial+channels<=curve_count,Check::SamplerChannels,track_rows+j*0x18+0xA,channels,curve_count-curve_serial))return false;uint32_t curves{};if(channels&&!check(relative(track_rows+j*0x18+0x10,curves),Check::SamplerCurves,track_rows+j*0x18+0x10,curves,1))return false;
  std::vector<uint8_t>curve_rows;if(channels&&!check(asset(curves,channels*0x24,curve_rows),Check::SamplerCurves,curves,channels*0x24,1))return false;
  // Native direct bind 027DFCF8 consumes only the selected constant
  // components from BoneAnim+14, in scale/rotation/translation order.
  const auto track=std::span<const uint8_t>(tracks).subspan(j*0x18,0x18);
  const auto constant_flags=w(track);const unsigned constant_floats=((constant_flags&8)?3:0)+((constant_flags&0x10)?4:0)+((constant_flags&0x20)?3:0);
  if(constant_floats){uint32_t constants{};std::vector<uint8_t>b;if(!check(relative(track_rows+j*0x18+0x14,constants)&&asset(constants,constant_floats*4,b),Check::SamplerConstants,track_rows+j*0x18+0x14,constants,constant_floats*4))return false;for(unsigned q=0;q<b.size();q+=4)if(!check(std::isfinite(fp(b,q)),Check::SamplerConstantFinite,constants+q,w(b,q),0))return false;}
  uint16_t outputs=0;
  for(unsigned k=0;k<channels;++k,++curve_serial){auto row=std::span<const uint8_t>(curve_rows).subspan(k*0x24,0x24);const unsigned flags=h(row),key_type=flags&3,value_type=(flags>>2)&3,mode=(flags>>4)&7,keys=h(row,2),offset=w(row,4);
   diag->sampler_flags=flags;diag->sampler_keys=keys;diag->animation_end=w(morf,0x10);
   // Bound every canonical sampler payload; keyed modes also attest the
   // native scratch search window before its unbounded key walk.
   if(!check(key_type<=2&&value_type<=2&&mode<=6&&keys&&keys<=4096&&!(offset&3)&&offset<=0x2C&&!(outputs&(1u<<(offset/4))),Check::SamplerLayout,curves+k*0x24,(flags<<16)|keys,offset))return false;outputs|=1u<<(offset/4);
   for(unsigned p:{8u,12u,16u,20u,24u})if(!check(std::isfinite(fp(row,p)),Check::SamplerFinite,curves+k*0x24+p,w(row,p),0))return false;
   // 027DEAD0 clamps post-range frames to high when post-wrap bits12..13
   // are zero. Shorter curves need not span the animation duration.
   const auto low=fp(row,8),high=fp(row,12);if(!check(low==0&&high>low&&high<=32767&&(high>=end_frame||!(flags&0x3000)),Check::SamplerFrameRange,curves+k*0x24+8,w(row,8),w(row,12)))return false;
   uint32_t payload{};if(!check(relative(curves+k*0x24+0x20,payload),Check::SamplerPayload,curves+k*0x24+0x20,payload,1))return false;
   const bool keyed=mode==0||mode==1||mode==3||mode==5;
   unsigned values=keys*(mode==0?4:mode==1?2:1);
   unsigned payload_bytes=0;
   if(mode==2){const auto last=unsigned(std::trunc(high)-std::trunc(low));if(!check(last<=32767&&last+2<=keys,Check::SamplerLayout,curves+k*0x24,last+2,keys))return false;values=last+2;}
   if(mode==4||mode==6){const auto last=unsigned(std::trunc(high)-std::trunc(low));if(!check(last<=32767,Check::SamplerFrameRange,curves+k*0x24,last,32767))return false;values=last+1;}
   payload_bytes=mode==5||mode==6?((values+31)/32)*4:values*(value_type==0?4:value_type==1?2:1);
   std::vector<uint8_t>value_bytes;if(!check(asset(payload,payload_bytes,value_bytes),Check::SamplerPayload,payload,payload_bytes,1))return false;
   if(mode<5&&value_type==0)for(unsigned p=0;p<value_bytes.size();p+=4)if(!check(std::isfinite(fp(value_bytes,p)),Check::SamplerValueFinite,payload+p,w(value_bytes,p),0))return false;
   if(keyed){uint32_t key_base{};if(!check(relative(curves+k*0x24+0x1C,key_base),Check::SamplerKeyPayload,curves+k*0x24+0x1C,key_base,1))return false;std::vector<uint8_t>key_bytes;if(!check(asset(key_base,keys*(key_type==0?4:key_type==1?2:1),key_bytes),Check::SamplerKeyPayload,key_base,keys,key_type))return false;
    std::vector<float>times(keys);for(unsigned z=0;z<keys;++z){times[z]=key_type==0?fp(key_bytes,z*4):key_type==1?float(int16_t(h(key_bytes,z*2)))/32.f:float(key_bytes[z]);if(!check(std::isfinite(times[z])&&(!z||times[z]>times[z-1]),Check::SamplerKeyOrder,key_base+z*(key_type==0?4:key_type==1?2:1),z,key_type))return false;}
    if(!check(times.front()==low&&times.back()==high,Check::SamplerKeyRange,key_base,keys,key_type))return false;
    const auto cache_row=std::span<const uint8_t>(cache).subspan(curve_serial*12,12);const auto index=w(cache_row,8);
    const float cache_low=fp(cache_row,0),cache_high=fp(cache_row,4);
    // 027E1C34 takes the cold path for every finite frame when low is
    // +infinity or low>high, overwriting index before the key search reads it.
    // Native initialization writes only low; old index/high bytes are unused.
    const bool stale=cache_low==INFINITY||cache_low>cache_high;
    if(!stale){
     if(!check(index<keys,Check::SamplerCacheIndex,w(morf,0x20)+curve_serial*12+8,index,keys))return false;
     if(!check(cache_low==times[index]&&cache_high==(index+1<keys?times[index+1]:times[index]+1),Check::SamplerCacheWindow,w(morf,0x20)+curve_serial*12,w(cache_row,0),w(cache_row,4)))return false;
    }
   }
  }
 }
 if(!check(curve_serial==curve_count,Check::SamplerCount,animation_data+0x18,curve_serial,curve_count))return false;
 // Record the final native dispatch table before the complete no-alias pass.
 const std::array<uint32_t,8>dispatch{0xFFFF,GC(0x027E4D20),0xFFFF,GC(0x027E50A8),0xFFFF,GC(0x027E565C),0xFFFF,GC(0x027E5DA4)};if(!words(GD(0x1016C6C0),dispatch))return false;
 // Every native write destination is a distinct actor-owned allocation slice.
 // The documented DCZeroRange alias is allowed only for the exact matrix span.
 phase(Refusal::WorkSpans);struct Slice{uint32_t p;unsigned n;};
 std::vector<Slice>work{{m.morf,0xC8},{m.model,0x170},{m.joint_tree,0x3C},{w(morf,0x30),count*0x30},{w(morf,0x78),count*0x38},{w(morf,0xA8),count*0x20},{w(morf,0xAC),count*0x10},{w(ref,0xC),count*0x40},{w(ref,0x10),count*0x30},{w(morf,0x3C),h(morf,0x42)*4}};
 if(matrix_mode==3)work.push_back({w(ref,0x14),count*12});
 // DCZeroRange rounds the address down and end up to32bytes. Native
 // animation-work clear first rounds count*38 up to64bytes (027E0710).
 auto zero_span=[&](uint32_t p,unsigned length){const auto begin=p&~31u;const uint64_t end=(uint64_t(p)+length+31)&~uint64_t(31);return Slice{begin,unsigned(end-begin)};};
 work[4]=zero_span(w(morf,0x78),(count*0x38+63)&~63u);
 if(w(ref,0x10)==w(ref,0x38))work[8]=zero_span(w(ref,0x10),count*0x30);
 for(const auto&range:work)if(!check(owned(range.p,range.n),Check::WorkRange,range.p,range.n,size))return false;
 if(curve_count)work.push_back({w(morf,0x20),cache_capacity*12});
 if(w(ref,0x38)!=w(ref,0x10))work.push_back({w(ref,0x38),count*0x30});
 auto overlaps=[](Slice x,Slice y){return uint64_t(x.p)<uint64_t(y.p)+y.n&&uint64_t(y.p)<uint64_t(x.p)+x.n;};
 for(unsigned x=0;x<work.size();++x){for(const auto&actor:expected_.actors)if(!check(!overlaps(work[x],{actor.address,0x920}),Check::WorkActorAlias,work[x].p,work[x].n,actor.address))return false;for(unsigned y=0;y<x;++y)if(!check(!overlaps(work[x],work[y]),Check::WorkOverlap,work[x].p,work[x].n,work[y].p))return false;}
 // The two native metadata writes are not actor-heap work: shared root
 // binding and animation frame cache. Neither may alias another immutable
 // payload, registry, wrapper or descriptor read. Do not exempt a field merely
 // because another read calls it writable: only its own typed read was split.
 if(!check(!overlaps({start,size},{resource_start,resource_size})&&!overlaps({start,size},{diag->payload_heap.address,diag->payload_heap.bytes}),Check::WorkHeapOverlap,start,size,resource_start))return false;
 std::vector<Slice>writes=work;writes.push_back({rows+0x14,4});writes.push_back({c.animation.address,4});
 // Source-fixed shared native scratch is deliberately left changed. Prove its
 // writes cannot alias any resource/registry/descriptor dependency either.
 writes.push_back({GD(0x1048D0CC),0x30}); // setMtx matrix stack
 writes.push_back({GD(0x101FF5F0),0x30}); // calc_mtx
 writes.push_back({GD(0x104B4868),0x30}); // People joint matrix copy022BE168
 writes.push_back({GD(0x104B4628),4});    // current MtxCalc
 writes.push_back({GD(0x104B462C),4});    // current model
 for(unsigned x=0;x<writes.size();++x){for(unsigned y=0;y<x;++y)if(!check(!overlaps(writes[x],writes[y]),Check::WorkOverlap,writes[x].p,writes[x].n,writes[y].p))return false;
  for(const auto&r:immutable){
   if(!r.forced&&(within(r.p,r.n,start,size)||(r.p==a.address&&r.n==actor.size())))continue;
   if(!check(!overlaps(writes[x],{r.p,unsigned(r.n)}),Check::WorkImmutableAlias,writes[x].p,writes[x].n,r.p))return false;
  }
 }
 out.identity.insert(out.identity.end(),{matrix_mode,matrix_mode==3?w(ref,0x14):0,track_rows,bound_skeleton,binding,w(morf,0x3C),h(morf,0x42),w(morf,0x20),cache_capacity,curve_count});
 std::copy_n(morf.begin()+0x98,16,c.animation.frame_control.begin());std::copy_n(morf.begin()+0xB0,12,c.animation.morph.begin());std::copy_n(animation.begin(),4,c.animation.cached_frame.begin());
 auto copy=[&](unsigned p,auto&b){std::copy_n(actor.begin()+p,b.size(),b.begin());};copy(0x314,c.transform.position);copy(0x330,c.transform.scale);std::copy_n(model.begin()+0xBC,12,c.transform.model_scale.begin());std::copy_n(model.begin()+0xC8,48,c.transform.base.begin());
 out.identity.insert(out.identity.end(),{control,info,w(archive,0x5C),w(archive,0x64),w(archive,0x88),body,w(data),count,rows,skeleton,w(ref,8),w(ref,0xC),w(ref,0x10),w(ref,0x38),w(morf,0x30),w(morf,0x78),w(morf,0xA8),w(morf,0xAC),animation_data,head,back});
 if(!sources_.resident(a,expected_.identity))return false;
 if(invocation&&(!scope.native_dispatch->current()||!scope.native_quiescence->current(scope.native_cpu))){phase(Refusal::Scope);return false;}
 for(const auto&r:writes)out.trace.writes.push_back({r.p,r.n});
 phase(Refusal::Complete);if(diag)diag->check=Check::None;return true;
}
Diagnostic Adapter::diagnose(unsigned n)const{
 Diagnostic first;first.actor_index=n;
 try{Graph a,b;if(!sample(n,a,&first,false))return first;
  Diagnostic second;second.actor_index=n;if(!sample(n,b,&second,false))return second;
  if(a.identity!=b.identity||a.descriptors!=b.descriptors||a.bones!=b.bones||a.animation_payload!=b.animation_payload||a.trace!=b.trace||!same(a.capture,b.capture)||first.cache_holder!=second.cache_holder||first.cache_resource!=second.cache_resource||first.cache_buffer.address!=second.cache_buffer.address||first.cache_buffer.bytes!=second.cache_buffer.bytes||first.cache_size!=second.cache_size||first.cache_membership!=second.cache_membership){second.refusal=Refusal::Changed;return second;}
  second.parsed=true;return second;
 }catch(...){first.refusal=Refusal::Changed;return first;}
}
bool Adapter::collect(unsigned n,Graph&out)const{try{Graph a,b;if(!sample(n,a)||!sample(n,b)||a.identity!=b.identity||a.descriptors!=b.descriptors||a.bones!=b.bones||a.animation_payload!=b.animation_payload||a.trace!=b.trace||!same(a.capture,b.capture))return false;out=std::move(b);return true;}catch(...){return false;}}
bool Adapter::prepare(){Scope s;try{if(!sources_.scope||!sources_.scope(s)||!s.preclaim)return false;std::array<Graph,2>b;if(!collect(0,b[0])||!collect(1,b[1]))return false;baseline_[0]=std::move(b[0]);baseline_[1]=std::move(b[1]);return true;}catch(...){return false;}}
std::unique_ptr<Adapter>Adapter::clone_sources(Sources sources)const{
 if(!baseline_[0]||!baseline_[1]||!sources.read||!sources.resident||!sources.scope)return {};
 auto clone=std::make_unique<Adapter>(expected_,std::move(sources));
 clone->baseline_=baseline_;return clone;
}
bool Adapter::capture(unsigned n,Capture&out)const{out={};if(n>=2||!baseline_[n])return false;Graph v;if(!collect(n,v)||v.identity!=baseline_[n]->identity||v.descriptors!=baseline_[n]->descriptors||v.bones!=baseline_[n]->bones||v.animation_payload!=baseline_[n]->animation_payload||v.capture.model!=baseline_[n]->capture.model)return false;out=v.capture;return true;}
bool Adapter::trace(unsigned n,story_native_model_saved_graph::Trace&out)const{
 out={};if(n>=2||!baseline_[n])return false;Graph v;
 if(!collect(n,v)||v.identity!=baseline_[n]->identity||v.descriptors!=baseline_[n]->descriptors||v.bones!=baseline_[n]->bones||v.animation_payload!=baseline_[n]->animation_payload||v.capture.model!=baseline_[n]->capture.model)return false;
 out=std::move(v.trace);return true;
}
bool Adapter::verify_model(const Expected&e,unsigned n,const R::ModelIdentity&m,const R::Transform&t)const{
 // Never turn a partially checked payload graph into a native-call capability.
 if(!refresh_admission_enabled())return false;
 Scope live;if(!sources_.scope||!sources_.scope(live)||!live.native_quiescence||live.native_quiescence->read_only()||!live.native_quiescence->current(live.native_cpu))return false;
 if(e!=expected_||n>=2||!baseline_[n]||m!=baseline_[n]->capture.model||t!=baseline_[n]->capture.transform)return false;Capture v;return capture(n,v);}
}
