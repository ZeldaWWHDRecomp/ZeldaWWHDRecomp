// SPDX-License-Identifier: MPL-2.0
#include "story_item_models.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
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
size_t checkpoint(){return declarations.size();}
void rollback(size_t n) noexcept{if(!installed&&n<=declarations.size())declarations.resize(n);}
void configure(uint32_t memory,void(*o)(Cpu*,uint32_t),Invoke i){area=memory;original=o;invoke=i;if(trace())LOG("[item_model] scratch=%08X bytes=%u",area,kMemoryBytes);}
bool install(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){if(installed)return true;if(declarations.empty())return true;if(!area||area<mem::kRuntimeStart||area>mem::kHostStart-kMemoryBytes||!original||!invoke||!owner(GC(0x02403C24)).empty()||!owner(GC(0x023DBBE4)).empty())return false;const uint32_t res[]={GC(0x02604550),GC(0x02604EB4),GC(0x02612E64)};unsigned empty=0;for(auto fn:res){const auto who=owner(fn);if(who.empty())++empty;else if(who!="the port's world actors")return false;}if(empty!=0&&empty!=3)return false;std::memset(mem::ptr(area),0,kMemoryBytes);
 if(empty==3){replace(res[0],table_hook);replace(res[1],archive_hook);replace(res[2],backing_hook);}
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
void tick(Cpu*c){if(!installed||!c)return;Keep keep(c);if(progress_cancel){if(valid()&&!release(c))return;progress_cancel=false;std::memset(mem::ptr(area),0,kMemoryBytes);return;}if(ld32(area+8)){uint32_t key=ld32(area+8);if(valid()&&!release(c))return;auto*d=definition(key);std::memset(mem::ptr(area),0,kMemoryBytes);if(d){uint32_t a=link();if(span(a,0x8260)&&ld16(a+0xE)==0xA8){st32(area,Magic);st32(area+4,key);st32(area+12,a);st32(area+16,ld32(a+4));st32(area+352,ld8(a+0x326));std::memcpy(mem::ptr(area+480),mem::ptr(GD(0x1047E6B8)),8);st32(area+20,d->frames);std::memcpy(mem::ptr(area+280),d->hash.data(),64);std::memcpy(mem::ptr(area+Name),d->archive.c_str(),d->archive.size()+1);}}}
 if(!ld32(area))return;if(!valid()){st32(area,0);return;}if(!exact(ld32(area+12))){if(release(c))st32(area,0);return;}uint32_t n=ld32(area+20);if(!n){if(release(c))st32(area,0);return;}st32(area+20,n-1);if(!ld32(area+28))create(c);
}
void progress_loaded(){progress_cancel=true;}
void state_loaded(){progress_cancel=false;/* Guest area, native heap and phase restore together. No host pointer or replay. */}
void reset(){declarations.clear();area=0;original=nullptr;invoke=nullptr;installed=false;progress_cancel=false;}
namespace hooks {
void hand(Cpu*c){uint32_t a=c->r[3];original(c,GC(0x02403C24));Keep keep(c);if(!exact(a)||ld32(area+Retiring)||!span(ld32(area+28),0x144))return;st32(area+248,0);uint32_t m=ld32(a+0x448);if(!span(m,0x30))return;uint32_t b=ld32(m+0x2C);if(!span(b,0x14))return;uint32_t mat=ld32(b+0x10)+8*0x30;if(!span(mat,0x30))return;for(unsigned i=0;i<12;++i)if(!std::isfinite(u32_as_f32(ld32(mat+i*4))))return;std::memcpy(mem::ptr(area+Matrix),mem::ptr(mat),0x30);std::memcpy(mem::ptr(ld32(area+28)+0xC8),mem::ptr(mat),0x30);st32(area+248,1);}
void draw(Cpu*c){uint32_t a=c->r[3];original(c,GC(0x023DBBE4));Keep keep(c);if(!exact(a)||ld32(area+Retiring)||!ld32(area+248)||!span(ld32(area+28),0x144))return;if(invoke(c,GC(0x02444E58),{a}))return;uint32_t m=ld32(area+28);uint32_t env=invoke(c,GC(0x02555D0C),{});invoke(c,GC(0x025626A4),{env,0,a+0x314,area+512});invoke(c,GC(0x02562F5C),{env,m,area+512});invoke(c,GC(0x025E2DE0),{m,0});}
}
}
