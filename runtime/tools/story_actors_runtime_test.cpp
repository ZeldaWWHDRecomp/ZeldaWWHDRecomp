#include <array>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
// Synthetic guest memory: exercise the shipped actor spawn/check/open hooks, with no game assets.
#include "mods/story_actors.h"
#include "mods/story_chart.h"
#include "mods/story_world_arrival.h"
#include "mods/mod_actor.h"
#include "mods/story_props.h"
#include "mods/story_game_objects.h"
#include "mods/story_switches.h"
#include "mods/story_enemies.h"
#include "mods/story_enemy_children.h"
#include "mods/story_enemy_projectiles.h"
#include "mods/story_actor_identity.h"
#include "mods/story_mail.h"
#include "mods/story_store.h"
#include "mods/story_items.h"
#include "mods/story_notebook.h"
#include "mods/story_treasure.h"
#include "mods/story_resident_routes.h"
#include "mods/story_world_placement.h"
#include "runtime.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
namespace story_chart {
// a dialogue's chart give: the chart's record, owned once (as story_chart.cpp gives it)
std::map<std::string,int32_t> given_charts;
int32_t give(const std::string& mod,const std::string& chart){auto& v=given_charts[mod+":"+chart];if(!v)v=kResultOwned;return v;}
void note_import(){} bool installed(){return true;}
bool publish_world_markers(const std::string&,std::span<const WorldMarker>){return true;}
SquarePlace square_place(float x,float z){const int col=int(std::floor((x+50000)/100000)),row=int(std::floor((z+50000)/100000));return {col,row,x-col*100000.f,z-row*100000.f};}
}
// placed chests of mod items: the chest item hook is the treasure chests' (story_treasure_chest_test)
namespace story_treasure_chest {bool placed_noted=false,hook_ready=false;void note_placed_chests(){placed_noted=true;}bool item_hook_ready(){return hook_ready;}}
namespace story_world_arrival {bool show(const std::string&,float){return true;}void state_loaded(){}void note_declared(){}}
std::vector<std::string> native_trace_records;
void log_msg(const char* format,...){if(std::strncmp(format,"[native_actor]",14)!=0)return;char line[1024];va_list args;va_start(args,format);std::vsnprintf(line,sizeof(line),format,args);va_end(args);native_trace_records.emplace_back(line);}
namespace {
constexpr uint32_t kArena=0x20000000,kScratch=0x60002000,kActor=kArena+0x4000,kSave=kArena+0x8000;
std::array<uint8_t,1024> extension{};
std::map<uint32_t,void(*)(Cpu*)> hooks;
uint32_t created=0,actor_id=100;unsigned originals=0,says=0,unsays=0,talk_finishes=0;
uint32_t current_layer=kArena+0x6800;bool room_available=true;
uint32_t expect_people_subtype=1,expect_people_params=0x00FF0000,expect_people_yaw=0x4000;
bool sign_native_fixture=false,sign_a_pressed=false;unsigned sign_end_transitions=0;uint32_t sign_status=14;
uint32_t ym1_partner=0,ym1_message_status=6;unsigned seated_actions=0,seated_bases=0;
bool prop_broken_fixture=false;unsigned prop_claim_calls=0,enemy_claim_calls=0;bool enemy_terminal=false,refuse_enemy_request=false,enemy_four_mode=false,mo_disarm_fixture=false;
bool pending=false,nested_chest=false,throw_search=false;
// game objects: the object name table knows these stage actor names (process, subtype); a made object's
// parameter word can be falsified (read-back) and its create phase can fail (the game gives up)
std::map<std::string,std::pair<uint16_t,uint8_t>> object_names={{"takara",{0x125,0xFF}},{"Oyashi",{0x49,0xFF}},{"Table",{0x116,0xFF}},{"kusax1",{0x1B3,0xFF}}};
// the stage's grass batch (dGrass_packet_c: 1500 units of 0x44 bytes from +0x9C, a unit is taken while its first byte is set)
constexpr uint32_t kGrassPacket=kArena+0xA0000;unsigned grass_plants=0;
bool game_object_falsify=false,game_object_fail=false;std::vector<std::array<uint32_t,8>> game_object_creates;int32_t quest_state=1,item_count=1;unsigned deleted=0,native_calls=0;
std::map<uint32_t,uint32_t> live;

void map_guest(uint32_t at,uint32_t size) {
#ifdef _WIN32
    // Like MAP_FIXED below: mapping a range again gives fresh zeroed pages. Windows refuses a second
    // reservation, so an already reserved range is decommitted and committed again.
    if(!VirtualAlloc(mem::ptr(at),size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)){
        assert(VirtualFree(mem::ptr(at),size,MEM_DECOMMIT));
        assert(VirtualAlloc(mem::ptr(at),size,MEM_COMMIT,PAGE_READWRITE));
    }
#else
    assert(mmap(mem::ptr(at),size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
uint32_t enemy_address(unsigned kind){return kActor+0x20000+kind*0x4000;}
uint32_t chuchu_params=0xFF000000u;  // the ChuChu variant the fixture creates
void enemy_fixture(story_enemies::Kind kind,uint32_t a,uint32_t pid){const auto&d=story_enemy_ownership::descriptor(kind);std::memset(mem::ptr(a),0,d.bytes);st32(a+4,pid);st16(a+0xE,d.profile);st32(a+0xB0,kind==story_enemies::Kind::ChuChu?chuchu_params:story_enemies::parameters(kind));st32(a+0xB4,kind==story_enemies::Kind::ChuChu?0x1000C5A0:kind==story_enemies::Kind::Bokoblin?0x1000957C:kind==story_enemies::Kind::Moblin?0x10014E9C:0x10012C24);st8(a+0x2DD,0xFF);st16(a+0x2D8,0xFFFF);st16(a+0x2FC,story_enemies::home_z(kind));
 if(kind==story_enemies::Kind::ChuChu){st8(a+0x3F9,uint8_t(chuchu_params>>8));st8(a+0x3FB,0xFF);}else if(kind==story_enemies::Kind::Keese){st8(a+0x486,0xFF);st8(a+0x487,0xFF);}else if(kind==story_enemies::Kind::Bokoblin){st8(a+0x3D0,4);st8(a+0x3D1,0xFF);st8(a+0x3D2,0xFF);st8(a+0x3D3,0xFF);st32(a+0x49C,a);st16(a+0x49C+8,2);}else{st8(a+0x3DA,0xFF);st8(a+0x3DB,0xFF);st32(a+0x8BC,a);st16(a+0x8BC+8,1);st8(a+0x10B0,1);}
 for(size_t n=0;n<d.collider_count;++n){auto&x=d.colliders[n];st32(a+x.object+0x44,a+x.stts);st32(a+x.stts+0xC,a);st32(a+x.stts+0x10,pid);if(x.helper)st32(a+x.helper,a);}
}
uint32_t child_address(uint32_t params){return kArena+0x60000+(params==4?0x2000:0);}
void queue_enemy_child(uint32_t parent,uint32_t params){Cpu request{};request.r[3]=0x1CF;request.r[4]=params;hooks.at(0x025D5834)(&request);assert(request.r[3]!=0xFFFFFFFFu);st32(parent+(params==4?0x2D60:0x1360),request.r[3]);}
void original(Cpu* c,uint32_t fn) {
    ++originals;
    if(fn==0x0218B234&&sign_native_fixture&&ld16(c->r[3]+0xF8)==1){
        if(sign_status==16&&sign_a_pressed)sign_status=18;
        if(sign_status==18){sign_status=19;st16(c->r[3]+0xF8,0);st16(0x1046F0B0+0x52B8,ld16(0x1046F0B0+0x52B8)|8);}
    }
    if(fn==0x02110D4C||fn==0x0219D568||fn==0x0209F918||fn==0x021CCB00){if(enemy_four_mode&&fn==0x0209F918)queue_enemy_child(c->r[3],0);c->r[3]=4;return;}
    if(fn==0x023EB120){const uint32_t link=c->r[3],arrow=kArena+0x70000;std::memset(mem::ptr(arrow),0,0x81C);st32(arrow+4,900);st16(arrow+0xE,0x1D8);st32(arrow+0x2E8,0xFFFFFFFFu);st32(arrow+0x4B8+0x44,arrow+0x47C);st32(arrow+0x488,arrow);st32(arrow+0x48C,900);st32(arrow+0x4B8+0x10,0x4000);st8(arrow+0x4B8+0x14,2);st32(link+0x6590,900);st32(link+0x6594,arrow);return;}
    if(fn==0x020C25A4){c->r[3]=4;return;}
    if(fn==0x025D5834&&c->r[3]==0x1CF){const uint32_t params=c->r[4],address=child_address(params),pid=actor_id+500+params;std::memset(mem::ptr(address),0,0xA34);st32(address+4,pid);st16(address+0xE,0x1CF);st32(address+0xB0,params);st8(address+0x2DD,0xFF);st32(address+0x2E8,0xFFFFFFFFu);st32(address+0x700+0x44,address+0x6C4);st32(address+0x82C+0x44,address+0x6C4);st32(address+0x6D0,address);st32(address+0x6D4,pid);live[pid]=address;c->r[3]=pid;return;}
    if(fn==0x021104B4||fn==0x0219CE98||fn==0x0209F164||fn==0x021CC44C){live.erase(ld32(c->r[3]+4));++deleted;c->r[3]=1;return;}
    if(fn==0x0210DD3C||fn==0x0219AAE8||fn==0x0209BD1C||fn==0x021C9B1C){if(enemy_four_mode&&fn==0x021C9B1C&&mo_disarm_fixture){queue_enemy_child(c->r[3],4);st8(c->r[3]+0x10B0,0);st8(c->r[3]+0x10B1,1);mo_disarm_fixture=false;}if(enemy_terminal){const uint32_t a=c->r[3];if(ld16(a+0xE)==0x7FFA){st8(a+0x3FC,4);st8(a+0x3FD,51);}else if(ld16(a+0xE)==0x7FF7)st8(a+0x489,30);Cpu nested=*c;nested.r[3]=a;hooks.at(0x025D99E8)(&nested);}c->r[3]=1;return;}

    if(fn==0x023975B0){const auto a=c->r[3];st32(a+0x3BC,kArena+0xC000);st32(a+0x46C,0);st32(a+0x470,0);st32(a+0x474,0);st16(a+0x47A,0xFFFF);stf32(a+0x490,1);c->r[3]=4;}
    if(fn==0x02058590){st32(c->r[3]+0x3B8,kArena+0xC800);c->r[3]=4;}
    if(fn==0x023988E8){st8(c->r[3]+0x48B,0);st32(c->r[3]+0x470,2);}
    if(fn==0x02397AD0||fn==0x020583D4){live.erase(ld32(c->r[3]+4));c->r[3]=1;}
    if(fn==0x022C36D0){
        assert(ld32(c->r[3]+0x100)==0&&ld32(c->r[3]+0x104)==0&&ld32(c->r[3]+0x108)==0);
        extension[100]=0xB7; // story extension remains independent
        st8(kSave+0x20,0x42); // unrelated game state is outside the rollback range
        c->r[3]=0xBEEF;
    }
    if(fn==0x022F8F2C){assert(ld32(c->r[3]+0x100)==0&&ld32(c->r[3]+0x104)==0&&ld32(c->r[3]+0x108)==0);c->r[3]=0xBEEF;}
    if(fn==0x022C1BBC){st32(c->r[3]+0x7C8,0xFFFFFFFFu);st16(c->r[3]+0x8F0,0x12);c->r[3]=0x12;}
    if(fn==0x022C2958)st8(kSave+0x644,0xEF); // dangerous retail event path
    if(fn==0x022C045C){st16(c->r[3]+0xFC,42);st8(c->r[3]+0x910,0);}
    if(fn==0x022C2A04&&c->r[3]==kActor+0x1000){assert(ld8(c->r[3]+0x901)<=2);assert(ld16(c->r[3]+0x8F2)==0x8000);assert(ld16(c->r[3]+0xFC)==0xFFFF);st16(c->r[3]+0xFA,0xFFFF);}
    if(fn==0x024B63A8){
        if(nested_chest&&c->r[3]==kActor){Cpu vanilla=*c;vanilla.r[3]=kActor+0x3000;hooks.at(0x024B63A8)(&vanilla);}
        Cpu set=*c;set.r[3]=kSave+0x798;set.r[4]=0;hooks.at(0x025B8C0C)(&set);
    }
    if(fn==0x025B8C0C)st32(c->r[3],ld32(c->r[3])|1);
}
uint32_t move_approval=1;unsigned held_executes=0;float held_drawn_x=0;
uint32_t call(Cpu*,uint32_t fn,std::initializer_list<uint32_t> args) {
    ++native_calls;const auto* a=args.begin();
    if(sign_native_fixture&&fn==0x025F795C)return sign_status;
    if(sign_native_fixture&&fn==0x025F74D0){assert(a[1]==16&&sign_status==14);sign_status=16;++sign_end_transitions;return 0;}
    if(fn==0x025200D4)return 0x1046F0B0;
    if(fn==0x025DC80C){assert((a[0]>=1000&&a[0]<=1003)||(a[0]>=2000&&a[0]<=2003));return room_available?kArena+0x6000+(a[0]%1000)*0x200:0;}
    if(fn==0x025DED64)return current_layer;
    if(fn==0x025DEAB4){current_layer=a[0];return 0;}
    if(fn==0x025D57E0){++deleted;assert(a[0]>=kActor&&a[0]<=kActor+0x40000);st8(a[0]+0xC,3);if(a[0]>=kActor+0x30000)live.erase(ld32(a[0]+4));return 1;}
    if(fn==0x025C109C){const auto found=object_names.find(reinterpret_cast<char*>(mem::ptr(a[0])));if(found==object_names.end())return 0;const uint32_t entry=kArena+0x10*uint32_t(std::distance(object_names.begin(),found));st16(entry+8,found->second.first);st8(entry+0xA,found->second.second);return entry;}
    if(fn==0x025D5834){++created;assert(args.size()==8);assert(current_layer==kArena+0x6000+a[3]*0x200+0xC0);
        if(a[0]==0x1B3){  // the grass placement actor: adds its units to the batch and is gone (never live)
            assert(a[1]==2&&a[6]==0xFFFFFFFFu&&ld16(a[4]+4)==0);++grass_plants;unsigned left=21;
            for(uint32_t i=0;i<1500&&left;++i)if(!ld8(kGrassPacket+0x9C+i*0x44)){st8(kGrassPacket+0x9C+i*0x44,1);--left;}
            return actor_id+900+grass_plants;}
        if(a[0]==0x49||a[0]==0x116){std::array<uint32_t,8> args_copy{};std::copy(args.begin(),args.end(),args_copy.begin());args_copy[4]=ld16(a[4]+2)|(uint32_t(ld16(a[4]+4))<<16);assert(ld16(a[4])==0);game_object_creates.push_back(args_copy);
            const uint32_t id=actor_id+60+uint32_t(game_object_creates.size()),address=kActor+0x30000+uint32_t(game_object_creates.size())*0x800;std::memset(mem::ptr(address),0,0x800);
            if(game_object_fail)return id;  // the request was taken; the game gives up in the create phase (never live)
            st32(address+4,id);st16(address+0xE,uint16_t(a[0]));st32(address+0xB0,game_object_falsify?a[1]^1:a[1]);st8(address+0x2DD,uint8_t(a[6]));st32(address+0xF4,kArena+0x9000);st32(address+0x2C,current_layer);live[id]=address;return id;}
        if(a[0]>=0x7FF7&&a[0]<=0x7FFA){if(refuse_enemy_request)return 0xFFFFFFFFu;const auto kind=story_enemies::Kind(0x7FFA-a[0]);const uint32_t address=enemy_address(unsigned(kind)),id=actor_id+40+unsigned(kind);assert(a[1]==(kind==story_enemies::Kind::ChuChu?chuchu_params:story_enemies::parameters(kind))&&a[6]==0xFFFFFFFFu&&ld16(a[4]+4)==story_enemies::home_z(kind));enemy_fixture(kind,address,id);st32(address+0x2C,current_layer);live[id]=address;return id;}
        uint32_t address=kActor+((a[0]==0x175||a[0]==0x13B)?0x1000:a[0]==story_switches::kDoorProfile?0x1000:a[0]==0xB6?0x2000:(a[0]==mod_actor::kProfile||a[0]==mod_actor::kWorldProfile)?0x3000:0);uint32_t id=actor_id+(address-kActor)/0x1000;
        if(a[0]==0x125){assert(a[1]==0);assert(a[3]==0);assert(ld16(a[4]+4)==0x06FF);}
        else if(a[0]==0x175){assert(a[1]==expect_people_params);assert(a[3]==1);assert(a[6]==expect_people_subtype);assert(ld16(a[4]+2)==expect_people_yaw);}
        else if(a[0]==0x13B){assert(a[1]==1&&a[3]==1&&a[6]==0xFFFFFFFFu);}
        else if(a[0]==story_props::kProfile){assert(a[1]==0x007FFF3F&&a[3]==0&&a[6]==0xFFFFFFFFu&&ld16(a[4]+4)==255);assert(u32_as_f32(ld32(a[5]))==2&&ld32(a[5])==ld32(a[5]+4)&&ld32(a[5])==ld32(a[5]+8));}
        else if(a[0]==story_switches::kSwitchProfile||a[0]==story_switches::kDoorProfile){assert(a[1]==story_switches::parameters(a[0]==story_switches::kSwitchProfile?story_switches::Kind::Switch:story_switches::Kind::Door)&&a[3]==0&&a[6]==0xFFFFFFFFu&&ld16(a[4]+4)==255);}
        else if(a[0]==mod_actor::kProfile){assert(a[1]==1&&a[3]==3);}
        else if(a[0]==mod_actor::kWorldProfile){assert(a[3]==3);}
        else {assert(a[0]==0xB6&&a[1]==0x2CF3&&a[3]==2);}
        st32(address+4,id);st16(address+0xE,uint16_t(a[0]));if(a[0]==0x175){st8(address+0x2DD,uint8_t(a[6]));st8(address+0x905,uint8_t(a[6]));st32(address+0xB0,a[1]);st32(address+0x10,0x101C4CD0);st32(address+0xB4,0x100209C4);st8(address+0x326,uint8_t(a[3]));}if(a[0]==0x13B){st32(address+0xB0,1);st8(address+0x2DD,0xFF);st8(address+0xA20,1);st8(address+0xA21,1);}if(a[0]==story_props::kProfile){st32(address+0xB0,a[1]);st8(address+0x2DD,0xFF);st32(address+0x790,0);}
        if(a[0]==story_switches::kSwitchProfile||a[0]==story_switches::kDoorProfile){st32(address+0xB0,a[1]);st8(address+0x2DD,0xFF);}
        st32(address+0x2C,current_layer);live[id]=address;return id;
    }
    if(fn==0x02057F50||fn==0x024F43DC)return 0;
    if(fn==0x02397584){st32(a[0]+0x470,2);st8(a[0]+0x48B,0);return 0;}
    if(fn==0x02398348){st32(a[0]+0x470,1);return 0;}
    if(fn==0x02520540)return 7;
    if(fn==0x025E1A40)return 0;
    if(fn==0x022F7388){assert(a[1]==0x10023418&&a[2]==0);++seated_actions;st32(a[0]+0x86C,ld32(a[1]));st32(a[0]+0x870,ld32(a[1]+4));st8(a[0]+0xA1D,11);st8(a[0]+0xA22,1);return 1;}
    if(fn==0x0253EE04){assert(a[0]==0x1046F0B0+0x51D0);return ym1_partner;}
    if(fn==0x02540158){assert(a[0]==0x1046F0B0+0x51D0);return move_approval;}  // dEvt_control_c::moveApproval
    if(fn==0x025DFCC4){++held_executes;st32(a[1]+0x400,0xDEAD);st32(a[1]+0x314,0);held_drawn_x=500;return 1;}  // fpcMtd_Execute: AI state and place change, matrices built
    if(fn==0x025F795C)return ym1_message_status;
    if(fn==0x022F9B64)return 1;
    if(fn==0x025A11EC){++seated_bases;assert(a[1]==1);if(ym1_message_status==0x13)st32(a[0]+0x7C8,0xFFFFFFFFu);return 0;}
    if(fn==0x022F9C2C){st8(a[0]+0xA1D,uint8_t(a[1]));return 1;}
    if(fn==0x021E1E78){assert(a[0]==15&&a[1]==30);return 20;}
    if(fn==0x025D54C4){if(throw_search){st32(a[1],0xBAD);throw std::runtime_error("query failure");}st32(a[1],pending?0:live[a[0]]);return live[a[0]]!=0;}
    assert(false);return 0;
}
}
namespace dispatch {PpcFunc lookup(uint32_t){return nullptr;}uint32_t register_host(PpcFunc,const char*){static uint32_t next=0xF1000000;next+=4;return next;}}
namespace story_store {story_mail::Book book(){return story_mail::Book(extension.data(),255);}}
namespace story_mail {
SayOwner fixture_owner;bool fixture_record=false;
void set_say_restore_record(uint32_t,uint32_t){fixture_record=false;}
void say(uint32_t n,const std::u16string* text){assert(n==0x2CF3&&text&&!text->empty());++says;}
void say_owned(uint32_t n,const std::u16string* text,SayOwner owner){say(n,text);fixture_owner=owner;fixture_record=true;}
bool restore_say_owner(uint32_t,SayOwner& out){if(!fixture_record)return false;out=fixture_owner;return true;}
bool resume_say(uint32_t n,const std::u16string* text,SayOwner owner){if(!fixture_record||owner!=fixture_owner)return false;say(n,text);return true;}
void unsay(){fixture_record=false;++unsays;}void state_loaded(){}bool can_say(){return true;}
}
namespace story_notebook {int32_t done_action(const std::string&,const std::string&){return quest_state=255;}int32_t state(const std::string&,const std::string&){return quest_state;}int32_t reach(const std::string&,const std::string&,uint32_t){return 1;}}
namespace story_items {
std::optional<Item> find(const std::string&,const std::string&){return {};}
prop_rewards::Registry marker_fixture;
PropMarkerCheckpoint prop_marker_checkpoint(){return marker_fixture.checkpoint();}
bool rollback_prop_markers(PropMarkerCheckpoint p){return marker_fixture.rollback(p);}
PropMarkerHandle register_prop_marker(const std::string&mod,const std::string&actor,const std::string&flag,std::string&why){if(flag=="throw.broken")throw std::runtime_error("injected startup allocation failure");return marker_fixture.add(mod,actor,flag,why);}
EnemyMarkerHandle register_enemy_marker(const std::string&mod,const std::string&actor,const std::string&flag,std::string&why){if(flag=="throw.dead")throw std::runtime_error("injected enemy declaration failure");return {marker_fixture.add(mod,actor,flag,why,prop_rewards::kDefeated)};}
bool forget_prop_marker(PropMarkerHandle h){return marker_fixture.remove(h);}
bool enemy_defeated(EnemyMarkerHandle handle){const auto*m=marker_fixture.get(handle.value,prop_rewards::kDefeated);return m&&story_store::book().get(prop_rewards::kDefeated,m->key)!=0;}
EnemyClaimStatus claim_enemy_drops(EnemyMarkerHandle handle,const std::string&,const std::vector<PropDrop>&drops,bool){const auto*m=marker_fixture.get(handle.value,prop_rewards::kDefeated);if(!m||!drops.empty())return EnemyClaimStatus::InvalidDefinition;if(enemy_defeated(handle))return EnemyClaimStatus::AlreadyDefeated;++enemy_claim_calls;return story_store::book().set(prop_rewards::kDefeated,m->key,1)?EnemyClaimStatus::Committed:EnemyClaimStatus::StoreUnavailable;}
bool prop_broken(PropMarkerHandle){return prop_broken_fixture;}
PropClaimStatus claim_prop_drops(PropMarkerHandle,const std::string&,const std::vector<PropDrop>&,bool){++prop_claim_calls;return PropClaimStatus::InvalidDefinition;}
int32_t give(const std::string&,const std::string&,uint32_t,const char*,bool){return 1;}
int32_t take(const std::string&,const std::string&,uint32_t){return 0;}
int32_t count(const std::string&,const std::string&){return item_count;}
}
namespace story_treasure {bool installed(){return false;}}
namespace story_scenes {bool scenes_active=false;void trigger(const std::string&,const std::string&,const std::string&,unsigned){++talk_finishes;}bool active(){return scenes_active;}}
int main(int argc,char** argv){
    map_guest(0x101B0000,0x10000);map_guest(0x101F0000,0x10000);map_guest(0x10460000,0x30000);map_guest(kArena,0x100000);map_guest(0x60000000,0x20000);
    st32(0x101F84DC,kSave);st32(0x1046F0B0+0x5B34,kArena+0x7000);for(unsigned room=0;room<4;++room)st32(0x1047E8F0+room*0x22C,1000+room);std::memcpy(mem::ptr(0x1046F0B0+0x5134),"sea",4);
    std::memset(mem::ptr(kSave),0xA5,0x800);std::array<uint8_t,0x800> before;std::memcpy(before.data(),mem::ptr(kSave),before.size());
    std::memset(mem::ptr(kScratch+story_actors::kEnemyProfilesScratchOffset),0xA5,story_actors::kScratchBytes-story_actors::kEnemyProfilesScratchOffset);
    st8(kScratch+story_actors::kScratchBytes,0x6D);
    story_actors::set_game_calls(original,call);story_actors::set_scratch_memory(kScratch);
    for(uint32_t i=story_actors::kEnemyProfilesScratchOffset;i<story_actors::kScratchBytes;++i)assert(ld8(kScratch+i)==0);
    assert(ld8(kScratch+story_actors::kScratchBytes)==0x6D);
    if(argc==2&&std::strcmp(argv[1],"--enemy-variant")==0){
        // a declared ChuChu colour: created with its own parameter word, owned and verified with its colour;
        // a copy with another colour is never recognized as the declared enemy
        map_guest(0x10000000,0x10000);st32(0x100073AC,0x4000);st8(0x100073B0,2);
        for(unsigned n=0;n<story_enemy_ownership::kKindCount;++n){const auto&d=story_enemy_ownership::descriptor(story_enemies::Kind(n));const uint32_t profile=kArena+0x80000+n*0x100;st32(0x101F3EE0+uint32_t(d.native_profile)*4,profile);st32(profile+0x10,d.bytes);}
        st32(kArena+0x7004,55);st16(kArena+0x700E,0xA8);
        story_notebook::ManifestActor cc;cc.id="red";cc.actor="chuchu";cc.defeated_flag="red.dead";cc.stage="sea";cc.variant="purple";std::string why;
        assert(!story_actors::load("test",{cc},why)&&!story_actors::declared());
        auto sign=cc;sign.actor="sign";sign.id="board";sign.flag="read";sign.defeated_flag.clear();assert(!story_actors::load("test",{sign},why));
        cc.variant="red";chuchu_params=0xFF000100u;assert(story_actors::load("test",{cc},why));
        story_actors::set_scratch_memory(kScratch);story_actors::install([](uint32_t){return std::string();},[](uint32_t f,void(*fn)(Cpu*)){hooks[f]=fn;});
        Cpu c{};pending=true;story_actors::native_room_loaded(&c,0);pending=false;assert(created==1);
        const uint32_t a=enemy_address(0);assert(ld32(a+0xB0)==0xFF000100u&&ld8(a+0x3F9)==1);
        Cpu create{};create.r[3]=a;hooks.at(0x02110D4C)(&create);
        Cpu exec{};exec.r[3]=a;hooks.at(0x0210DD3C)(&exec);
        assert(story_enemy_ownership::lookup_actor(a)!=0);
        st8(a+0x3F9,0);assert(!story_enemies::owns_actor(a));st8(a+0x3F9,1);assert(story_enemies::owns_actor(a));
        std::puts("enemy variant: declared ChuChu colour created, owned and verified");return 0;
    }
    if(argc==2&&(std::strcmp(argv[1],"--enemies")==0||std::strcmp(argv[1],"--enemies-four")==0)){
        enemy_four_mode=std::strcmp(argv[1],"--enemies-four")==0;const unsigned enemy_count=enemy_four_mode?4:2;map_guest(0x10000000,0x10000);st32(0x100073AC,0x4000);st8(0x100073B0,2);if(enemy_four_mode){map_guest(0x10190000,0x10000);map_guest(0x101B0000,0x10000);}
        for(unsigned n=0;n<story_enemy_ownership::kKindCount;++n){const auto&d=story_enemy_ownership::descriptor(story_enemies::Kind(n));const uint32_t profile=kArena+0x80000+n*0x100;st32(0x101F3EE0+uint32_t(d.native_profile)*4,profile);st32(profile+0x10,d.bytes);}
        st32(kArena+0x7004,55);st16(kArena+0x700E,0xA8);
        story_notebook::ManifestActor cc;cc.id="cc";cc.actor="chuchu";cc.defeated_flag="cc.dead";cc.stage="sea";cc.quest="quest";cc.quest_step=1;auto ki=cc;ki.id="ki";ki.actor="keese";ki.defeated_flag="ki.dead";ki.quest.clear();std::string why;
        auto invalid=ki;invalid.drops.push_back({"missing",1});assert(!story_actors::load("test",{cc,invalid},why)&&!story_actors::declared()&&story_enemies::checkpoint()==0&&story_enemy_ownership::checkpoint()==0&&story_items::prop_marker_checkpoint()==0);
        auto throwing=ki;throwing.defeated_flag="throw.dead";bool caught=false;try{story_actors::load("test",{cc,throwing},why);}catch(const std::runtime_error&){caught=true;}assert(caught&&!story_actors::declared()&&story_enemies::checkpoint()==0&&story_enemy_ownership::checkpoint()==0&&story_items::prop_marker_checkpoint()==0);
        std::vector<story_notebook::ManifestActor> declarations{cc,ki};if(enemy_four_mode){auto bk=ki;bk.id="bk";bk.actor="bokoblin";bk.defeated_flag="bk.dead";auto mo=ki;mo.id="mo";mo.actor="moblin";mo.defeated_flag="mo.dead";declarations.push_back(bk);declarations.push_back(mo);}assert(story_actors::load("test",declarations,why));story_actors::set_scratch_memory(kScratch);story_actors::install([](uint32_t){return std::string();},[](uint32_t f,void(*fn)(Cpu*)){hooks[f]=fn;});assert(hooks.contains(0x0200CE78)&&hooks.contains(0x023DF600)&&hooks.contains(0x025D4B4C));
        std::array<uint8_t,story_actors::kScratchBytes> fresh{},queued{},active{};std::memcpy(fresh.data(),mem::ptr(kScratch),fresh.size());Cpu c{};refuse_enemy_request=true;story_actors::native_room_loaded(&c,0);assert(ld32(kScratch+128)==0xFFFFFFFFu&&ld32(kScratch+132)==0xFFFFFFFFu);refuse_enemy_request=false;const unsigned rejected_attempts=created;pending=true;story_actors::native_room_loaded(&c,0);assert(created==rejected_attempts+enemy_count&&ld32(kScratch+story_actors::kEnemyEpochScratchOffset)==2);std::memcpy(queued.data(),mem::ptr(kScratch),queued.size());story_actors::state_loaded();story_actors::native_room_loaded(&c,0);assert(created==rejected_attempts+enemy_count&&enemy_claim_calls==0);
        pending=false;const uint32_t cca=enemy_address(0),kia=enemy_address(3);c.r[3]=cca;hooks.at(0x02110D4C)(&c);assert(c.r[3]==4);c.r[3]=kia;hooks.at(0x0219D568)(&c);assert(c.r[3]==4);if(enemy_four_mode){const uint32_t bka=enemy_address(1),moa=enemy_address(2);c.r[3]=bka;hooks.at(0x0209F918)(&c);assert(c.r[3]==4);story_enemy_ownership::LineageInfo child{};assert(story_enemy_ownership::lineage_info(ld32(bka+0x1360),child)&&!child.address);assert(!story_enemy_children::ready(story_enemy_ownership::lookup_actor(bka),bka));c.r[3]=child_address(0);hooks.at(0x020C25A4)(&c);assert(c.r[3]==4&&story_enemy_children::ready(story_enemy_ownership::lookup_actor(bka),bka));assert(story_enemy_ownership::pair_policy(child_address(0)+0x700,kActor)==story_enemy_ownership::PairPolicy::Reject);
            c.r[3]=moa;hooks.at(0x021CCB00)(&c);assert(c.r[3]==4);mo_disarm_fixture=true;c.r[3]=moa;hooks.at(0x021C9B1C)(&c);assert(story_enemy_ownership::lineage_info(ld32(moa+0x2D60),child)&&!child.address);assert(!story_enemy_children::ready(story_enemy_ownership::lookup_actor(moa),moa));c.r[3]=child_address(4);hooks.at(0x020C25A4)(&c);assert(c.r[3]==4&&story_enemy_children::ready(story_enemy_ownership::lookup_actor(moa),moa));
        }
        auto token=story_enemy_ownership::lookup_actor(cca);assert(token&&story_enemy_ownership::source_epoch(token)==2);c.r[3]=kArena+0x7000;hooks.at(0x023EB120)(&c);assert(story_enemy_projectiles::owns_attack(kArena+0x70000+0x4B8));assert(story_enemy_ownership::pair_policy(kArena+0x70000+0x4B8,cca+0x7C4)==story_enemy_ownership::PairPolicy::Delegate);assert(story_enemy_ownership::pair_policy(cca+0x7C4,kArena+0x70000+0x4B8)==story_enemy_ownership::PairPolicy::Reject);std::memcpy(active.data(),mem::ptr(kScratch),active.size());story_actors::state_loaded();assert(story_actors::actor_address(&c,"test","cc")==cca&&story_enemy_ownership::lookup_actor(cca)==token);
        st8(kScratch+story_actors::kIdentityScratchOffset+39,uint8_t(ld8(kScratch+story_actors::kIdentityScratchOffset+39)^1));story_actors::state_loaded();assert(!story_actors::actor_address(&c,"test","cc")&&!story_enemy_ownership::lookup_actor(cca));story_actors::native_room_loaded(&c,0);assert(created==rejected_attempts+enemy_count);
        std::memcpy(mem::ptr(kScratch),active.data(),active.size());story_actors::state_loaded();assert(story_enemy_ownership::lookup_actor(cca)==token);st32(kScratch+story_actors::kEnemyEpochScratchOffset,3);story_actors::state_loaded();assert(!story_actors::actor_address(&c,"test","cc"));story_actors::native_room_loaded(&c,0);assert(created==rejected_attempts+enemy_count);
        std::memcpy(mem::ptr(kScratch),active.data(),active.size());story_actors::state_loaded();quest_state=0;story_actors::service(&c);assert(deleted==1);c.r[3]=cca;hooks.at(0x021104B4)(&c);assert(enemy_claim_calls==0&&ld32(kScratch+128)==0xFFFFFFFFu);story_actors::state_loaded();quest_state=1;story_actors::native_room_loaded(&c,0);assert(created==rejected_attempts+enemy_count);actor_id+=10;st32(0x1047E8F0,2000);story_actors::native_room_loaded(&c,0);assert(created==rejected_attempts+2*enemy_count&&ld32(kScratch+story_actors::kEnemyEpochScratchOffset)==3);c.r[3]=cca;hooks.at(0x02110D4C)(&c);assert(c.r[3]==4);
        enemy_terminal=true;c.r[3]=cca;hooks.at(0x0210DD3C)(&c);c.r[3]=cca;hooks.at(0x0210DD3C)(&c);assert(enemy_claim_calls==1&&ld8(cca+0xC)==3);enemy_terminal=false;assert(std::memcmp(before.data(),mem::ptr(kSave),before.size())==0);
        // Restore the true before-first-create snapshot, including native readiness
        // counters and story scratch, rather than inventing a fresh host generation.
        extension.fill(0);std::memcpy(mem::ptr(kScratch),fresh.data(),fresh.size());live.clear();st32(0x1047E8F0,1000);story_actors::state_loaded();actor_id+=10;story_actors::native_room_loaded(&c,0);assert(created==rejected_attempts+3*enemy_count&&ld32(kScratch+story_actors::kEnemyEpochScratchOffset)==2);assert(enemy_claim_calls==1);return 0;
    }
    if(argc==2&&std::strcmp(argv[1],"--switches")==0){
        map_guest(0x101C0000,0x10000);map_guest(0x10180000,0x20000);st32(0x101CD034+0x10,story_switches::kSwitchClassBytes);st32(0x1018FFE8+0x10,story_switches::kDoorClassBytes);
        story_notebook::ManifestActor button;button.id="button";button.actor="switch";button.switch_flag="pressed";button.stage="sea";
        auto door=button;door.id="gate";door.actor="door";door.switch_flag.clear();door.switch_actor="button";
        auto bad=door;bad.id="bad";bad.switch_actor="missing";std::string why;
        assert(!story_actors::load("test",{button,bad},why)&&!story_actors::declared()&&story_switches::checkpoint()==0);
        assert(story_actors::load("test",{door,button},why)); // forward reference: door declared first
        auto foreign=door;foreign.id="foreign";assert(!story_actors::load("foreign",{foreign},why));
        story_actors::install([](uint32_t){return std::string();},[](uint32_t f,void(*fn)(Cpu*)){hooks[f]=fn;});
        Cpu c{};story_actors::native_room_loaded(&c,0);assert(created==2&&ld32(kScratch+128)==101&&ld32(kScratch+132)==100);
        assert(story_store::book().has(story_actors::kRecordActorFlag,story_actors::flag_key("test","pressed"))&&!story_actors::flag(story_store::book(),"test","pressed"));
        c.r[3]=kActor;hooks.at(0x023975B0)(&c);assert(c.r[3]==4);c.r[3]=kActor+0x1000;hooks.at(0x02058590)(&c);assert(c.r[3]==4&&ld8(kActor+0x1000+0x3BC)==1);
        stf32(kActor+0x490,0);st8(kActor+0x48B,1);c.r[3]=kActor;hooks.at(0x023988E8)(&c);assert(story_actors::flag(story_store::book(),"test","pressed"));
        c.r[3]=kActor+0x1000;hooks.at(0x02058144)(&c);assert(ld8(kActor+0x1000+0x3BC)==4);
        const uint32_t row=kScratch+story_actors::kIdentityScratchOffset+story_actors::identity::kRecordBytes;
        std::array<uint8_t,story_actors::identity::kRecordBytes> saved{};std::memcpy(saved.data(),mem::ptr(row),saved.size());
        story_actors::state_loaded();assert(story_actors::actor_address(&c,"test","button")==kActor); // unchanged identity remains bound
        st8(row+39,uint8_t(ld8(row+39)^1));story_actors::state_loaded();assert(ld32(kScratch+132)==0xFFFFFFFFu&&!story_actors::actor_address(&c,"test","button"));const auto count=created;story_actors::native_room_loaded(&c,0);assert(created==count);
        std::memcpy(mem::ptr(row),saved.data(),saved.size());st32(kScratch+132,100);story_actors::state_loaded();assert(story_actors::actor_address(&c,"test","button")==kActor);
        c.r[3]=kActor;hooks.at(0x02397AD0)(&c);assert(ld32(kScratch+132)==0xFFFFFFFFu&&ld32(row)==0);story_actors::state_loaded();assert(!story_actors::actor_address(&c,"test","button"));
        st32(0x1047E8F0,2000);story_actors::native_room_loaded(&c,0);assert(created==count+1); // actual native room epoch admits fresh switch
        assert(story_actors::flag(story_store::book(),"test","pressed"));assert(std::memcmp(before.data(),mem::ptr(kSave),before.size())==0);return 0;
    }
    if(argc==2&&std::strcmp(argv[1],"--game-objects")==0){
        // the game's own objects by reference: created by name on the retail profile with the fixed parameter
        // word and a turn only, tracked by process id, read back, quarantined on a mismatch or after three failures
        story_notebook::ManifestActor palm;palm.id="palm";palm.actor="game:palm";palm.variant="palm";palm.stage="sea";palm.room=0;palm.position={10,20,30};palm.rotation={0,90,0};
        auto stool=palm;stool.id="stool";stool.actor="game:table";stool.variant="stool";stool.rotation={};
        auto tree=palm;tree.id="tree";tree.actor="game:tree-large";tree.variant="tree";  // the fixture's game has no "lwood"
        std::string why;auto bad=palm;bad.id="bad";bad.params=7;assert(!story_actors::load("test",{bad},why)&&!story_actors::declared());
        bad=palm;bad.id="bad";bad.variant="coconut";assert(!story_actors::load("test",{bad},why));
        bad=palm;bad.id="bad";bad.scale={1.5f,1.5f,1.5f};assert(!story_actors::load("test",{bad},why));
        bad=palm;bad.id="bad";bad.rotation={5,0,0};assert(!story_actors::load("test",{bad},why));
        assert(story_actors::load("test",{palm,stool,tree},why));
        story_actors::install([](uint32_t){return std::string();},[](uint32_t a,void(*fn)(Cpu*)){hooks[a]=fn;});
        assert(story_actors::editor_kind_live("game:palm",true)&&!story_actors::editor_restart_only(palm));
        Cpu c{};story_actors::native_room_loaded(&c,0);
        // palm and stool made by name into the room layer; the tree's name is unknown to this game: omitted
        assert(game_object_creates.size()==2&&current_layer==kArena+0x6800);
        const auto& p=game_object_creates[0];assert(p[0]==0x49&&p[1]==0xFFFFFFFFu&&p[3]==0&&p[6]==0xFFFFFFFFu&&p[7]==0);
        assert((p[4]&0xFFFF)==0x4000&&(p[4]>>16)==0);  // yaw 90 degrees, no tilt: angle.z stays 0 (no switch or item field)
        assert(game_object_creates[1][0]==0x116&&game_object_creates[1][1]==0xFFFFFF01);
        const uint32_t palm_pid=ld32(kScratch+128),stool_pid=ld32(kScratch+132);assert(palm_pid!=0xFFFFFFFFu&&stool_pid!=0xFFFFFFFFu&&palm_pid!=stool_pid&&!story_actors::actor_address(&c,"test","tree"));
        assert(story_actors::actor_address(&c,"test","palm")==live[palm_pid]);
        story_actors::service(&c);assert(deleted==0&&game_object_creates.size()==2);  // read back: both match; nothing made again
        {   // the game takes the stool away itself (a broken rock, a blown-up boulder): it stays gone this room visit
            const uint32_t stool=ld32(kScratch+132);const uint32_t at=live[stool];live.erase(stool);
            story_actors::native_room_loaded(&c,0);story_actors::service(&c);assert(game_object_creates.size()==2&&ld32(kScratch+132)>>24==0xFE);
            live[stool]=at;st32(kScratch+132,stool);story_actors::state_loaded();story_actors::service(&c);  // (back as it was for the rest)
        }
        // the saved identity row binds a full state load; a changed row drops the binding
        story_actors::state_loaded();story_actors::service(&c);assert(story_actors::actor_address(&c,"test","palm")==live[palm_pid]&&game_object_creates.size()==2);
        const uint32_t row=kScratch+story_actors::kIdentityScratchOffset;st8(row+39,uint8_t(ld8(row+39)^1));story_actors::state_loaded();
        assert(ld32(kScratch+128)==0xFFFFFFFFu&&!story_actors::actor_address(&c,"test","palm"));
        story_actors::service(&c);assert(game_object_creates.size()==2);  // quarantined until the room loads again
        // a new room visit makes it again; the game gives up three times: omitted until the next visit
        live.clear();game_object_fail=true;const size_t made=game_object_creates.size();
        st32(0x1047E8F0,2000);story_actors::native_room_loaded(&c,0);
        for(int i=0;i<6;++i)story_actors::service(&c);
        assert(game_object_creates.size()==made+6);  // three tries each for palm and stool, then nothing more
        story_actors::service(&c);assert(game_object_creates.size()==made+6);
        // the next visit tries again; a parameter word that doesn't read back is deleted and quarantined for good
        game_object_fail=false;game_object_falsify=true;st32(0x1047E8F0,1000);story_actors::native_room_loaded(&c,0);assert(game_object_creates.size()==made+8);
        const unsigned before_delete=deleted;story_actors::service(&c);assert(deleted==before_delete+2);
        game_object_falsify=false;story_actors::service(&c);story_actors::service(&c);assert(game_object_creates.size()==made+8);
        st32(0x1047E8F0,2000);story_actors::native_room_loaded(&c,0);assert(game_object_creates.size()==made+8);  // a mismatch stays quarantined
        // the editor: a new game object is made while the game runs; a changed one is made again (never moved in place)
        auto palm2=palm;palm2.id="palm2";std::vector<std::string> pending_list;
        assert(story_actors::editor_live_capable("test",palm2,why));
        assert(story_actors::editor_update("test",{palm,stool,tree,palm2},{"palm2"},why,&pending_list)&&pending_list.empty());
        story_actors::room_loaded(&c,0);assert(game_object_creates.size()==made+9&&game_object_creates.back()[0]==0x49);
        assert(!story_actors::editor_pose(&c,"test","palm2",{1,2,3},{0,45,0}));
        // a shared batch (grass): once per room visit, after the room's own placements, into the stage's own batch with a quarter left
        auto grass=palm;grass.id="grass";grass.actor="game:grass";grass.variant="grass-21";grass.rotation={};
        st32(0x1046F0B0+0x5AB8,0);  // the stage has no grass of its own
        assert(story_actors::editor_update("test",{palm,stool,tree,palm2,grass},{"grass"},why,&pending_list)&&pending_list.empty());
        for(int i=0;i<40;++i)story_actors::room_loaded(&c,0);
        assert(grass_plants==0);
        st32(0x1046F0B0+0x5AB8,kGrassPacket);std::memset(mem::ptr(kGrassPacket),0,0x9C+1500*0x44);
        for(uint32_t i=0;i<1110;++i)st8(kGrassPacket+0x9C+i*0x44,1);  // the game's own use: 390 free, 375 must stay
        st32(0x1047E8F0,1000);story_actors::native_room_loaded(&c,0);for(int i=0;i<40;++i)story_actors::room_loaded(&c,0);
        assert(grass_plants==0);
        for(uint32_t i=900;i<1110;++i)st8(kGrassPacket+0x9C+i*0x44,0);  // 600 free
        story_game_objects::forget_resolved();  // the fixture reuses room process ids; the game never does
        st32(0x1047E8F0,2000);story_actors::native_room_loaded(&c,0);
        for(int i=0;i<20;++i)story_actors::room_loaded(&c,0);
        assert(grass_plants==0);  // the room's own placements first
        for(int i=0;i<20;++i)story_actors::room_loaded(&c,0);
        assert(grass_plants==1);  // once this visit
        const uint32_t grass_slot=kScratch+128+4*4;assert((ld32(grass_slot)>>24)==0xFE&&!story_actors::actor_address(&c,"test","grass"));
        story_actors::state_loaded();for(int i=0;i<40;++i)story_actors::room_loaded(&c,0);assert(grass_plants==1);  // a full state keeps the mark
        st32(0x1047E8F0,1000);story_actors::native_room_loaded(&c,0);for(int i=0;i<40;++i)story_actors::room_loaded(&c,0);assert(grass_plants==2);  // the next visit plants again
        // the editor: a removed batch goes when the room loads again (said so), and is not planted again
        assert(story_actors::editor_update("test",{palm,stool,tree,palm2},{},why,&pending_list)&&pending_list.size()==1&&pending_list[0].find("(Reload room)")!=std::string::npos);
        st32(0x1047E8F0,2000);story_actors::native_room_loaded(&c,0);for(int i=0;i<40;++i)story_actors::room_loaded(&c,0);assert(grass_plants==2);
        assert(std::memcmp(before.data(),mem::ptr(kSave),before.size())==0);return 0;
    }
    if(argc==2&&std::strcmp(argv[1],"--props")==0){
        map_guest(0x101D0000,0x10000);st32(0x101D2B1C+0x10,story_props::kClassBytes);
        story_notebook::ManifestActor prop;prop.id="pot";prop.actor="pot";prop.broken_flag="pot.broken";prop.stage="sea";prop.scale={2,2,2};std::string why;
        auto invalid=prop;invalid.id="invalid";invalid.broken_flag="invalid.broken";invalid.drops.push_back({"missing",1});
        // Scene-only flags must not survive rejected or throwing actor batches.
        auto collision_pair=[](const std::string& mod){std::map<uint16_t,std::string> seen;for(unsigned i=0;i<100000;++i){auto id="scene"+std::to_string(i);auto [it,fresh]=seen.emplace(story_actors::flag_key(mod,id),id);if(!fresh)return std::make_pair(it->second,id);}assert(false);return std::make_pair(std::string(),std::string());};
        auto failed_flags=collision_pair("scene-failed");
        assert(!story_actors::load("scene-failed",{prop,invalid},why,{failed_flags.first}));
        assert(story_actors::register_flags("scene-failed",{failed_flags.second},why));
        auto throw_flags=collision_pair("scene-throw");auto flag_throw=prop;flag_throw.id="throwing";flag_throw.broken_flag="throw.broken";bool flag_caught=false;
        try{story_actors::load("scene-throw",{prop,flag_throw},why,{throw_flags.first});}catch(const std::runtime_error&){flag_caught=true;}
        assert(flag_caught&&story_actors::register_flags("scene-throw",{throw_flags.second},why));
        auto committed_flags=collision_pair("scene-commit");assert(story_actors::load("scene-commit",{},why,{committed_flags.first}));
        assert(!story_actors::register_flags("scene-commit",{committed_flags.second},why));
        const auto old_models=mod_actor::checkpoint();assert(!story_actors::load("test",{prop,invalid},why)&&!story_actors::declared()&&story_props::checkpoint()==0&&mod_actor::checkpoint()==old_models&&story_items::prop_marker_checkpoint()==0);
        auto throwing=prop;throwing.id="throwing";throwing.broken_flag="throw.broken";bool caught=false;try{story_actors::load("test",{prop,throwing},why);}catch(const std::runtime_error&){caught=true;}assert(caught&&!story_actors::declared()&&story_props::checkpoint()==0&&story_items::prop_marker_checkpoint()==0);
        assert(story_actors::register_model("test","model","original_model",1,why));auto generic=prop;generic.actor="mod";generic.kind="mod";generic.id="model.actor";generic.model="model";generic.broken_flag.clear();
        assert(!story_actors::load("test",{generic,prop,invalid},why)&&!story_actors::declared()&&story_props::checkpoint()==0&&mod_actor::checkpoint()==old_models&&story_items::prop_marker_checkpoint()==0);
        assert(story_actors::load("test",{prop},why));story_actors::install([](uint32_t){return std::string();},[](uint32_t a,void(*fn)(Cpu*)){hooks[a]=fn;});
        Cpu c{};story_actors::native_room_loaded(&c,0);assert(created==1&&current_layer==kArena+0x6800&&ld32(kScratch+128)==100);
        assert(story_actors::actor_address(&c,"test","pot")==kActor);
        // Scene particles must use exact owned native identity, not merely a
        // same-room address returned by native SearchByID.
        story_notebook::WorldPoint particle_point;
        st32(0x1047E6C4,77);st32(kScratch+0x318,77);
        st8(kActor+0x326,0);st32(kActor+0x314,f32_as_u32(12));st32(kActor+0x318,f32_as_u32(34));st32(kActor+0x31C,f32_as_u32(56));
        assert(story_actors::particle_position(&c,"test","pot",0,particle_point));
        assert(particle_point.x==12&&particle_point.y==34&&particle_point.z==56);
        st32(kActor+4,999);assert(!story_actors::particle_position(&c,"test","pot",0,particle_point));st32(kActor+4,100);
        st16(kActor+0xE,0x1C5);assert(!story_actors::particle_position(&c,"test","pot",0,particle_point));st16(kActor+0xE,story_props::kProfile);
        assert(!story_actors::particle_position(&c,"foreign","pot",0,particle_point));
        assert(!story_actors::particle_position(&c,"test","pot",1,particle_point));
        assert(story_actors::particle_position(&c,"test","pot",0,particle_point));

        st32(0x1047E6C4,0);st32(kScratch+0x318,0);
        const uint32_t fp=kScratch+story_actors::kIdentityScratchOffset;std::array<uint8_t,story_actors::identity::kRecordBytes> snapshot{};std::memcpy(snapshot.data(),mem::ptr(fp),snapshot.size());
        story_actors::state_loaded();assert(story_actors::actor_address(&c,"test","pot")==kActor); // legitimate same-config restore
        auto other=prop;other.id="other-pot";other.broken_flag="other.broken";const auto foreign=story_actors::identity::record(story_actors::identity::fingerprint("test",other));
        auto changed=prop;changed.drops.push_back({"changed",2});const auto changed_record=story_actors::identity::record(story_actors::identity::fingerprint("test",changed));
        for(unsigned failure=0;failure<4;++failure){std::memcpy(mem::ptr(fp),snapshot.data(),snapshot.size());st32(kScratch+128,100);if(failure==0)st32(fp,0);else if(failure==1)st8(fp+39,uint8_t(ld8(fp+39)^1));else if(failure==2)std::memcpy(mem::ptr(fp),foreign.data(),foreign.size());else std::memcpy(mem::ptr(fp),changed_record.data(),changed_record.size());story_actors::state_loaded();assert(ld32(kScratch+128)==0xFFFFFFFFu&&!story_actors::actor_address(&c,"test","pot"));c.r[3]=kActor;hooks.at(0x024CC004)(&c);assert(prop_claim_calls==0);story_actors::native_room_loaded(&c,0);assert(created==1);}
        std::memcpy(mem::ptr(fp),snapshot.data(),snapshot.size());st32(kScratch+128,100);story_actors::state_loaded();assert(story_actors::actor_address(&c,"test","pot")==kActor);
        st32(kActor+4,999);c.r[3]=kActor;hooks.at(0x024D09BC)(&c);assert(ld32(kScratch+128)==100);st32(kActor+4,100); // foreign delete cannot clear a legitimate slot
        c.r[3]=kActor;hooks.at(0x024CC004)(&c);live.erase(100);story_actors::native_room_loaded(&c,0);assert(created==1); // same epoch cannot retry a failed claim
        st32(0x1047E8F0,2000);story_actors::native_room_loaded(&c,0);assert(created==2); // new native room process retries
        const unsigned before_create=created;
        prop_broken_fixture=true;live.erase(100);story_actors::room_loaded(&c,0);assert(created==before_create);
        prop_broken_fixture=false;story_actors::state_loaded();story_actors::native_room_loaded(&c,0);assert(created==before_create+1);
        c.r[3]=kActor;hooks.at(0x024D09BC)(&c);assert(ld32(kScratch+128)==0xFFFFFFFFu&&ld32(fp)==0);const auto old_claims=prop_claim_calls;c.r[3]=kActor;hooks.at(0x024CC004)(&c);assert(prop_claim_calls==old_claims);story_actors::state_loaded();c.r[3]=kActor;hooks.at(0x024CC004)(&c);assert(prop_claim_calls==old_claims&&ld32(kScratch+128)==0xFFFFFFFFu);
        assert(std::memcmp(before.data(),mem::ptr(kSave),before.size())==0);return 0;
    }
    if(argc==2&&std::strcmp(argv[1],"--ym1")==0){
        map_guest(0x10020000,0x10000);st32(0x10023418,0x0000FFFF);st32(0x1002341C,0x022FAB88);st32(0x101F4B5C,kArena+0xA000);
        story_notebook::ManifestActor seated;seated.id="seated";seated.actor="resident";seated.resident=19;seated.stage="sea";seated.room=1;seated.flag="spoken";
        std::string why;auto hazardous=seated;hazardous.position={-202066,0,320123};assert(!story_actors::load("bad",{hazardous},why));
        assert(story_actors::load("test",{seated},why));story_actors::install([](uint32_t){return std::string{};},[](uint32_t a,void(*h)(Cpu*)){hooks[a]=h;});
        Cpu cpu{};story_actors::native_room_loaded(&cpu,1);assert(created==1);const uint32_t npc=kActor+0x1000;ym1_partner=npc;
        Cpu c{};c.r[3]=npc;c.r[10]=0x12345678;const Cpu input=c;
        hooks.at(0x022F75E4)(&c);assert(c.r[3]==1&&seated_actions==1&&c.r[10]==input.r[10]&&ld32(npc+0x870)==0x022FAB88);
        c=input;hooks.at(0x022F75E4)(&c);assert(c.r[3]==1&&seated_actions==2);
        const uint8_t progress_byte=ld8(kSave+0x644+5);
        for(uint8_t value:{uint8_t(progress_byte&~0x20u),uint8_t(progress_byte|0x20u)}){st8(kSave+0x644+5,value);c=input;hooks.at(0x022F75E4)(&c);assert(c.r[3]==1&&ld8(kSave+0x644+5)==value);}st8(kSave+0x644+5,progress_byte);

        st32(npc+0x870,0xDEAD);c=input;hooks.at(0x022F75E4)(&c);assert(c.r[3]==0&&seated_actions==4);st32(npc+0x870,0x022FAB88);
        c=input;unsigned prior_calls=originals;st8(npc+0xA21,0);hooks.at(0x022F75E4)(&c);assert(originals==prior_calls+1);st8(npc+0xA21,1);
        for(uint32_t addr:{0x022F8344u,0x022F8504u,0x022F9ACCu}){c=input;prior_calls=originals;hooks.at(addr)(&c);assert(originals==prior_calls);assert(c.r[3]==(addr==0x022F8344?0u:addr==0x022F8504?0xFFFFFFFFu:1u));}
        st16(npc+0xF8,1);c=input;hooks.at(0x022F9A48)(&c);assert(c.r[3]==0x2CF3&&ld8(kScratch+0x280)==1);
        c=input;hooks.at(0x022F9464)(&c);assert(c.r[3]==0x10);
        st16(npc+0xF8,1);st32(npc+0x7C0,0x2CF3);st32(npc+0x7C8,700);st8(npc+0x7CC,1);st8(npc+0xA1D,5);st8(npc+0xA1E,11);st8(npc+0xA13,1);st8(npc+0x905,0x77);
        story_actors::state_loaded();const unsigned binds=says;const unsigned calls_before_resume=native_calls;story_mail::state_loaded();story_actors::restore_talk_text();assert(says==binds+1&&native_calls==calls_before_resume);c=input;hooks.at(0x022F8F2C)(&c);assert(c.r[3]==0xBEEF&&says==binds+1&&ld8(npc+0x905)==0x77);
        ym1_message_status=0x13;c=input;hooks.at(0x022FA288)(&c);assert(c.r[3]==1&&ld8(kScratch+0x280)==2&&ld8(npc+0xA0A)==0xFF&&ld8(npc+0xA13)==0&&ld8(npc+0xA1D)==11&&ld16(npc+0x9FE)==20&&ld8(npc+0xA18)==0xFF&&ld8(npc+0xA19)==0xFF&&(ld16(0x1046F0B0+0x52B8)&8));
        const unsigned bases=seated_bases;c=input;hooks.at(0x022FA288)(&c);assert(seated_bases==bases);
        // Completed native cleanup survives restore through a dedicated guest latch, not stale message/status inference.
        story_actors::state_loaded();story_mail::state_loaded();const unsigned terminal_before=native_calls;story_actors::restore_talk_text();assert(native_calls==terminal_before);c=input;hooks.at(0x022F8F2C)(&c);st16(npc+0xF8,0);c=input;hooks.at(0x022F8F2C)(&c);assert(talk_finishes==1&&ld8(kScratch+0x280)==0);c=input;hooks.at(0x022F8F2C)(&c);assert(talk_finishes==1);
        // No terminal latch: stale carrier/manager13 never manufactures completion after restore.
        st16(npc+0xF8,1);st8(kScratch+0x280,0);story_actors::state_loaded();const unsigned old_binds=says;c=input;hooks.at(0x022F8F2C)(&c);assert(says==old_binds);st16(npc+0xF8,0);c=input;hooks.at(0x022F8F2C)(&c);assert(talk_finishes==1);
        // Foreign talk partner cannot reset the global event or pass native retail completion switch.
        st16(npc+0xF8,1);c=input;hooks.at(0x022F9A48)(&c);st16(npc+0xF8,1);ym1_partner=npc+0x5000;st16(0x1046F0B0+0x52B8,0);c=input;hooks.at(0x022FA288)(&c);assert(ld16(0x1046F0B0+0x52B8)==0&&ld8(kScratch+0x280)==0&&ld8(npc+0xA1D)==11&&ld32(npc+0x7C8)==0xFFFFFFFFu&&ld8(npc+0x7CC)==0);ym1_partner=npc;
        // An active restored message without provenance cancels its exact owned event, never unkeyed binds.
        story_mail::fixture_record=false;st8(kScratch+0x280,1);st16(npc+0xF8,1);st32(npc+0x7C0,0x2CF3);st32(npc+0x7C8,700);st8(npc+0x7CC,1);st8(npc+0xA1D,5);ym1_partner=npc;st16(0x1046F0B0+0x52B8,0);
        story_actors::state_loaded();story_mail::state_loaded();story_actors::restore_talk_text();const unsigned invalid_binds=says,invalid_originals=originals;c=input;hooks.at(0x022F8F2C)(&c);
        assert(says==invalid_binds&&originals==invalid_originals&&talk_finishes==1&&ld8(kScratch+0x280)==3&&(ld16(0x1046F0B0+0x52B8)&8)&&ld8(npc+0xA1D)==11);
        c=input;hooks.at(0x022F8F2C)(&c);assert(originals==invalid_originals);st16(npc+0xF8,0);c=input;hooks.at(0x022F8F2C)(&c);assert(ld8(kScratch+0x280)==0&&talk_finishes==1);
        // Identical invalid provenance under a foreign native partner cannot reset the foreign event.
        st8(kScratch+0x280,1);st16(npc+0xF8,1);st32(npc+0x7C0,0x2CF3);st32(npc+0x7C8,700);st8(npc+0xA1D,5);ym1_partner=npc+0x5000;st16(0x1046F0B0+0x52B8,0);story_actors::state_loaded();story_mail::state_loaded();story_actors::restore_talk_text();c=input;hooks.at(0x022F8F2C)(&c);assert(ld16(0x1046F0B0+0x52B8)==0&&says==invalid_binds&&talk_finishes==1);ym1_partner=npc;
        // A genuinely unopened restored seat is free to use normal native getMsg on its next talk.
        st8(kScratch+0x280,0);st32(npc+0x7C8,0xFFFFFFFFu);st8(npc+0xA1D,11);st16(npc+0xF8,0);story_actors::state_loaded();story_mail::state_loaded();story_actors::restore_talk_text();c=input;hooks.at(0x022F8F2C)(&c);st16(npc+0xF8,1);c=input;hooks.at(0x022F9A48)(&c);assert(c.r[3]==0x2CF3&&says==invalid_binds+1);
        // Canonical profile/stage subtype/params and class identity are all required.
        for(uint32_t off:{0xEu,0x2DDu,0xB0u,0xA20u,0xA21u}){const uint32_t old=off==0xE?ld16(npc+off):off==0xB0?ld32(npc+off):ld8(npc+off);if(off==0xE)st16(npc+off,0);else if(off==0xB0)st32(npc+off,2);else st8(npc+off,0);c=input;prior_calls=originals;hooks.at(0x022F9A48)(&c);assert(originals==prior_calls+1);if(off==0xE)st16(npc+off,uint16_t(old));else if(off==0xB0)st32(npc+off,old);else st8(npc+off,uint8_t(old));}
        c=input;prior_calls=originals;hooks.at(0x025D9E64)(&c);assert(c.r[3]==0&&originals==prior_calls);c=input;c.r[4]=npc;hooks.at(0x025BED80)(&c);assert(originals==prior_calls);
        c=input;hooks.at(0x022F82AC)(&c);assert(ld8(kScratch+0x280)==0&&talk_finishes==1&&ld32(kScratch+128)==0xFFFFFFFFu);
        assert(std::memcmp(before.data(),mem::ptr(kSave),before.size())==0);
        std::puts("Ym1 seated initializer, guarded native closure, restore latch, isolation and delete passed");return 0;
    }
    if(argc==2&&std::strcmp(argv[1],"--editor")==0){
        // the world editor's live changes: new actors are created, start-up-only kinds wait for a restart,
        // a refused list changes nothing, removed actors leave the game and come back on undo
        story_notebook::ManifestActor sign;sign.id="notice";sign.actor="sign";sign.stage="sea";sign.room=2;sign.flag="read";
        std::string why;assert(story_actors::load("test",{sign},why));
        story_actors::install([](uint32_t){return std::string{};},[](uint32_t a,void(*h)(Cpu*)){hooks[a]=h;});
        Cpu c{};st8(kArena+0x7000+0x326,2);story_actors::native_room_loaded(&c,2);assert(created==1);
        story_notebook::ManifestActor chest;chest.id="box";chest.actor="chest";chest.stage="sea";chest.room=0;chest.flag="opened";
        story_notebook::ManifestActor pot;pot.id="pot";pot.actor="pot";pot.stage="sea";pot.room=0;pot.broken_flag="pot.broken";
        std::vector<std::string> pending;
        assert(story_actors::editor_update("test",{sign,chest,pot},{"box","pot"},why,&pending));
        assert(pending.size()==1&&pending[0].rfind("pot:",0)==0);
        story_actors::native_room_loaded(&c,0);assert(created==2&&story_actors::actor_address(&c,"test","box")==kActor);
        std::vector<story_notebook::ManifestActor> crowded{sign,chest};std::vector<std::string> ids;
        for(unsigned n=0;n<16;++n){auto more=chest;more.id="box"+std::to_string(n);more.flag="opened"+std::to_string(n);crowded.push_back(more);ids.push_back(more.id);}
        const unsigned before=created;
        assert(!story_actors::editor_update("test",crowded,ids,why)&&why.find("16")!=std::string::npos);
        story_actors::native_room_loaded(&c,0);assert(created==before);
        assert(story_actors::editor_update("test",{chest},{},why));
        // a running mod scene (native or fallback) keeps its actors until it ends
        story_scenes::scenes_active=true;story_actors::service(&c);assert(deleted==0);story_scenes::scenes_active=false;
        story_actors::service(&c);assert(deleted==1);
        live.erase(ld32(kScratch+128));story_actors::service(&c);assert(ld32(kScratch+128)==0xFFFFFFFFu&&!story_actors::actor_address(&c,"test","notice"));
        assert(story_actors::editor_update("test",{sign,chest},{},why));
        story_actors::native_room_loaded(&c,2);assert(created==3&&story_actors::actor_address(&c,"test","notice"));
        std::puts("editor live creation, pending kinds, refusal and removal passed");return 0;
    }
    if(argc==2&&std::strcmp(argv[1],"--editor-left-out")==0){
        // issue #281: a world object the game left out at start-up (its place was not clear: Link's start point) is
        // declared when the editor moves it where it fits, instead of "the model can't serve this spec"; moved to
        // another place that isn't clear, the editor is told what is in the way there
        mod_actor::configure_world_objects("synthetic",[](const std::string&stage,int room){story_world_placement::Index index;index.coverage.push_back({stage,room,true,true,true,{}});
            index.bounds.push_back({stage,room,"start","PLYR:0",{{-100,-100,-100},{100,300,100}}});return index;});
        std::string why;assert(story_actors::register_model("test","ext","unique_ext",1,why));
        st32(0x101F3EE0+0xB6*4,kArena+0x6000);st32(0x101F3EE0+0x44*4,kArena+0x6100);  // the native model actor and raft profiles
        story_notebook::ManifestActor ext;ext.kind="mod";ext.id="ext";ext.model="ext";ext.stage="sea";ext.room=3;ext.behaviour="static";
        ext.collision_shape="box";ext.collision_size={200,200,200};ext.collision_surface="stone";ext.position={0,0,0};
        assert(story_actors::load("test",{ext},why));
        map_guest(0x101D0000,0x10000);st32(0x101D2B1C+0x10,story_props::kClassBytes);  // --editor: the live kinds' native classes, as --editor-empty
        map_guest(0x101C0000,0x10000);map_guest(0x10180000,0x20000);st32(0x101CD034+0x10,story_switches::kSwitchClassBytes);st32(0x1018FFE8+0x10,story_switches::kDoorClassBytes);
        story_actors::editor_live_kinds();
        auto replaced=[](uint32_t){return std::string{};};auto replace=[](uint32_t a,void(*h)(Cpu*)){hooks[a]=h;};
        story_actors::install(replaced,replace);
        Cpu c{};st8(kArena+0x7000+0x326,3);story_actors::native_room_loaded(&c,3);assert(created==0);  // left out at start-up
        // moved, still over the start point: not in the game, with what is in the way
        auto blocked=ext;blocked.position={60,0,0};std::vector<std::string> pending;
        assert(story_actors::editor_update("test",{blocked},{"ext"},why,&pending));
        for(const auto& p:pending)std::printf("pending: %s\n",p.c_str());
        assert(pending.size()==1&&pending[0].rfind("ext: can't stand here (",0)==0&&pending[0].find("start")!=std::string::npos);
        story_actors::native_room_loaded(&c,3);assert(created==0);
        // moved where it fits (rotation and scale unchanged): declared and created there
        auto moved=ext;moved.position={3000,0,0};pending.clear();
        assert(story_actors::editor_update("test",{moved},{"ext"},why,&pending));
        for(const auto& p:pending)std::printf("pending: %s\n",p.c_str());
        assert(pending.empty());
        story_actors::native_room_loaded(&c,3);assert(created==1&&story_actors::actor_address(&c,"test","ext"));
        // moved again: an ordinary change of a declared object from now on
        auto again=moved;again.position={3200,0,0};pending.clear();
        assert(story_actors::editor_update("test",{again},{"ext"},why,&pending)&&pending.empty());
        std::puts("editor: a world object left out at start-up appears where it is moved to fit");return 0;
    }
    if(argc==2&&std::strcmp(argv[1],"--editor-empty")==0){
        // --editor with no package declaring actors at start-up: the machinery installs anyway, so
        // residents, props, switches and doors are live (no "after a restart") and a new resident appears
        assert(!story_actors::declared());
        map_guest(0x101D0000,0x10000);st32(0x101D2B1C+0x10,story_props::kClassBytes);  // the native prop and switch/door classes
        map_guest(0x101C0000,0x10000);map_guest(0x10180000,0x20000);st32(0x101CD034+0x10,story_switches::kSwitchClassBytes);st32(0x1018FFE8+0x10,story_switches::kDoorClassBytes);
        story_actors::editor_live_kinds();
        story_actors::install([](uint32_t){return std::string{};},[](uint32_t a,void(*h)(Cpu*)){hooks[a]=h;});
        assert(!hooks.empty()&&hooks.contains(0x0218B234));
        for(const char* type:{"resident","pot","barrel","crate","switch","door"})assert(story_actors::editor_kind_live(type,false));
        assert(story_actors::editor_kind_live("sign",true));
        story_notebook::ManifestActor resident;resident.id="guide";resident.actor="resident";resident.stage="sea";resident.room=1;resident.resident=1;resident.rotation.y=90;
        std::string why;assert(story_actors::editor_live_capable("late",resident,why));
        auto seated=resident;seated.id="seated";seated.resident=19;assert(story_actors::editor_live_capable("late",seated,why));
        assert(story_actors::editor_update("late",{resident},{"guide"},why));
        Cpu c{};story_actors::native_room_loaded(&c,1);assert(created==1);
        // a drag moves it in place only while the game runs its Execute (the editor's event lets it move);
        // held by the event, it keeps its place and the drag creates it again instead
        const uint32_t at=story_actors::actor_address(&c,"late","guide");assert(at);
        const float x0=u32_as_f32(ld32(at+0x314));
        (void)x0;
        // running (the event lets it move): moved in place, its own Execute does the rest next step
        move_approval=1;assert(story_actors::editor_pose(&c,"late","guide",{500,10,600},{0,90,0})&&u32_as_f32(ld32(at+0x314))==500&&u32_as_f32(ld32(at+0x31C))==600&&held_executes==0);
        // held by the editor's event: its own Execute runs once for its matrices, its record comes back
        // as it was (AI state untouched) with the new place
        const uint32_t profile=kArena+0x90000,methods=kArena+0x90100;st32(at+0x10,profile);st32(at+0xF0,methods);st32(profile+0x10,0x1000);st32(profile+0x14,0);
        st32(at+0x400,7);move_approval=0;
        // its first held pose keeps that Execute (its one-time start), at the pose's place
        assert(story_actors::editor_pose(&c,"late","guide",{700,20,800},{0,90,0})&&held_executes==1);
        assert(ld32(at+0x400)==0xDEAD&&u32_as_f32(ld32(at+0x314))==700&&u32_as_f32(ld32(at+0x31C))==800&&u32_as_f32(ld32(at+0x2EC))==700);
        // the next ones put its record back (AI state untouched) with the new place
        st32(at+0x400,7);
        assert(story_actors::editor_pose(&c,"late","guide",{720,20,820},{0,90,0})&&held_executes==2&&held_drawn_x==500);
        assert(ld32(at+0x400)==7&&u32_as_f32(ld32(at+0x314))==720&&u32_as_f32(ld32(at+0x31C))==820);
        // no readable record: refused (the drag then creates it again)
        st32(at+0x10,0);assert(!story_actors::editor_pose(&c,"late","guide",{710,20,800},{0,90,0})&&held_executes==2);st32(at+0x10,profile);
        std::puts("editor with no declared actors: live kinds installed, a new resident appears at once");return 0;
    }
    if(argc==2&&std::strcmp(argv[1],"--routes")==0){
        // a walking resident: created with the route marker as its route number and given its record;
        // the record names its copy; a deleted copy lets go; a restored copy keeps its record; a resident
        // without a route keeps its fixed parameters
        constexpr uint32_t kRoutes=0x60100000;map_guest(kRoutes,story_resident_route::kMemoryBytes);
        story_resident_routes::set_memory(kRoutes);
        story_notebook::ManifestActor walker;walker.id="walker";walker.actor="resident";walker.stage="sea";walker.room=1;walker.resident=1;walker.position={10,5,20};
        walker.path.points={{200,5,20},{200,5,200}};walker.path.loop=true;walker.path.speed_given=false;walker.path.speed=0;
        std::string why;assert(story_actors::resident_routes_declared()==false);
        assert(story_actors::load("routes",{walker},why));assert(story_actors::resident_routes_declared());
        story_actors::install([](uint32_t){return std::string{};},[](uint32_t a,void(*h)(Cpu*)){hooks[a]=h;});
        assert(story_resident_routes::installed()&&hooks.contains(story_resident_routes::set_route_function())&&hooks.contains(story_resident_routes::walk_function())&&hooks.contains(story_resident_routes::turn_function())&&hooks.contains(story_resident_routes::passed_function()));
        expect_people_subtype=1;expect_people_params=0x00FE0000;expect_people_yaw=0;
        Cpu c{};story_actors::native_room_loaded(&c,1);assert(created==1);
        const uint32_t npc=kActor+0x1000;assert(story_actors::actor_address(&c,"routes","walker")==npc);
        const uint32_t pid=ld32(npc+4);assert(story_resident_routes::owner(0)==pid&&story_resident_routes::slot_of(npc)==0);
        // its creation asks for the marker: it gets its record
        Cpu set{};set.r[3]=npc+0x870;set.r[4]=story_resident_route::kRouteMarker;set.r[5]=1;set.r[6]=1;hooks.at(story_resident_routes::set_route_function())(&set);
        assert(set.r[3]==1&&ld32(npc+0x870)==story_resident_routes::route_address(0)&&ld16(story_resident_routes::route_address(0))==3);
        story_actors::ResidentRoute state;assert(story_actors::resident_route("routes","walker",state)&&state.declared&&state.live&&state.status=="walking");
        // a full state: the record keeps its owner; a changed declaration's record is written again
        story_actors::state_loaded();assert(story_resident_routes::owner(0)==pid&&story_resident_routes::slot_of(npc)==0);
        // the copy goes (room teardown): the record lets go of it
        Cpu del{};del.r[3]=npc;hooks.at(0x022BFF00)(&del);assert(story_resident_routes::owner(0)==0&&story_resident_routes::slot_of(npc)==-1);
        assert(story_actors::resident_route("routes","walker",state)&&state.declared&&!state.live);
        std::puts("resident routes: marker, record, owner, restore and delete passed");return 0;
    }
    if(argc==2&&std::strcmp(argv[1],"--catalogue")==0){
        // the villager catalogue: a non-uo2/um1 character is created with its own subtype and fixed
        // parameters and gets every placed-copy guard; refused characters and places never load
        std::memcpy(mem::ptr(0x1046F0B0+0x5134),"sea",4);
        std::string why;story_notebook::ManifestActor bad;bad.id="stargazer";bad.actor="resident";bad.stage="sea";bad.room=1;
        for(unsigned refused:{11u,16u,17u,18u,23u}){bad.resident=refused;assert(!story_actors::load("cat",{bad},why));}
        bad.resident=6;bad.stage="Ocmera";bad.room=0;assert(!story_actors::load("cat",{bad},why)&&why.find("Ocmera")!=std::string::npos);
        story_notebook::ManifestActor young;young.id="young";young.actor="resident";young.stage="sea";young.room=1;young.resident=10;young.lines="Mod words";young.flag="young-talked";
        expect_people_subtype=10;expect_people_params=0x00FF00FF;expect_people_yaw=0;
        assert(story_actors::load("cat",{young},why));story_actors::set_scratch_memory(kScratch);
        story_actors::install([](uint32_t){return std::string{};},[](uint32_t a,void(*h)(Cpu*)){hooks[a]=h;});
        Cpu c{};story_actors::native_room_loaded(&c,1);assert(created==1);
        const uint32_t npc=kActor+0x1000;assert(ld8(npc+0x905)==10&&ld8(npc+0x2DD)==10&&ld32(npc+0xB0)==0x00FF00FF);
        assert(story_actors::actor_address(&c,"cat","young")==npc);
        // never a retail event order, at either time of day
        st16(npc+0xFC,0x1234);st8(npc+0x910,0);Cpu order{};order.r[3]=npc;hooks.at(0x022C045C)(&order);assert(ld16(npc+0xFC)==0xFFFF&&ld8(npc+0x910)==1);
        // the message is the mod's text
        st16(npc+0xF8,1);Cpu talk{};talk.r[3]=npc;hooks.at(0x022C6C68)(&talk);assert(talk.r[3]==0x2CF3);
        // excluded from retail name searches, photos and event staff
        const unsigned before=originals;Cpu named{};named.r[3]=npc;hooks.at(0x025D9E64)(&named);assert(named.r[3]==0&&originals==before);
        Cpu photo{};photo.r[4]=npc;hooks.at(0x025BED80)(&photo);assert(originals==before);
        Cpu staff{};staff.r[5]=npc;hooks.at(0x02542D88)(&staff);assert(staff.r[3]==0xFFFFFFFFu&&originals==before);
        std::puts("villager catalogue: subtype, fixed parameters, guards and refusals passed");return 0;
    }
    if(argc==2&&std::strcmp(argv[1],"--no-resident")==0){
        story_notebook::ManifestActor sign;sign.id="notice";sign.actor="sign";sign.stage="sea";sign.room=2;sign.flag="read";
        std::string why;assert(story_actors::load("sign-only",{sign},why));
        auto conflict=[](uint32_t address){return address==0x025BED80?std::string("photo-plugin"):std::string{};};
        story_actors::install(conflict,[](uint32_t address,void(*hook)(Cpu*)){hooks[address]=hook;});
        assert(hooks.contains(0x0218B234));
        for(uint32_t address:{0x025BED80u,0x025BEBB8u,0x025BEB4Cu,0x025D9E64u,0x02542A38u,0x02542AF4u,0x02542BE0u,0x02542D88u,0x022C1BBCu,0x022BFF00u})assert(!hooks.contains(address));
        std::puts("non-resident world actors ignore unrelated photo/staff hook conflicts");return 0;
    }
    auto replaced=[](uint32_t){return std::string{};};auto replace=[](uint32_t a,void(*h)(Cpu*)){hooks[a]=h;};
    Cpu idle{};story_actors::service(&idle);assert(native_calls==0);
    story_actors::install(replaced,replace);assert(hooks.empty());assert((extension==std::array<uint8_t,1024>{}));
    story_notebook::ManifestActor chest;chest.id="chest";chest.actor="chest";chest.stage="sea";chest.room=0;chest.flag="opened";
    std::string why;story_notebook::ManifestActor resident;resident.id="guide";resident.actor="resident";resident.stage="sea";resident.room=1;resident.resident=1;resident.rotation.y=90;resident.quest="q";resident.quest_step=1;resident.item="item:token";
    const bool talks_mode=argc==2&&std::strcmp(argv[1],"--talks")==0;
    const bool sign_talks_mode=argc==2&&std::strcmp(argv[1],"--talks-sign")==0;
    if(talks_mode){story_notebook::ManifestTalk first;first.lines="First";first.when_flag="ready";first.then={{story_notebook::StoryAction::Kind::Flag,"first",1}};
        story_notebook::ManifestTalk fallback;fallback.lines="Second";fallback.then={{story_notebook::StoryAction::Kind::Flag,"second",1}};resident.talks={first,fallback};}
    story_notebook::ManifestActor sign;sign.id="sign";sign.actor="sign";sign.stage="sea";sign.room=2;sign.flag="read";sign.time="night";
    if(sign_talks_mode){story_notebook::ManifestTalk first;first.lines="First";first.when_flag="ready";first.then={{story_notebook::StoryAction::Kind::Flag,"first",1}};
        story_notebook::ManifestTalk fallback;fallback.lines="Second";fallback.then={{story_notebook::StoryAction::Kind::Flag,"second",1}};sign.talks={first,fallback};}
    story_notebook::ManifestActor own;own.kind="mod";own.id="post";own.model="postmodel";own.stage="sea";own.room=3;
    assert(story_actors::register_model("test","postmodel","unique_post",1,why));
    assert(!story_actors::register_model("test","othermodel","unique_post",1,why));
    st32(0x101F3EE0+0xB6*4,kArena+0x6000);
    auto rejected_resident=resident;rejected_resident.resident=11;assert(!story_actors::load("bad",{rejected_resident},why));
    assert(story_actors::load("test",{chest,resident,sign,own},why));const unsigned declaration_calls=native_calls;story_actors::ResidentEventDeclaration declaration;
    assert(story_actors::resident_event_declaration("test","guide",declaration)&&declaration.token&&declaration.stage=="sea"&&declaration.room==1&&declaration.subtype==1&&native_calls==declaration_calls);
    const auto declared_token=declaration.token;const auto declared_hash=declaration.fingerprint;declaration.stage="mutated";declaration.fingerprint.fill(0);assert(story_actors::resident_event_declaration("test","guide",declaration)&&declaration.stage=="sea"&&declaration.fingerprint==declared_hash);
    assert(!story_actors::resident_event_declaration("foreign","guide",declaration)&&!declaration.token);assert(!story_actors::resident_event_declaration("test","chest",declaration)&&!declaration.token&&native_calls==declaration_calls);
    assert(story_actors::resident_event_declaration("test","guide",declaration)&&declaration.token==declared_token);
    story_actors::set_scratch_memory(kScratch);story_actors::install(replaced,replace);
    Cpu cpu{};story_actors::native_room_loaded(&cpu,0);assert(created==1&&current_layer==kArena+0x6800&&ld32(kActor+0x2C)==kArena+0x60C0);
    pending=true;story_actors::native_room_loaded(&cpu,0);assert(created==1);pending=false;
    Cpu check{};check.r[3]=kActor;hooks.at(0x024B4AB4)(&check);assert(check.r[3]==0);
    Cpu open{};open.r[3]=kActor;hooks.at(0x024B63A8)(&open);
    assert(story_actors::flag(story_store::book(),"test","opened"));
    check.r[3]=kActor;hooks.at(0x024B4AB4)(&check);assert(check.r[3]==1);
    assert(std::memcmp(before.data(),mem::ptr(kSave),before.size())==0);
    story_actors::state_loaded();assert(story_actors::actor_address(&cpu,"test","chest")==kActor);
    // A vanilla open nested inside a mod open clears the outer mapping for its own setter,
    // then restores it for the outer mod chest's setter.
    assert(story_actors::set_flag(story_store::book(),"test","opened",false));
    st32(kSave+0x798,0);nested_chest=true;open.r[3]=kActor;hooks.at(0x024B63A8)(&open);nested_chest=false;
    assert(ld32(kSave+0x798)==1);
    assert(story_actors::flag(story_store::book(),"test","opened"));
    // An ordinary chest still writes the game's own flag through the unmodified original route.
    st32(kSave+0x798,0);Cpu ordinary{};ordinary.r[3]=kSave+0x798;ordinary.r[4]=0;hooks.at(0x025B8C0C)(&ordinary);assert(ld32(kSave+0x798)==1);
    // Room/stage and mod prerequisites control creation; existing actor ids prevent duplicates.
    quest_state=0;story_actors::native_room_loaded(&cpu,1);assert(created==1);
    quest_state=1;item_count=0;story_actors::native_room_loaded(&cpu,1);assert(created==1);
    item_count=1;std::memcpy(mem::ptr(0x1046F0B0+0x5134),"other",6);story_actors::native_room_loaded(&cpu,1);assert(created==1);
    std::memcpy(mem::ptr(0x1046F0B0+0x5134),"sea",4);story_actors::native_room_loaded(&cpu,1);assert(created==2);
    story_actors::native_room_loaded(&cpu,1);assert(created==2);
    if(talks_mode){
        const uint32_t npc=kActor+0x1000;Cpu talk{};auto b=story_store::book();
        auto start=[&](){st16(npc+0xF8,1);st32(npc+0x7C0,0x2CF3);st32(npc+0x7C8,700);st8(npc+0x7CC,1);st16(npc+0x8F0,6);talk.r[3]=npc;assert(story_actors::people_message(&talk)&&talk.r[3]==0x2CF3);};
        auto finish=[&](){talk.r[3]=npc;hooks.at(0x022C1BBC)(&talk);st16(npc+0xF8,0);talk.r[3]=npc;story_actors::people_execute(&talk,false);};
        start();assert((ld8(kScratch+0x281)>>2)==2); // first matching declaration wins
        assert(story_actors::set_flag(b,"test","ready")); // changes must not replace an active selection
        talk.r[3]=npc;assert(story_actors::people_message(&talk)&&talk.r[3]==0x2CF3);
        assert((ld8(kScratch+0x281)>>2)==2); // repeated ingress is idempotent
        story_actors::state_loaded();story_actors::restore_talk_text();talk.r[3]=npc;assert(story_actors::people_execute(&talk,true));
        finish();assert(story_actors::flag(b,"test","second")&&!story_actors::flag(b,"test","first"));
        start();assert((ld8(kScratch+0x281)>>2)==1);finish();assert(story_actors::flag(b,"test","first"));
        // Restoring an old selection must not reinterpret a modified then list,
        // even when the displayed line remains byte-for-byte identical.
        assert(story_actors::set_flag(b,"test","first",false));start();
        auto edited=resident;edited.talks[0].then={{story_notebook::StoryAction::Kind::Flag,"retarget",1}};
        assert(story_actors::editor_update("test",{sign,edited},{"guide"},why));
        story_actors::state_loaded();story_actors::restore_talk_text();
        assert((ld8(kScratch+0x281)&3)==3);finish();
        assert(!story_actors::flag(b,"test","first")&&!story_actors::flag(b,"test","retarget"));
        talk.r[3]=npc;story_actors::people_execute(&talk,true); // native reset permits a later new talk
        start();assert((ld8(kScratch+0x281)>>2)==1);
        auto reordered=edited;std::swap(reordered.talks[0],reordered.talks[1]);
        assert(story_actors::editor_update("test",{sign,reordered},{"guide"},why));
        story_actors::state_loaded();story_actors::restore_talk_text();finish();
        assert(!story_actors::flag(b,"test","retarget"));

        assert(story_actors::actions("test",{{story_notebook::StoryAction::Kind::QuestDone,"q",1}})&&quest_state==255);
        // "stage": handed to mod_stages with room and point; without a stage hook (or a refusal) the action fails
        assert(!story_actors::actions("test",{{story_notebook::StoryAction::Kind::EnterStage,"sky",1u<<8|2u}}));
        std::string entered;
        story_actors::set_stage_enter([&](const std::string& mod,const std::string& id,unsigned room,unsigned point){
            entered=mod+"/"+id+"/"+std::to_string(room)+"/"+std::to_string(point);return id=="sky";});
        assert(story_actors::actions("test",{{story_notebook::StoryAction::Kind::EnterStage,"sky",1u<<8|2u}})&&entered=="test/sky/1/2");
        assert(!story_actors::actions("test",{{story_notebook::StoryAction::Kind::EnterStage,"cave",0}}));
        story_actors::set_stage_enter(nullptr);
        // a chart from dialogue: given once; a second give (or a finished chart) changes nothing
        assert(story_actors::actions("test",{{story_notebook::StoryAction::Kind::GiveChart,"chart:keeper-map",1}})&&story_chart::given_charts.at("test:keeper-map")==story_chart::kResultOwned);
        story_chart::given_charts["test:keeper-map"]=story_chart::kResultDone;
        assert(story_actors::actions("test",{{story_notebook::StoryAction::Kind::GiveChart,"chart:keeper-map",1}})&&story_chart::given_charts.at("test:keeper-map")==story_chart::kResultDone);
        return 0;
    }
    if(sign_talks_mode){
        st32(kSave+0x44,f32_as_u32(300.0f));story_actors::native_room_loaded(&cpu,2);const uint32_t npc=kActor+0x2000;Cpu call{};auto b=story_store::book();
        assert(story_actors::actor_address(&cpu,"test","sign")==npc);sign_native_fixture=true;st32(0x101F4B5C,kArena+0xD000);st32(0x101B7CB8,1);st16(npc+0xF8,1);call.r[3]=npc;hooks.at(0x0218B234)(&call);assert((ld8(kScratch+0x282)>>2)==2);
        assert(story_actors::set_flag(b,"test","ready"));call.r[3]=npc;hooks.at(0x0218B234)(&call);assert((ld8(kScratch+0x282)>>2)==2);
        assert(sign_status==16&&sign_end_transitions==1&&!story_actors::flag(b,"test","second"));
        call.r[3]=npc;hooks.at(0x0218B234)(&call);assert(ld16(npc+0xF8)==1&&sign_status==16);
        Cpu foreign{};foreign.r[3]=kActor+0x5000;st16(foreign.r[3]+0xF8,1);hooks.at(0x0218B234)(&foreign);assert(sign_end_transitions==1&&!story_actors::flag(b,"test","second"));
        story_actors::state_loaded();story_actors::restore_talk_text();sign_a_pressed=true;call.r[3]=npc;hooks.at(0x0218B234)(&call);
        assert(sign_status==19&&ld16(npc+0xF8)==0);const unsigned finished=talk_finishes;call.r[3]=npc;hooks.at(0x0218B234)(&call);assert(talk_finishes==finished);
        assert(story_actors::flag(b,"test","second")&&!story_actors::flag(b,"test","first"));return 0;
    }
    // Pure native-event resident identity uses committed declaration token,
    // current guest PID and bounded native class/stage/room identity only.
    // Restored lease config is independently validated by the native arena.
    map_guest(0x101C0000,0x10000);st16(0x101C4CD0+8,0x175);st32(0x101C4CD0+0x10,0x920);
    st32(0x1047E6C4,77);story_actors::native_room_loaded(&cpu,1);
    const uint32_t event_npc=kActor+0x1000,event_pid=ld32(event_npc+4),event_scene=kArena+0x6200;
    const uint32_t root_layer=0x1048A9BC;
    st32(event_scene+4,1001);st32(kArena+0x62C0+0x18,event_scene);st32(event_scene+0x2C,root_layer);
    st32(root_layer+0xC,0);st32(root_layer+0x18,0);
    story_actors::ResidentEventIdentity event_identity;unsigned pure_calls=native_calls;
    assert(story_actors::resident_event_identity("test","guide",event_npc,event_pid,event_identity));assert(native_calls==pure_calls&&event_identity.token&&event_identity.stage_pid==77&&event_identity.room_pid==1001&&event_identity.actor==event_npc);
    st32(event_npc+0x2C,root_layer);assert(!story_actors::resident_event_identity("test","guide",event_npc,event_pid,event_identity));st32(event_npc+0x2C,kArena+0x62C0);
    st32(root_layer+0x18,event_scene);assert(!story_actors::resident_event_identity("test","guide",event_npc,event_pid,event_identity));st32(root_layer+0x18,0);
    st32(root_layer+0xC,1);assert(!story_actors::resident_event_identity("test","guide",event_npc,event_pid,event_identity));st32(root_layer+0xC,0);
    st32(event_scene+0x2C,0);assert(!story_actors::resident_event_identity("test","guide",event_npc,event_pid,event_identity));st32(event_scene+0x2C,root_layer);
    st32(event_scene+4,1002);assert(!story_actors::resident_event_identity("test","guide",event_npc,event_pid,event_identity));st32(event_scene+4,1001);
    assert(story_actors::resident_event_identity("test","guide",event_npc,event_pid,event_identity));
    story_actors::ResidentEventIdentity quiet_identity;
    assert(story_actors::resident_event_identity_quiet("test","guide",event_npc,event_pid,quiet_identity)&&quiet_identity.token==event_identity.token);
    assert(!story_actors::resident_event_identity_quiet("foreign","guide",event_npc,event_pid,quiet_identity)&&!quiet_identity.token);
    // The placed-process producer is pure host/scratch data: it never runs
    // native Search and distinguishes unplaced from a malformed PID row.
    story_actors::ResidentEventProcess placed;
    const Cpu placed_cpu=cpu;const unsigned placed_calls=native_calls;
    assert(story_actors::resident_event_process("test","guide",placed));
    assert(placed.presence==story_actors::ResidentEventProcess::Presence::Present&&placed.pid==event_pid);
    assert(placed.pid_slot==kScratch+132&&placed.identity_row==kScratch+story_actors::kIdentityScratchOffset+40&&placed.scratch_window==kScratch+0x300);
    assert(placed.declaration.token==event_identity.token&&placed.declaration.fingerprint==event_identity.fingerprint);
    assert(!std::memcmp(placed.identity_bytes.data(),mem::ptr(placed.identity_row),40));
    assert(!std::memcmp(placed.scratch_bytes.data(),mem::ptr(kScratch+0x300),0x200));
    st32(placed.pid_slot,event_pid+1);assert(!story_actors::resident_event_process("test","guide",placed)&&placed.pid==0);st32(kScratch+132,event_pid);
    const uint32_t placed_row=kScratch+story_actors::kIdentityScratchOffset+40;
    const auto placed_magic=ld32(placed_row);st32(placed_row,0);assert(!story_actors::resident_event_process("test","guide",placed)&&placed.pid==0);st32(placed_row,placed_magic);
    assert(!story_actors::resident_event_process("foreign","guide",placed));
    assert(!story_actors::resident_event_process("test","chest",placed));
    // state_loaded obtains the unplaced host sentinel from the actual saved
    // PID slot. A historical authentic REV1 row remains valid after deletion.
    st32(kScratch+132,0xFFFFFFFFu);story_actors::state_loaded();
    assert(story_actors::resident_event_process("test","guide",placed)&&placed.presence==story_actors::ResidentEventProcess::Presence::Absent&&placed.pid==0xFFFFFFFFu);
    st32(kScratch+132,event_pid);story_actors::state_loaded();
    assert(story_actors::resident_event_process("test","guide",placed)&&placed.presence==story_actors::ResidentEventProcess::Presence::Present);
    assert(native_calls==placed_calls&&!std::memcmp(&cpu,&placed_cpu,sizeof cpu));
    const Cpu query_before=cpu;st32(kScratch+0x40,0xAABBCCDD);assert(story_actors::query_resident_event_address(&cpu,"test","guide")==event_npc&&ld32(kScratch+0x40)==0xAABBCCDD&&!std::memcmp(&cpu,&query_before,sizeof cpu));
    pending=true;assert(story_actors::query_resident_event_address(&cpu,"test","guide")==0&&ld32(kScratch+0x40)==0xAABBCCDD&&!std::memcmp(&cpu,&query_before,sizeof cpu));pending=false;
    throw_search=true;bool query_threw=false;try{story_actors::query_resident_event_address(&cpu,"test","guide");}catch(const std::runtime_error&){query_threw=true;}throw_search=false;assert(query_threw&&ld32(kScratch+0x40)==0xAABBCCDD&&!std::memcmp(&cpu,&query_before,sizeof cpu));
    pure_calls=native_calls;
    const auto event_token=event_identity.token;const uint32_t event_row=kScratch+story_actors::kIdentityScratchOffset+40;
    std::array<uint8_t,40> event_record{};std::memcpy(event_record.data(),mem::ptr(event_row),40);
    for(unsigned fault=0;fault<16;++fault){const uint32_t old_pid=ld32(event_npc+4),old_stage=ld32(0x1047E6C4);const uint8_t old_room=ld8(event_npc+0x326);
        if(fault==0)st32(event_npc+4,999);if(fault==1)st16(event_npc+0xE,0x123);if(fault==2)st32(event_npc+0xB0,0);if(fault==3)st8(event_npc+0x2DD,9);if(fault==4)st8(event_npc+0x905,9);if(fault==5)st8(event_npc+0x326,2);if(fault==6)st32(0x1047E6C4,78);if(fault==7)st32(kScratch+132,event_pid+1);if(fault==8)st32(event_npc+0x10,0);if(fault==9)st8(event_scene+0x5C,1);if(fault==10)st32(event_npc+0x2C,0xFFFFFFFC);if(fault==11)st32(event_npc+0xB4,0);if(fault==12)st32(0x101C4CD0+0x10,0xA24);if(fault==13)st32(event_row+4,event_token+1);if(fault==14)st8(event_row+8,uint8_t(ld8(event_row+8)^1));if(fault==15)std::memset(mem::ptr(event_row),0,40);
        assert(!story_actors::resident_event_identity("test","guide",event_npc,event_pid,event_identity)&&!event_identity.token&&native_calls==pure_calls);
        if(fault==15){Cpu legacy{};legacy.r[3]=event_npc;assert(story_actors::people_next_message(&legacy)&&legacy.r[3]==0x10);}
        st32(event_npc+4,old_pid);st16(event_npc+0xE,0x175);st32(event_npc+0xB0,0x00FF0000);st8(event_npc+0x2DD,1);st8(event_npc+0x905,1);st8(event_npc+0x326,old_room);st32(0x1047E6C4,old_stage);st8(event_scene+0x5C,0);st32(event_npc+0x2C,kArena+0x62C0);st32(kScratch+132,event_pid);st32(event_npc+0x10,0x101C4CD0);st32(event_npc+0xB4,0x100209C4);st32(0x101C4CD0+0x10,0x920);std::memcpy(mem::ptr(event_row),event_record.data(),40);
    }
    assert(!story_actors::resident_event_identity("foreign","guide",event_npc,event_pid,event_identity));assert(!story_actors::resident_event_identity("test","guide",0xFFFFFFFC,event_pid,event_identity));
    story_actors::state_loaded();const unsigned after_rebind=native_calls;assert(story_actors::resident_event_identity("test","guide",event_npc,event_pid,event_identity)&&event_identity.token==event_token&&native_calls==after_rebind);
    // Resident execution strips private retail callbacks and preserves CPU return.
    const uint8_t event_before=ld8(kSave+0x644),memory_before=ld8(kSave+0x798);
    Cpu execute{};execute.r[3]=kActor+0x1000;hooks.at(0x022C36D0)(&execute);
    assert(execute.r[3]==0xBEEF);assert(ld8(kSave+0x644)==event_before&&ld8(kSave+0x798)==memory_before);
    assert(extension[100]==0xB7&&ld8(kSave+0x20)==0x42);
    Cpu guarded{};guarded.r[3]=kActor+0x1000;
    unsigned calls_before=originals;hooks.at(0x022C2958)(&guarded);assert(originals==calls_before&&ld8(kSave+0x644)==event_before);
    hooks.at(0x022C045C)(&guarded);assert(ld16(guarded.r[3]+0xFC)==0xFFFF&&ld8(guarded.r[3]+0x910)==1);
    st8(guarded.r[3]+0x901,9);st16(guarded.r[3]+0x8F2,0xFFFF);hooks.at(0x022C2A04)(&guarded);
    assert(ld8(guarded.r[3]+0x901)==0&&ld16(guarded.r[3]+0xFA)==0xFFDF);
    // PID alone never authorizes People offsets or retail cohort exclusion.
    const uint32_t npc=kActor+0x1000;Cpu gate{};gate.r[3]=npc;
    st16(npc+0xE,0x123);assert(!story_actors::people_execute(&gate,true));st16(npc+0xE,0x175);
    st8(npc+0x2DD,9);assert(!story_actors::people_execute(&gate,true));st8(npc+0x2DD,1);
    st8(npc+0x905,9);assert(!story_actors::people_execute(&gate,true));st8(npc+0x905,1);
    for(uint32_t hook:{0x025D9E64u,0x02542A38u}){
        gate.r[3]=npc;const unsigned prior=originals;hooks.at(hook)(&gate);assert(gate.r[3]==0&&originals==prior);
        gate.r[3]=kActor+0x5000;hooks.at(hook)(&gate);assert(originals==prior+1);
        // The predicate rejects first owned candidate; iterator can continue to retail second.
        gate.r[3]=npc;hooks.at(hook)(&gate);assert(gate.r[3]==0);gate.r[3]=kActor+0x5000;hooks.at(hook)(&gate);assert(gate.r[3]==kActor+0x5000);
    }
    const uint32_t name=kArena+0x9000,query=name+32,snapshot=name+64;
    std::memcpy(mem::ptr(name),"Uo2",4);st32(query,name);
    for(uint32_t hook:{0x02542AF4u,0x02542BE0u}){gate.r[3]=npc;gate.r[4]=hook==0x02542AF4?query:name;unsigned prior=originals;hooks.at(hook)(&gate);assert(originals==prior&&gate.r[3]==0);
        std::memcpy(mem::ptr(name),"ALL",4);gate.r[3]=npc;hooks.at(hook)(&gate);assert(originals==prior+1);std::memcpy(mem::ptr(name),"Uo2",4);
    }
    gate.r[3]=name;gate.r[5]=npc;unsigned prior=originals;hooks.at(0x02542D88)(&gate);assert(gate.r[3]==0xFFFFFFFFu&&originals==prior);
    gate.r[5]=0;hooks.at(0x02542D88)(&gate);assert(originals==prior+1);
    for(uint32_t hook:{0x025BED80u,0x025BEBB8u}){gate.r[4]=npc;prior=originals;hooks.at(hook)(&gate);assert(originals==prior);gate.r[4]=kActor+0x5000;hooks.at(hook)(&gate);assert(originals==prior+1);}
    // Draw ingress trace is bounded, and does not alter the callback CPU.
    gate.r[4]=npc;const Cpu photo_cpu=gate;
    for(unsigned draw=0;draw<4096;++draw)hooks.at(0x025BED80)(&gate);
    assert(std::memcmp(&gate,&photo_cpu,sizeof(Cpu))==0);
    st32(snapshot+0x14,101);gate.r[3]=snapshot;prior=originals;hooks.at(0x025BEB4C)(&gate);assert(originals==prior);st32(snapshot+0x14,999);hooks.at(0x025BEB4C)(&gate);assert(originals==prior+1);
    // Restore open own carrier without getMsg; bind before execute, and deliver only terminal closure.
    st16(npc+0xF8,1);st32(npc+0x7C0,0x2CF3);st32(npc+0x7C8,700);st8(npc+0x7CC,1);st16(npc+0x8F0,6);
    story_actors::state_loaded();gate.r[3]=npc;unsigned said=says;assert(story_actors::people_execute(&gate,true)&&says==said+1);
    assert(story_actors::people_execute(&gate,true)&&says==said+1);
    gate.r[3]=npc;hooks.at(0x022C1BBC)(&gate);assert(gate.r[3]==0x12);st16(npc+0xF8,0);gate.r[3]=npc;
    assert(story_actors::people_execute(&gate,false)&&talk_finishes==1);story_actors::people_execute(&gate,false);assert(talk_finishes==1);
    // Repeated loads can rebind the exact terminal snapshot; each successful timeline closes once.
    for(unsigned load=0;load<2;++load){st16(npc+0xF8,1);st32(npc+0x7C0,0x2CF3);st32(npc+0x7C8,0xFFFFFFFFu);st16(npc+0x8F0,0x12);story_actors::state_loaded();gate.r[3]=npc;story_actors::people_execute(&gate,true);st16(npc+0xF8,0);story_actors::people_execute(&gate,false);assert(talk_finishes==2+load);}
    // A restored pre-opening/foreign carrier or premature cancellation never triggers actions.
    for(uint32_t carrier:{0x2CF3u,0x999u}){st16(npc+0xF8,1);st32(npc+0x7C0,carrier);st32(npc+0x7C8,700);st16(npc+0x8F0,6);story_actors::state_loaded();gate.r[3]=npc;story_actors::people_execute(&gate,true);st16(npc+0xF8,0);story_actors::people_execute(&gate,false);assert(talk_finishes==3);}
    // Delete and room drain discard the host override and never complete a cancelled dialogue.
    gate.r[3]=npc;assert(story_actors::people_message(&gate));gate.r[3]=npc;unsigned cleared=unsays;
    hooks.at(0x022BFF00)(&gate);assert(unsays==cleared+1&&talk_finishes==3);
    // Restore known PID only, as native snapshots do, after synthetic deletion.
    st32(kScratch+128+4,101);story_actors::state_loaded();st16(npc+0xF8,1);st32(npc+0x7C0,0x2CF3);st32(npc+0x7C8,700);gate.r[3]=npc;story_actors::people_execute(&gate,true);
    const uint32_t room_scene=kArena+0x6C00;st32(kArena+0x62C0+0x18,room_scene);st8(room_scene+0x5C,1);cleared=unsays;story_actors::people_execute(&gate,true);assert(unsays==cleared+1&&talk_finishes==3);st8(room_scene+0x5C,0);
    // Missing native PID cancels even before native execute can run again.
    gate.r[3]=npc;assert(story_actors::people_message(&gate));cleared=unsays;pending=true;story_actors::service(&cpu);pending=false;assert(unsays==cleared+1&&talk_finishes==3);
    assert(!hooks.contains(0x02542C9C)&&!hooks.contains(0x02542D74)&&!hooks.contains(0x025D5218)&&!hooks.contains(0x025E121C));
    Cpu ordinary_resident{};ordinary_resident.r[3]=kActor+0x3000;
    assert(!story_actors::people_execute(&ordinary_resident,true));
    hooks.at(0x022C2958)(&ordinary_resident);assert(ld8(kSave+0x644)==0xEF);
    st32(kSave+0x44,f32_as_u32(180.0f));story_actors::native_room_loaded(&cpu,2);assert(created==2);
    st32(kSave+0x44,f32_as_u32(300.0f));story_actors::native_room_loaded(&cpu,2);assert(created==3);assert(story_actors::actor_address(&cpu,"test","sign")==kActor+0x2000);
    // Reconcile the active native room only after its room setup readiness marker exists.
    st32(0x1046F0B0+0x5B34,kArena+0x7000);st8(kArena+0x7000+0x326,1);
    quest_state=0;story_actors::service(&cpu);assert(deleted==1&&created==3);
    story_actors::service(&cpu);assert(deleted==1); // native deletion is asynchronous
    quest_state=1;story_actors::service(&cpu);assert(created==3); // wait for queued deletion
    live.erase(101);story_actors::service(&cpu);assert(created==4);
    pending=true;quest_state=0;story_actors::service(&cpu);assert(deleted==1&&created==4);
    pending=false;story_actors::service(&cpu);assert(deleted==2); // create materialized: now remove
    live.erase(101);story_actors::service(&cpu);assert(created==4);
    quest_state=1;story_actors::service(&cpu);assert(created==5);
    // A state without readiness for this room must not race native room actor setup.
    st8(kScratch+0x308,uint8_t(ld8(kScratch+0x308)&~2u));quest_state=0;
    story_actors::service(&cpu);assert(deleted==2);
    story_actors::native_room_loaded(&cpu,1);story_actors::service(&cpu);assert(deleted==3);
    live.erase(101);quest_state=1;story_actors::service(&cpu);assert(created==6);
    item_count=0;story_actors::service(&cpu);assert(deleted==4);live.erase(101);item_count=1;story_actors::service(&cpu);assert(created==7);
    st8(kArena+0x7000+0x326,2);st32(kSave+0x44,f32_as_u32(180));story_actors::service(&cpu);assert(deleted==5);
    // Flag names from scene actions and actor actions share a collision-checked registry.
    std::map<uint16_t,std::string> candidates;std::string first,second;
    for(unsigned i=0;i<100000&&second.empty();++i){const std::string id="flag"+std::to_string(i);const auto key=story_actors::flag_key("collision",id);auto [it,inserted]=candidates.emplace(key,id);if(!inserted){first=it->second;second=id;}}
    assert(!second.empty());assert(story_actors::register_flags("collision",{first},why));
    assert(!story_actors::register_flags("collision",{second},why)&&why.find("collision")!=std::string::npos);
    auto colliding=sign;colliding.id="colliding";colliding.flag="unique";colliding.then={{story_notebook::StoryAction::Kind::Flag,second,1}};
    assert(!story_actors::load("collision",{colliding},why));
    assert(story_actors::register_flags("collision",{first},why));
    const unsigned prior_created=created;room_available=false;story_actors::native_room_loaded(&cpu,3);assert(created==prior_created);room_available=true;st8(kArena+0x6600+0x5C,1);story_actors::native_room_loaded(&cpu,3);assert(created==prior_created);st8(kArena+0x6600+0x5C,0);
    st32(kArena+0x6600+0x2C,kArena+0x6800);st32(kArena+0x6800+0x18,kArena+0x6A00);st8(kArena+0x6A00+0x5C,1);story_actors::native_room_loaded(&cpu,3);assert(created==prior_created);st8(kArena+0x6A00+0x5C,0);
    story_actors::native_room_loaded(&cpu,3);assert(created==prior_created+1);
    assert(story_actors::actor_address(&cpu,"test","post")==kActor+0x3000);
    story_actors::state_loaded();assert(story_actors::actor_address(&cpu,"test","post")==kActor+0x3000);
    assert(!story_actors::register_model("test","late","late_archive",1,why));
    // Failed native creation stays bounded while service polls the same room, then retries on real room setup.
    live.erase(103);st8(kScratch+story_actors::kModScratchOffset+256,1);const unsigned failed_count=created;
    story_actors::room_loaded(&cpu,3);story_actors::room_loaded(&cpu,3);assert(created==failed_count);
    cpu.r[5]=3;hooks.at(0x025C2E8C)(&cpu);assert(created==failed_count+1);
    // Old readiness bits cannot authorize placement in a newly created room or stage process.
    live.erase(103);const unsigned epoch_count=created;st32(0x1047E8F0+3*0x22C,2003);
    story_actors::room_loaded(&cpu,3);assert(created==epoch_count);story_actors::native_room_loaded(&cpu,3);assert(created==epoch_count+1);
    live.erase(103);st32(0x1047E6C4,99);story_actors::room_loaded(&cpu,3);assert(created==epoch_count+1);
    story_actors::native_room_loaded(&cpu,3);assert(created==epoch_count+2);
    // Owned native execute paths must tolerate the frame where native teardown has already removed Link.
    st32(0x1046F0B0+0x5B34,0);const unsigned teardown_originals=originals;
    Cpu teardown{};teardown.r[3]=kActor+0x2000;hooks.at(0x0218B234)(&teardown);assert(teardown.r[3]==1&&originals==teardown_originals);
    teardown.r[3]=kActor+0x1000;hooks.at(0x022C36D0)(&teardown);assert(teardown.r[3]==1&&originals==teardown_originals);
    teardown.r[3]=kActor+0x5000;hooks.at(0x0218B234)(&teardown);assert(originals==teardown_originals+1);
    if(const char* enabled=std::getenv("WWHD_NATIVE_ACTOR_TRACE");enabled&&std::strcmp(enabled,"1")==0){
        assert(!native_trace_records.empty()&&native_trace_records.size()<=1024);
        for(const auto* event:{"blocked-photo-actor","blocked-name","blocked-staff-candidate","restore-text-rebind","terminal-12","completed","delete","missing-pid"}){
            bool found=false;for(const auto& line:native_trace_records)if(line.find(event)!=std::string::npos)found=true;assert(found);
        }
        unsigned photos=0;for(const auto& line:native_trace_records)if(line.find("blocked-photo")!=std::string::npos)++photos;assert(photos<=25);
    }else assert(native_trace_records.empty());
    std::puts("actor runtime spawn, conditions, CPU return, retail event prevention, chest isolation and state restore passed");
}
