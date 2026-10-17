#include "story_actors.h"
#include "mod_actor.h"
#include "story_props.h"
#include "story_switches.h"
#include "story_enemies.h"
#include "story_enemy_children.h"
#include "story_enemy_projectiles.h"
#include "native_actor_carry.h"
#include "story_actor_identity.h"
#include "actor_model_cache.h"
#include "mod_actor_shadow_proof.h"
#include "story_items.h"
#include "story_notebook.h"
#include "story_mail.h"
#include "story_store.h"
#include "story_scenes.h"
#include "story_treasure.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <map>
#include <set>

namespace story_actors {
static_assert(kPropsScratchOffset>=kModScratchOffset+mod_actor::kScratchBytes);
static_assert(kIdentityScratchOffset>=kPropsScratchOffset+0x100);
static_assert(kScratchBytes>=kIdentityScratchOffset+128*identity::kRecordBytes);
std::function<void()> mod_talked(const std::string& mod,const std::string& id);
namespace {
using story_notebook::ManifestActor;
struct Actor {std::string mod; ManifestActor spec; uint32_t process=0xFFFFFFFFu; bool deleting=false; uint32_t mod_token=0; bool restore_talk=false;uint32_t prop_token=0,switch_token=0,switch_source=0,enemy_token=0;story_items::EnemyMarkerHandle combat_marker{};story_items::PropMarkerHandle carry_marker=0;identity::Fingerprint fingerprint{};bool restore_quarantined=false;bool respawn=false;bool removed=false;};
std::vector<Actor> g_actors;
struct ModelResource {std::string archive;int index;story_notebook::WorldPoint minimum,maximum;bool skinned=false;std::vector<std::string> clips;mod_actor::ModelRig rig;};
std::map<std::pair<std::string,std::string>,ModelResource> g_models;
std::map<uint16_t,std::pair<std::string,std::string>> g_flag_names;

void(*g_original)(Cpu*,uint32_t)=nullptr;
uint32_t(*g_call)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr;
uint32_t g_scratch=0; bool g_installed=false,g_resident_hooks=false,g_ym1_hooks=false;
std::function<bool(const std::string&,const std::string&)> g_scene;
const uint32_t kRoom=GC(0x025C2E8C),kCreate=GC(0x025D5834),kSearch=GC(0x025D54C4),kSearchName=GC(0x025C109C);
const uint32_t kPlay=GC(0x025200D4),kDelete=GC(0x025D57E0);
const uint32_t kStage=GD(0x1046F0B0)+0x5134,kSave=GD(0x101F84DC);
const uint32_t kChestCheck=GC(0x024B4AB4),kChestOpen=GC(0x024B63A8),kOnBox=GC(0x025B8C0C);
const uint32_t kSignExecute=GC(0x0218B234),kSignDelete=GC(0x0218C824);
const uint32_t kPeopleMsg=GC(0x022C6C68),kPeopleNext=GC(0x022C6108),kPeopleExecute=GC(0x022C36D0);
const uint32_t kPeopleTalk=GC(0x022C1BBC),kPeopleDelete=GC(0x022BFF00);
const uint32_t kPhotoActor=GC(0x025BED80),kPhotoPosition=GC(0x025BEBB8),kPhotoSnapshot=GC(0x025BEB4C);
const uint32_t kNamedCandidate=GC(0x025D9E64),kStaffCandidate=GC(0x02542A38),kExtraOn=GC(0x02542AF4),kExtraOff=GC(0x02542BE0),kStaffId=GC(0x02542D88);
const uint32_t kPeopleCheckOrder=GC(0x022C045C),kPeopleEventMove=GC(0x022C2958),kPeopleEventOrder=GC(0x022C2A04);
constexpr uint32_t kCarrier=0x2CF3,kYm1=19,kTalkLatchOffset=0x280;
static_assert(kTalkLatchOffset+128<=0x300); // Native PID slots precede this; room readiness follows it.
const uint32_t kYm1Init=GC(0x022F75E4),kYm1SetAction=GC(0x022F7388),kYm1Pmf=GD(0x10023418);
const uint32_t kYm1Msg=GC(0x022F9A48),kYm1Next=GC(0x022F9464),kYm1Talk=GC(0x022FA288),kYm1Execute=GC(0x022F8F2C),kYm1Delete=GC(0x022F82AC);
const uint32_t kYm1Demo=GC(0x022F8344),kYm1Staff=GC(0x022F8504),kYm1CheckTalk=GC(0x022F9ACC);
Actor* g_opening=nullptr;
struct Talk {uint32_t npc=0; Actor* actor=nullptr;std::u16string text;bool started=false;bool terminal=false;};
Talk g_talk;
bool enemy_kind(const ManifestActor& a,story_enemies::Kind& kind){if(a.actor=="chuchu")kind=story_enemies::Kind::ChuChu;else if(a.actor=="bokoblin")kind=story_enemies::Kind::Bokoblin;else if(a.actor=="moblin")kind=story_enemies::Kind::Moblin;else if(a.actor=="keese")kind=story_enemies::Kind::Keese;else return false;return true;}
bool enemies_declared(){return std::any_of(g_actors.begin(),g_actors.end(),[](const Actor&a){return a.enemy_token!=0;});}
bool combat_declared(){return enemies_declared()||std::any_of(g_actors.begin(),g_actors.end(),[](const Actor&a){return a.spec.kind=="mod"&&a.spec.combat_enabled;});}
bool pure_link(uint32_t& actor,uint32_t& pid){actor=ld32(GD(0x1046F0B0)+0x5B34);if(actor<mem::kMem2Start||actor>mem::kMem2End-0x8260u||(actor&3)||ld16(actor+0xE)!=0xA8)return false;pid=ld32(actor+4);return pid&&pid!=0xFFFFFFFFu;}
void refresh_link(){if(!combat_declared())return;uint32_t actor=0,pid=0;if(pure_link(actor,pid))story_enemy_ownership::set_link(actor,pid);}
// Typed child routes passed source and real module composition contracts.
// Native per-weapon/type/mode acceptance remains a separate proof gate.
constexpr bool kEnemyChildRoutesReady=true;
bool configure_enemy_declarations(){return story_enemies::configure_children({story_enemy_children::enter,story_enemy_children::leave,story_enemy_children::ready,story_enemy_children::retire},kEnemyChildRoutesReady);}
Actor* find(uint32_t address) {
    if(!address||g_actors.empty())return nullptr;
    const uint32_t id=ld32(address+4);
    for(auto& a:g_actors)if(a.process==id){
        if(story_props::is_prop(a.spec)&&(ld16(address+0xE)!=story_props::kProfile||ld32(address+0xB0)!=story_props::parameters(a.spec)||ld8(address+0x2DD)!=0xFF))return nullptr;
        if(a.enemy_token){story_enemies::Kind kind;if(!enemy_kind(a.spec,kind)||ld16(address+0xE)!=story_enemy_ownership::descriptor(kind).profile||ld32(address+0xB0)!=story_enemies::parameters(kind)||ld8(address+0x2DD)!=0xFF)return nullptr;}
        if(a.switch_token&&(ld16(address+0xE)!=(a.spec.actor=="switch"?story_switches::kSwitchProfile:story_switches::kDoorProfile)||ld32(address+0xB0)!=story_switches::parameters(a.spec.actor=="switch"?story_switches::Kind::Switch:story_switches::Kind::Door)||ld8(address+0x2DD)!=0xFF))return nullptr;
        if(a.spec.actor=="resident"){const bool seated=a.spec.resident==kYm1;if(ld16(address+0xE)!=(seated?0x13B:0x175)||ld8(address+0x2DD)!=(seated?0xFF:a.spec.resident))return nullptr;if(seated&&ld32(address+0xB0)!=1)return nullptr;}
        return &a;
    }
    return nullptr;
}
Actor* people(uint32_t address) {
    Actor* a=find(address);
    return a&&a->spec.actor=="resident"&&a->spec.resident!=kYm1&&ld8(address+0x905)==a->spec.resident?a:nullptr;
}
Actor* ym1(uint32_t address){Actor* a=find(address);return a&&a->spec.actor=="resident"&&a->spec.resident==kYm1&&ld8(address+0xA20)==1&&ld8(address+0xA21)==1?a:nullptr;}
uint32_t talk_latch_address(const Actor* a){return g_scratch+kTalkLatchOffset+uint32_t(a-g_actors.data());}
void talk_latch(const Actor* a,uint8_t value){if(a&&a->spec.resident==kYm1&&g_scratch)st8(talk_latch_address(a),value);}
uint8_t talk_latch(const Actor* a){return a&&a->spec.resident==kYm1&&g_scratch?ld8(talk_latch_address(a)):0;}
bool owned_resident(uint32_t address){Actor* a=find(address);return a&&a->spec.actor=="resident";}
// Host-only opt-in diagnostics. Repeated draw/search ingress cannot consume the lifecycle reserve.
void native_trace(Actor* a,uint32_t npc,const char* event,unsigned category=4){
    const char* enabled=std::getenv("WWHD_NATIVE_ACTOR_TRACE");if(!enabled||std::strcmp(enabled,"1")!=0||!a)return;
    static unsigned ingress_total=0,lifecycle_total=0;
    static std::map<std::pair<uint32_t,unsigned>,unsigned> counts;
    if(category<4){const unsigned n=++counts[{a->process,category}];if((n>4&&n%256)||ingress_total>=512)return;++ingress_total;}
    else {if(lifecycle_total>=512)return;++lifecycle_total;}
    LOG("[native_actor] %s:%s event=%s pid=%08X actor=%08X profile=%04X subtype=%u expected=%u",a->mod.c_str(),a->spec.id.c_str(),event,a->process,npc,npc?unsigned(ld16(npc+0xE)):0,npc?unsigned(ld8(npc+0x2DD)):255,a->spec.resident);
}
void cancel_talk(const char* reason="cancel",bool native_valid=true,bool keep_latch=false){if(g_talk.actor){if(!keep_latch)talk_latch(g_talk.actor,0);native_trace(g_talk.actor,native_valid?g_talk.npc:0,reason);if(!keep_latch)story_mail::unsay();}g_talk={};}
void bind_talk(Actor* a,uint32_t npc,bool terminal=false){
    cancel_talk();g_talk.npc=npc;g_talk.actor=a;g_talk.started=true;g_talk.terminal=terminal;
    story_mail::append_utf16(a->spec.lines.empty()?"Hello.":a->spec.lines,true,g_talk.text);
    if(ld16(npc+0xF8)==1)story_mail::say_owned(kCarrier,&g_talk.text,{npc,a->process,uint32_t(a-g_actors.data())+1,ld32(GD(0x1046F0B0)+0x529C)});
    else story_mail::say(kCarrier,&g_talk.text);talk_latch(a,terminal?2:1);
}
bool eligible(const Actor& a) {
    if(a.removed)return false;  // removed by the world editor (deleted from the game, kept for undo)
    if(a.spec.combat_enabled&&story_items::enemy_defeated(a.combat_marker))return false;
    if(a.spec.carry_break_on_impact&&story_items::prop_broken(a.carry_marker))return false;
    if(!a.spec.quest.empty()&&story_notebook::state(a.mod,a.spec.quest)!=int32_t(a.spec.quest_step))return false;
    if(!a.spec.item.empty()&&story_items::count(a.mod,a.spec.item.substr(5))<=0)return false;
    if(!a.spec.time.empty()) {
        const uint32_t save=ld32(kSave);if(!save)return false;
        const float hour=u32_as_f32(ld32(save+0x44))/15.0f;
        const bool day=hour>=6&&hour<18;
        if(day!=(a.spec.time=="day"))return false;
    }
    return true;
}
void room_hook(Cpu* c) {const int room=int32_t(c->r[5]);g_original(c,kRoom);native_room_loaded(c,room);}
void chest_check(Cpu* c) {
    Actor* a=find(c->r[3]);if(!a||a->spec.actor!="chest")return g_original(c,kChestCheck);
    c->r[3]=flag(story_store::book(),a->mod,a->spec.flag)?1:0;
}
void chest_on(Cpu* c) {
    if(!g_opening)return g_original(c,kOnBox);
    if(!set_flag(story_store::book(),g_opening->mod,g_opening->spec.flag))LOG("[actors] cannot persist chest %s: story records full",g_opening->spec.id.c_str());
}
void chest_open(Cpu* c) {
    Actor* a=find(c->r[3]);
    Actor* keep=g_opening;g_opening=a&&a->spec.actor=="chest"?a:nullptr;
    g_original(c,kChestOpen);g_opening=keep;
}
void end_talk() {
    if(!g_talk.actor)return;
    Actor* actor=g_talk.actor;
    native_trace(actor,g_talk.npc,"completed");talk_latch(actor,0);
    story_mail::unsay();g_talk={};
    LOG("[actors] %s:%s dialogue finished",actor->mod.c_str(),actor->spec.id.c_str());
    if(!actor->spec.flag.empty())set_flag(story_store::book(),actor->mod,actor->spec.flag);
    actions(actor->mod,actor->spec.then);
    story_scenes::trigger("talk",actor->mod,actor->spec.id);
}
bool scene_is_deleting(uint32_t scene){
    // Native delete_tag's in-queue flag is +0x4C+0x10. Follow layer ownership to include a
    // parent stage already draining its children, before this room itself reaches the delete queue.
    for(unsigned depth=0;scene&&depth<16;++depth){
        if(ld8(scene+0x5C))return true;
        const uint32_t parent_layer=ld32(scene+0x2C);
        if(!parent_layer)return false;
        scene=ld32(parent_layer+0x18);
        if(!scene)return false;
    }
    return scene!=0;
}
bool player_present(Cpu* c){Cpu probe=*c;const uint32_t play=g_call(&probe,kPlay,{});return play&&ld32(play+0x5B34);}
void sign_execute(Cpu* c) {
    const uint32_t npc=c->r[3];Actor* a=find(npc);
    if(!a||a->spec.actor!="sign")return g_original(c,kSignExecute);
    if(!player_present(c)){c->r[3]=1;return;}
    if(ld16(npc+0xF8)==1&&g_talk.npc==0&&!a->spec.lines.empty()&&story_mail::can_say()) {
        g_talk.npc=npc;g_talk.actor=a;g_talk.started=true;
        story_mail::append_utf16(a->spec.lines,true,g_talk.text);story_mail::say(kCarrier,&g_talk.text);
    }
    g_original(c,kSignExecute);
    if(g_talk.npc==npc&&ld16(npc+0xF8)==0)end_talk();
}
void sign_delete(Cpu* c) {
    Actor* a=find(c->r[3]);if(a){if(g_talk.actor==a){story_mail::unsay();g_talk={};}a->process=0xFFFFFFFFu;}
    g_original(c,kSignDelete);
}
// Um1 normally routes nighttime talks through a retail event script. Mod residents always
// use the generic talk command instead, and never acquire a retail event staff/cut interpreter.
void people_check_order(Cpu* c) {
    const uint32_t actor=c->r[3];Actor* a=people(actor);
    g_original(c,kPeopleCheckOrder);
    if(!a||a->spec.actor!="resident")return;
    st16(actor+0xFC,0xFFFF);  // eventInfo.mEventId: no retail event
    st8(actor+0x910,1);      // m79C: generic native dialogue at either time of day
}
void people_event_move(Cpu* c) {
    Actor* a=people(c->r[3]);
    if(!a)g_original(c,kPeopleEventMove);
}
void people_event_order(Cpu* c) {
    const uint32_t actor=c->r[3];Actor* a=people(actor);
    if(a){
        if(ld8(actor+0x901)>2)st8(actor+0x901,0); // permit native talk orders only
        st16(actor+0x8F2,uint16_t(ld16(actor+0x8F2)&0x8000)); // drop retail reward/event transitions
        st16(actor+0xFC,0xFFFF);
    }
    g_original(c,kPeopleEventOrder);
    if(a&&a->spec.actor=="resident")st16(actor+0xFA,uint16_t(ld16(actor+0xFA)&~0x0020u)); // no retail item-show conversation
}
void people_msg_hook(Cpu* c){if(!people_message(c))g_original(c,kPeopleMsg);}
void people_next_hook(Cpu* c){if(!people_next_message(c))g_original(c,kPeopleNext);}
void people_execute_hook(Cpu* c){const uint32_t actor=c->r[3];if(people(actor)&&!player_present(c)){if(g_talk.npc==actor)cancel_talk("no-player");c->r[3]=1;return;}people_execute(c,true);g_original(c,kPeopleExecute);Cpu keep=*c;c->r[3]=actor;people_execute(c,false);*c=keep;}
void people_delete(Cpu* c){const uint32_t actor=c->r[3];Actor* a=find(actor);if(a&&a->spec.actor=="resident"){if(g_talk.actor==a)cancel_talk("delete");native_trace(a,actor,"native-delete");a->process=0xFFFFFFFFu;a->restore_talk=false;if(g_scratch)st32(g_scratch+128+uint32_t(a-g_actors.data())*4,0xFFFFFFFFu);}g_original(c,kPeopleDelete);}
void people_talk(Cpu* c){const uint32_t actor=c->r[3];Actor* a=people(actor);g_original(c,kPeopleTalk);if(a&&g_talk.npc==actor&&c->r[3]==0x12&&ld32(actor+0x7C0)==kCarrier&&ld32(actor+0x7C8)==0xFFFFFFFFu){g_talk.terminal=true;native_trace(a,actor,"terminal-12");}}
void photo_actor(Cpu* c){if(owned_resident(c->r[4]))native_trace(find(c->r[4]),c->r[4],"blocked-photo-actor",0);else g_original(c,kPhotoActor);}
void photo_position(Cpu* c){if(owned_resident(c->r[4]))native_trace(find(c->r[4]),c->r[4],"blocked-photo-position",0);else g_original(c,kPhotoPosition);}
void photo_snapshot(Cpu* c){const uint32_t obj=c->r[3];if(obj){const uint32_t id=ld32(obj+0x14);for(const auto& a:g_actors)if(a.spec.actor=="resident"&&a.process==id){Cpu probe=*c;st32(g_scratch+0x40,0);g_call(&probe,kSearch,{id,g_scratch+0x40});if(owned_resident(ld32(g_scratch+0x40))){const uint32_t npc=ld32(g_scratch+0x40);native_trace(find(npc),npc,"blocked-photo-snapshot",0);return;}}}g_original(c,kPhotoSnapshot);}
void named_candidate(Cpu* c){if(owned_resident(c->r[3])){native_trace(find(c->r[3]),c->r[3],"blocked-name",1);c->r[3]=0;}else g_original(c,kNamedCandidate);}
void staff_candidate(Cpu* c){if(owned_resident(c->r[3])){native_trace(find(c->r[3]),c->r[3],"blocked-staff-candidate",2);c->r[3]=0;}else g_original(c,kStaffCandidate);}
bool all_name(uint32_t name){return name&&ld8(name)=='A'&&ld8(name+1)=='L'&&ld8(name+2)=='L'&&ld8(name+3)==0;}
void extra_on(Cpu* c){const uint32_t arg=c->r[4];if(owned_resident(c->r[3])&&arg&&!all_name(ld32(arg))){native_trace(find(c->r[3]),c->r[3],"blocked-extra-on",3);c->r[3]=0;}else g_original(c,kExtraOn);}
void extra_off(Cpu* c){if(owned_resident(c->r[3])&&!all_name(c->r[4])){native_trace(find(c->r[3]),c->r[3],"blocked-extra-off",3);c->r[3]=0;}else g_original(c,kExtraOff);}
void staff_id(Cpu* c){if(owned_resident(c->r[5])){native_trace(find(c->r[5]),c->r[5],"blocked-staff-id",2);c->r[3]=0xFFFFFFFFu;}else g_original(c,kStaffId);}

uint32_t ym1_play(Cpu* c){Cpu probe=*c;return g_call(&probe,kPlay,{});}
bool ym1_owns_event(Cpu* c,uint32_t npc){
    if(ld16(npc+0xF8)!=1)return false;const uint32_t play=ym1_play(c);if(!play)return false;
    Cpu probe=*c;return g_call(&probe,GC(0x0253EE04),{play+0x51D0,ld32(play+0x529C)})==npc;
}
uint32_t ym1_status(Cpu* c){Cpu probe=*c;const uint32_t manager=ld32(GD(0x101F4B5C));return manager?g_call(&probe,GC(0x025F795C),{manager}):0xFF;}
void ym1_init(Cpu* c){
    const uint32_t npc=c->r[3];Actor* a=ym1(npc);if(!a)return g_original(c,kYm1Init);
    const bool empty=ld32(npc+0x86C)==0&&ld32(npc+0x870)==0;
    const bool same=ld32(npc+0x86C)==ld32(kYm1Pmf)&&ld32(npc+0x870)==ld32(kYm1Pmf+4);
    if(!empty&&!same){native_trace(a,npc,"rejected-seated-pmf");c->r[3]=0;return;}
    Cpu probe=*c;const uint32_t ok=g_call(&probe,kYm1SetAction,{npc,kYm1Pmf,0});
    c->r[3]=ok&&ld32(npc+0x86C)==ld32(kYm1Pmf)&&ld32(npc+0x870)==ld32(kYm1Pmf+4)&&ld8(npc+0xA22)>=1&&ld8(npc+0xA22)<=3&&ld8(npc+0xA1D)==11?1:0;
    native_trace(a,npc,c->r[3]?"seated-init":"rejected-seated-init");
}
void ym1_message(Cpu* c){
    const uint32_t npc=c->r[3];Actor* a=ym1(npc);if(!a)return g_original(c,kYm1Msg);
    const uint32_t layer=ld32(npc+0x2C);
    if(talk_latch(a)==3||!ym1_owns_event(c,npc)||!story_mail::can_say()||(layer&&scene_is_deleting(ld32(layer+0x18)))||(g_talk.actor&&g_talk.npc!=npc)){c->r[3]=0;return;}
    bind_talk(a,npc);a->restore_talk=false;native_trace(a,npc,"ym1-text-bind");c->r[3]=kCarrier;
}
void ym1_next(Cpu* c){if(ym1(c->r[3]))c->r[3]=0x10;else g_original(c,kYm1Next);}
void ym1_demo(Cpu* c){if(ym1(c->r[3])){st8(c->r[3]+0xA16,0);c->r[3]=0;}else g_original(c,kYm1Demo);}
void ym1_staff(Cpu* c){if(ym1(c->r[3]))c->r[3]=0xFFFFFFFFu;else g_original(c,kYm1Staff);}
void ym1_check_talk(Cpu* c){if(Actor* a=ym1(c->r[3])){st8(c->r[3]+0xA0A,0xFF);c->r[3]=talk_latch(a)==2?0:1;}else g_original(c,kYm1CheckTalk);}
void ym1_talk(Cpu* c){
    const uint32_t npc=c->r[3];Actor* a=ym1(npc);if(!a)return g_original(c,kYm1Talk);
    Cpu probe=*c;const uint32_t still=g_call(&probe,GC(0x022F9B64),{npc});
    const bool message_owned=ld32(npc+0x7C8)==0xFFFFFFFFu||(g_talk.npc==npc&&talk_latch(a)>0&&ld32(npc+0x7C0)==kCarrier&&ld8(npc+0x7CC)<=1);
    if(!ym1_owns_event(c,npc)||!message_owned){
        if(g_talk.npc==npc)cancel_talk("ym1-foreign-event");talk_latch(a,0);
        st8(npc+0xA0A,0xFF);st8(npc+0xA13,0);st8(npc+0xA18,0xFF);st8(npc+0xA19,0xFF);
        st32(npc+0x7C0,0);st32(npc+0x7C8,0xFFFFFFFFu);st8(npc+0x7CC,0);
        probe=*c;g_call(&probe,GC(0x022F9C2C),{npc,11}); // Return only this seated copy to SITwai.
        c->r[3]=still;return;
    }
    if(talk_latch(a)!=2){probe=*c;g_call(&probe,GC(0x025A11EC),{npc,1});}
    if(talk_latch(a)==1&&g_talk.npc==npc&&ld8(npc+0x7CC)==1&&ld32(npc+0x7C0)==kCarrier&&ld32(npc+0x7C8)==0xFFFFFFFFu&&ym1_status(c)==0x13&&ym1_owns_event(c,npc)){
        st8(npc+0xA0A,0xFF);st8(npc+0xA13,0);probe=*c;g_call(&probe,GC(0x022F9C2C),{npc,uint32_t(int32_t(int8_t(ld8(npc+0xA1E))))});
        probe=*c;st16(npc+0x9FE,uint16_t(g_call(&probe,GC(0x021E1E78),{15,30})));
        st8(npc+0xA18,0xFF);st8(npc+0xA19,0xFF);
        const uint32_t play=ym1_play(c);st16(play+0x52B8,uint16_t(ld16(play+0x52B8)|8));
        g_talk.terminal=true;talk_latch(a,2);native_trace(a,npc,"ym1-terminal-13");
    }
    c->r[3]=still;
}
bool ym1_cancel_restored(Cpu* c,uint32_t npc,Actor* a){
    // Only the exact current native owner may request the same reset used by native Ym1 endEvent.
    if(ld32(npc+0x7C0)!=kCarrier||!ym1_owns_event(c,npc))return false;
    if(g_talk.npc==npc)cancel_talk("ym1-missing-restored-text");else if(!g_talk.actor)story_mail::unsay();
    const uint32_t play=ym1_play(c);st16(play+0x52B8,uint16_t(ld16(play+0x52B8)|8));
    st8(npc+0xA0A,0xFF);st8(npc+0xA13,0);st8(npc+0xA18,0xFF);st8(npc+0xA19,0xFF);
    st32(npc+0x7C0,0);st32(npc+0x7C8,0xFFFFFFFFu);st8(npc+0x7CC,0);
    Cpu probe=*c;g_call(&probe,GC(0x022F9C2C),{npc,11});
    talk_latch(a,3); // Wait for the owned native reset; never reopen this cancelled message.
    native_trace(a,npc,"ym1-restored-text-cancelled");return true;
}
void ym1_execute(Cpu* c){
    const uint32_t npc=c->r[3];Actor* a=ym1(npc);if(!a)return g_original(c,kYm1Execute);
    const uint32_t layer=ld32(npc+0x2C);const bool draining=layer&&scene_is_deleting(ld32(layer+0x18));
    if(draining||!player_present(c)){if(g_talk.npc==npc)cancel_talk(draining?"draining":"no-player");talk_latch(a,0);a->restore_talk=false;c->r[3]=1;return;}
    if(talk_latch(a)==3){if(ld16(npc+0xF8)==1){c->r[3]=1;return;}talk_latch(a,0);}
    if(a->restore_talk){a->restore_talk=false;const uint8_t latch=talk_latch(a);
        const bool open=latch==1&&ld8(npc+0xA1D)==5&&ld32(npc+0x7C8)!=0xFFFFFFFFu;
        const bool terminal=latch==2&&ld32(npc+0x7C8)==0xFFFFFFFFu&&ld8(npc+0x7CC)==1&&ld8(npc+0xA1D)==11&&ld8(npc+0xA13)==0;
        const bool valid=(open||terminal)&&ld32(npc+0x7C0)==kCarrier&&ld8(npc+0x7CC)<=1&&ym1_owns_event(c,npc)&&story_mail::can_say();
        if(g_talk.actor==a&&g_talk.npc==npc){if(!valid)cancel_talk("ym1-invalid-restored-owner");}
        else if(ld32(npc+0x7C8)!=0xFFFFFFFFu||latch==1||latch==2){
            if(ym1_cancel_restored(c,npc,a)){c->r[3]=1;return;}
            talk_latch(a,0); // Foreign/unknown native event is never reset here.
        }else talk_latch(a,0);
    }
    st16(npc+0xFC,0xFFFF);st32(npc+0x100,0);st32(npc+0x104,0);st32(npc+0x108,0);st16(npc+0xFA,uint16_t(ld16(npc+0xFA)&~0x20u));
    g_original(c,kYm1Execute);
    if(g_talk.npc==npc&&ld16(npc+0xF8)==0){if(g_talk.terminal)end_talk();else cancel_talk("ym1-nonterminal-cancel");}
}
void ym1_delete(Cpu* c){Actor* a=find(c->r[3]);if(a&&a->spec.resident==kYm1){if(g_talk.actor==a)cancel_talk("delete");talk_latch(a,0);a->restore_talk=false;native_trace(a,c->r[3],"ym1-native-delete");a->process=0xFFFFFFFFu;st32(g_scratch+128+uint32_t(a-g_actors.data())*4,0xFFFFFFFFu);}g_original(c,kYm1Delete);}

// The checks a mod's actor list must pass, shared by start-up loading and the world editor's live
// reload so both validate identically. `replacing`: the list replaces the mod's own loaded actors.
bool check_specs(const std::string& mod,const std::vector<ManifestActor>& actors,bool replacing,std::string& why){
    for(size_t i=0;i<actors.size();++i){if(!replacing)for(const auto&old:g_actors)if(old.mod==mod&&old.spec.id==actors[i].id)return why="duplicate enabled actor identity",false;for(size_t j=0;j<i;++j)if(actors[j].id==actors[i].id)return why="duplicate actor identity",false;}
    if(!replacing&&g_actors.size()+actors.size()>128)return why="enabled mods exceed 128 actors overall",false;
    for(const auto& a:actors) {
        unsigned count=0;for(const auto& old:g_actors)if((!replacing||old.mod!=mod)&&old.spec.stage==a.stage&&old.spec.room==a.room)++count;
        for(const auto& other:actors)if(other.stage==a.stage&&other.room==a.room)++count;
        if(count>story_notebook::kMaxActorsPerRoom)return why="enabled mods exceed 16 actors per room",false;
    }
    for(const auto& a:actors)if(a.actor=="resident"&&a.resident!=1&&a.resident!=9&&a.resident!=kYm1)return why="unsupported resident subtype",false;
    for(const auto& a:actors)if(a.actor=="resident"&&a.resident==kYm1&&std::fabs(a.position.x+202066.f)<10&&std::fabs(a.position.z-320123.f)<10)return why="ym1 position intersects native relocation area",false;
    for(const auto&a:actors)if(a.kind=="mod"){
        const auto resource=g_models.find({mod,a.model});if(resource==g_models.end())return why="prepared actor model missing",false;
        if(a.combat_enabled){
            if(!resource->second.skinned||a.defeated_flag.empty()||!story_mail::valid_id(a.defeated_flag)||a.rotation.x!=0||a.rotation.z!=0||a.scale.x!=1||a.scale.y!=1||a.scale.z!=1||!a.flag.empty()||!a.lines.empty()||!a.then.empty())return why="unsupported generic combat configuration",false;
            for(const auto* clip:{&a.combat.flinch.clip,&a.combat.stun.clip,&a.combat.death.clip})if(((clip==&a.combat.stun.clip||clip==&a.combat.death.clip)&&clip->empty())||(!clip->empty()&&std::find(resource->second.clips.begin(),resource->second.clips.end(),*clip)==resource->second.clips.end()))return why="combat reaction clip unavailable",false;
            if(a.drops.size()>8)return why="too many generic combat drops",false;for(size_t n=0;n<a.drops.size();++n){const auto&drop=a.drops[n];if(!drop.quantity||drop.quantity>254||!story_items::find(mod,drop.item))return why="invalid generic combat item drop",false;for(size_t j=0;j<n;++j)if(a.drops[j].item==drop.item)return why="duplicate generic combat drop",false;}
        }
        const auto& rig=resource->second.rig;
        if(a.look_at.enabled){const auto head=std::find(rig.joint_names.begin(),rig.joint_names.end(),a.look_at.head);if(head==rig.joint_names.end())return why="look_at head is not a model joint",false;if(rig.joint_look_compatible.size()!=rig.joint_names.size()||!rig.joint_look_compatible[size_t(head-rig.joint_names.begin())])return why="look_at requires identity bind rotation along the joint chain",false;if(std::fabs(std::remainder(a.rotation.x,360.f))>1e-4||std::fabs(std::remainder(a.rotation.z,360.f))>1e-4||a.scale.x<=0||std::fabs(a.scale.x-a.scale.y)>1e-4||std::fabs(a.scale.x-a.scale.z)>1e-4)return why="look_at requires upright actor and uniform positive scale",false;
            if(!a.look_at.upper_bone.empty()){const auto upper=std::find(rig.joint_names.begin(),rig.joint_names.end(),a.look_at.upper_bone);if(upper==rig.joint_names.end())return why="look_at upper_body is not a model joint",false;if(!rig.joint_look_compatible[size_t(upper-rig.joint_names.begin())])return why="look_at upper_body has incompatible bind rotation",false;int parent=rig.joint_parents[size_t(head-rig.joint_names.begin())];const int target=int(upper-rig.joint_names.begin());while(parent>=0&&parent!=target)parent=rig.joint_parents[size_t(parent)];if(parent!=target)return why="look_at upper_body must be an ancestor of head",false;}}
        if(!a.path.points.empty()&&(std::find(resource->second.clips.begin(),resource->second.clips.end(),"idle")==resource->second.clips.end()||std::find(resource->second.clips.begin(),resource->second.clips.end(),"walk")==resource->second.clips.end()))return why="authored paths require idle and walk clips",false;
    }
    for(size_t i=0;i<actors.size();++i){const auto&a=actors[i];if(a.actor!="switch"&&a.actor!="door")continue;
        if(a.params||a.rotation.x!=0||a.rotation.z!=0||a.scale.x!=1||a.scale.y!=1||a.scale.z!=1||!a.flag.empty()||!a.lines.empty()||!a.then.empty()||!a.drops.empty()||!a.broken_flag.empty())return why="unsupported native switch/door configuration",false;
        if(a.actor=="switch"){if(a.switch_flag.empty()||!a.switch_actor.empty())return why="invalid switch flag",false;if(!replacing)for(const auto&old:g_actors)if(old.mod==mod&&old.spec.actor=="switch"&&old.spec.switch_flag==a.switch_flag)return why="duplicate switch flag",false;for(size_t j=0;j<i;++j)if(actors[j].actor=="switch"&&actors[j].switch_flag==a.switch_flag)return why="duplicate switch flag",false;}
        else if(a.switch_actor.empty()||!a.switch_flag.empty())return why="invalid door switch reference",false;
    }
    for(const auto&a:actors){story_enemies::Kind kind;if(!enemy_kind(a,kind))continue;if((!replacing&&!configure_enemy_declarations())||!story_enemies::supported(kind))return why="Native enemy child ownership route is not admitted",false;
        if(!a.kind.empty()||a.params||a.rotation.x!=0||a.rotation.z!=0||a.scale.x!=1||a.scale.y!=1||a.scale.z!=1||a.defeated_flag.empty()||!story_mail::valid_id(a.defeated_flag)||!a.flag.empty()||!a.broken_flag.empty()||!a.switch_flag.empty()||!a.switch_actor.empty()||!a.lines.empty()||!a.then.empty()||!a.model.empty()||!a.animation.empty()||a.look_at.enabled||!a.path.points.empty())return why="Unsupported native enemy configuration",false;
    }
    return true;
}
}
// a mod actor's talk ended: its flag, then actions and talk scenes
std::function<void()> mod_talked(const std::string& mod,const std::string& id){
    return [mod,id](){
        auto found=std::find_if(g_actors.begin(),g_actors.end(),[&](const Actor& a){return a.mod==mod&&a.spec.id==id;});if(found==g_actors.end())return;
        if(!found->spec.flag.empty())set_flag(story_store::book(),mod,found->spec.flag);
        actions(mod,found->spec.then);story_scenes::trigger("talk",mod,id);
    };
}
bool load(const std::string& mod,const std::vector<ManifestActor>& actors,std::string& why) {return load(mod,actors,why,{});}
bool load(const std::string& mod,const std::vector<ManifestActor>& actors,std::string& why,const std::vector<std::string>& scene_flags) {
    if(g_installed)return why="world actors require startup declaration",false;
    if(!check_specs(mod,actors,false,why))return false;
    if(g_actors.capacity()<128)g_actors.reserve(128);  // the world editor adds actors later: their addresses stay put
    std::vector<std::string> flags=scene_flags;
    for(const auto& a:actors){if(!a.flag.empty())flags.push_back(a.flag);if(!a.switch_flag.empty())flags.push_back(a.switch_flag);for(const auto& action:a.then)if(action.kind==story_notebook::StoryAction::Kind::Flag)flags.push_back(action.id);}
    std::vector<Actor> staged;staged.reserve(actors.size());g_actors.reserve(g_actors.size()+actors.size());
    struct LoadRollback {size_t props=story_props::checkpoint(),models=mod_actor::checkpoint(),switches=story_switches::checkpoint(),enemies=story_enemies::checkpoint(),ownership=story_enemy_ownership::checkpoint();story_items::PropMarkerCheckpoint markers=story_items::prop_marker_checkpoint();decltype(g_flag_names) flags=g_flag_names;bool committed=false;~LoadRollback(){if(!committed){story_props::rollback(props);mod_actor::rollback(models);story_switches::rollback(switches);story_enemies::rollback(enemies);story_enemy_ownership::rollback(ownership);story_items::rollback_prop_markers(markers);g_flag_names=std::move(flags);}}} rollback;
    if(!register_flags(mod,flags,why))return false;
    for(const auto& a:actors){Actor added{mod,a};if(a.kind=="mod"&&a.combat_enabled){added.combat_marker=story_items::register_enemy_marker(mod,a.id,a.defeated_flag,why);if(!added.combat_marker.value)return false;added.fingerprint=identity::fingerprint(mod,a);}if(a.kind=="mod"&&a.carry_break_on_impact){added.carry_marker=story_items::register_prop_marker(mod,a.id,a.broken_flag,why);if(!added.carry_marker)return false;added.fingerprint=identity::fingerprint(mod,a);}story_enemies::Kind enemy;if(enemy_kind(a,enemy)){added.fingerprint=identity::fingerprint(mod,a);story_enemies::Spec spec{enemy,mod,a.id,a.defeated_flag,{},added.fingerprint,1};for(const auto&drop:a.drops)spec.drops.push_back({drop.item,drop.quantity});added.enemy_token=story_enemies::declare(spec,why);if(!added.enemy_token)return false;}if(story_props::is_prop(a)){added.fingerprint=identity::fingerprint(mod,a);added.prop_token=story_props::declare(mod,a,why);if(!added.prop_token)return false;}if(a.actor=="switch"){if(a.switch_flag.empty())return why="switch requires an own flag",false;added.fingerprint=identity::fingerprint(mod,a);added.switch_token=story_switches::declare_switch(mod,a.id,why);added.switch_source=added.switch_token;if(!added.switch_token)return false;}staged.push_back(std::move(added));}
    for(auto& a:staged)if(a.spec.actor=="door"){
        const Actor* source=nullptr;for(const auto& candidate:staged)if(candidate.spec.actor=="switch"&&candidate.spec.id==a.spec.switch_actor)source=&candidate;
        if(!source)for(const auto& candidate:g_actors)if(candidate.mod==mod&&candidate.spec.actor=="switch"&&candidate.spec.id==a.spec.switch_actor)source=&candidate;
        if(!source||!source->switch_token)return why="door must reference a same-mod switch",false;
        a.fingerprint=identity::fingerprint(mod,a.spec);a.switch_source=source->switch_token;a.switch_token=story_switches::declare_door(mod,a.spec.id,a.switch_source,why);if(!a.switch_token)return false;
    }
    for(auto& a:staged)g_actors.push_back(std::move(a));rollback.committed=true;return true;
}
bool register_flags(const std::string& mod,const std::vector<std::string>& flags,std::string& why){
    auto names=g_flag_names;
    for(const auto& id:flags){
        if(!story_mail::valid_id(id))return why="invalid mod-owned actor/scene flag id",false;
        const auto key=flag_key(mod,id);auto found=names.find(key);
        if(found!=names.end()&&found->second!=std::make_pair(mod,id))return why="mod-owned flag hash collision: "+mod+":"+id+" conflicts with "+found->second.first+":"+found->second.second,false;
        names[key]={mod,id};
    }
    g_flag_names=std::move(names);return true;
}
bool declared(){return !g_actors.empty();}
bool mod_declared(){return std::any_of(g_actors.begin(),g_actors.end(),[](const Actor& a){return a.spec.kind=="mod";});}
bool register_model(const std::string& mod,const std::string& model,const std::string& archive,int index,std::string& why,story_notebook::WorldPoint minimum,story_notebook::WorldPoint maximum,bool skinned,std::vector<std::string> clips,mod_actor::ModelRig rig){
    if(g_installed)return why="actor models must be registered before hooks are installed",false;
    if(mod.empty()||model.empty()||archive.empty()||archive.size()>63||index<0||index>16)return why="invalid prepared actor model resource",false;
    const float lo[]={minimum.x,minimum.y,minimum.z},hi[]={maximum.x,maximum.y,maximum.z};for(unsigned i=0;i<3;++i)if(!std::isfinite(lo[i])||!std::isfinite(hi[i])||lo[i]>hi[i])return why="invalid prepared model bounds",false;
    if(rig.joint_names.size()!=rig.joint_parents.size()||rig.joint_names.size()>32||(!skinned&&!rig.joint_names.empty()))return why="invalid prepared model rig",false;
    for(size_t i=0;i<rig.joint_parents.size();++i)if(rig.joint_parents[i]>=int(i)||(i&&rig.joint_parents[i]<0)||rig.joint_parents[i]<-1)return why="invalid prepared joint hierarchy",false;
    if(clips.size()>8||(!skinned&&!clips.empty()))return why="invalid prepared animation list",false;
    for(size_t i=0;i<clips.size();++i)if(!story_mail::valid_id(clips[i])||std::find(clips.begin(),clips.begin()+i,clips[i])!=clips.begin()+i)return why="invalid prepared animation name",false;
    const auto key=std::make_pair(mod,model);if(g_models.contains(key))return why="duplicate prepared actor model",false;
    for(const auto& entry:g_models)if(entry.second.archive==archive)return why="actor model archive identity collision",false;
    g_models.emplace(key,ModelResource{archive,index,minimum,maximum,skinned,std::move(clips),std::move(rig)});return true;
}
bool editor_register_model(const std::string& mod,const std::string& model,const std::string& archive,bool skinned,std::string& why){
    if(g_models.contains({mod,model}))return true;
    story_notebook::WorldPoint minimum,maximum;
    if(!actor_model_cache::bounds_for(archive,minimum,maximum))return why="the prepared model has no bounds",false;
    for(const auto& entry:g_models)if(entry.second.archive==archive)return why="actor model archive identity collision",false;
    auto clips=actor_model_cache::clips_for(archive);auto rig=actor_model_cache::rig_for(archive);
    if(rig.joint_names.size()!=rig.joint_parents.size()||rig.joint_names.size()>32||clips.size()>8||(!skinned&&(!clips.empty()||!rig.joint_names.empty())))return why="invalid prepared model rig or clips",false;
    g_models.emplace(std::make_pair(mod,model),ModelResource{archive,1,minimum,maximum,skinned,std::move(clips),std::move(rig)});
    LOG("[actors] %s:%s model prepared for the editor (%s)",mod.c_str(),model.c_str(),archive.c_str());
    return true;
}
void set_game_calls(void(*original)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>)){g_original=original;g_call=call;}
void set_scratch_memory(uint32_t guest){g_scratch=guest;story_mail::set_say_restore_record(guest?guest+0x320:0,128);for(size_t i=0;i<g_actors.size();++i)st32(g_scratch+128+uint32_t(i)*4,0xFFFFFFFFu);std::memset(mem::ptr(g_scratch+0x300),0,28);std::memset(mem::ptr(g_scratch+0x400),0,256);std::memset(mem::ptr(g_scratch+kIdentityScratchOffset),0,128*identity::kRecordBytes);std::memset(mem::ptr(g_scratch+kEnemyProfilesScratchOffset),0,kScratchBytes-kEnemyProfilesScratchOffset);for(size_t i=0;i<g_actors.size();++i)if(g_actors[i].enemy_token)st32(g_scratch+kEnemyEpochScratchOffset+uint32_t(i)*4,1);}
void set_scene_start(std::function<bool(const std::string&,const std::string&)> start){g_scene=std::move(start);}
void native_room_loaded(Cpu* c,int room){
    if(!g_scratch||room<0||room>63)return;
    const uint32_t stage_process=ld32(GD(0x1047E6C4));
    const uint32_t room_process=ld32(GD(0x1047E8F0)+uint32_t(room)*0x22C);
    const bool new_prop_epoch=ld32(g_scratch+0x318)!=stage_process||ld32(g_scratch+0x400+uint32_t(room)*4)!=room_process||std::memcmp(mem::ptr(g_scratch+0x300),mem::ptr(kStage),8)!=0;
    if(ld32(g_scratch+0x318)!=stage_process){std::memset(mem::ptr(g_scratch+0x308),0,8);std::memset(mem::ptr(g_scratch+0x400),0,256);st32(g_scratch+0x318,stage_process);}
    st32(g_scratch+0x400+uint32_t(room)*4,ld32(GD(0x1047E8F0)+uint32_t(room)*0x22C));
    if(new_prop_epoch&&combat_declared())story_enemy_projectiles::room_changed();
    for(size_t i=0;i<g_actors.size();++i){auto&a=g_actors[i];if(!a.enemy_token||!new_prop_epoch||a.spec.room!=unsigned(room)||std::strncmp(reinterpret_cast<const char*>(mem::ptr(kStage)),a.spec.stage.c_str(),8)!=0)continue;uint32_t counter=g_scratch+kEnemyEpochScratchOffset+uint32_t(i)*4,epoch=ld32(counter);if(!epoch||epoch==0xFFFFFFFFu){a.restore_quarantined=true;story_enemies::quarantine(a.enemy_token);continue;}++epoch;if(story_enemies::begin_epoch(a.enemy_token,epoch)){st32(counter,epoch);a.restore_quarantined=false;a.process=0xFFFFFFFFu;a.deleting=false;st32(g_scratch+128+uint32_t(i)*4,0xFFFFFFFFu);}}
    refresh_link();
    for(auto& a:g_actors)if(a.spec.room==unsigned(room)){if(a.spec.kind=="mod"&&(!a.spec.combat_enabled||(new_prop_epoch&&std::strncmp(reinterpret_cast<const char*>(mem::ptr(kStage)),a.spec.stage.c_str(),8)==0)))mod_actor::retry(a.mod_token);if(a.prop_token&&new_prop_epoch&&std::strncmp(reinterpret_cast<const char*>(mem::ptr(kStage)),a.spec.stage.c_str(),8)==0){a.restore_quarantined=false;story_props::retry(a.prop_token);}if(a.switch_token&&new_prop_epoch&&std::strncmp(reinterpret_cast<const char*>(mem::ptr(kStage)),a.spec.stage.c_str(),8)==0){a.restore_quarantined=false;story_switches::retry(a.switch_token);}}room_loaded(c,room);}
