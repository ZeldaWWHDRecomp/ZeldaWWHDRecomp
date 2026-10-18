// SPDX-License-Identifier: MPL-2.0
#include "story_world_grapple.h"
#include "guest_addr.h"
#include "runtime.h"
#include <cmath>
#include <cstring>
#include <array>
#include <cstdlib>
namespace story_world_grapple { namespace {
using story_actors::identity::Fingerprint;
constexpr uint32_t Magic=0x57475250,Version=1,Rows=0x100,Stride=0x70,Count=128;
const uint32_t Candidate=GC(0x0216DD3C),Move=GC(0x0216F7FC),Event=GC(0x025B8B68);
uint32_t arena=0;bool installed=false;OwnerAlive alive;
void(*original)(Cpu*,uint32_t)=nullptr;
uint32_t(*invoke)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr;
thread_local uint32_t scope_rope=0,scope_target=0,pending=0;
bool tracing(){const char*v=std::getenv("WWHD_WORLD_GRAPPLE_TRACE");return v&&std::strcmp(v,"1")==0;}
bool address(uint32_t a,uint32_t n){return !(a&3)&&a>=0x10000000u&&a<=0x50000000u-n;}
bool storage(){return arena&&ld32(arena+0x50)==Magic&&ld32(arena+0x54)==Version;}
uint32_t row(unsigned i){return arena+Rows+i*Stride;}
Fingerprint fingerprint(uint32_t r){Fingerprint f{};std::memcpy(f.data(),mem::ptr(r+0x30),f.size());return f;}
bool row_identity(uint32_t r){return ld32(r+0x28)==story_actors::identity::kMagic&&ld32(r+0x2C)==story_actors::identity::kVersion;}
bool owner_alive(uint32_t r){try{return row_identity(r)&&alive&&alive(ld32(r+0xC),ld32(r+0x10),fingerprint(r));}catch(...){return false;}}
uint32_t call(Cpu*c,uint32_t fn,std::initializer_list<uint32_t> args){Cpu p=*c;return invoke(&p,fn,args);}
// A retiring proxy is still ours after its parent's declaration has disappeared.
uint32_t claimed(uint32_t a){if(!storage()||!address(a,kProxyBytes)||ld16(a+0xE)!=kProfile)return 0;
 for(unsigned i=0;i<Count;++i){uint32_t r=row(i);if(ld32(r)&&ld32(r+8)==a&&ld32(r+4)==ld32(a+4))return r;}return 0;}
bool proxy(uint32_t a){return address(a,kProxyBytes)&&ld16(a+0xE)==kProfile;}
bool complete(uint32_t a,uint32_t r){return address(a,kProxyBytes)&&r&&claimed(a)==r&&ld32(r)==2&&ld32(a+0xB0)==0x00FF0000&&ld32(a+0x3AC)==Magic&&ld32(a+0x3B0)==ld32(a+4)&&ld32(a+0x3B4)==(r-arena-Rows)/Stride&&ld32(a+0x3B8)==ld32(r+0xC)&&ld32(a+0x3BC)==ld32(r+0x10)&&row_identity(r)&&std::memcmp(mem::ptr(a+0x3C4),mem::ptr(r+0x28),40)==0&&owner_alive(r);}
void trace_invalid(uint32_t a,uint32_t r){if(!tracing()||!proxy(a)||!r)return;LOG("[world_grapple] invalid proxy=%08X pid=%u row=%08X phase=%u params=%08X construct=%u magic=%08X saved_pid=%u parent=%08X saved_parent=%08X parent_pid=%u saved_parent_pid=%u identity=%u owner_alive=%u",a,ld32(a+4),r,ld32(r),ld32(a+0xB0),unsigned(ld8(a+0x3AA)),ld32(a+0x3AC),ld32(a+0x3B0),ld32(r+0xC),ld32(a+0x3B8),ld32(r+0x10),ld32(a+0x3BC),unsigned(row_identity(r)&&std::memcmp(mem::ptr(a+0x3C4),mem::ptr(r+0x28),40)==0),unsigned(owner_alive(r)));}
bool pose(const Target&t){return std::isfinite(t.position.x)&&std::isfinite(t.position.y)&&std::isfinite(t.position.z)&&std::abs(t.position.x)<=350000&&std::abs(t.position.y)<=350000&&std::abs(t.position.z)<=350000&&std::isfinite(t.yaw)&&std::abs(t.yaw)<=360;}
void apply(uint32_t a,uint32_t r){for(unsigned i=0;i<3;++i){const uint32_t value=ld32(r+0x18+i*4);for(uint32_t off:{0x2ECu,0x300u,0x314u,0x37Cu,0x390u})st32(a+off+i*4,value);}const int16_t yaw=int16_t(int32_t(std::remainder(ldf32(r+0x24),360.f)*(65536.f/360.f)));st16(a+0x2FA,uint16_t(yaw));st16(a+0x322,uint16_t(yaw));st16(a+0x32A,uint16_t(yaw));}
uint32_t search(Cpu*c,uint32_t pid){st32(arena+0xB0,0);call(c,GC(0x025D54C4),{pid,arena+0xB0});return ld32(arena+0xB0);}
bool attached(Cpu*c,uint32_t r){const uint32_t rope=ld32(r+0x50);if(!rope)return false;const uint32_t found=search(c,ld32(r+0x54));if(found!=rope||!address(rope,0x2B10)){st32(r+0x50,0);return false;}if(ld32(rope+0x267C)!=ld32(r+8)){st32(r+0x50,0);return false;}return true;}
bool proxy_attached(Cpu*c,uint32_t a){if(!proxy(a))return false;const uint32_t rope=ld32(a+0x3F0);return address(rope,0x2B10)&&search(c,ld32(a+0x3F4))==rope&&ld32(rope+0x267C)==a;}
void remember(uint32_t a,uint32_t r,uint32_t rope){st32(a+0x3F0,rope);st32(a+0x3F4,ld32(rope+4));if(r){st32(r+0x50,rope);st32(r+0x54,ld32(rope+4));}}
// Shadow ownership survives a lost arena row, but cannot authorize a recycled PID.
bool shadow_owned(uint32_t a){return proxy(a)&&ld32(a+0x3AC)==Magic&&ld32(a+0x3B0)==ld32(a+4);}
void orphan_retire(Cpu*c,uint32_t a){if(!shadow_owned(a))return;const bool held=proxy_attached(c,a);const uint32_t bit=held?1u:2u;if(ld32(a+0x3F8)&bit)return;st32(a+0x3F8,ld32(a+0x3F8)|bit);const uint32_t victim=held?ld32(a+0x3F0):a;if(tracing())LOG("[world_grapple] orphan_retire proxy=%08X pid=%u attached=%u delete=%08X",a,ld32(a+4),unsigned(held),victim);call(c,GC(0x025D57E0),{victim});}
void trace_move(uint32_t rope,uint32_t a,uint32_t r){if(!tracing())return;const uint32_t state=ld32(rope+0x3F8),selected=ld32(rope+0x267C);if(r){const uint32_t ticks=ld32(r+0x64)+1;st32(r+0x64,ticks);if(ld32(r+0x6C)&&ld32(r+0x60)==state&&ld32(r+0x68)==selected&&ticks%30)return;st32(r+0x60,state);st32(r+0x68,selected);st32(r+0x6C,1);}LOG("[world_grapple] move rope=%08X pid=%u state=%u target=%08X proxy=%08X demo=%d demo_raw=%02X command=%04X",rope,ld32(rope+4),state,selected,a,int(int8_t(ld8(rope+0x2B09))),unsigned(ld8(rope+0x2B09)),unsigned(ld16(rope+0xF8)));}

// Native REQUEST is s8(-1), not unsigned255; ordinary posts coil in state2.
bool ordinary_demo(uint32_t rope){const int8_t demo=int8_t(ld8(rope+0x2B09));return demo==-1||demo==0||demo==2;}
void request_detach(Cpu*c,uint32_t r){if(!attached(c,r))return;const uint32_t rope=ld32(r+0x50);
 // A malformed/demo rope cannot consume the ordinary force-end request. Retire
 // the rope through its own native process, keeping the target alive meanwhile.
 if((ld32(rope+0x3F8)==0||ld32(rope+0x3F8)>11)||!ordinary_demo(rope)||ld16(rope+0xF8)==2||ld32(ld32(r+8)+0xB0)!=0x00FF0000){if(!ld32(r+0x5C)){st32(r+0x5C,1);call(c,GC(0x025D57E0),{rope});}return;}
 const uint32_t play=call(c,GC(0x025200D4),{});if(address(play,0x5B38)){const uint32_t link=ld32(play+0x5B34);if(address(link,0x3C4))st32(link+0x3C0,ld32(link+0x3C0)|0x40000000u);}}
void retire(Cpu*c,uint32_t r){const uint32_t a=ld32(r+8);if(!a||claimed(a)!=r){std::memset(mem::ptr(r),0,Stride);return;}if(ld32(r)!=3&&tracing())LOG("[world_grapple] retire proxy=%08X pid=%u parent=%08X parent_pid=%u",a,ld32(a+4),ld32(r+0xC),ld32(r+0x10));st32(r,3);request_detach(c,r);if(!attached(c,r)&&!ld32(r+0x58)){st32(r+0x58,1);call(c,GC(0x025D57E0),{a});}}
void base_allocated(uint32_t,uint32_t a,uint32_t){if(!pending||!address(a,kProxyBytes))return;st32(pending+4,ld32(a+4));st32(pending+8,a);}
void create(Cpu*c){const uint32_t a=c->r[3];uint32_t r=claimed(a);if(!r&&storage()&&address(a,kProxyBytes))for(unsigned i=0;i<Count;++i){const uint32_t p=row(i);if(ld32(p)==1&&ld32(p+4)==ld32(a+4)){r=p;st32(r+8,a);break;}}
 if(!r||ld32(r)!=1||!owner_alive(r)||ld32(a+0xB0)!=0x00FF0000){if(r)std::memset(mem::ptr(r),0,Stride);c->r[3]=5;return;}call(c,GC(0x025D4ED0),{a});std::memset(mem::ptr(a+0x3AC),0,kProxyBytes-0x3AC);st32(a+0x3AC,Magic);st32(a+0x3B0,ld32(a+4));st32(a+0x3B4,(r-arena-Rows)/Stride);st32(a+0x3B8,ld32(r+0xC));st32(a+0x3BC,ld32(r+0x10));std::memcpy(mem::ptr(a+0x3C4),mem::ptr(r+0x28),40);for(unsigned i=0;i<3;++i)stf32(a+0x330+i*4,1);apply(a,r);st8(a+0x3A1,0);st32(r,2);if(tracing())LOG("[world_grapple] create proxy=%08X pid=%u parent=%08X parent_pid=%u point=%u pos=%.3f,%.3f,%.3f yaw=%.3f",a,ld32(a+4),ld32(r+0xC),ld32(r+0x10),ld32(r+0x14),double(ldf32(r+0x18)),double(ldf32(r+0x1C)),double(ldf32(r+0x20)),double(ldf32(r+0x24)));c->r[3]=4;}
void execute(Cpu*c){uint32_t a=c->r[3],r=claimed(a);if(r){if(complete(a,r))apply(a,r);else{trace_invalid(a,r);retire(c,r);}}else if(proxy(a))orphan_retire(c,a);c->r[3]=1;}
void is_delete(Cpu*c){const uint32_t a=c->r[3],r=claimed(a);if((r&&attached(c,r))||proxy_attached(c,a)){if(r)request_detach(c,r);else orphan_retire(c,a);c->r[3]=0;}else c->r[3]=1;}
void remove(Cpu*c){const uint32_t a=c->r[3],r=claimed(a);if((r&&attached(c,r))||proxy_attached(c,a)){if(r){st32(r,3);request_detach(c,r);}else orphan_retire(c,a);c->r[3]=0;return;}if(shadow_owned(a)&&tracing())LOG("[world_grapple] delete proxy=%08X pid=%u",a,ld32(a+4));if(r)std::memset(mem::ptr(r),0,Stride);c->r[3]=1;}
void draw(Cpu*c){c->r[3]=1;}
void candidate(Cpu*c){const uint32_t a=c->r[3],rope=c->r[4];if(!address(a,0x10)||ld16(a+0xE)!=kProfile){original(c,Candidate);return;}const uint32_t r=claimed(a);if(complete(a,r)&&address(rope,0x2B10)){const unsigned n=ld8(rope+0x281C);if(n<100){st32(rope+0x268C+4*n,a);st8(rope+0x281C,uint8_t(n+1));remember(a,r,rope);}}else if(r){trace_invalid(a,r);retire(c,r);}c->r[3]=0;}
void event(Cpu*c){if(scope_rope&&(proxy(scope_target)||(address(scope_rope,0x2B10)&&proxy(ld32(scope_rope+0x267C))))){if(tracing())LOG("[world_grapple] suppress_event flag=%04X rope=%08X target=%08X",c->r[4]&0xFFFF,scope_rope,ld32(scope_rope+0x267C));return;}original(c,Event);}
void move(Cpu*c){const uint32_t rope=c->r[3];if(!address(rope,0x2B10)){original(c,Move);return;}const uint32_t a=ld32(rope+0x267C),r=claimed(a);const uint32_t previous_rope=scope_rope,previous_target=scope_target;scope_rope=rope;scope_target=proxy(a)?a:0;
 if(proxy(a))trace_move(rope,a,r);
 if(proxy(a)&&!r){remember(a,0,rope);orphan_retire(c,a);scope_rope=previous_rope;scope_target=previous_target;return;}
 if(r){remember(a,r,rope);if(!complete(a,r)){trace_invalid(a,r);st32(r,3);request_detach(c,r);}
 // Do not enter retail special/demo branches with a corrupt owned target.
 if(ld32(a+0xB0)!=0x00FF0000||!ordinary_demo(rope)||ld32(rope+0x3F8)>11){if(tracing())LOG("[world_grapple] guard_retire proxy=%08X pid=%u rope=%08X params=%08X state=%u demo=%d demo_raw=%02X command=%04X",a,ld32(a+4),rope,ld32(a+0xB0),ld32(rope+0x3F8),int(int8_t(ld8(rope+0x2B09))),unsigned(ld8(rope+0x2B09)),unsigned(ld16(rope+0xF8)));st32(r,3);request_detach(c,r);scope_rope=previous_rope;scope_target=previous_target;return;}}
 original(c,Move);const uint32_t selected=ld32(rope+0x267C);if(proxy(selected)){const uint32_t selected_row=claimed(selected);remember(selected,selected_row,rope);trace_move(rope,selected,selected_row);if(!selected_row)orphan_retire(c,selected);}if(r&&ld32(r)==3)retire(c,r);scope_rope=previous_rope;scope_target=previous_target;}
}
bool configure(void(*o)(Cpu*,uint32_t),uint32_t(*i)(Cpu*,uint32_t,std::initializer_list<uint32_t>),uint32_t a,OwnerAlive v){if(installed)return arena==a&&original==o&&invoke==i;if(!o||!i||!a||a<mem::kRuntimeStart||a>mem::kHostStart-kStorageBytes||(a&15)||!v)return false;original=o;invoke=i;arena=a;alive=std::move(v);return true;}
bool available(const native_actor_profiles::Owner&o){if(installed)return true;if(!arena||!original||!invoke||!alive||!native_actor_profiles::valid_storage(arena))return false;for(uint32_t f:{Candidate,Move,Event})if(!o(f).empty())return false;const uint32_t p=ld32(GD(0x101F3EE0)+0xFA*4);return address(p,0x30);}
bool install(const native_actor_profiles::Owner&o,const native_actor_profiles::Replace&replace){if(installed)return true;if(!available(o))return false;const uint32_t p=ld32(GD(0x101F3EE0)+0xFA*4);std::memset(mem::ptr(arena),0,kStorageBytes);std::memcpy(mem::ptr(arena),mem::ptr(p),0x30);st16(arena+8,kProfile);st32(arena+0x10,kProxyBytes);st32(arena+0x14,0);st32(arena+0x18,0);st32(arena+0x24,arena+0x30);void(*methods[])(Cpu*)={create,remove,execute,is_delete,draw};for(unsigned n=0;n<5;++n)st32(arena+0x30+n*4,dispatch::register_host(methods[n],"world_grapple_proxy"));if(!native_actor_profiles::add(kProfile,arena,base_allocated))return false;st32(arena+0x50,Magic);st32(arena+0x54,Version);replace(Candidate,candidate);replace(Move,move);replace(Event,event);installed=true;return true;}
bool update(Cpu*c,uint32_t parent,uint32_t pid,const Fingerprint&f,std::span<const Target> targets){if(!installed||!storage()||!c||!address(parent,0x3AC)||ld32(parent+4)!=pid||targets.size()>8)return false;for(const auto&t:targets)if(!pose(t))return false;try{if(!alive(parent,pid,f))return false;}catch(...){return false;}
 // SearchByID returns true with a null output while native creation is pending.
 // Reclaim only proven absent PIDs, including failed creation before class Create.
 for(unsigned i=0;i<Count;++i){const uint32_t r=row(i);if(ld32(r)&&ld32(r+4)!=0xFFFFFFFFu){st32(arena+0xB0,0);const uint32_t found=call(c,GC(0x025D54C4),{ld32(r+4),arena+0xB0});if(!found){if(tracing())LOG("[world_grapple] reclaim row=%08X phase=%u proxy=%08X pid=%u search_result=%u search_actor=%08X",r,ld32(r),ld32(r+8),ld32(r+4),found,ld32(arena+0xB0));std::memset(mem::ptr(r),0,Stride);}}}
 // Reject partial admission: enough rows must exist before creating any process.
 unsigned missing=0,free=0;for(unsigned i=0;i<Count;++i)if(!ld32(row(i)))++free;for(unsigned n=0;n<targets.size();++n){bool found=false;for(unsigned i=0;i<Count;++i){uint32_t r=row(i);if(ld32(r)&&ld32(r)!=3&&ld32(r+0xC)==parent&&ld32(r+0x10)==pid&&ld32(r+0x14)==n&&story_actors::identity::matches(mem::ptr(r+0x28),f)){found=true;break;}}if(!found)++missing;}if(missing>free)return false;
 for(unsigned i=0;i<Count;++i){uint32_t r=row(i);if(ld32(r)&&ld32(r+0xC)==parent&&ld32(r+0x10)==pid&&(!story_actors::identity::matches(mem::ptr(r+0x28),f)||ld32(r+0x14)>=targets.size()))retire(c,r);}
 for(unsigned n=0;n<targets.size();++n){uint32_t r=0;for(unsigned i=0;i<Count;++i){uint32_t p=row(i);if(ld32(p)&&ld32(p)!=3&&ld32(p+0xC)==parent&&ld32(p+0x10)==pid&&ld32(p+0x14)==n&&story_actors::identity::matches(mem::ptr(p+0x28),f)){r=p;break;}}bool fresh=!r;if(fresh){for(unsigned i=0;i<Count;++i)if(!ld32(row(i))){r=row(i);break;}std::memset(mem::ptr(r),0,Stride);st32(r,1);st32(r+4,0xFFFFFFFFu);st32(r+0xC,parent);st32(r+0x10,pid);st32(r+0x14,n);auto record=story_actors::identity::record(f);std::memcpy(mem::ptr(r+0x28),record.data(),record.size());}
 const auto&t=targets[n];stf32(r+0x18,t.position.x);stf32(r+0x1C,t.position.y);stf32(r+0x20,t.position.z);stf32(r+0x24,t.yaw);
 if(fresh){for(unsigned j=0;j<3;++j){st32(arena+0x80+j*4,ld32(r+0x18+j*4));stf32(arena+0x94+j*4,1);}st16(arena+0x8C,0);st16(arena+0x8E,uint16_t(int16_t(int32_t(std::remainder(t.yaw,360.f)*(65536.f/360.f)))));st16(arena+0x90,0);uint32_t layer=call(c,GC(0x025DED64),{});call(c,GC(0x025DEAB4),{ld32(parent+0x2C)});pending=r;uint32_t made=call(c,GC(0x025D5834),{kProfile,0x00FF0000,arena+0x80,uint32_t(int32_t(int8_t(ld8(parent+0x326)))),arena+0x8C,arena+0x94,0xFFFFFFFFu,0});pending=0;call(c,GC(0x025DEAB4),{layer});if(made==0xFFFFFFFFu){std::memset(mem::ptr(r),0,Stride);return false;}if(!ld32(r))return false;st32(r+4,made);
 }else if(ld32(r)==2){const uint32_t a=ld32(r+8);if(complete(a,r))apply(a,r);else{trace_invalid(a,r);retire(c,r);return false;}}}
 return true;}
void destroy(Cpu*c,uint32_t parent,uint32_t pid){if(!storage()||!c)return;for(unsigned i=0;i<Count;++i){const uint32_t r=row(i);if(ld32(r)&&ld32(r+0xC)==parent&&ld32(r+0x10)==pid)retire(c,r);}}
void state_loaded(){scope_rope=scope_target=pending=0;}
}
