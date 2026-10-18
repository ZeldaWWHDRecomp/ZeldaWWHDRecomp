// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_runtime_route_capture.h"
#include "story_native_event_image.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
namespace story_native_wait_runtime_route_capture {namespace {
uint32_t word(std::span<const uint8_t>b,unsigned p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
uint16_t half(std::span<const uint8_t>b,unsigned p){return uint16_t(b[p])<<8|b[p+1];}
bool inside(uint32_t p,const Expected&e){return p>=e.identity.image&&uint64_t(p)<uint64_t(e.identity.image)+e.identity.bytes;}
std::array<uint32_t,8> published(const Expected&e){
 const auto base=e.identity.image;std::array<uint32_t,8>out{base,base+0x40,base+0xF0,base+0x230,base+0x410,0,base+0x510,0};
 if(e.program){const auto offsets=e.program->pool_offsets();for(unsigned n=0;n<8;++n)out[n]=offsets[n]?base+offsets[n]:n?0:base;}return out;
}
constexpr const char*owned_names[]={"SDK_ACTOR_A","SDK_ACTOR_B","CAMERA","TIMEKEEPER"};
}
Adapter::Adapter(Expected e,Sources s):expected_(std::move(e)),sources_(std::move(s)){}
bool Adapter::read(uint32_t at,std::span<uint8_t>out)const{
 if(!sources_.read||out.empty()||uint64_t(at)+out.size()>0x100000000ull)return false;
 const bool native=at>=mem::kMem2Start&&uint64_t(at)+out.size()<=mem::kMem2End;
 const bool owned=at>=expected_.identity.image&&uint64_t(at)+out.size()<=uint64_t(expected_.identity.image)+expected_.identity.bytes;
 return (native||owned)&&sources_.read(at,out);
}
bool Adapter::retained()const{return sources_.retained&&sources_.retained(expected_);}
bool Adapter::context()const{
 std::array<uint8_t,4>b{};std::array<uint8_t,8>name{};
 return expected_.room_number<64&&read(GD(0x104753A8),b)&&word(b)&&
  read(GD(0x1047E6C4),b)&&word(b)==expected_.identity.stage&&
  read(GD(0x1047E8F0)+expected_.room_number*0x22C,b)&&word(b)==expected_.identity.room&&
  read(GD(0x1046F0B0)+0x5134,name)&&name==expected_.stage_name;
}
bool Adapter::image()const{
 if((expected_.program?(expected_.identity.bytes!=expected_.program->bytes()||expected_.frames!=expected_.program->frames()):expected_.identity.bytes!=0x520)||!expected_.frames||expected_.frames>32767)return false;
 std::vector<uint8_t>b(expected_.identity.bytes);if(!read(expected_.identity.image,b))return false;
 const auto canonical=expected_.program?std::vector<uint8_t>(expected_.program->pristine().begin(),expected_.program->pristine().end()):story_native_event_image::make_wait_fixture(expected_.frames);
 for(unsigned p=0;p<b.size();++p){bool mutable_byte=p>=0xE4&&p<0xE8;
  for(unsigned n=0;n<4;++n)mutable_byte|=p>=0xF0+n*0x50+0x38&&p<0xF0+n*0x50+0x48;
  if(!mutable_byte&&b[p]!=canonical[p])return false;
 }
 const auto state=word(b,0xE4);return state==0||state==1||state==2||state==4;
}
bool Adapter::name_miss(uint32_t at)const{
 if(!at||inside(at,expected_))return false;
 std::array<uint8_t,32>name{},again{};
 if(!read(at,name)||!read(at,again)||name!=again)return false;
 const auto end=std::find(name.begin(),name.end(),0);if(end==name.end()||end==name.begin())return false;
 const auto length=size_t(end-name.begin());
 for(const char*owned:owned_names){const auto n=std::char_traits<char>::length(owned);
  // Prefixes and extensions of reserved staff names also stay closed; the
  // ordinary retail escape is for independently distinct terminated names.
  if(std::equal(name.begin(),name.begin()+std::min(length,n),owned))return false;
 }
 return true;
}
bool Adapter::native_name(uint32_t at)const{
 if(!name_miss(at))return false;
 // The source-audited People paths use this exact native 19-entry table for
 // both common cut binding and privateCut's independent lookup.
 std::array<uint8_t,19*4>table{};if(!read(GD(0x101C3E7C),table))return false;
 for(unsigned n=0;n<19;++n)if(word(table,n*4)==at)return true;
 return false;
}
bool Adapter::people(uint32_t at)const{
 if((at&3)||at<mem::kMem2Start||uint64_t(at)+0x920>mem::kMem2End)return false;
 for(const auto&a:expected_.actors)if(at==a.address)return false; // reuse uncertain
 std::array<uint8_t,0x920>a{},b{};std::array<uint8_t,0x14>profile{};
 if(!read(at,a)||!read(at,b)||a!=b||!word(a,4)||word(a,4)==~0u||a[0xC]==3||half(a,0xE)!=0x175||
    word(a,0x10)!=GD(0x101C4CD0)||word(a,0xB4)!=GD(0x100209C4)||a[0x905]>=19)return false;
 for(const auto&owned:expected_.actors)if(word(a,4)==owned.pid)return false;
 if(!read(GD(0x101C4CD0),profile)||half(profile,8)!=0x175||word(profile,0x10)!=0x920)return false;
 std::array<uint8_t,4>name{};if(!read(GD(0x101C3E7C)+a[0x905]*4,name))return false;
 const auto native=word(name);if(!native_name(native)||word(a,0x3E0)!=native||
    (word(a,0x3E8)!=0&&word(a,0x3E8)!=at))return false;
 // No current common-cut pointer may already borrow the private native image.
 for(unsigned p=0x3E0;p+4<=0x44C;p+=4)if(inside(word(a,p),expected_))return false;
 return true;
}
bool Adapter::active_layer(uint32_t layer)const{
 struct Node {uint32_t layer{},scene{};std::array<uint8_t,0x2C>data{};std::array<uint8_t,0x60>owner{};};
 std::array<Node,16>chain{};unsigned count=0;bool ended=false;
 auto native=[](uint32_t at,uint32_t bytes){return !(at&3)&&at>=mem::kMem2Start&&uint64_t(at)+bytes<=mem::kMem2End;};
 for(unsigned n=0;n<chain.size();++n){
  if(!native(layer,0x2C))return false;
  for(unsigned j=0;j<count;++j)if(chain[j].layer==layer)return false;
  auto&node=chain[count++];node.layer=layer;if(!read(layer,node.data))return false;
  node.scene=word(node.data,0x18);
  if(layer==GD(0x1048A9BC)){
   // Global Ship may attach directly to the canonical root. Unlike SDK
   // residents, its proof does not require a particular first room owner.
   if(word(node.data,0xC)||node.scene)return false;ended=true;break;
  }
  if(!node.scene||!native(node.scene,0x60)||!read(node.scene,node.owner)||
     node.owner[0xC]==3||node.owner[0x5C])return false;
  layer=word(node.owner,0x2C);if(!layer)return false;
 }
 if(!ended)return false;
 for(unsigned n=0;n<count;++n){const auto&node=chain[n];std::array<uint8_t,0x2C>data{};
  if(!read(node.layer,data)||data!=node.data)return false;
  if(node.scene){std::array<uint8_t,0x60>owner{};if(!read(node.scene,owner)||owner!=node.owner)return false;}
 }
 return true;
}
bool Adapter::ship(uint32_t at)const{
 if(!sources_.ship||!sources_.ship->address||!sources_.ship->pid||sources_.ship->pid==~0u||
    at!=sources_.ship->address||(at&3)||at<mem::kMem2Start||uint64_t(at)+0xD198>mem::kMem2End)return false;
 for(const auto&a:expected_.actors)if(at==a.address||sources_.ship->pid==a.pid)return false;
 constexpr uint32_t ProfileId=0xA5;
 const uint32_t profile=GD(0x101D04A8),methods=GD(0x101D0394),vtable=GD(0x1003A2EC);
 std::array<uint8_t,4>global{},table{},global_after{},table_after{};
 std::array<uint8_t,0x328>first{},second{};
 std::array<uint8_t,0x28>meta{},meta_after{};
 if(!read(GD(0x1046F0B0)+0x5B3C,global)||word(global)!=at||
    !read(GD(0x101F3EE0)+ProfileId*4,table)||word(table)!=profile||
    !read(profile,meta)||half(meta,8)!=ProfileId||word(meta,0x10)!=0xD198||word(meta,0x24)!=methods||
    !read(at,first)||word(first,4)!=sources_.ship->pid||first[0xC]==3||first[0x5C]||
    half(first,0xE)!=ProfileId||word(first,0x10)!=profile||word(first,0xB4)!=vtable||
    (first[0x326]>=64&&first[0x326]!=0xFF)||!active_layer(word(first,0x2C)))return false;
 // Ship's room follows ground/stay updates without reparenting. Its actual
 // valid room is independently bounded, never equated to the SDK actors' room.
 // FF is native GetRoomId's unknown-room sentinel; ancestry still must be live.
 // Only a readable status for a guaranteed nonmatching native lookup is issued;
 // no owned-object lease or cached borrow is inferred from this class prefix.
 if(!read(at,second)||second!=first||!active_layer(word(second,0x2C))||
    !read(GD(0x1046F0B0)+0x5B3C,global_after)||global_after!=global||
    !read(GD(0x101F3EE0)+ProfileId*4,table_after)||table_after!=table||
    !read(profile,meta_after)||meta_after!=meta)return false;
 return true;
}
bool Adapter::retail_safe(const Cpu*c,uint32_t fn)const noexcept{
 try{
  if(!c||!retained()||!context()||!image())return false;
  bool safe=false;std::array<uint8_t,32>lookup_name{};bool lookup=false;
  if(fn==GC(0x022C36D0)||fn==GC(0x022C2958))safe=people(c->r[3]);
  else if(fn==GC(0x0259F858))safe=c->r[3]>=0x3E0&&people(c->r[3]-0x3E0);
  else if(fn==GC(0x02542D88)){
   std::array<uint8_t,0x20>pools{};
   const auto expected=published(expected_);
   // Native getMyStaffId reads actor_status before comparing any name. Admit
   // a nonnull argument only through the independently rooted exact Ship proof.
   if(c->r[3]!=expected_.identity.manager||c->r[6]||!name_miss(c->r[4])||
      (c->r[5]&&!ship(c->r[5]))||!read(expected_.identity.manager,pools))return false;
   if(!read(c->r[4],lookup_name))return false;
   lookup=true;safe=true;for(unsigned n=0;n<8;++n)safe&=word(pools,n*4)==expected[n];
  }
  // Camera identity/reuse, WAIT callbacks, allOff and foreign manager/control
  // receivers remain unsupported; an empty cache alone cannot prove a later
  // native EventCamera will not acquire this manager's CAMERA staff.
  if(!safe||!retained()||!context()||!image())return false;
  if(lookup){std::array<uint8_t,32>again{};if(!name_miss(c->r[4])||!read(c->r[4],again)||again!=lookup_name||
     (c->r[5]&&!ship(c->r[5])))return false;}
  return true;
 }catch(...){return false;}
}
bool Adapter::end_observation(Cpu*c,story_native_event_end_guard::Observation&out)const noexcept{
 out={};try{
  if(!c||!retained()||!context()||!image()||!sources_.resident||!sources_.camera)return false;
  const auto&i=expected_.identity;
  if(c->r[3]!=i.manager-0xF4&&(c->r[3]!=i.manager||c->r[4]!=0))return false;
  for(const auto&a:expected_.actors)if(!sources_.resident(a,i))return false;
  Camera camera;if(!sources_.camera(camera)||camera.address!=expected_.camera.address||camera.pid!=expected_.camera.pid)return false;
  std::array<uint8_t,0xF4>control{};std::array<uint8_t,0x540>manager{};
  if(!read(i.manager-0xF4,control)||!read(i.manager,manager))return false;
  const auto pools=published(expected_);
  for(unsigned n=0;n<8;++n)if(word(manager,n*4)!=pools[n])return false;
  std::array<uint8_t,4>state{};if(!read(i.image+0xE4,state))return false;
  // mainProc naturally clears mCurrentEvent after CLOSE. Retained event
  // identity comes from the exact table/index; zero is accepted at native CLOSE/NONE.
  const auto event=word(manager,0x53C),native_state=word(state);
  if(native_state==1){
   if(event||word(manager,0x20)||!sources_.pending_order||!sources_.pending_order(expected_))return false;
   // Native order only writes ORDER. Any advance/cache/timer mutation means
   // this is not the pristine pending event certified by that live receipt.
   std::vector<uint8_t>ordered(i.bytes);if(!read(i.image,ordered))return false;
   const auto canonical=expected_.program?std::vector<uint8_t>(expected_.program->pristine().begin(),expected_.program->pristine().end()):story_native_event_image::make_wait_fixture(expected_.frames);
   for(unsigned n=0;n<4;++n)for(unsigned p=0x38;p<0x48;++p)
    if(ordered[0xF0+n*0x50+p]!=canonical[0xF0+n*0x50+p])return false;
  }else if(event!=i.image+0x40&&!(event==0&&(native_state==0||native_state==4)))return false;
  story_native_event_end_guard::Observation o;
  o.identity={i.manager,i.image,i.bytes,i.generation,i.program,0,i.stage,i.room};o.table=pools[1];o.event=i.image+0x40;
  o.mode=control[0xC2];o.partner1=word(control,0xC4);o.partner2=word(control,0xC8);o.talk_partner=word(control,0xCC);o.item_partner=word(control,0xD0);
  const auto id=half(control,0xD8);o.control_event_id=id&0x8000?int32_t(id)-65536:int32_t(id);
  if(control[0xC0]||o.mode!=3||o.partner1!=~0u||o.partner2!=~0u||o.talk_partner!=~0u||o.item_partner!=~0u||o.control_event_id!=-1)return false;
  std::array<uint8_t,0xF4>control_after{};std::array<uint8_t,0x540>manager_after{};
  if(!read(i.manager-0xF4,control_after)||!read(i.manager,manager_after)||control_after!=control||manager_after!=manager)return false;
  if(!retained()||!context())return false;
  for(const auto&a:expected_.actors)if(!sources_.resident(a,i))return false;
  o.retained_claim=o.identity_valid=true;out=o;return true;
 }catch(...){return false;}
}
}