void room_loaded(Cpu* c,int room) {
    if(!g_installed||!g_scratch)return;
    refresh_link();
    const char* stage=reinterpret_cast<const char*>(mem::ptr(kStage));
    if(room<0||room>63)return;
    if(ld32(g_scratch+0x318)!=ld32(GD(0x1047E6C4))||ld32(g_scratch+0x400+uint32_t(room)*4)!=ld32(GD(0x1047E8F0)+uint32_t(room)*0x22C))return;
    // Remember native room setup in guest memory so state loads preserve the readiness gate.
    if(std::memcmp(mem::ptr(g_scratch+0x300),stage,8)!=0){std::memcpy(mem::ptr(g_scratch+0x300),stage,8);std::memset(mem::ptr(g_scratch+0x308),0,8);}
    st8(g_scratch+0x308+uint32_t(room)/8,uint8_t(ld8(g_scratch+0x308+uint32_t(room)/8)|(1u<<(uint32_t(room)%8))));
    Cpu room_probe=*c;const uint32_t room_scene=g_call(&room_probe,GC(0x025DC80C),{ld32(GD(0x1047E8F0)+uint32_t(room)*0x22C)});
    if(!room_scene||scene_is_deleting(room_scene))return; // Never enqueue new children into a draining room/stage.
    const uint32_t room_layer=room_scene+0xC0;
    unsigned count=0;
    for(auto& a:g_actors) {
        if(a.restore_quarantined||a.spec.room!=unsigned(room)||std::strncmp(stage,a.spec.stage.c_str(),8)!=0||!eligible(a))continue;
        if(a.process!=0xFFFFFFFFu) {
            Cpu probe=*c;st32(g_scratch+0x40,0);const uint32_t exists=g_call(&probe,kSearch,{a.process,g_scratch+0x40});
            if(exists)continue;  // also true while asynchronous actor creation is pending
        }
        if(++count>story_notebook::kMaxActorsPerRoom)break;
        std::string reserved_flag=a.spec.flag;
        if(a.switch_token){const Actor* source=nullptr;for(const auto& candidate:g_actors)if(candidate.spec.actor=="switch"&&candidate.switch_token==a.switch_source&&candidate.mod==a.mod)source=&candidate;if(!source)continue;reserved_flag=source->spec.switch_flag;}
        if(!reserved_flag.empty()){
            const auto book=story_store::book();
            if(!book.has(kRecordActorFlag,flag_key(a.mod,reserved_flag))&&!set_flag(book,a.mod,reserved_flag,false)){
                LOG("[actors] %s:%s omitted: no story flag record available",a.mod.c_str(),a.spec.id.c_str());continue;
            }
        }
        if(a.spec.kind=="mod"&&(!a.mod_token||mod_actor::failed(a.mod_token)))continue;
        if(a.spec.carry_break_on_impact&&story_items::prop_broken(a.carry_marker))continue;
        if(a.prop_token&&!story_props::available(a.prop_token))continue;
        if(a.switch_token&&!story_switches::available(a.switch_token))continue;
        if(a.enemy_token&&(!story_enemies::available(a.enemy_token)||!story_enemy_ownership::can_spawn(a.enemy_token)))continue;
        story_enemies::Kind enemy{};(void)enemy_kind(a.spec,enemy);
        uint32_t proc=a.enemy_token?story_enemy_ownership::descriptor(enemy).profile:a.switch_token?(a.spec.actor=="switch"?story_switches::kSwitchProfile:story_switches::kDoorProfile):a.prop_token?story_props::kProfile:a.spec.kind=="mod"?mod_actor::kProfile:a.spec.actor=="sign"?0xB6u:a.spec.resident==kYm1?0x13Bu:0x175u;
        if(a.spec.actor=="chest") {
            std::memcpy(mem::ptr(g_scratch+0x50),"takara",7);Cpu probe=*c;
            const uint32_t entry=g_call(&probe,kSearchName,{g_scratch+0x50});
            if(!entry){LOG("[actors] takara object name unavailable; chest omitted");continue;}proc=ld16(entry+8);
        }
        const float p[]={a.spec.position.x,a.spec.position.y,a.spec.position.z};
        const float s[]={a.spec.scale.x,a.spec.scale.y,a.spec.scale.z};
        const float r[]={a.spec.rotation.x,a.spec.rotation.y,a.spec.rotation.z};
        for(unsigned i=0;i<3;++i){st32(g_scratch+i*4,f32_as_u32(p[i]));st32(g_scratch+0x14+i*4,f32_as_u32(s[i]));st16(g_scratch+0xC+i*2,uint16_t(int32_t(r[i]*65536.0f/360.0f)));}
        if(a.prop_token||a.switch_token)st16(g_scratch+0x10,255);
        if(a.enemy_token)st16(g_scratch+0x10,story_enemies::home_z(enemy));
        if(a.spec.actor=="chest")st16(g_scratch+0x10,uint16_t((uint32_t(a.spec.item_game)<<8)|255));
        const uint32_t params=a.enemy_token?story_enemies::parameters(enemy):a.switch_token?story_switches::parameters(a.spec.actor=="switch"?story_switches::Kind::Switch:story_switches::Kind::Door):a.prop_token?story_props::parameters(a.spec):a.spec.kind=="mod"?a.mod_token:a.spec.actor=="sign"?kCarrier:a.spec.actor=="resident"?(a.spec.resident==kYm1?1u:0x00FF0000u):0;
        Cpu call=*c;a.deleting=false;
        const uint32_t previous_layer=g_call(&call,GC(0x025DED64),{});
        g_call(&call,GC(0x025DEAB4),{room_layer});
        try{talk_latch(&a,0);a.process=g_call(&call,kCreate,{proc,params,g_scratch,uint32_t(room),g_scratch+0xC,g_scratch+0x14,a.spec.actor=="resident"&&a.spec.resident!=kYm1?a.spec.resident:0xFFFFFFFFu,0});}
        catch(...){g_call(&call,GC(0x025DEAB4),{previous_layer});throw;}
        g_call(&call,GC(0x025DEAB4),{previous_layer});
        if(a.enemy_token&&a.process&&a.process!=0xFFFFFFFFu)story_enemies::bind(a.enemy_token,a.process);
        if(a.prop_token)story_props::bind(a.prop_token,a.process);
        if(a.switch_token)story_switches::bind(a.switch_token,a.process);
        st32(g_scratch+128+uint32_t(&a-g_actors.data())*4,a.process);
        if((a.prop_token||a.switch_token||a.enemy_token)&&a.process&&a.process!=0xFFFFFFFFu){const auto record=identity::record(a.fingerprint);std::memcpy(mem::ptr(g_scratch+kIdentityScratchOffset+uint32_t(&a-g_actors.data())*identity::kRecordBytes),record.data(),record.size());}
        LOG("[actors] %s:%s room %d process %08X room_layer=%08X",a.mod.c_str(),a.spec.id.c_str(),room,a.process,room_layer);
    }
}
void service(Cpu* c){
    if(!g_installed||!g_scratch)return;
    refresh_link();
    if(g_talk.actor&&g_talk.actor->spec.actor=="resident"){
        Cpu lookup=*c;st32(g_scratch+0x40,0);g_call(&lookup,kSearch,{g_talk.actor->process,g_scratch+0x40});
        const uint32_t npc=ld32(g_scratch+0x40);
        const uint32_t layer=npc?ld32(npc+0x2C):0;
        if(!npc||(g_talk.actor->spec.resident==kYm1?ym1(npc):people(npc))!=g_talk.actor)cancel_talk("missing-pid",false);
        else if(layer&&scene_is_deleting(ld32(layer+0x18)))cancel_talk("draining");
    }
    Cpu probe=*c;const uint32_t play=g_call(&probe,kPlay,{});if(!play)return;
    const uint32_t player=ld32(play+0x5B34);if(!player)return;
    // the editor's changed actors: removed wherever they are, then created again by room_loaded below
    for(auto& a:g_actors){
        if(!a.respawn)continue;
        if(a.process==0xFFFFFFFFu){a.respawn=false;continue;}
        if(g_talk.actor==&a)continue;
        st32(g_scratch+0x40,0);probe=*c;const uint32_t exists=g_call(&probe,kSearch,{a.process,g_scratch+0x40});
        const uint32_t actor=ld32(g_scratch+0x40);
        if(mod_actor::talking(actor))continue;
        if(actor&&!a.deleting){probe=*c;if(g_call(&probe,kDelete,{actor}))a.deleting=true;}
        if(!exists){a.process=0xFFFFFFFFu;st32(g_scratch+128+uint32_t(&a-g_actors.data())*4,a.process);a.respawn=false;a.deleting=false;
            if(a.mod_token)mod_actor::retry(a.mod_token);LOG("[actors] %s:%s removed for the editor; created again from its new spec",a.mod.c_str(),a.spec.id.c_str());}
    }
    const int room=int8_t(ld8(player+0x326));if(room<0||room>63)return;
    if(ld32(g_scratch+0x318)!=ld32(GD(0x1047E6C4))||ld32(g_scratch+0x400+uint32_t(room)*4)!=ld32(GD(0x1047E8F0)+uint32_t(room)*0x22C))return;
    if(std::memcmp(mem::ptr(g_scratch+0x300),mem::ptr(kStage),8)!=0||!(ld8(g_scratch+0x308+uint32_t(room)/8)&(1u<<(uint32_t(room)%8))))return;
    for(auto& a:g_actors){
        if(a.spec.room!=unsigned(room)||std::strncmp(reinterpret_cast<const char*>(mem::ptr(kStage)),a.spec.stage.c_str(),8)!=0||a.process==0xFFFFFFFFu||eligible(a))continue;
        if(g_talk.actor==&a)continue; // finish native dialogue before removing its talk partner
        st32(g_scratch+0x40,0);probe=*c;const uint32_t exists=g_call(&probe,kSearch,{a.process,g_scratch+0x40});
        const uint32_t actor=ld32(g_scratch+0x40);
        if(mod_actor::talking(actor))continue; // native events retain their talk target until closure
        if(actor&&!a.deleting){probe=*c;if(g_call(&probe,kDelete,{actor}))a.deleting=true;}
        // Keep creating/queued-for-deletion IDs until native lookup confirms they are gone.
        if(!exists){a.process=0xFFFFFFFFu;st32(g_scratch+128+uint32_t(&a-g_actors.data())*4,a.process);}
    }
    room_loaded(c,room);
}
void state_loaded(){if(combat_declared())story_enemy_ownership::state_loaded();if(enemies_declared()){story_enemies::state_loaded();story_enemy_children::state_loaded();}
    mod_actor::state_loaded();story_props::state_loaded();story_switches::state_loaded();cancel_talk("state-load",false,true);g_opening=nullptr;
    for(size_t i=0;i<g_actors.size();++i){auto&a=g_actors[i];const uint32_t slot=g_scratch?g_scratch+128+uint32_t(i)*4:0;const uint32_t id=slot?ld32(slot):0;a.process=id?id:0xFFFFFFFFu;a.deleting=false;a.restore_talk=a.spec.actor=="resident";a.restore_quarantined=false;
        if(a.enemy_token){story_enemy_ownership::Binding binding;const bool known=story_enemy_ownership::binding_state(a.enemy_token,binding);const uint32_t epoch=g_scratch?ld32(g_scratch+kEnemyEpochScratchOffset+uint32_t(i)*4):0;const bool queued=known&&(binding.state==story_enemy_ownership::BindingState::Queued||binding.state==story_enemy_ownership::BindingState::Active);const bool fresh=known&&binding.state==story_enemy_ownership::BindingState::Fresh;const bool retired=known&&binding.state==story_enemy_ownership::BindingState::Retired;const bool identity_ok=queued&&a.process==binding.pid&&identity::matches(mem::ptr(g_scratch+kIdentityScratchOffset+uint32_t(i)*identity::kRecordBytes),a.fingerprint);const bool no_pid=a.process==0xFFFFFFFFu;
            if(!epoch||!known||epoch!=binding.epoch||(!identity_ok&&!(no_pid&&(fresh||retired)))){
                if(known&&binding.actor&&story_enemy_ownership::lookup_actor(binding.actor)==a.enemy_token)story_enemy_ownership::retire_before_delete(binding.actor);
                story_enemies::quarantine(a.enemy_token);a.process=0xFFFFFFFFu;a.restore_quarantined=true;if(slot)st32(slot,0xFFFFFFFFu);LOG("[native_enemy] %s:%s restored binding/epoch/fingerprint rejected",a.mod.c_str(),a.spec.id.c_str());
            }else if(queued)story_enemies::bind(a.enemy_token,a.process,true);
            else {a.process=0xFFFFFFFFu;a.restore_quarantined=retired;}
        }
        if(a.prop_token||a.switch_token){
            const bool live=a.process!=0xFFFFFFFFu;
            if(live&&(!g_scratch||!identity::matches(mem::ptr(g_scratch+kIdentityScratchOffset+uint32_t(i)*identity::kRecordBytes),a.fingerprint))){a.process=0xFFFFFFFFu;a.restore_quarantined=true;if(slot)st32(slot,0xFFFFFFFFu);if(a.prop_token)story_props::quarantine(a.prop_token);if(a.switch_token)story_switches::quarantine(a.switch_token);LOG("[native_prop] %s:%s restored declaration fingerprint rejected",a.mod.c_str(),a.spec.id.c_str());}
            else {if(a.prop_token)story_props::bind(a.prop_token,a.process);if(a.switch_token)story_switches::bind(a.switch_token,a.process,live);}
        }
    }
    if(combat_declared())story_enemy_projectiles::state_loaded();
}
void restore_talk_text(){
    story_mail::SayOwner owner;
    if(!story_mail::restore_say_owner(kCarrier,owner)||owner.token>g_actors.size())return;
    Actor* a=&g_actors[owner.token-1];const uint32_t npc=owner.actor;
    if(a->spec.actor!="resident"||a->process!=owner.pid||find(npc)!=a||(a->spec.resident==kYm1?ym1(npc):people(npc))!=a||ld16(npc+0xF8)!=1||ld32(npc+0x7C0)!=kCarrier||ld8(npc+0x7CC)>1||ld32(GD(0x1046F0B0)+0x529C)!=owner.partner)return;
    const uint32_t layer=ld32(npc+0x2C);if(layer&&scene_is_deleting(ld32(layer+0x18)))return;
    bool terminal=false;
    if(a->spec.resident==kYm1){
        const bool open=talk_latch(a)==1&&ld8(npc+0xA1D)==5&&ld32(npc+0x7C8)!=0xFFFFFFFFu;
        terminal=talk_latch(a)==2&&ld32(npc+0x7C8)==0xFFFFFFFFu&&ld8(npc+0x7CC)==1&&ld8(npc+0xA1D)==11&&ld8(npc+0xA13)==0;
        if(!open&&!terminal)return;
    }else{terminal=ld16(npc+0x8F0)==0x12&&ld32(npc+0x7C8)==0xFFFFFFFFu;if(ld32(npc+0x7C8)==0xFFFFFFFFu&&!terminal)return;}
    g_talk={};g_talk.npc=npc;g_talk.actor=a;g_talk.started=true;g_talk.terminal=terminal;
    story_mail::append_utf16(a->spec.lines.empty()?"Hello.":a->spec.lines,true,g_talk.text);
    if(!story_mail::resume_say(kCarrier,&g_talk.text,owner)){g_talk={};return;}
    native_trace(a,npc,"pre-resume-text-hydrated");
}
uint32_t actor_address(Cpu* c,const std::string& mod,const std::string& id) {
    if(!g_call||!g_scratch)return 0;
    for(auto& a:g_actors)if(a.mod==mod&&a.spec.id==id&&a.process!=0xFFFFFFFFu){Cpu call=*c;st32(g_scratch+0x40,0);g_call(&call,kSearch,{a.process,g_scratch+0x40});return ld32(g_scratch+0x40);}return 0;
}
namespace {
// Actors whose game-side state is declared once at start-up (native props, switches, doors and enemies,
// mod enemies and carried props): their markers, reward/break sinks and saved identities follow the
// declaration, so a new or changed one takes effect after a restart.
const char* restart_kind(const ManifestActor& a){
    story_enemies::Kind enemy;
    if(enemy_kind(a,enemy))return "an enemy";
    if(story_props::is_prop(a))return "a pot, barrel or crate";
    if(a.actor=="switch"||a.actor=="door")return "a switch or door";
    if(a.kind=="mod"&&a.combat_enabled)return "a mod enemy (combat)";
    if(a.kind=="mod"&&a.carry_enabled)return "a carried prop";
    return nullptr;
}
// Whether `a` can be created while the game runs: the hooks its kind needs are installed and a mod
// model is prepared. `why` says what waits for a restart otherwise.
bool live_capable(const std::string& mod,const ManifestActor& a,std::string& why){
    if(const char* kind=restart_kind(a))return why=std::string(kind)+" appears after a restart",false;
    if(!g_installed)return why="the game had no mod actors at start-up: new actors appear after a restart",false;
    if(a.actor=="resident"&&!(a.resident==kYm1?g_ym1_hooks:g_resident_hooks))return why="the first resident of this kind appears after a restart",false;
    if(a.kind=="mod"){
        if(!mod_actor::live_ready())return why="the first mod model actor appears after a restart",false;
        if(!g_models.contains({mod,a.model}))return why="model "+a.model+" is not prepared yet: it appears after a reload or restart",false;
    }
    return true;
}
bool same_spec(const std::string& mod,const ManifestActor& a,const ManifestActor& b){return identity::fingerprint(mod,a)==identity::fingerprint(mod,b);}
}
bool editor_restart_only(const ManifestActor& a){return restart_kind(a)!=nullptr;}
bool editor_live_capable(const std::string& mod,const ManifestActor& a,std::string& why){return live_capable(mod,a,why);}
bool editor_update(const std::string& mod,const std::vector<ManifestActor>& actors,const std::vector<std::string>& changed,std::string& why,std::vector<std::string>* pending){
    std::map<std::string,Actor*> own;for(auto& a:g_actors)if(a.mod==mod)own[a.spec.id]=&a;
    struct Step {const ManifestActor* spec=nullptr;Actor* actor=nullptr;bool create=false,update=false;};
    std::vector<Step> plan;std::vector<std::string> later;std::vector<Actor*> remove;
    std::vector<ManifestActor> effective;  // what the game will have: checked like a start-up list
    std::set<std::string> listed;
    const auto is_changed=[&](const std::string& id){return std::find(changed.begin(),changed.end(),id)!=changed.end();};
    for(const auto& spec:actors){
        listed.insert(spec.id);
        const auto it=own.find(spec.id);
        std::string reason;
        if(it==own.end()){
            if(!live_capable(mod,spec,reason)){later.push_back(spec.id+": "+reason);continue;}
            plan.push_back({&spec,nullptr,true,false});effective.push_back(spec);continue;
        }
        Actor* a=it->second;
        if(!a->removed&&!is_changed(spec.id)){effective.push_back(a->spec);continue;}
        if(a->removed&&same_spec(mod,a->spec,spec)){plan.push_back({&spec,a,false,false});effective.push_back(spec);continue;}  // a removal undone
        const char* kind=restart_kind(a->spec);if(!kind)kind=restart_kind(spec);
        if(kind||(a->spec.kind=="mod")!=(spec.kind=="mod")||(spec.kind=="mod"&&!g_models.contains({mod,spec.model}))||
           (spec.actor=="resident"&&!live_capable(mod,spec,reason))){
            later.push_back(spec.id+": "+(kind?std::string(kind)+" takes changes after a restart":reason.empty()?std::string("takes this change after a restart"):reason));
            if(!a->removed)effective.push_back(a->spec);
            continue;
        }
        plan.push_back({&spec,a,false,true});effective.push_back(spec);
    }
    for(auto& [id,a]:own)if(!a->removed&&!listed.contains(id))remove.push_back(a);
    size_t creates=0;for(const auto& step:plan)creates+=step.create;
    if(g_actors.size()+creates>128)return why="more than 128 world actors in all",false;
    if(!check_specs(mod,effective,true,why))return false;
    std::vector<std::string> flags;for(const auto& a:effective){if(!a.flag.empty())flags.push_back(a.flag);if(!a.switch_flag.empty())flags.push_back(a.switch_flag);for(const auto& action:a.then)if(action.kind==story_notebook::StoryAction::Kind::Flag)flags.push_back(action.id);}
    auto saved_flags=g_flag_names;if(!register_flags(mod,flags,why))return false;
    // check every changed mod model first, then apply: a refusal leaves all declarations unchanged
    for(const bool apply:{false,true})for(const auto& step:plan){if(!step.update||step.spec->kind!="mod")continue;
        const auto& r=g_models.at({mod,step.spec->model});
        if(!step.actor->mod_token||!mod_actor::redeclare(step.actor->mod_token,*step.spec,r.archive,r.index,r.minimum,r.maximum,r.skinned,r.clips,r.rig,apply)){
            g_flag_names=saved_flags;return why="actor "+step.spec->id+": the model can't serve this spec (animation, look_at, rotation or scale)",false;}
    }
    for(const auto& step:plan){
        if(step.create){
            Actor added{mod,*step.spec};
            if(step.spec->kind=="mod"){
                const auto& r=g_models.at({mod,step.spec->model});
                added.mod_token=mod_actor::declare(mod,added.spec,r.archive,r.index,mod_talked(mod,added.spec.id),r.minimum,r.maximum,r.skinned,r.clips,r.rig);
                if(!added.mod_token){LOG("[actors] %s:%s: the model can't serve this spec; it appears after a restart",mod.c_str(),added.spec.id.c_str());later.push_back(added.spec.id+": the model can't serve this spec");continue;}
            }
            g_actors.push_back(std::move(added));
            const uint32_t i=uint32_t(g_actors.size()-1);
            if(g_scratch){st32(g_scratch+128+i*4,0xFFFFFFFFu);st8(g_scratch+kTalkLatchOffset+i,0);std::memset(mem::ptr(g_scratch+kIdentityScratchOffset+i*identity::kRecordBytes),0,identity::kRecordBytes);}
            LOG("[actors] %s:%s added by the editor",mod.c_str(),g_actors.back().spec.id.c_str());
            continue;
        }
        Actor* a=step.actor;
        const bool was_removed=a->removed;
        a->removed=false;
        if(step.update){a->spec=*step.spec;a->respawn=true;}
        LOG("[actors] %s:%s %s by the editor",mod.c_str(),a->spec.id.c_str(),was_removed?"restored":"changed");
    }
    for(Actor* a:remove){a->removed=true;a->respawn=false;LOG("[actors] %s:%s removed by the editor",mod.c_str(),a->spec.id.c_str());}
    if(pending)*pending=std::move(later);
    return true;
}
unsigned editor_pending(){unsigned n=0;for(const auto& a:g_actors)n+=a.respawn;return n;}
bool position(Cpu* c,const std::string& mod,const std::string& id,story_notebook::WorldPoint& p){const uint32_t a=actor_address(c,mod,id);if(!a)return false;p={u32_as_f32(ld32(a+0x314)),u32_as_f32(ld32(a+0x318)),u32_as_f32(ld32(a+0x31C))};return true;}
bool particle_position(Cpu* c,const std::string& mod,const std::string& id,unsigned room,story_notebook::WorldPoint& out){
    out={};if(!c||!g_installed||!g_scratch||room>63)return false;
    for(auto& entry:g_actors)if(entry.mod==mod&&entry.spec.id==id){
        if(entry.process==0xFFFFFFFFu||entry.deleting||entry.restore_quarantined||entry.spec.room!=room||
           std::strncmp(reinterpret_cast<const char*>(mem::ptr(kStage)),entry.spec.stage.c_str(),8)!=0||
           std::memcmp(mem::ptr(g_scratch+0x300),mem::ptr(kStage),8)!=0)return false;
        const uint32_t actor=actor_address(c,mod,id),pid=entry.process;
        if((actor&3)||actor<mem::kMem2Start||uint64_t(actor)+0xA24>mem::kMem2End||
           ld32(actor+4)!=pid||ld8(actor+0x326)!=room||find(actor)!=&entry)return false;
        if(entry.spec.kind=="mod"){
            mod_actor::Identity identity;
            if(!entry.mod_token||!mod_actor::identify(c,actor,identity)||identity.mod!=mod||identity.id!=id||
               identity.token!=entry.mod_token||identity.process_id!=pid)return false;
        }else if(entry.spec.actor=="resident"){
            if((entry.spec.resident==kYm1?ym1(actor):people(actor))!=&entry)return false;
        }else if(!entry.prop_token&&!entry.enemy_token&&!entry.switch_token)return false;
        if((entry.prop_token||entry.enemy_token||entry.switch_token)&&
           !identity::matches(mem::ptr(g_scratch+kIdentityScratchOffset+uint32_t(&entry-g_actors.data())*identity::kRecordBytes),entry.fingerprint))return false;
        const uint32_t stage=ld32(GD(0x1047E6C4)),active_room=ld32(GD(0x1047E8F0)+room*0x22C);
        if(!stage||!active_room||ld32(g_scratch+0x318)!=stage||ld32(g_scratch+0x400+room*4)!=active_room)return false;
        out={u32_as_f32(ld32(actor+0x314)),u32_as_f32(ld32(actor+0x318)),u32_as_f32(ld32(actor+0x31C))};
        return std::isfinite(out.x)&&std::isfinite(out.y)&&std::isfinite(out.z);
    }
    return false;
}
bool prop_texture_identity(uint32_t actor,uint32_t pid,uint32_t& token,uint32_t& epoch,uint32_t& generation){
    token=epoch=generation=0;
    if(!g_installed||!g_scratch||actor<mem::kMem2Start||actor>mem::kMem2End-story_props::kClassBytes||
       !pid||pid==0xFFFFFFFFu||ld32(actor+4)!=pid||ld16(actor+0xE)!=story_props::kProfile)return false;
    Actor* a=find(actor);if(!a||!a->prop_token||!a->spec.texture_token||a->process!=pid)return false;
    const size_t index=size_t(a-g_actors.data());if(ld32(g_scratch+128+uint32_t(index)*4)!=pid||
       !identity::matches(mem::ptr(g_scratch+kIdentityScratchOffset+uint32_t(index)*identity::kRecordBytes),a->fingerprint))return false;
    if(a->spec.room>63||ld8(actor+0x326)!=a->spec.room||
       std::strncmp(reinterpret_cast<const char*>(mem::ptr(kStage)),a->spec.stage.c_str(),8)!=0||
       std::memcmp(mem::ptr(g_scratch+0x300),mem::ptr(kStage),8)!=0)return false;
    const uint32_t stage=ld32(GD(0x1047E6C4)),room=ld32(GD(0x1047E8F0)+a->spec.room*0x22C);
    if(!stage||!room||ld32(g_scratch+0x318)!=stage||ld32(g_scratch+0x400+a->spec.room*4)!=room)return false;
    token=a->prop_token;epoch=stage;generation=room;return true;
}
bool actions(const std::string& mod,const std::vector<story_notebook::StoryAction>& list) {
    using Kind=story_notebook::StoryAction::Kind;
    for(const auto& a:list){bool ok=false;switch(a.kind){
        case Kind::Give:ok=story_items::give(mod,a.id.substr(5),a.value,"actor dialogue")>=0;break;
        case Kind::Take:ok=story_items::take(mod,a.id.substr(5),a.value)>=0;break;
        case Kind::QuestStep:ok=story_notebook::reach(mod,a.id,a.value)>=0;break;
        case Kind::Flag:ok=set_flag(story_store::book(),mod,a.id,a.value!=0);break;
        case Kind::StartScene:ok=g_scene&&g_scene(mod,a.id);break;
    }if(!ok){LOG("[actors] %s: action failed (%s)",mod.c_str(),a.id.c_str());return false;}}return true;
}
bool people_message(Cpu* c){Actor* a=people(c->r[3]);if(!a||a->spec.actor!="resident")return false;
    // A placed resident never falls through to original dialogue (which sets first-talk bits).
    const uint32_t layer=ld32(c->r[3]+0x2C);
    if((layer&&scene_is_deleting(ld32(layer+0x18)))||!story_mail::can_say()){c->r[3]=0;return true;}
    if(g_talk.npc&&g_talk.npc!=c->r[3]){c->r[3]=0;return true;}
    bind_talk(a,c->r[3]);a->restore_talk=false;native_trace(a,c->r[3],"text-bind");
    LOG("[actors] %s:%s native dialogue uses mod text (%zu UTF-16 units)",a->mod.c_str(),a->spec.id.c_str(),g_talk.text.size());
    st32(c->r[3]+0x8A8,0);st32(c->r[3]+0x8AC,0);c->r[3]=kCarrier;return true;
}
bool people_next_message(Cpu* c){Actor* a=people(c->r[3]);if(!a||a->spec.actor!="resident")return false;c->r[3]=0x10;return true;}
bool people_execute(Cpu* c,bool before){Actor* a=people(c->r[3]);if(!a||a->spec.actor!="resident")return false;
    if(before){
        const uint32_t npc=c->r[3],layer=ld32(npc+0x2C);
        if(layer&&scene_is_deleting(ld32(layer+0x18))){if(g_talk.npc==npc)cancel_talk("draining");a->restore_talk=false;}
        else if(a->restore_talk){
            a->restore_talk=false;
            if(!g_talk.actor&&ld16(npc+0xF8)==1&&ld32(npc+0x7C0)==kCarrier&&ld8(npc+0x7CC)<=1&&
               (ld32(npc+0x7C8)!=0xFFFFFFFFu||ld16(npc+0x8F0)==0x12)&&story_mail::can_say()){bind_talk(a,npc,ld16(npc+0x8F0)==0x12&&ld32(npc+0x7C8)==0xFFFFFFFFu);native_trace(a,npc,"restore-text-rebind");}
        }
        // Native wait/talk and visual/collision updates are retained. Script entry points are
        // intercepted separately, so no game-save snapshot/rollback is needed.
        st16(c->r[3]+0xFC,0xFFFF);st8(c->r[3]+0x910,1);
        // Item-show and photography callbacks belong to the retail character's quests.
        st32(c->r[3]+0x100,0);st32(c->r[3]+0x104,0);st32(c->r[3]+0x108,0);
    } else if(g_talk.npc==c->r[3]&&ld16(c->r[3]+0xF8)==0){if(g_talk.terminal)end_talk();else cancel_talk("nonterminal-cancel");}
    return true;
}

