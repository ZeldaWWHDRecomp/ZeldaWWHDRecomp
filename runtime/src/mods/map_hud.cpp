// Native map/HUD bridge. Canonical USA identities are translated by guest_addr.h.
// Provenance and lifecycle: docs/mod-sdk-map-hud.md. No pane is ever mutated.
#include "map_hud.h"
#include "map_hud_rules.h"
#include "mods.h"
#include "hud_visibility.h"
#include "runtime.h"
#include "guest_addr.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <mutex>
namespace guestmods::maphud {
namespace { bool trace_enabled(){ static const bool on=std::getenv("WWHD_MAP_HUD_TRACE")!=nullptr; return on; } }

namespace {
std::recursive_mutex mutex;
Original original=nullptr;
bool supported=false,logic_open=false;
std::atomic<uint32_t> suppressed{0};
thread_local bool game_thread_bound=false;
std::atomic<bool> invalidated{false};
CompassLease lease;
ChartQueue charts;
uint64_t generation=1;
uint32_t ui=0,hud=0,compass=0,map=0,coordinator=0,compass_layout=0,map_layout=0;
char stage[8]{};
Rect rect{};
bool eligible=false,have_rect=false;
constexpr uint32_t kEvent=0x0269B238;
bool object(uint32_t p,uint32_t n=0x200){return !(p&3)&&p>=mem::kMem2Start&&p<=mem::kMem2End-n;}
uint32_t word(uint32_t p,uint32_t offset){return object(p,offset+4)?ld32(p+offset):0;}
void clear(){suppressed.store(0,std::memory_order_release);lease.clear();charts.clear();eligible=have_rect=false;++generation;}
bool logic(Cpu* c,bool draw){return c&&supported&&logic_open&&!draw&&game_thread_bound;}
uint32_t query(Cpu* c,uint32_t fn,uint32_t a,uint32_t b){Cpu copy=*c;copy.r[3]=a;copy.r[4]=b;original(&copy,GC(fn));return copy.r[3];}
void context(){
    if(invalidated.exchange(false,std::memory_order_acq_rel))clear();
    const uint32_t play=GD(0x1046F0B0),next_ui=ld32(GD(0x101F8344));
    const uint32_t next_hud=word(next_ui,0x178),children=word(next_hud,0x4C);
    uint32_t next_compass=word(next_hud,0x48)>4?word(children,4*4):0;
    if(!object(next_compass,0x64)||ld32(next_compass+4)!=GD(0x100EE998))next_compass=0;
    const uint32_t next_coord=word(next_ui,0x218);
    uint32_t next_map=word(next_coord,0x28);
    if(!object(next_map)||ld32(next_map+4)!=GD(0x100FB8B8))next_map=0;
    const uint32_t next_cl=word(next_compass,0x44),next_ml=word(next_map,0x44);
    if(ui!=next_ui||hud!=next_hud||compass!=next_compass||map!=next_map||coordinator!=next_coord||
       compass_layout!=next_cl||map_layout!=next_ml||std::memcmp(stage,mem::ptr(play+0x5134),8)||ld8(play+0x514C))clear();
    ui=next_ui;hud=next_hud;compass=next_compass;map=next_map;coordinator=next_coord;
    compass_layout=next_cl;map_layout=next_ml;std::memcpy(stage,mem::ptr(play+0x5134),8);
    const auto gates=mods::game_ui_now();
    eligible=compass&&(ld32(play+0x5CD8)&0x10000)&&!(ld32(play+0x5CD8)&0x202000)&&ld8(GD(0x1047B093))&&!ld8(play+0x514C)&&!mods::game_ui_hides_hud(gates);
    if(!eligible){lease.clear();suppressed.store(0,std::memory_order_release);}
}
// Enclose the native compass/clock pictures in their current global matrices.
// basePosition is a 3x3 origin code. Account for all four corners and rotation.
bool bounds(uint32_t pane,Rect& out){
    if(!object(pane)||word(word(pane,8),0x94)!=GC(0x02874D54)||!ld8(pane+0x46))return false;
    uint32_t parent=pane;
    for(unsigned depth=0;parent;depth++){
        if(depth==32||!object(parent)||!(ld8(parent+0x44)&1))return false;
        if(parent!=pane&&(ld8(parent+0x44)&2)&&!ld8(parent+0x45))return false;
        parent=ld32(parent+0xC);
    }
    const float w=ldf32(pane+0x3C),h=ldf32(pane+0x40);
    if(!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0||w>1280||h>720)return false;
    const int base=ld8(pane+0x47);if(base>8)return false;
    const float x0=-w*(base%3)*.5f,y0=h*(base/3)*.5f;
    float left=1e9f,right=-1e9f,bottom=1e9f,top=-1e9f;
    for(int i=0;i<4;i++){
        const float x=x0+(i&1?w:0),y=y0-(i&2?h:0);
        const float gx=ldf32(pane+0x48)*x+ldf32(pane+0x4C)*y+ldf32(pane+0x54);
        const float gy=ldf32(pane+0x58)*x+ldf32(pane+0x5C)*y+ldf32(pane+0x64);
        if(!std::isfinite(gx)||!std::isfinite(gy))return false;
        left=std::min(left,gx);right=std::max(right,gx);bottom=std::min(bottom,gy);top=std::max(top,gy);
    }
    left=std::max(left,-640.f);right=std::min(right,640.f);bottom=std::max(bottom,-360.f);top=std::min(top,360.f);
    out={left,top,right-left,top-bottom};return out.w>0&&out.h>0;
}
uint32_t root(){return word(word(compass_layout,4),0xC);}
void measure(){
    have_rect=false;if(!eligible)return;
    unsigned budget=512;
    auto walk=[&](auto&& self,uint32_t p,unsigned depth)->void{
        if(!budget||depth>24||!object(p))return;--budget;
        Rect b{};
        if(bounds(p,b)){
            if(!have_rect){rect=b;have_rect=true;}
            else {float x=std::min(rect.x,b.x),y=std::max(rect.y,b.y);rect={x,y,std::max(rect.x+rect.w,b.x+b.w)-x,y-std::min(rect.y-rect.h,b.y-b.h)};}
        }
        uint32_t sentinel=p+0x14,child=ld32(sentinel);
        for(unsigned i=0;child&&child!=sentinel&&budget&&i<256;i++){
            if(!object(child))break;self(self,child,depth+1);child=ld32(child);
        }
    };
    walk(walk,root(),0);
    if(trace_enabled()){
        static unsigned count=0;if(count==0||count==120||count==600){
            LOG("[map-hud] compass bounds root=%08X pictures=%d rect=%.1f,%.1f %.1fx%.1f",root(),have_rect,rect.x,rect.y,rect.w,rect.h);
            uint32_t p=word(compass,0x50);
            for(int i=0;i<8&&object(p);i++,p=ld32(p+0xC))LOG("[map-hud] compass ancestor %08X %.24s flags=%02X alpha=%u/%u size=%.1fx%.1f draw=%08X",p,(const char*)mem::ptr(p+0x80),ld8(p+0x44),ld8(p+0x45),ld8(p+0x46),ldf32(p+0x3C),ldf32(p+0x40),word(word(p,8),0x94));
        }++count;
    }
    have_rect=have_rect&&rect.w>0&&rect.h>0;
}
bool valid_chart(Cpu* c,uint32_t index){
    if(index>=49||!object(coordinator)||!map)return false;
    const uint32_t entry=query(c,0x02687A64,coordinator,index);
    if(!object(entry,0x26))return false;
    const uint8_t type=ld8(entry+3);const int sea=int8_t(ld8(entry)),save=int8_t(ld8(entry+1));
    if(sea<1||sea>49||save<1||save>128||type>8||!((1u<<type)&0x167))return false;
    if(!query(c,0x0268A570,coordinator,index)||!query(c,0x0268A5DC,coordinator,index))return false;
    const uint32_t saved=ld32(GD(0x101F84DC));
    return type!=0||(object(saved,0x200)&&query(c,0x025B8308,saved+0xE4,save-1));
}

void event(Cpu* c){
    std::lock_guard guard(mutex);
    const uint32_t instance=c->r[3];context();
    const uint32_t before=word(instance,0xAC);
    original(c,GC(kEvent)); // always consume real input first
    if(instance!=map)return;
    context();if(instance!=map)return;
    const uint32_t after=ld32(map+0xAC);
    auto request=charts.take(generation,ld32(map+0x90)==0,after!=before);
    if(!request||!valid_chart(c,request->index)||int32_t(after)<0||after>=61)return;
    // Dispatch the SDK's explicit-index carousel event. These are exactly the
    // three stores of native previous/next handling (0269B2C4 / 0269B310).
    // updateChartChange (0269BDE0), later in displayExecute, owns all art/caption
    // changes and the START -> WAIT -> FINISH -> processed lifecycle.
    st32(map+0xB8,request->index>after?1:uint32_t(-1));
    st32(map+0xB0,after);st32(map+0xAC,request->index);
    if(trace_enabled())LOG("[map-hud] carousel commit %u -> %u phase=%u",after,request->index,ld32(map+0x90));
}
void invalidate(){suppressed.store(0,std::memory_order_release);invalidated.store(true,std::memory_order_release);if(game_thread_bound){clear();invalidated.store(false,std::memory_order_release);}}
void map_destroy(Cpu* c){std::lock_guard guard(mutex);invalidate();original(c,GC(0x026986C4));}
void compass_destroy(Cpu* c){std::lock_guard guard(mutex);invalidate();original(c,GC(0x0264C4BC));}
}
void install(Original call,const std::function<bool(uint32_t,bool)>& available,const std::function<void(uint32_t,Hook)>& replace){
    if(std::strcmp(g_guest_build_name,"USA")&&std::strcmp(g_guest_build_name,"EU"))return;
    const uint32_t hooks[]={kEvent,0x026986C4,0x0264C4BC};
    const uint32_t queries[]={0x02877100,0x02874D54,0x02687A64,0x0268A570,0x0268A5DC,0x025B8308};
    for(auto fn:hooks)if(!guest_code_valid(fn)||!available(GC(fn),true))return;
    for(auto fn:queries)if(!guest_code_valid(fn)||!available(GC(fn),false))return;
    original=call;replace(GC(kEvent),event);
    replace(GC(0x026986C4),map_destroy);replace(GC(0x0264C4BC),compass_destroy);supported=true;
}
bool hide_pane(uint32_t pane){
    const uint32_t wanted=suppressed.load(std::memory_order_acquire);
    if(!wanted||wanted!=pane)return false;
    if(trace_enabled()){static std::atomic<unsigned> count{0};if(count.fetch_add(1)<8)LOG("[map-hud] suppressed native compass layout root=%08X",pane);}
    return true;
}
uint32_t capability(){return supported?1:0;}
void begin_logic(Cpu*,double dt){std::lock_guard guard(mutex);if(!supported)return;game_thread_bound=true;logic_open=true;context();measure();lease.begin(std::clamp(dt,0.,.25),eligible&&have_rect);}
void finish_logic(){std::lock_guard guard(mutex);if(!supported)return;context();lease.finish();suppressed.store(eligible&&lease.held()?root():0,std::memory_order_release);
    if(trace_enabled()){static bool previous=false;bool now=eligible&&lease.held();if(previous!=now){previous=now;LOG("[map-hud] finish suppression=%d",now);}}
    logic_open=false;}
void state_loaded(){std::lock_guard guard(mutex);invalidate();if(game_thread_bound)logic_open=false;}
void revoke(uint32_t owner){std::lock_guard guard(mutex);lease.revoke(owner);charts.revoke(owner);if(!lease.held())suppressed.store(0,std::memory_order_release);}
int compasses(Cpu* c,uint32_t owner,uint32_t replace,bool draw_phase){
    std::lock_guard guard(mutex);
    if(!supported)return Unsupported;if(!logic(c,draw_phase))return Phase;if(replace>1)return Invalid;
    context();measure();int result=lease.request(owner,replace,eligible&&have_rect);suppressed.store(lease.held()?root():0,std::memory_order_release);
    if(trace_enabled()){static int last=-999;if(last!=result){last=result;LOG("[map-hud] compass result=%d eligible=%d rectangle=%d part=%08X",result,eligible,have_rect,compass);}}
    return result;
}
int rectangle(uint32_t owner,Rect& out){std::lock_guard guard(mutex);if(!supported)return Unsupported;if(!game_thread_bound)return Phase;if(!eligible||!have_rect||lease.owner()!=owner)return 0;out={rect.x+640,360-rect.y,rect.w,rect.h};return 1;}
int selected(Cpu* c,bool draw_phase){std::lock_guard guard(mutex);if(!supported)return Unsupported;if(!logic(c,draw_phase))return Phase;context();if(!map)return Invalid;int index=int32_t(ld32(map+0xAC));return index>=0&&index<61?index:Invalid;}
int select(Cpu* c,uint32_t owner,uint32_t index,bool draw_phase){
    std::lock_guard guard(mutex);
    if(!supported)return Unsupported;if(!logic(c,draw_phase))return Phase;context();
    if(!map||int32_t(ld32(map+0xAC))<0||ld32(map+0xAC)>=61||!valid_chart(c,index)||ld8(GD(0x1046F0B0)+0x514C))return Invalid;
    return charts.select(owner,index,int32_t(ld32(map+0xAC)),generation);
}
int cancel(Cpu* c,uint32_t owner,bool draw_phase){std::lock_guard guard(mutex);if(!supported)return Unsupported;if(!logic(c,draw_phase))return Phase;context();return charts.cancel(owner);}
}
