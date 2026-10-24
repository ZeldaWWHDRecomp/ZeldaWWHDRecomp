// SPDX-License-Identifier: MPL-2.0
#include "story_item_models.h"
#include "guest_addr.h"
#include "runtime.h"
#include "story_treasure_chest.h"
#include <algorithm>
#include <deque>
#include <mutex>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include "mod_hash.h"
#include <cstring>
#include <vector>
namespace story_item_models { namespace {
struct Decl{uint16_t key;std::string archive;unsigned frames;std::string hash;};
std::vector<Decl> declarations;
uint32_t area=0;void(*original)(Cpu*,uint32_t)=nullptr;Invoke invoke=nullptr;bool installed=false;bool progress_cancel=false;
// Item get (hold_up, the header): the queue of items to show (host), the text arming, and the guest
// record of the running demo at kDemoOffset: magic, phase (1 ordering, 2 running), the demo item's
// process id, ticks in the phase, the item key, a word for fopAcM_SearchByID's answer.
std::mutex queue_mu;std::deque<uint16_t> queue;bool(*arm)(uint16_t,bool)=nullptr;bool rearm=false;
constexpr uint32_t DemoMagic=0x48555031,Demo=kDemoOffset,GetFrames=3600,ModelBytes=kDemoOffset,GetRequest=0x80000000u;
static_assert(512+0x1C8<=kDemoOffset&&kDemoOffset+24<=kMemoryBytes,"the demo record lies after the model's light settings");
// Guest record: magic,key,pending,Link,PID,ticks,heap,model,failed,hash; phase40/name80/safe180/matrix200.
constexpr uint32_t Magic=0x494D4431,Phase=40,Name=80,Safe=180,Matrix=200,Retiring=488;
void table_hook(Cpu*);void archive_hook(Cpu*);void backing_hook(Cpu*);
struct Keep{Cpu*c;Cpu saved;explicit Keep(Cpu*p):c(p),saved(*p){}~Keep(){*c=saved;}};
bool span(uint32_t p,uint32_t n){return p>=0x10000000u&&p<0x68000000u&&n<=0x68000000u-p;}
bool trace(){const char* v=std::getenv("WWHD_ITEM_TRACE");return v&&v[0]=='1'&&v[1]==0;}
Decl* definition(uint32_t key){for(auto&d:declarations)if(d.key==key)return &d;return nullptr;}
std::string fingerprint(uint16_t key,const std::string&s,unsigned frames){return mods::hash::sha256_text(std::to_string(key)+":"+s+":"+std::to_string(frames));}
bool valid(){auto*d=area?definition(ld32(area+4)):nullptr;return d&&ld32(area)==Magic&&std::memcmp(mem::ptr(area+280),d->hash.data(),64)==0&&std::memcmp(mem::ptr(area+Name),d->archive.c_str(),d->archive.size()+1)==0;}
uint32_t link(){return ld32(GD(0x1046F0B0)+0x5B34);}
bool exact(uint32_t a){return valid()&&span(a,0x8260)&&a==link()&&a==ld32(area+12)&&ld32(a+4)&&ld32(a+4)!=0xFFFFFFFFu&&ld32(a+4)==ld32(area+16)&&ld8(a+0x326)==ld32(area+352)&&std::memcmp(mem::ptr(area+480),mem::ptr(GD(0x1047E6B8)),8)==0&&ld16(a+0xE)==0xA8;}
uint32_t free_space(Cpu*c,uint32_t heap){if(!span(heap,0x30))return 0;uint32_t vt=ld32(heap+0xC);if(!span(vt,0x78))return 0;uint32_t fn=ld32(vt+0x74);return fn?invoke(c,fn,{heap}):0;}
bool release(Cpu*c){if(!valid())return true;
 st32(area+Retiring,1);st32(area+248,0);
 // Packet+94 is its actual native DrawBuffer entry pointer. Native reset027F093C clears it.
 // Stopping submission and observing all links clear is required before the solid heap destructor.
 const uint32_t model=ld32(area+28);
 if(model){
  if(!span(model,0x144))return false;
  const uint32_t count=ld32(model+0x128),packets=ld32(model+0x12C);
  if(count>64||(count&&!packets)||(packets&&!span(packets,std::max(1u,count)*0xAC)))return false;
  for(uint32_t i=0;i<count;++i)if(ld32(packets+i*0xAC+0x94))return false;
 }
 // Native resDelete asserts on phase.id1. Drain the outstanding native load before retirement.
 if(ld32(area+Phase+4)==1){invoke(c,GC(0x02520460),{area+Phase,area+Name});if(ld32(area+Phase+4)==1)return false;}
 const uint32_t heap=ld32(area+24);st32(area+28,0);st32(area+24,0);
 if(trace())LOG("[item_model] release key=%u link=%08X pid=%08X heap=%08X",ld32(area+4),ld32(area+12),ld32(area+16),heap);
 if(heap&&span(heap,0x30))invoke(c,GC(0x025E3868),{heap});
 if(ld32(area+Phase+4)==2)invoke(c,GC(0x025204C8),{area+Phase,area+Name});
 std::memset(mem::ptr(area+Phase),0,140);st32(area+20,0);return true;
}
void create(Cpu*c){auto*d=definition(ld32(area+4));if(!d||ld32(area+32))return;
 uint32_t phase=invoke(c,GC(0x02520460),{area+Phase,area+Name});if(phase!=4){if(phase>=5)st32(area+32,1);return;}
 st32(area+Safe,area+Name);st32(area+Safe+4,GD(0x10012504));
 uint32_t data=invoke(c,GC(0x026066C4),{ld32(GD(0x101F4F28)),area+Safe,1});if(!data){st32(area+32,1);return;}
 // Native solid heap owns every model allocation. No mutation of Link's equip heap/fields.
 const uint32_t old=invoke(c,GC(0x027EC230),{}),savedHeap=ld32(GD(0x101F4754)),savedAlign=ld32(GD(0x101F4758));
 const uint32_t parent=invoke(c,GC(0x025E2FBC),{});if(free_space(c,parent)<0x140000){st32(area+32,1);return;}uint32_t heap=invoke(c,GC(0x025E33A8),{0x40000,parent,32});uint32_t model=0;
 if(heap){invoke(c,GC(0x027EC220),{heap});try{model=invoke(c,GC(0x025E38E0),{data,0x80000});}catch(...){invoke(c,GC(0x027EC220),{old});invoke(c,GC(0x025E3868),{heap});st32(GD(0x101F4754),savedHeap);st32(GD(0x101F4758),savedAlign);throw;}invoke(c,GC(0x027EC220),{old});}
 st32(GD(0x101F4754),savedHeap);st32(GD(0x101F4758),savedAlign);
 if(!model){if(heap)invoke(c,GC(0x025E3868),{heap});st32(area+32,1);return;}st32(area+24,heap);st32(area+28,model);if(trace())LOG("[item_model] ready key=%u link=%08X pid=%08X heap=%08X model=%08X",ld32(area+4),ld32(area+12),ld32(area+16),heap,model);
}
}
bool declare(uint16_t key,const std::string&archive,unsigned frames,std::string&why,const std::string&asset_digest){if(installed||!key||archive.empty()||archive.size()>95||frames<1||frames>300||declarations.size()>=8)return why="invalid held model or more than eight item models",false;for(unsigned char c:archive)if(!(std::isalnum(c)||c=='_'||c=='-'))return why="invalid prepared archive identity",false;if(definition(key))return why="duplicate item model key",false;declarations.push_back({key,archive,frames,fingerprint(key,archive+":"+asset_digest,frames)});return true;}
bool declared(){return !declarations.empty();}
size_t checkpoint(){return declarations.size();}
void rollback(size_t n) noexcept{if(!installed&&n<=declarations.size())declarations.resize(n);}
void configure(uint32_t memory,void(*o)(Cpu*,uint32_t),Invoke i){area=memory;original=o;invoke=i;if(trace())LOG("[item_model] scratch=%08X bytes=%u",area,kMemoryBytes);}
bool install(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){if(installed)return true;if(declarations.empty())return true;if(!area||area<mem::kRuntimeStart||area>mem::kHostStart-kMemoryBytes||!original||!invoke||!owner(GC(0x02403C24)).empty()||!owner(GC(0x023DBBE4)).empty())return false;const uint32_t res[]={GC(0x02604550),GC(0x02604EB4),GC(0x02612E64)};unsigned empty=0;for(auto fn:res){const auto who=owner(fn);if(who.empty())++empty;else if(who!="the port's world actors")return false;}if(empty!=0&&empty!=3)return false;std::memset(mem::ptr(area),0,kMemoryBytes);
 if(empty==3){replace(res[0],table_hook);replace(res[1],archive_hook);replace(res[2],backing_hook);}
 // the demo item's draw (daDitem_Draw) is story_item_get_icon's hook, which asks draw_demo_item() first
 replace(GC(0x02403C24),hooks::hand);replace(GC(0x023DBBE4),hooks::draw);installed=true;return true;}
bool owns_archive(const std::string& name){return std::any_of(declarations.begin(),declarations.end(),[&](const Decl&d){return d.archive==name;});}
namespace {
std::string archive_arg(Cpu*c){uint32_t key=c->r[4];if(!span(key,4))return {};uint32_t p=ld32(key);if(!span(p,96))return {};const char* s=reinterpret_cast<const char*>(mem::ptr(p));const void* end=std::memchr(s,0,96);return end?std::string(s,static_cast<const char*>(end)):std::string();}
void table_hook(Cpu*c){if(!resource_table(c))original(c,GC(0x02604550));}
void archive_hook(Cpu*c){if(!archive_wrapper(c))original(c,GC(0x02604EB4));}
void backing_hook(Cpu*c){if(!backing_archive_wrapper(c))original(c,GC(0x02612E64));}
}
bool resource_table(Cpu*c){const auto name=archive_arg(c);if(!owns_archive(name))return false;Keep keep(c);const uint32_t info=c->r[3],heap=c->r[5];if(!span(info,0x70)||!span(heap,0x30))return true;
 const auto filename=name+".bmd";std::memcpy(mem::ptr(area+360),filename.c_str(),filename.size()+1);st32(area+464,area+360);st32(area+468,GD(0x10012504));
 invoke(c,GC(0x02604470),{info,2,heap});invoke(c,GC(0x026044E0),{info,2,heap});invoke(c,GC(0x02608308),{info,1,1,area+464,heap});return true;
}
bool archive_wrapper(Cpu*c){if(!owns_archive(archive_arg(c)))return false;original(c,GC(0x02604EB4));return true;}
bool backing_archive_wrapper(Cpu*c){if(!owns_archive(archive_arg(c)))return false;const Cpu in=*c;uint32_t heap=c->r[6];if(!span(heap,0x30)){c->r[3]=0;return true;}const bool cached=invoke(c,GC(0x02612504),{in.r[3],in.r[4]})!=0;uint32_t free=cached?0xFFFFFFFFu:free_space(c,heap);*c=in;if(free<0x300000){c->r[3]=0;return true;}original(c,GC(0x02612E64));return true;}
void request(uint16_t key){if(installed&&definition(key)){st32(area+8,key);if(trace())LOG("[item_model] request key=%u",key);}}
namespace {
// the get presentation: the model on the demo item (area+36), until end_get
void request_get(uint16_t key){if(installed&&definition(key)){st32(area+8,key|GetRequest);if(trace())LOG("[item_model] hold-up request key=%u",key);}}
// (without a declared held model nothing is installed and `area` is 0: nothing to end; #269)
void end_get(uint16_t key){if(!installed||!area)return;if(ld32(area+8)==(key|GetRequest))st32(area+8,0);if(valid()&&ld32(area+36)&&ld32(area+4)==key)st32(area+20,0);}
const uint32_t kPresentItem=GC(0x025D7DEC),kSearchById=GC(0x025D54C4),kOrderItemEvent=GC(0x025D7B98),kEndCheck=GC(0x0254457C),kDeleteActor=GC(0x025D57E0);
uint32_t play(){return GD(0x1046F0B0);}  // g_dComIfG_gameInfo.play
constexpr uint32_t PlayEventMode=0x5292,PlayItemPartner=0x52A0,PlayEventFlags=0x52B8,PlayEventManager=0x52C4;
constexpr uint32_t ActorPos=0x314,ActorRoom=0x326,EventCommand=0xF8,EventCondition=0xFA,CommandItem=4,CanGetItem=8;
constexpr uint32_t DemoItemModel=0x3B4,DemoItemNo=0x74E,DemoItemBytes=0x770;
// the demo item of the record, 0 when it is gone
uint32_t demo_item(Cpu*c){const uint32_t id=ld32(area+Demo+8);if(id==0xFFFFFFFFu)return 0;st32(area+Demo+20,0);invoke(c,kSearchById,{id,area+Demo+20});const uint32_t it=ld32(area+Demo+20);return span(it,DemoItemBytes)&&ld32(it+4)==id?it:0;}
void demo_end(Cpu*c,bool reset_event,const char* how){
 const uint16_t key=uint16_t(ld32(area+Demo+16));
 if(reset_event)st16(play()+PlayEventFlags,uint16_t(ld16(play()+PlayEventFlags)|8));  // dComIfGp_event_reset
 if(const uint32_t it=demo_item(c))invoke(c,kDeleteActor,{it});  // argument flag 1: its Delete gives nothing
 end_get(key);if(arm)arm(key,false);std::memset(mem::ptr(area+Demo),0,kMemoryBytes-Demo);
 LOG("[item_model] item get of key %u: %s",key,how);
}
}
bool hold_up(uint16_t key){if(!installed||!definition(key))return false;std::lock_guard lock(queue_mu);queue.push_back(key);return true;}
void chest_hold_up(uint16_t key,bool on){if(on)request_get(key);else end_get(key);}
void set_text(bool(*a)(uint16_t,bool)){arm=a;}
bool demo_active(){std::lock_guard lock(queue_mu);return !queue.empty()||(area&&ld32(area+Demo)==DemoMagic);}
static void demo_tick(Cpu*c){
 const uint32_t d=area+Demo;
 if(ld32(d)!=DemoMagic){
  uint16_t key=0;{std::lock_guard lock(queue_mu);if(queue.empty())return;key=queue.front();}
  // a free moment: Link in play, no event (a talk, a scene, another get) running
  const uint32_t a=link();if(!span(a,0x8260)||ld16(a+0xE)!=0xA8||ld8(play()+PlayEventMode)!=0)return;
  if(arm&&!arm(key,true))return;  // the get message can't be replaced now (the notebook's): later
  const uint32_t id=invoke(c,kPresentItem,{a+ActorPos,story_treasure_chest::kStandIn,1,0xFFFFFFFFu,uint32_t(int32_t(int8_t(ld8(a+ActorRoom)))),0,0});
  {std::lock_guard lock(queue_mu);if(!queue.empty())queue.pop_front();}
  if(id==0xFFFFFFFFu){if(arm)arm(key,false);LOG("[item_model] item get of key %u: no demo item",key);return;}
  std::memset(mem::ptr(d),0,kMemoryBytes-Demo);st32(d,DemoMagic);st32(d+4,1);st32(d+8,id);st32(d+16,key);
  request_get(key);LOG("[item_model] item get of key %u: Link holds it up (demo item %08X)",key,id);return;
 }
 if(rearm){rearm=false;if(arm)arm(uint16_t(ld32(d+16)),true);}  // a full state brought the demo back; its text with it
 const uint32_t phase=ld32(d+4),ticks=ld32(d+12)+1;st32(d+12,ticks);
 if(phase==1){
  const uint32_t it=demo_item(c);
  if(!it||!span(ld32(it+DemoItemModel),0x144)){if(ticks>300)demo_end(c,false,"the demo item never came");return;}
  if(ld16(it+EventCommand)==CommandItem){st32(play()+PlayItemPartner,ld32(d+8));st32(d+4,2);st32(d+12,0);  // dComIfGp_event_setItemPartnerId
   LOG("[item_model] item get of key %u: the item event started after %u ticks",ld32(d+16),ticks);return;}
  if(ticks>900)return demo_end(c,false,"no free moment for the item event");
  invoke(c,kOrderItemEvent,{it,0xFFFF});st16(it+EventCondition,uint16_t(ld16(it+EventCondition)|CanGetItem));return;
 }
 const uint32_t over=invoke(c,kEndCheck,{play()+PlayEventManager,GD(0x10011EB8)});  // "DEFAULT_GETITEM"
 if(trace()&&ticks%30==0)LOG("[item_model] item get running: tick %u end %u mode %u item %08X",ticks,over,ld8(play()+PlayEventMode),demo_item(c));
 if(over)return demo_end(c,true,"shown");
 if(ticks>3600)demo_end(c,true,"the item event did not end");
}
static void demo_tick(Cpu*c);
void tick(Cpu*c){if(!installed||!c)return;Keep keep(c);if(progress_cancel){if(valid()&&!release(c))return;progress_cancel=false;std::memset(mem::ptr(area),0,kMemoryBytes);{std::lock_guard lock(queue_mu);queue.clear();}return;}demo_tick(c);if(ld32(area+8)){const uint32_t req=ld32(area+8),key=req&0xFFFF;const bool get=(req&GetRequest)!=0;if(valid()&&!release(c))return;auto*d=definition(key);std::memset(mem::ptr(area),0,ModelBytes);if(d){uint32_t a=link();if(span(a,0x8260)&&ld16(a+0xE)==0xA8){st32(area,Magic);st32(area+4,key);st32(area+12,a);st32(area+16,ld32(a+4));st32(area+352,ld8(a+0x326));std::memcpy(mem::ptr(area+480),mem::ptr(GD(0x1047E6B8)),8);st32(area+20,get?GetFrames:d->frames);st32(area+36,get?1:0);std::memcpy(mem::ptr(area+280),d->hash.data(),64);std::memcpy(mem::ptr(area+Name),d->archive.c_str(),d->archive.size()+1);}}}
 if(!ld32(area))return;if(!valid()){st32(area,0);return;}if(!exact(ld32(area+12))){if(release(c))st32(area,0);return;}uint32_t n=ld32(area+20);if(!n){if(release(c))st32(area,0);return;}st32(area+20,n-1);if(!ld32(area+28))create(c);
}
void progress_loaded(){progress_cancel=true;}
void state_loaded(){progress_cancel=false;/* Guest area, native heap and phase restore together. No host pointer or replay. */
 {std::lock_guard lock(queue_mu);queue.clear();}rearm=area&&ld32(area+Demo)==DemoMagic;}
void reset(){declarations.clear();area=0;original=nullptr;invoke=nullptr;installed=false;progress_cancel=false;{std::lock_guard lock(queue_mu);queue.clear();}arm=nullptr;rearm=false;}
namespace hooks {
void hand(Cpu*c){uint32_t a=c->r[3];original(c,GC(0x02403C24));Keep keep(c);if(!exact(a)||ld32(area+Retiring)||ld32(area+36)||!span(ld32(area+28),0x144))return;st32(area+248,0);uint32_t m=ld32(a+0x448);if(!span(m,0x30))return;uint32_t b=ld32(m+0x2C);if(!span(b,0x14))return;uint32_t mat=ld32(b+0x10)+8*0x30;if(!span(mat,0x30))return;for(unsigned i=0;i<12;++i)if(!std::isfinite(u32_as_f32(ld32(mat+i*4))))return;std::memcpy(mem::ptr(area+Matrix),mem::ptr(mat),0x30);std::memcpy(mem::ptr(ld32(area+28)+0xC8),mem::ptr(mat),0x30);st32(area+248,1);}
void draw(Cpu*c){uint32_t a=c->r[3];original(c,GC(0x023DBBE4));Keep keep(c);if(!exact(a)||ld32(area+Retiring)||ld32(area+36)||!ld32(area+248)||!span(ld32(area+28),0x144))return;if(invoke(c,GC(0x02444E58),{a}))return;uint32_t m=ld32(area+28);uint32_t env=invoke(c,GC(0x02555D0C),{});invoke(c,GC(0x025626A4),{env,0,a+0x314,area+512});invoke(c,GC(0x02562F5C),{env,m,area+512});invoke(c,GC(0x025E2DE0),{m,0});}
}  // namespace hooks
// daDitem_Draw(item) (story_item_get_icon's hook asks first): the stand-in's demo item while a held model is shown in its place: the model with the
// demo item's matrix (set_mtx: over Link's head, turning to the camera), lit at its place. Until the model
// is ready nothing is drawn (never the stand-in's own card). Any other demo item: false (the caller draws it).
bool draw_demo_item(Cpu*c){
 const uint32_t it=c->r[3];
 if(!installed||!valid()||!ld32(area+36)||!span(it,DemoItemBytes)||ld8(it+DemoItemNo)!=story_treasure_chest::kStandIn)return false;
 {Keep keep(c);const uint32_t m=ld32(area+28),own=ld32(it+DemoItemModel);
  bool finite=span(own,0x144);for(unsigned i=0;finite&&i<12;++i)finite=std::isfinite(u32_as_f32(ld32(own+0xC8+i*4)));
  if(!ld32(area+Retiring)&&span(m,0x144)&&finite){
   std::memcpy(mem::ptr(m+0xC8),mem::ptr(own+0xC8),0x30);
   const uint32_t env=invoke(c,GC(0x02555D0C),{});invoke(c,GC(0x025626A4),{env,0,it+ActorPos,area+512});invoke(c,GC(0x02562F5C),{env,m,area+512});invoke(c,GC(0x025E2DE0),{m,0});
  }}
 c->r[3]=1;return true;
}
}