void install(const std::function<std::string(uint32_t)>& replaced_by,const std::function<void(uint32_t,void(*)(Cpu*))>& replace) {
    if(g_installed||!declared()||!g_original||!g_call||!g_scratch)return;
    std::vector<std::pair<uint32_t,void(*)(Cpu*)>> hooks={{kChestCheck,chest_check},{kChestOpen,chest_open},{kOnBox,chest_on},{kSignExecute,sign_execute},{kSignDelete,sign_delete},{kPeopleCheckOrder,people_check_order},{kPeopleEventMove,people_event_move},{kPeopleEventOrder,people_event_order}};
    if(std::any_of(g_actors.begin(),g_actors.end(),[](const Actor& a){return a.spec.actor=="resident";})){
        const std::pair<uint32_t,void(*)(Cpu*)> resident_hooks[]={
            {kPeopleDelete,people_delete},{kPeopleTalk,people_talk},
            {kPhotoActor,photo_actor},{kPhotoPosition,photo_position},{kPhotoSnapshot,photo_snapshot},
            {kNamedCandidate,named_candidate},{kStaffCandidate,staff_candidate},{kExtraOn,extra_on},{kExtraOff,extra_off},{kStaffId,staff_id}};
        hooks.insert(hooks.end(),std::begin(resident_hooks),std::end(resident_hooks));
        g_resident_hooks=true;
    }
    if(std::any_of(g_actors.begin(),g_actors.end(),[](const Actor& a){return a.spec.actor=="resident"&&a.spec.resident==kYm1;})){
        const std::pair<uint32_t,void(*)(Cpu*)> seated_hooks[]={{kYm1Init,ym1_init},{kYm1Msg,ym1_message},{kYm1Next,ym1_next},{kYm1Talk,ym1_talk},{kYm1Execute,ym1_execute},{kYm1Delete,ym1_delete},{kYm1Demo,ym1_demo},{kYm1Staff,ym1_staff},{kYm1CheckTalk,ym1_check_talk}};
        hooks.insert(hooks.end(),std::begin(seated_hooks),std::end(seated_hooks));
        g_ym1_hooks=true;
    }
    if(!story_treasure::installed())hooks.push_back({kRoom,room_hook});
    if(replaced_by(kPeopleMsg).empty()) {hooks.push_back({kPeopleMsg,people_msg_hook});hooks.push_back({kPeopleNext,people_next_hook});hooks.push_back({kPeopleExecute,people_execute_hook});}
    for(const auto& h:hooks)if(!replaced_by(h.first).empty()){LOG("[actors] hook conflict %08X; world actors disabled",h.first);return;}
    mod_actor::configure(g_original,g_call,g_scratch+kModScratchOffset);
    story_props::configure(g_original,g_call,g_scratch+kPropsScratchOffset,[](uint32_t token,uint32_t pid){
        if(!g_scratch)return;for(size_t i=0;i<g_actors.size();++i){auto&a=g_actors[i];const uint32_t slot=g_scratch+128+uint32_t(i)*4;if(a.prop_token==token&&a.process==pid&&ld32(slot)==pid){a.process=0xFFFFFFFFu;a.deleting=false;st32(slot,0xFFFFFFFFu);std::memset(mem::ptr(g_scratch+kIdentityScratchOffset+uint32_t(i)*identity::kRecordBytes),0,identity::kRecordBytes);return;}}
    });
    story_switches::configure(g_original,g_call,g_scratch+kSwitchScratchOffset,g_scratch+kDoorScratchOffset,
        [](uint32_t token,bool& on){for(const auto& a:g_actors)if(a.spec.actor=="switch"&&a.switch_token==token){const auto book=story_store::book();if(!book.usable()||!book.has(kRecordActorFlag,flag_key(a.mod,a.spec.switch_flag)))return false;on=flag(book,a.mod,a.spec.switch_flag);return true;}return false;},
        [](uint32_t token,bool on){for(const auto& a:g_actors)if(a.spec.actor=="switch"&&a.switch_token==token)return set_flag(story_store::book(),a.mod,a.spec.switch_flag,on);return false;},
        [](uint32_t token,uint32_t pid){if(!g_scratch)return;for(size_t i=0;i<g_actors.size();++i){auto&a=g_actors[i];const uint32_t slot=g_scratch+128+uint32_t(i)*4;if(a.switch_token==token&&a.process==pid&&ld32(slot)==pid){a.process=0xFFFFFFFFu;a.deleting=false;st32(slot,0xFFFFFFFFu);std::memset(mem::ptr(g_scratch+kIdentityScratchOffset+uint32_t(i)*identity::kRecordBytes),0,identity::kRecordBytes);return;}}});
    const bool has_enemies=enemies_declared(),has_combat=combat_declared();
    if(has_combat){
        if(!story_enemy_ownership::configure(g_scratch+kEnemyOwnershipScratchOffset,story_enemy_ownership::kStorageBytes,g_original,story_enemies::verify_actor))exception_report::raise("Cannot configure combat snapshot ownership");
        story_enemy_ownership::initialize_storage();
        if(!story_enemy_projectiles::configure(g_original,pure_link,true))exception_report::raise("Cannot configure combat projectile ownership");
        if(has_enemies){
            const std::array<uint32_t,4> profiles={g_scratch+kEnemyProfilesScratchOffset,g_scratch+kEnemyProfilesScratchOffset+0x30,g_scratch+kEnemyProfilesScratchOffset+0x60,g_scratch+kEnemyProfilesScratchOffset+0x90};
            story_enemies::configure(g_original,g_call,profiles,[](uint32_t token,uint32_t pid){if(!g_scratch)return;for(size_t i=0;i<g_actors.size();++i){auto&a=g_actors[i];const uint32_t slot=g_scratch+128+uint32_t(i)*4;if(a.enemy_token==token&&a.process==pid&&ld32(slot)==pid){a.process=0xFFFFFFFFu;a.deleting=false;st32(slot,0xFFFFFFFFu);return;}}});
            if(!story_enemy_children::configure(g_original,g_call,story_enemies::child_retired)||!native_actor_carry::configure(g_original,g_call)||!native_actor_carry::register_guard("enemies",story_enemies::deny_carry))exception_report::raise("Cannot configure native enemy isolation bridges");
        }
        if(!story_enemy_ownership::install(replaced_by,replace)||!story_enemy_projectiles::install(replaced_by,replace)||(has_enemies&&!story_enemy_children::install(replaced_by,replace)))exception_report::raise("Cannot install combat ownership hooks");
    }
    bool has_mod=false;
    for(size_t i=0;i<g_actors.size();++i){auto& a=g_actors[i];if(a.spec.kind!="mod")continue;
        auto model=g_models.find({a.mod,a.spec.model});if(model==g_models.end()){exception_report::raise("Prepared model unavailable for mod actor "+a.mod+":"+a.spec.id);}
        const std::string mod=a.mod,id=a.spec.id;
        a.mod_token=mod_actor::declare(mod,a.spec,model->second.archive,model->second.index,mod_talked(mod,id),model->second.minimum,model->second.maximum,model->second.skinned,model->second.clips,model->second.rig);if(!a.mod_token)exception_report::raise("Cannot declare generic mod actor "+mod+":"+id);has_mod=true;
        if(a.spec.combat_enabled){const auto marker=a.combat_marker;std::vector<story_items::PropDrop> drops;for(const auto&drop:a.spec.drops)drops.push_back({drop.item,drop.quantity});
            if(!mod_actor::set_combat_reward(mod,id,[marker,mod,drops](){const auto result=story_items::claim_enemy_drops(marker,mod,drops);return result==story_items::EnemyClaimStatus::Committed?mod_actor::CombatReward::Committed:result==story_items::EnemyClaimStatus::AlreadyDefeated?mod_actor::CombatReward::AlreadyDone:mod_actor::CombatReward::Failed;}))exception_report::raise("Cannot bind generic combat rewards");}
        if(a.spec.carry_break_on_impact){const auto marker=a.carry_marker;std::vector<story_items::PropDrop> drops;for(const auto&drop:a.spec.drops)drops.push_back({drop.item,drop.quantity});
            if(!mod_actor::set_carry_break(mod,id,[marker,mod,drops](){const auto result=story_items::claim_prop_drops(marker,mod,drops);return result==story_items::PropClaimStatus::Committed||result==story_items::PropClaimStatus::AlreadyBroken;}))exception_report::raise("Cannot bind generic carry impact rewards");}
    }
    if(has_mod&&!mod_actor::install(replaced_by,replace))exception_report::raise("Cannot install generic mod actor profile");
    if(has_mod){mod_actor_shadow_proof::configure(g_original,g_call);mod_actor_shadow_proof::install(replaced_by,replace);}
    if(std::any_of(g_actors.begin(),g_actors.end(),[](const Actor&a){return a.prop_token!=0;})&&!story_props::install(replaced_by,replace))exception_report::raise("Cannot install isolated native prop profile");
    if(std::any_of(g_actors.begin(),g_actors.end(),[](const Actor&a){return a.switch_token!=0;})&&!story_switches::install(replaced_by,replace))exception_report::raise("Cannot install isolated native switch/door profiles");
    if(has_enemies&&!story_enemies::install(replaced_by,replace))exception_report::raise("Cannot install isolated native enemy profiles");
    for(const auto& h:hooks)replace(h.first,h.second);g_installed=true;
}
}
