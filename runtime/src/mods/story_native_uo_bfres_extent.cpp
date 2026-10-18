// SPDX-License-Identifier: MPL-2.0
#include "story_native_uo_bfres_extent.h"
#include "guest_addr.h"
#include <algorithm>
#include <vector>
namespace story_native_uo_bfres_extent {
namespace {constexpr uint32_t kMaximumHeapBytes=32u*1024*1024;}
namespace {
uint32_t w(std::span<const uint8_t>b,unsigned p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
uint16_t h(std::span<const uint8_t>b,unsigned p=0){return uint16_t(b[p])<<8|b[p+1];}
bool inside(uint32_t p,uint64_t n,Range r){return r.address&&r.bytes&&uint64_t(r.address)+r.bytes<=0x100000000ull&&p>=r.address&&uint64_t(p)+n<=uint64_t(r.address)+r.bytes;}
bool aligned(uint32_t p){return p&&!(p&3);}
}
bool verify(const Read&read,const Input&i,Extent&out){
 out={};try{
  const auto allocation=i.parent_allocation;
  if(!read||!aligned(i.parent_heap)||i.parent_heap!=i.archive_parent_heap||i.parent_heap!=i.holder_heap||
   !aligned(allocation.address)||allocation.bytes>kMaximumHeapBytes||!inside(i.parent_heap,i.heap.size(),allocation)||
   w(i.heap,0xC)!=GD(0x10145B38)||w(i.heap,0x1C)!=allocation.address||w(i.heap,0x20)!=allocation.bytes||
   // Ordinary02610B40/02612C88 set LoadArg heap AND instanceHeap to
   // this same supplied heap; readPackEntry also allocates both here.
   // Pack-scanned temporary-heap holders are intentionally unsupported.
   !aligned(i.resource_address)||!inside(i.resource_address,i.resource.size(),allocation)||
   w(i.resource,0x10)!=GD(0x1015E184)||!aligned(i.archive_file))return false;
  // These are the exact native getter/lookup entries, never invoked here.
  for(const auto&pair:std::array<std::pair<uint32_t,uint32_t>,4>{{{GD(0x10145B38)+0x5C,GC(0x02755280)},{GD(0x10145B38)+0x6C,GC(0x02755298)},{GD(0x1015E184)+0x3C,GC(0x0275E208)},{GD(0x1015E184)+0x34,GC(0x0275CF28)}}}){
   std::array<uint8_t,4>b{};if(!read(pair.first,b)||w(b)!=pair.second)return false;
  }
  const uint32_t base=w(i.resource,0x14),logical=w(i.resource,0x18),capacity=w(i.resource,0x1C);
  if(!aligned(base)||logical<0x28||logical>capacity||!inside(base,capacity,allocation)||w(i.resource,0x28)!=base)return false;
  auto load=[&](uint32_t offset,std::span<uint8_t>b){return uint64_t(offset)+b.size()<=logical&&read(base+offset,b);};
  std::array<uint8_t,0x20>header{};if(!load(0,header)||w(header)!=0x53415243||h(header,4)!=0x14||h(header,6)!=0xFEFF||w(header,8)!=logical||h(header,0x10)!=0x100||w(header,0x14)!=0x53464154||h(header,0x18)!=0xC)return false;
  const uint32_t count=h(header,0x1A),multiplier=w(header,0x1C),data=w(header,0xC);
  if(!count||count>0x3FFF||!multiplier||uint64_t(0x20)+uint64_t(count)*0x10+8>logical)return false;
  const uint32_t sfnt=0x20+count*0x10,names=sfnt+8;
  if(data<names||data>logical||w(i.resource,0x2C)!=base+0x14||w(i.resource,0x30)!=base+names||w(i.resource,0x34)!=count||w(i.resource,0x38)!=base+0x20||w(i.resource,0x3C)!=base+data)return false;
  std::array<uint8_t,8>name_header{};if(!load(sfnt,name_header)||w(name_header)!=0x53464E54||h(name_header,4)!=8)return false;
  constexpr std::array<uint8_t,9>name{'U','o','.','b','f','r','e','s',0};uint32_t wanted=0;for(unsigned n=0;n<8;++n)wanted=wanted*multiplier+name[n];
  uint32_t previous=0;bool found=false;Extent candidate{};
  for(uint32_t n=0;n<count;++n){
   std::array<uint8_t,16>entry{};if(!load(0x20+n*0x10,entry))return false;
   const auto hash=w(entry),attr=w(entry,4),start=w(entry,8),end=w(entry,12);
   if((n&&hash<previous)||end<start||uint64_t(data)+end>logical)return false;previous=hash;
   if(hash!=wanted)continue;
   // Initial supported grammar has no selected-name hash collisions. Native
   // collision ordinals >1 and unnamed entries need a separate audited path.
   if(found||attr>>24!=1)return false;
   const uint64_t offset=uint64_t(names)+uint64_t(attr&0xFFFFFF)*4;
   if(offset+name.size()>data)return false;
   std::array<uint8_t,name.size()>actual{};if(!load(uint32_t(offset),actual)||actual!=name||end==start||uint64_t(base)+data+start!=i.archive_file)return false;
   candidate={i.archive_file,end-start,base,logical,capacity};found=true;
  }
  if(!found)return false;
  // The copied metadata must still describe the same bounded native objects.
  std::array<uint8_t,0x28>heap{};std::array<uint8_t,0x44>resource{};
  if(!read(i.parent_heap,heap)||heap!=i.heap||!read(i.resource_address,resource)||resource!=i.resource)return false;
  out=candidate;return true;
 }catch(...){out={};return false;}
}
}
