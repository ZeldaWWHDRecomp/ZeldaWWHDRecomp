// SPDX-License-Identifier: MPL-2.0
#include "story_world_visuals.h"
#include "story_chart.h"
#include "story_mail_book.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <vector>
namespace story_world_visuals { namespace {
using Fingerprint=story_actors::identity::Fingerprint;
constexpr uint32_t Magic=0x57564953,Version=1,Rows=0x100,Stride=64,None=0xFFFFFFFFu;
constexpr uint16_t Profile=0x7FFE;
struct Receipt {uint32_t token;Fingerprint identity;};
struct Entry {Spec spec;bool enabled=true;std::vector<Receipt> receipts;};
std::vector<Entry> entries;
Config config;
bool configured=false;
bool tracing(){const char*v=std::getenv("WWHD_WORLD_VISUAL_TRACE");return v&&std::strcmp(v,"1")==0;}
const char* role_name(Role r){return r==Role::Distant?"distant":"shoreline";}
void retiring(const Entry&e,uint32_t r,const char*reason){if(ld32(r)!=3&&tracing())LOG("[world_visual] retire mod=%s id=%s role=%s token=%u pid=%u room=%d reason=%s",e.spec.mod.c_str(),e.spec.id.c_str(),role_name(e.spec.role),ld32(r+0xC),ld32(r+4),int32_t(ld32(r+0x10)),reason);st32(r,3);}

bool address(uint32_t a,uint32_t n){return !(a&3)&&a>=mem::kMem2Start&&a<=mem::kMem2End-n;}
uint32_t row(size_t i){return config.arena+Rows+uint32_t(i)*Stride;}
bool storage(){return configured&&ld32(config.arena)==Magic&&ld32(config.arena+4)==Version;}
uint32_t call(Cpu*c,uint32_t fn,std::initializer_list<uint32_t> args){Cpu copy=*c;return config.invoke(&copy,fn,args);}
void empty(uint32_t r){std::memset(mem::ptr(r),0,Stride);st32(r+4,None);}
bool bound(const Entry&e,uint32_t r){for(const auto& b:e.receipts)if(ld32(r+0xC)==b.token&&story_actors::identity::matches(mem::ptr(r+0x14),b.identity))return true;return false;}
bool current(const Entry&e,uint32_t r){return ld32(r+0xC)==e.spec.token&&story_actors::identity::matches(mem::ptr(r+0x14),e.spec.identity);}
bool owned(const Entry&e,uint32_t r,uint32_t a){if(!bound(e,r)||!address(a,0x3AC)||ld32(a+4)!=ld32(r+4)||ld16(a+0xE)!=Profile||ld32(a+0xB0)!=ld32(r+0xC))return false;try{return config.owns&&config.owns(a,ld32(r+0xC));}catch(...){return false;}}
bool eligible(const Entry&e){try{return e.enabled&&config.eligible&&config.eligible(e.spec.mod,e.spec.id);}catch(...){return false;}}
bool draining(uint32_t scene){for(unsigned n=0;n<16;++n){if(!address(scene,0xC4)||ld8(scene+0x5C))return true;const uint32_t layer=ld32(scene+0x2C);if(!layer)return false;if(!address(layer,0x1C))return true;scene=ld32(layer+0x18);if(!scene)return false;}return true;}
bool wanted(const Entry&e,int room){if(!eligible(e))return false;const auto place=story_chart::square_place(e.spec.position.x,e.spec.position.z);const int home=1+(place.row+3)*7+place.column+3;if(home!=e.spec.home_room)return false;if(e.spec.role==Role::Shoreline)return room==home;const int c=(room-1)%7-3,r=(room-1)/7-3;return room!=home&&std::abs(c-place.column)<=1&&std::abs(r-place.row)<=1;}
bool valid(const Spec&s,const std::string&mod,std::string&why){
    if(s.mod!=mod||!story_mail::valid_id(s.id)||!s.token||s.token==None||s.home_room<1||s.home_room>49||(s.role!=Role::Distant&&s.role!=Role::Shoreline)){why="invalid visual key, token, role or sea room";return false;}
    const float p[]={s.position.x,s.position.y,s.position.z},r[]={s.rotation.x,s.rotation.y,s.rotation.z},scale[]={s.scale.x,s.scale.y,s.scale.z};
    for(unsigned i=0;i<3;++i)if(!std::isfinite(p[i])||std::abs(p[i])>350000||!std::isfinite(r[i])||std::abs(r[i])>360||!std::isfinite(scale[i])||scale[i]<=0||scale[i]>100){why="visual pose is outside supported bounds";return false;}
    if(std::abs(s.position.x)>=350000||std::abs(s.position.z)>=350000){why="visual is outside the sea";return false;}
    const auto square=story_chart::square_place(s.position.x,s.position.z);if(s.home_room!=1+(square.row+3)*7+square.column+3){why="visual position and home sea room disagree";return false;}return true;
}
}
bool configure(Config c){if(configured)return c.arena==config.arena&&c.invoke==config.invoke;if(!c.invoke||!c.eligible||!c.owns||c.arena<mem::kRuntimeStart||c.arena>mem::kHostStart-kStorageBytes||(c.arena&15))return false;config=std::move(c);std::memset(mem::ptr(config.arena),0,kStorageBytes);st32(config.arena,Magic);st32(config.arena+4,Version);for(unsigned i=0;i<kMaxKeys;++i)empty(row(i));configured=true;return true;}
bool replace(const std::string&mod,std::span<const Spec> specs,std::string&why,bool apply){
    why.clear();std::u16string checked;if(mod.empty()||mod.size()>128||!story_mail::append_utf16(mod,false,checked)||specs.size()>kMaxKeys){why="invalid visual mod or declaration count";return false;}
    for(size_t i=0;i<specs.size();++i){if(!valid(specs[i],mod,why))return false;for(size_t j=0;j<i;++j)if(specs[i].id==specs[j].id&&specs[i].role==specs[j].role){why="duplicate visual key";return false;}}
    auto next=entries;for(auto&e:next)if(e.spec.mod==mod)e.enabled=false;
    for(const auto&s:specs){auto it=std::find_if(next.begin(),next.end(),[&](const Entry&e){return e.spec.mod==mod&&e.spec.id==s.id&&e.spec.role==s.role;});
        if(it==next.end()){if(next.size()==kMaxKeys){why="visual historical key capacity reached; restart required";return false;}next.push_back({s,true,{}});it=std::prev(next.end());}
        for(const auto&e:next)if(&e!=&*it)for(const auto&b:e.receipts)if(b.token==s.token){why="visual token belongs to another key";return false;}
        if(std::none_of(it->receipts.begin(),it->receipts.end(),[&](const Receipt&b){return b.token==s.token&&b.identity==s.identity;})){if(it->receipts.size()==kMaxKeys){why="visual edit history capacity reached; restart required";return false;}it->receipts.push_back({s.token,s.identity});}
        it->spec=s;it->enabled=true;
    }
    if(apply)entries.swap(next);return true;
}
void service(Cpu*c,std::string_view stage,int room,int observer_square){
    if(!c||!storage())return;
    if(observer_square==-1)observer_square=room;
    const bool square_valid=observer_square>=1&&observer_square<=49;
    bool ready=stage=="sea"&&room>=0&&room<=49&&square_valid;uint32_t scene=0;
    if(ready){scene=call(c,GC(0x025DC80C),{ld32(GD(0x1047E8F0)+uint32_t(room)*0x22C)});ready=scene&&!draining(scene);}
    std::vector<bool> desired(entries.size());unsigned admitted=0,active=0;
    for(size_t i=0;i<entries.size();++i){desired[i]=ready&&wanted(entries[i],observer_square)&&admitted<kMaxActive;if(desired[i])++admitted;}
    // Resolve all old processes before admitting any new ones. TRUE/null is a
    // legitimate pending native creation; never clear or replace that PID early.
    for(size_t i=0;i<entries.size();++i){const auto&e=entries[i];const uint32_t r=row(i);if(!ld32(r))continue;st32(config.arena+0x40,0);const uint32_t found=call(c,GC(0x025D54C4),{ld32(r+4),config.arena+0x40}),a=ld32(config.arena+0x40);if(!found){if(tracing())LOG("[world_visual] reclaim mod=%s id=%s role=%s token=%u pid=%u room=%d",e.spec.mod.c_str(),e.spec.id.c_str(),role_name(e.spec.role),ld32(r+0xC),ld32(r+4),int32_t(ld32(r+0x10)));empty(r);continue;}++active;
        const bool keep=desired[i]&&current(e,r)&&int32_t(ld32(r+0x10))==room&&ld32(r)!=3;
        if(!a){if(!keep)retiring(e,r,"pending-not-desired");continue;}
        if(!owned(e,r,a)){retiring(e,r,"ownership-rejected");continue;}st32(r+8,a);
        if(keep){st32(r,2);continue;}
        retiring(e,r,"not-desired");if(!ld32(r+0x3C)){st32(r+0x3C,1);if(tracing())LOG("[world_visual] delete actor=%08X token=%u pid=%u room=%d",a,ld32(r+0xC),ld32(r+4),int32_t(ld32(r+0x10)));call(c,GC(0x025D57E0),{a});}
    }
    if(!ready)return;
    for(size_t i=0;i<entries.size()&&active<kMaxActive;++i){if(!desired[i])continue;const uint32_t r=row(i);if(ld32(r))continue;const auto&s=entries[i].spec;
        const float p[]={s.position.x,s.position.y,s.position.z},rot[]={s.rotation.x,s.rotation.y,s.rotation.z},scale[]={s.scale.x,s.scale.y,s.scale.z};
        for(unsigned j=0;j<3;++j){stf32(config.arena+0x50+j*4,p[j]);st16(config.arena+0x60+j*2,uint16_t(int16_t(int32_t(rot[j]*(32768.f/180.f)))));stf32(config.arena+0x70+j*4,scale[j]);}
        const uint32_t previous=call(c,GC(0x025DED64),{});uint32_t pid=None;
        try{call(c,GC(0x025DEAB4),{scene+0xC0});pid=call(c,GC(0x025D5834),{Profile,s.token,config.arena+0x50,uint32_t(room),config.arena+0x60,config.arena+0x70,None,0});}
        catch(...){call(c,GC(0x025DEAB4),{previous});throw;}
        call(c,GC(0x025DEAB4),{previous});if(pid==None)continue;
        empty(r);st32(r,1);st32(r+4,pid);st32(r+0xC,s.token);st32(r+0x10,uint32_t(room));const auto identity=story_actors::identity::record(s.identity);std::memcpy(mem::ptr(r+0x14),identity.data(),identity.size());++active;if(tracing())LOG("[world_visual] create mod=%s id=%s role=%s home=%d current=%d square=%d token=%u pid=%u",s.mod.c_str(),s.id.c_str(),role_name(s.role),s.home_room,room,observer_square,s.token,pid);
    }
}
void state_loaded(){/* No host PID registry: service revalidates the restored guest rows. */}
}
