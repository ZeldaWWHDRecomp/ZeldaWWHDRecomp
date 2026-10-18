// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_table_scan.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
namespace story_native_event_table_scan {namespace {
const uint32_t Play=GD(0x1046F0B0),Stage=GD(0x1047E6C4),Rooms=GD(0x1047E8F0),Initialized=GD(0x104753A8),Arc=GD(0x1004DF68),Name=GD(0x1004DF70);
uint32_t word(std::span<const uint8_t>b,unsigned p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool mem2(uint32_t a,uint64_t n){return !(a&3)&&n&&a>=mem::kMem2Start&&uint64_t(a)+n<=mem::kMem2End;}
bool provenance(const Context&c){const auto&r=c.resource;return c.exclusive_idle&&c.stage_pid&&c.room_pid&&c.room_number<64&&c.stage_name[0]&&r.loaded_retail&&r.generation&&r.lookup_arc==Arc&&r.lookup_name==Name&&r.bytes>=0x38&&r.bytes<=2*1024*1024&&mem2(r.base,r.bytes);}
}
Result inspect(const Read&read,const Query&query){
 Result result;auto fail=[&](Reason r){result.idle=false;result.reason=r;return result;};
 try{
  Context before;if(!read||!query||!query(before)||!provenance(before))return fail(Reason::Provenance);
  auto current=[&](){std::array<uint8_t,4>s{},r{},ready{};std::array<uint8_t,8>name{};return read(Initialized,ready)&&word(ready)&&read(Stage,s)&&word(s)==before.stage_pid&&read(Rooms+before.room_number*0x22C,r)&&word(r)==before.room_pid&&read(Play+0x5134,name)&&name==before.stage_name;};
  if(!current())return fail(Reason::Changed);
  std::array<uint8_t,0x24>manager{};std::array<uint8_t,4>event{};std::array<uint8_t,0x38>header{};
  if(!read(Play+0x52C4,manager)||!read(Play+0x52C4+0x53C,event)||!read(before.resource.base,header))return fail(Reason::Read);
  if(word(manager)!=before.resource.base)return fail(Reason::Provenance);
  if(word(event)||word(manager,0x20))return fail(Reason::Busy);
  // Native set_data025443B4 installs base+sectionOffset for seven pools;
  // event_data02544044 uses signed headercount and event stride176.
  const uint32_t strides[]={176,80,80,64,4,4,1};
  std::array<uint64_t,7>begin{},end{};
  for(unsigned n=0;n<7;++n){auto offset=word(header,n*8),count=word(header,n*8+4);
   // Retail Stage tables can exceed the small authored WAIT fixture limits.
   // The independently verified, capped resource extent bounds every pool
   // and the full census; negative native signed counts remain invalid.
   if(count>0x7FFFFFFFu||uint64_t(count)*strides[n]>before.resource.bytes)return fail(Reason::Layout);
   if(!count){if(word(manager,(n+1)*4))return fail(Reason::Layout);continue;}
   const uint64_t bytes=uint64_t(count)*strides[n];if(offset<0x38||(n<6&&(offset&3))||uint64_t(offset)+bytes>before.resource.bytes)return fail(Reason::Layout);
   begin[n]=offset;end[n]=uint64_t(offset)+bytes;for(unsigned k=0;k<n;++k)if(end[k]&&begin[n]<end[k]&&begin[k]<end[n])return fail(Reason::Layout);
   if(uint64_t(word(manager,(n+1)*4))!=uint64_t(before.resource.base)+offset)return fail(Reason::Layout);
  }
  result.events=word(header,4);std::array<uint8_t,176>row{};
  for(unsigned n=0;n<result.events;++n){const uint32_t at=word(manager,4)+n*176;if(!read(at,row))return fail(Reason::Read);auto state=word(row,0xA4);
   if(state){result.blocking_index=n;result.blocking_state=state;return fail(state==1||state==2||state==4?Reason::Busy:Reason::State);}}
  // Re-read every event state: no stale mixed scan may become an idle receipt.
  for(unsigned n=0;n<result.events;++n){std::array<uint8_t,4>state{};if(!read(word(manager,4)+n*176+0xA4,state))return fail(Reason::Read);if(word(state))return fail(Reason::Changed);}
  Context after;std::array<uint8_t,0x24>fresh_manager{};std::array<uint8_t,4>fresh_event{};std::array<uint8_t,0x38>fresh_header{};
  if(!query(after)||after!=before||!current()||!read(Play+0x52C4,fresh_manager)||fresh_manager!=manager||!read(Play+0x52C4+0x53C,fresh_event)||fresh_event!=event||!read(before.resource.base,fresh_header)||fresh_header!=header)return fail(Reason::Changed);
  result.idle=true;result.reason=Reason::None;return result;
 }catch(...){return fail(Reason::Read);}
}
}
