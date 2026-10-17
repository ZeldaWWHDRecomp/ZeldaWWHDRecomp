#include "story_scenes.h"
#include "story_mail.h"
#include "story_scene_choice.h"
#include "story_scene_motion.h"
#include "story_particles.h"
#include "mod_actor.h"
#include "story_notebook_game.h"
#include "guest_addr.h"
#include "runtime.h"
#include "input.h"
#include <atomic>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <utility>

namespace story_scenes {
namespace {
using namespace story_notebook;
const uint32_t kPlay=GC(0x025200D4),kOrder=GC(0x0253EC0C),kCheckStart=GC(0x0253FD1C),kEnd=GC(0x0253EFA8);
const uint32_t kCamera=GC(0x024F8044),kStopCamera=GC(0x02514F2C),kStartCamera=GC(0x02514F38),kSetCamera=GC(0x02514F50),kResetCamera=GC(0x02515048),kTrim=GC(0x02515280);
const uint32_t kMessageManager=GD(0x101F4B5C),kMessage=GC(0x025F7DB0),kMessageStatus=GC(0x025F795C),kSetMessageStatus=GC(0x025F74D0),kResetMessage=GC(0x025F78C4),kFade=GC(0x025F069C);
constexpr uint32_t kControl=0x51D0,kMode=0x5292,kOrderCount=0x5290,kStage=0x5134,kPlayer=0x5B34,kPos=0x314,kRoom=0x326;
const uint32_t kCalcTrim=GC(0x024FF8A0),kHideHud=GC(0x025916C4),kShowHud=GC(0x0259169C);
const uint32_t kSearchActor=GC(0x025D54C4);
const uint32_t kSetAnm=GC(0x022BED70),kPlayAnm=GC(0x022C2D6C),kPlayTex=GC(0x022C2CC0),kSetMtx=GC(0x022BF3A0),kPosMove=GC(0x025D6870),kGround=GC(0x024F08A8),kCollision=GC(0x022BF480);
const uint32_t kWalkTable=GD(0x101C31BA),kWaitTable=GD(0x101C31B4);
constexpr uint32_t kMarker=0x53434E33,kCarrier=0x2CF3;
uint32_t (*g_call)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr;
uint32_t g_scratch=0,g_buttons=0;
unsigned g_frame_units=1;
void (*g_original)(Cpu*,uint32_t)=nullptr;
bool g_trim_installed=false,g_choice_installed=false;
std::atomic<bool> g_active_input{false},g_swallow_plus{false};
uint64_t g_generation=0;
bool g_loaded=false,g_suppress_loaded_room=false,g_cleanup_pending=false,g_cleanup_loaded=false,g_abort_pending=false;
Cpu* g_cpu=nullptr;
Resolve g_resolve;
Actions g_actions;ResolvePlace g_places;ResolveParticle g_particle_resolve;std::map<std::string,MusicHook> g_music;
StopReason g_restore_reason=StopReason::Aborted;uint32_t g_lease_serial=0;
motion::Path g_camera_path;motion::Follow g_camera_follow;
struct Entry {std::string mod;ManifestScene scene;};
std::vector<Entry> g_entries;
RequestQueue g_queue;std::set<size_t> g_pending;
std::set<size_t> g_fired;
size_t g_current=0;
Player g_player;
std::string g_room;
std::u16string g_text;
unsigned g_message_phase=0;std::array<std::u16string,2> g_choice_labels;
uint32_t call(uint32_t fn,std::initializer_list<uint32_t> args={}) { Cpu c=*g_cpu;return g_call(&c,fn,args); }
void scalar_call(uint32_t fn,float value) {Cpu c=*g_cpu;c.f[1].ps0=c.f[1].ps1=value;g_call(&c,fn,{});}
uint32_t resolve(const std::string& id) {
    if(id=="link")return ld32(call(kPlay)+kPlayer);
    return g_resolve?g_resolve(g_cpu,g_entries[g_current].mod,id):0;
}
WorldPoint position(uint32_t actor) {return {u32_as_f32(ld32(actor+kPos)),u32_as_f32(ld32(actor+kPos+4)),u32_as_f32(ld32(actor+kPos+8))};}
void point(uint32_t at,const WorldPoint& p){st32(at,f32_as_u32(p.x));st32(at+4,f32_as_u32(p.y));st32(at+8,f32_as_u32(p.z));}
bool same_room(uint32_t play) {
    const uint32_t link=ld32(play+kPlayer);
    return link&&std::memcmp(mem::ptr(play+kStage),mem::ptr(g_scratch+16),8)==0&&ld8(link+kRoom)==ld8(g_scratch+24);
}
bool owns(uint32_t play) {
    return play&&play==ld32(g_scratch+4)&&ld8(play+kMode)==3&&ld16(play+kControl+0xD8)==0xFFFF&&ld32(play+kControl+0xC4)==0xFFFFFFFF&&ld32(play+kControl+0xC8)==0xFFFFFFFF;
}

constexpr uint32_t ChoiceScratch=256,ActorLeases=768,ActorLeaseStride=12,MusicMask=200,LeaseID=224,SceneIndex=168;
bool scene_lease(Cpu* cpu){if(!cpu||!g_scratch||ld32(g_scratch)!=kMarker)return false;Cpu copy=*cpu;return owns(g_call(&copy,kPlay,{}));}
mod_actor::Identity identity(uint32_t actor){mod_actor::Identity value;if(!actor||!mod_actor::identify(g_cpu,actor,value)||value.mod!=g_entries[g_current].mod)return {};return value;}
bool lease_actor(const mod_actor::Identity& value,uint32_t actor){
    if(!value.token||!ld32(g_scratch+LeaseID))return false;
    for(unsigned i=0;i<16;++i){const uint32_t slot=g_scratch+ActorLeases+i*ActorLeaseStride;
        if(ld32(slot)==actor&&ld32(slot+4)==value.process_id&&ld32(slot+8)==value.token)return true;
        if(!ld32(slot)){if(!mod_actor::begin_scene(g_cpu,value,ld32(g_scratch+LeaseID)))return false;st32(slot,actor);st32(slot+4,value.process_id);st32(slot+8,value.token);return true;}}
    return false;
}
void release_actors(){
    const uint32_t index=ld32(g_scratch+SceneIndex);if(index>=g_entries.size())return;
    for(unsigned i=0;i<16;++i){const uint32_t slot=g_scratch+ActorLeases+i*ActorLeaseStride,actor=ld32(slot);if(!actor)continue;
        mod_actor::Identity value;if(mod_actor::identify(g_cpu,actor,value)&&value.mod==g_entries[index].mod&&value.process_id==ld32(slot+4)&&value.token==ld32(slot+8))mod_actor::end_scene(g_cpu,value,ld32(g_scratch+LeaseID));st32(slot,0);}
}
bool music_preflight(const Entry& entry){for(const auto& step:entry.scene.steps)if(step.kind==SceneStep::Kind::Music&&!g_music.count(entry.mod)){LOG("[scenes] %s:%s requires an owned music hook",entry.mod.c_str(),entry.scene.id.c_str());return false;}return true;}
bool dispatch_music(const MusicHook& hook,const std::string& cue,MusicPhase phase,unsigned fade,StopReason reason){try{return hook(g_cpu,cue,phase,fade,reason);}catch(...){LOG("[scenes] owned music hook raised an exception");return false;}}
bool restore_music(){
    const uint32_t index=ld32(g_scratch+SceneIndex);if(index>=g_entries.size())return false;const auto& entry=g_entries[index];const auto found=g_music.find(entry.mod);bool success=true;
    std::set<std::string> restored;
    for(size_t i=0;i<entry.scene.steps.size();++i){const uint32_t at=g_scratch+MusicMask+uint32_t(i/32)*4;const uint32_t bit=1u<<(i%32);if(!(ld32(at)&bit))continue;st32(at,ld32(at)&~bit);
        const auto& step=entry.scene.steps[i];if(step.kind==SceneStep::Kind::Music&&restored.insert(step.cue).second&&found!=g_music.end()){if(!dispatch_music(found->second,step.cue,MusicPhase::Restore,step.fade_frames,g_restore_reason)){success=false;LOG("[scenes] music hook restore failed for %s:%s",entry.mod.c_str(),step.cue.c_str());}}}return success;
}
bool distinct(motion::Point a,motion::Point b){double x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return x*x+y*y+z*z>1e-6;}
motion::Point motion_point(WorldPoint p){return {p.x,p.y,p.z};}
bool submit_camera(const motion::Camera& view){if(!motion::valid(view.eye)||!motion::valid(view.target)||!distinct(view.eye,view.target))return false;point(g_scratch+80,{float(view.target.x),float(view.target.y),float(view.target.z)});point(g_scratch+92,{float(view.eye.x),float(view.eye.y),float(view.eye.z)});call(kSetCamera,{ld32(g_scratch+8),g_scratch+80,g_scratch+92});return true;}
bool preflight(const Entry& entry){
    if(!music_preflight(entry))return false;
    for(const auto& step:entry.scene.steps){
        if(step.kind==SceneStep::Kind::Choice&&!g_choice_installed)return false;
        if(step.kind==SceneStep::Kind::Animation||step.kind==SceneStep::Kind::Walk){const uint32_t actor=resolve(step.actor);const auto value=identity(actor);if(!value.token)return false;
            if(step.kind==SceneStep::Kind::Animation&&!mod_actor::has_animation(g_cpu,value,step.clip))return false;
            if(step.kind==SceneStep::Kind::Walk&&!step.quest_place.empty()){if(!g_places)return false;const auto target=g_places(g_cpu,entry.mod,step.quest_place);if(!target)return false;const auto from=position(actor);const double dx=target->x-from.x,dz=target->z-from.z;if(!std::isfinite(dx)||!std::isfinite(dz)||dx*dx+dz*dz>10000.*10000.)return false;}}
    }return true;
}

void finish_move() {
    const uint32_t actor=ld32(g_scratch+108);
    if(!actor)return;
    st32(g_scratch+140,0);
    if(!call(kSearchActor,{ld32(g_scratch+112),g_scratch+140})||ld32(g_scratch+140)!=actor){st32(g_scratch+108,0);return;}
    st32(actor+0x370,ld32(g_scratch+116));
    std::memcpy(mem::ptr(actor+0x33C),mem::ptr(g_scratch+124),12);
    const uint32_t table=ld32(g_scratch+120);
    call(kSetAnm,{actor,table?table:kWaitTable,1});
    call(kPlayAnm,{actor});call(kSetMtx,{actor});st32(g_scratch+108,0);
}
void resident_visual(uint32_t actor) {
    call(kPlayTex,{actor});call(kPlayAnm,{actor});
    const uint32_t dat=ld32(actor+0x8A0);
    Cpu c=*g_cpu;c.f[1].ps0=c.f[1].ps1=u32_as_f32(ld32(actor+0x8C0));c.f[2].ps0=c.f[2].ps1=u32_as_f32(ld32(dat+0x40));
    g_call(&c,kCollision,{actor,actor+0x690,actor+kPos});
    call(kSetMtx,{actor});
}
WorldPoint mix(WorldPoint a,WorldPoint b,float t) {return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}
// Snapshot lives in guest scratch as well as the ownership marker, so loading a full state
// captured mid-scene can release its lease even after the host player has already finished.
void restore(bool loaded,bool immediate=false) {
    immediate=immediate||loaded;
    g_active_input.store(false,std::memory_order_relaxed);
    story_mail::unsay();
    if(!g_cpu||!g_call||!g_scratch||ld32(g_scratch)!=kMarker){g_cleanup_pending=false;return;}
    const uint32_t play=call(kPlay),camera=call(kCamera);
    const bool ours=owns(play);
    if(!g_cleanup_pending){
        const uint32_t guest_index=ld32(g_scratch+SceneIndex);
        const Entry* guest=guest_index<g_entries.size()?&g_entries[guest_index]:nullptr;
        const Entry* host=g_current<g_entries.size()?&g_entries[g_current]:nullptr;
        LOG("[scenes] cleanup guest_owner=%s guest_scene=%s guest_index=%u lease=%u host_owner=%s host_scene=%s reason=%u native_owned=%u",
            guest?guest->mod.c_str():"invalid",guest?guest->scene.id.c_str():"invalid",guest_index,ld32(g_scratch+LeaseID),
            host?host->mod.c_str():"invalid",host?host->scene.id.c_str():"invalid",unsigned(g_restore_reason),unsigned(ours));
    }
    finish_move();release_actors();restore_music();story_scene_choice::close(g_cpu);g_camera_follow.reset();
    if(ours) {
        const uint32_t manager=ld32(kMessageManager);
        if(manager&&ld8(g_scratch+12)) {
            // setStatus is only a play-state byte write. Native window states must observe
            // FINISH (16), animate through their close states and report CLOSED (18)
            // before DELETE (19). An immediate 16 -> 19 leaves the native window wedged.
            const uint32_t status=call(kMessageStatus,{manager});
            if(status==18){call(kSetMessageStatus,{manager,19});st8(g_scratch+12,0);LOG("[scenes] native message close complete");}
            else if(status==0||status==19)st8(g_scratch+12,0);
            else if(immediate) {
                // FINISH may require another confirm in the HD window state machine.
                // An aborted scene cannot wait for that input: reset the native message
                // core while this lease still owns the event/message, then restore control.
                LOG("[scenes] aborted message: native message core reset (status %u)",status);
                call(kResetMessage,{manager});st8(g_scratch+12,0);
            } else {
                if(!ld8(g_scratch+13)) {
                    call(kSetMessageStatus,{manager,16});st8(g_scratch+13,1);st32(g_scratch+160,0);
                    LOG("[scenes] waiting for native message close (status %u)",status);
                } else st32(g_scratch+160,ld32(g_scratch+160)+1);
                if(ld32(g_scratch+160)<180) {
                    g_cleanup_pending=true;g_cleanup_loaded=g_cleanup_loaded||loaded;
                    g_active_input.store(true,std::memory_order_relaxed);return;
                }
                // Corrupt/interrupted message states must not retain control indefinitely.
                // Native core reset clears processor, screen/render, sequence and resources.
                LOG("[scenes] message close deadline: native message core reset");
                call(kResetMessage,{manager});st8(g_scratch+12,0);
            }
        }
        if(camera&&camera==ld32(g_scratch+8)&&same_room(play)) {
            // Camera writes are limited to verified view fields; native reset refreshes caches.
            std::memcpy(mem::ptr(camera+0x10),mem::ptr(g_scratch+32),24);
            call(kResetCamera,{camera});call(kTrim,{camera,ld32(g_scratch+60)});
            st32(camera+0x5FC,ld32(g_scratch+148));st32(camera+0x604,ld32(g_scratch+152));
            if(ld8(g_scratch+64))call(kStartCamera,{camera});
        }
        call(ld8(g_scratch+156)?kShowHud:kHideHud);
        scalar_call(kFade,-1.0f); // restore visibility even if interrupted after fade-out
        call(kEnd,{play+kControl});
    }
    st32(g_scratch,0);g_active_input.store(false,std::memory_order_relaxed);
    g_cleanup_pending=false;loaded=loaded||g_cleanup_loaded;g_cleanup_loaded=false;
    if(loaded)LOG("[scenes] state load: scene lease %s",ours?"released":"absent; loaded game event untouched");
}
class NativeDriver final:public Driver {
public:
    bool acquire() override {
        if(!g_cpu||!g_call||!g_scratch||!g_trim_installed||!preflight(g_entries[g_current]))return false;
        const uint32_t play=call(kPlay),camera=call(kCamera);
        if(!play||!camera||!ld32(play+kPlayer)||ld8(play+kMode)||ld8(play+kOrderCount))return false;
        // Native compulsory admission with no event resource/partners: no stage flags, no
        // event-list pointer replacement and no saved game event bits are involved.
        std::array<uint8_t,0xF4> previous;
        std::memcpy(previous.data(),mem::ptr(play+kControl),previous.size());
        if(!call(kOrder,{play+kControl,3,1,0,0,0,0,0xFFFFFFFFu,0xFF}))return false;
        if(!call(kCheckStart,{play+kControl})||ld8(play+kMode)!=3) {
            std::memcpy(mem::ptr(play+kControl),previous.data(),previous.size());
            return false;
        }
        std::memset(mem::ptr(g_scratch+168),0,kScratchBytes-168);st32(g_scratch+SceneIndex,uint32_t(g_current));if(!++g_lease_serial)++g_lease_serial;st32(g_scratch+LeaseID,g_lease_serial);
        st32(g_scratch+108,0);st32(g_scratch,kMarker);st32(g_scratch+4,play);st32(g_scratch+8,camera);st8(g_scratch+12,0);st8(g_scratch+13,0);st32(g_scratch+160,0);
        std::memcpy(mem::ptr(g_scratch+32),mem::ptr(camera+0x10),24);
        std::memcpy(mem::ptr(g_scratch+16),mem::ptr(play+kStage),8);st8(g_scratch+24,ld8(ld32(play+kPlayer)+kRoom));
        st32(g_scratch+60,ld32(camera+0x600));st8(g_scratch+64,ld8(camera+4));
        st32(g_scratch+148,ld32(camera+0x5FC));st32(g_scratch+152,ld32(camera+0x604));st8(g_scratch+156,ld8(play+0x5C22));
        call(kStopCamera,{camera});call(kTrim,{camera,2});call(kHideHud);
        g_active_input.store(true,std::memory_order_relaxed);
        LOG("[scenes] %s:%s started (native compulsory event / direct camera-message fallback)",g_entries[g_current].mod.c_str(),g_entries[g_current].scene.id.c_str());
        return true;
    }
    bool enter(size_t index) override {
        const SceneStep& step=g_entries[g_current].scene.steps[index];
        LOG("[scenes] step %zu kind %u",index,unsigned(step.kind));
        if(step.kind==SceneStep::Kind::CameraPath){
            std::vector<motion::Key> keys;for(const auto& key:step.camera_keys)keys.push_back({{motion_point(key.eye),motion_point(key.target)},key.frames});
            return g_camera_path.set(std::move(keys),step.easing=="smoothstep"?motion::Easing::Smoothstep:motion::Easing::Linear);
        }else if(step.kind==SceneStep::Kind::CameraFollow){
            const uint32_t actor=resolve(step.actor);if(!actor)return false;st32(g_scratch+176,actor);st32(g_scratch+180,ld32(actor+4));
            return g_camera_follow.start((uint64_t(ld32(actor+4))<<32)|actor,motion_point(position(actor)),motion_point(step.eye_offset),motion_point(step.target_offset),step.half_life);
        }else if(step.kind==SceneStep::Kind::Animation){
            const uint32_t actor=resolve(step.actor);const auto value=identity(actor);return lease_actor(value,actor)&&mod_actor::scene_animation(g_cpu,value,ld32(g_scratch+LeaseID),step.clip,step.blend_seconds*30,step.loop);
        }else if(step.kind==SceneStep::Kind::Walk){
            const uint32_t actor=resolve(step.actor);const auto value=identity(actor);if(!lease_actor(value,actor))return false;
            auto points=step.walk_points;if(!step.quest_place.empty()){if(!g_places)return false;const auto target=g_places(g_cpu,g_entries[g_current].mod,step.quest_place);if(!target)return false;const auto from=position(actor);WorldPoint destination=*target;destination.y=from.y;const double dx=destination.x-from.x,dz=destination.z-from.z;if(!std::isfinite(dx)||!std::isfinite(dz)||dx*dx+dz*dz>10000.*10000.)return false;points={from,destination};}
            st32(g_scratch+176,actor);st32(g_scratch+180,value.process_id);return mod_actor::scene_path(g_cpu,value,ld32(g_scratch+LeaseID),points,step.speed);
        }else if(step.kind==SceneStep::Kind::Choice){
            g_text.clear();if(!story_mail::can_say()||!story_mail::append_utf16(step.text,true,g_text)||step.choices.size()!=2)return false;
            for(unsigned n=0;n<2;++n){g_choice_labels[n].clear();if(!story_mail::append_utf16(step.choices[n].text,false,g_choice_labels[n]))return false;}
            story_mail::say(kCarrier,&g_text);g_message_phase=0;st8(g_scratch+12,1);st8(g_scratch+13,0);return true;
        }else if(step.kind==SceneStep::Kind::Music){
            const auto found=g_music.find(g_entries[g_current].mod);if(found==g_music.end())return false;
            const uint32_t at=g_scratch+MusicMask+uint32_t(index/32)*4;st32(at,ld32(at)|(1u<<(index%32)));
            return dispatch_music(found->second,step.cue,step.music_action=="start"?MusicPhase::Start:MusicPhase::Stop,step.fade_frames,StopReason::Finished);
        }else if(step.kind==SceneStep::Kind::Particle){
            if(!g_cpu||!owns(call(kPlay))||!same_room(call(kPlay)))return false;
            WorldPoint where=step.position;
            if(step.actor=="link"){
                const uint32_t actor=resolve(step.actor);
                if((actor&3)||actor<mem::kMem2Start||uint64_t(actor)+kPos+12>mem::kMem2End||
                   !ld32(actor+4)||ld32(actor+4)==0xFFFFFFFFu||ld8(actor+kRoom)!=ld8(g_scratch+24)||
                   ld16(actor+0xE)!=0x00A8)return false;
                where=position(actor);
            }else if(!step.actor.empty()&&(!g_particle_resolve||
                    !g_particle_resolve(g_cpu,g_entries[g_current].mod,step.actor,ld8(g_scratch+24),where)))return false;
            return story_particles::burst_named(g_cpu,step.particle,
                {{where.x,where.y,where.z},int32_t(ld8(g_scratch+24))},
                {step.particle_scale,step.particle_alpha});
        }else if(step.kind==SceneStep::Kind::Move) {
            const uint32_t actor=resolve(step.actor);const auto value=identity(actor);
            if(value.token){const WorldPoint from=position(actor);const double dx=step.position.x-from.x,dy=step.position.y-from.y,dz=step.position.z-from.z;const float speed=float(std::sqrt(dx*dx+dy*dy+dz*dz)*60/std::max(1u,step.frames));if(!lease_actor(value,actor))return false;st32(g_scratch+176,actor);st32(g_scratch+180,value.process_id);return mod_actor::scene_path(g_cpu,value,ld32(g_scratch+LeaseID),{from,step.position},speed);}
            if(!actor||ld16(actor+8)!=0x175||!ld32(actor+0x8A0))return false;
            const WorldPoint from=position(actor);const float dx=step.position.x-from.x,dz=step.position.z-from.z;
            if(!step.frames||std::sqrt(dx*dx+dz*dz)/float(step.frames)>50.0f)return false;
            st32(g_scratch+108,actor);st32(g_scratch+112,ld32(actor+4));st32(g_scratch+116,ld32(actor+0x370));st32(g_scratch+120,ld32(actor+0x89C));
            std::memcpy(mem::ptr(g_scratch+124),mem::ptr(actor+0x33C),12);
            call(kSetAnm,{actor,kWalkTable,1});
        } else if(step.kind==SceneStep::Kind::Say) {
            g_text.clear();if(!story_mail::can_say()||!story_mail::append_utf16(step.text,true,g_text))return false;
            story_mail::say(kCarrier,&g_text);g_message_phase=0;st8(g_scratch+12,1);st8(g_scratch+13,0);st32(g_scratch+160,0);
        } else if(step.kind==SceneStep::Kind::Sound) {
            const auto& ui=story_notebook::game_ui();if(!ui.sound)return false;ui.sound(g_cpu,step.sound.c_str());
        } else if(step.kind==SceneStep::Kind::Fade) scalar_call(kFade,(step.fade_in?-1.0f:1.0f)*float(g_frame_units)/float(step.frames));
        else if(step.kind==SceneStep::Kind::Face) {
            const uint32_t actor=resolve(step.actor);if(!actor)return false;const auto value=identity(actor);if(!value.token&&ld16(actor+8)!=0x175)return false;
            WorldPoint target=step.position;
            if(!step.target_actor.empty()){const uint32_t other=resolve(step.target_actor);if(!other)return false;target=position(other);}
            const WorldPoint from=position(actor);
            const uint16_t angle=uint16_t(int32_t(std::atan2(target.x-from.x,target.z-from.z)*(32768.0/3.14159265358979323846)));
            if(value.token){mod_actor::State state;if(!mod_actor::snapshot(g_cpu,value,state))return false;state.rotation.y=float(angle)*(360.f/65536.f);if(!lease_actor(value,actor)||!mod_actor::scene_rotation(g_cpu,value,ld32(g_scratch+LeaseID),state.rotation))return false;}
            else {st16(actor+0x322,angle);st16(actor+0x32A,angle);call(kSetMtx,{actor});}
        }
        return true;
    }
    StepResult update(size_t index,unsigned elapsed) override {
        const SceneStep& step=g_entries[g_current].scene.steps[index];
        if(step.kind==SceneStep::Kind::CameraPath){motion::Camera view;if(!g_camera_path.sample(elapsed,view)||!submit_camera(view))return StepResult::Failed;
        }else if(step.kind==SceneStep::Kind::CameraFollow){const uint32_t actor=resolve(step.actor);if(!actor||actor!=ld32(g_scratch+176)||ld32(actor+4)!=ld32(g_scratch+180))return StepResult::Failed;motion::Camera view;if(!g_camera_follow.update((uint64_t(ld32(actor+4))<<32)|actor,motion_point(position(actor)),1./60,view)||!submit_camera(view))return StepResult::Failed;
        }else if(step.kind==SceneStep::Kind::Choice){
            if(!g_message_phase){const uint32_t manager=ld32(kMessageManager);if(!manager)return StepResult::Failed;const uint32_t status=call(kMessageStatus,{manager});if(status!=0&&status!=15)return StepResult::Running;if(!story_scene_choice::begin(g_cpu,g_choice_labels,step.default_choice))return StepResult::Failed;g_message_phase=1;return StepResult::Running;}
            unsigned selected=0;const auto result=story_scene_choice::poll(g_cpu,selected);if(result==story_scene_choice::Result::Failed)return StepResult::Failed;
            if(result==story_scene_choice::Result::Complete){if(selected>=step.choices.size())return StepResult::Failed;st32(g_scratch+184,uint32_t(step.choices[selected].next_step));story_mail::unsay();st8(g_scratch+12,0);return StepResult::Complete;}return StepResult::Running;
        }else if(step.kind==SceneStep::Kind::Walk||(step.kind==SceneStep::Kind::Move&&identity(resolve(step.actor)).token)){
            const uint32_t actor=resolve(step.actor);const auto value=identity(actor);if(!value.token||actor!=ld32(g_scratch+176)||value.process_id!=ld32(g_scratch+180))return StepResult::Failed;mod_actor::MotionState state;if(!mod_actor::get_motion(g_cpu,value,state))return StepResult::Failed;return state.finished?StepResult::Complete:StepResult::Running;
        }else if(step.kind==SceneStep::Kind::Move) {
            const uint32_t actor=resolve(step.actor);
            if(!actor||actor!=ld32(g_scratch+108)||ld32(actor+4)!=ld32(g_scratch+112))return StepResult::Failed;
            const WorldPoint from=position(actor);const float dx=step.position.x-from.x,dz=step.position.z-from.z;
            const float remaining=std::sqrt(dx*dx+dz*dz);
            if(remaining<=1.0f&&std::fabs(step.position.y-from.y)<=30.0f){finish_move();return StepResult::Complete;}
            const unsigned frames_left=step.frames>elapsed?step.frames-elapsed+1:1;
            const float speed=std::min(50.0f,remaining/float(frames_left));
            const uint16_t angle=uint16_t(int32_t(std::atan2(dx,dz)*(32768.0/3.14159265358979323846)));
            st16(actor+0x322,angle);st16(actor+0x32A,angle);st32(actor+0x370,f32_as_u32(speed));
            std::memcpy(mem::ptr(actor+0x300),mem::ptr(actor+kPos),12);
            call(kPosMove,{actor,actor+0x654});call(kGround,{actor+0x450,call(kPlay)+0x12A0});
            const WorldPoint to=position(actor);const WorldPoint delta{to.x-from.x,to.y-from.y,to.z-from.z};
            for(uint32_t off:{0x37Cu,0x390u}){WorldPoint p{u32_as_f32(ld32(actor+off)),u32_as_f32(ld32(actor+off+4)),u32_as_f32(ld32(actor+off+8))};point(actor+off,{p.x+delta.x,p.y+delta.y,p.z+delta.z});}
            resident_visual(actor);
            const float rx=step.position.x-to.x,rz=step.position.z-to.z;
            if(rx*rx+rz*rz<=1.0f&&std::fabs(step.position.y-to.y)<=30.0f){finish_move();return StepResult::Complete;}
            return StepResult::Running;
        } else if(step.kind==SceneStep::Kind::Camera) {
            WorldPoint eye=step.eye,target=step.target;
            if(step.pan){const float t=float(elapsed)/float(step.frames);eye=mix(eye,step.eye_to,t);target=mix(target,step.target_to,t);}
            if(!step.target_actor.empty()){uint32_t actor=resolve(step.target_actor);if(!actor)return StepResult::Failed;target=position(actor);}
            point(g_scratch+80,target);point(g_scratch+92,eye);call(kSetCamera,{ld32(g_scratch+8),g_scratch+80,g_scratch+92});
        } else if(step.kind==SceneStep::Kind::Say) {
            const uint32_t manager=ld32(kMessageManager);if(!manager)return StepResult::Failed;
            if(g_message_phase==0){if(call(kMessage,{manager,kCarrier,0})!=0xFFFFFFFFu)g_message_phase=1;return StepResult::Running;}
            const uint32_t status=call(kMessageStatus,{manager});
            if(g_message_phase==1&&status==14){call(kSetMessageStatus,{manager,16});g_message_phase=2;}
            else if(g_message_phase==2&&(status==18)){call(kSetMessageStatus,{manager,19});story_mail::unsay();st8(g_scratch+12,0);return StepResult::Complete;}
            return StepResult::Running;
        }
        return elapsed>=std::max(1u,step.frames)?StepResult::Complete:StepResult::Running;
    }
    size_t successor(size_t index) override {const auto& step=g_entries[g_current].scene.steps[index];if(step.kind==SceneStep::Kind::Choice)return ld32(g_scratch+184);return step.successors.empty()?index+1:step.successors.front();}
    bool finish() override {
        g_restore_reason=StopReason::Finished;if(!restore_music())return false;
        const auto& end=g_entries[g_current].scene.steps[g_player.step()];
        return end.then.empty()||(g_actions&&g_actions(g_entries[g_current].mod,end.then));
    }
    void release(StopReason reason) override {g_restore_reason=reason;restore(reason==StopReason::StateLoaded,reason!=StopReason::Finished);LOG("[scenes] ended (%u)",unsigned(reason));}
} g_driver;
}
void set_game_calls(uint32_t (*fn)(Cpu*,uint32_t,std::initializer_list<uint32_t>)){g_call=fn;}
void set_scratch_memory(uint32_t guest){g_scratch=guest;}
void set_callbacks(Resolve resolve_fn,Actions actions_fn){g_resolve=std::move(resolve_fn);g_actions=std::move(actions_fn);}
void set_place_resolver(ResolvePlace resolver){g_places=std::move(resolver);}
void set_particle_resolver(ResolveParticle resolver){g_particle_resolve=std::move(resolver);}
bool register_music_hook(const std::string& owner,MusicHook hook){if(owner.empty()||!hook||g_player.active()||g_cleanup_pending||g_music.count(owner))return false;g_music.emplace(owner,std::move(hook));return true;}
bool validate(const std::vector<ManifestScene>& scenes,std::string& why) {
    for(const auto& scene:scenes){
        if(scene.steps.empty()||scene.steps.size()>kMaxSceneSteps||scene.steps.back().kind!=SceneStep::Kind::End)return why="scene "+scene.id+": invalid step list",false;
        std::vector<Timing> timing;const bool graph=std::any_of(scene.steps.begin(),scene.steps.end(),[](const auto& step){return !step.successors.empty();});
        for(size_t i=0;i<scene.steps.size();++i){const auto& step=scene.steps[i];
            if(unsigned(step.kind)>unsigned(SceneStep::Kind::Particle))return why="unsupported scene step",false;
            if(step.kind!=SceneStep::Kind::Choice&&step.successors.size()>1)return why="only choices have two successors",false;
            if(!graph&&step.kind==SceneStep::Kind::End&&i+1!=scene.steps.size())return why="scene "+scene.id+": end must be last",false;
            const bool placed=step.kind==SceneStep::Kind::Move||step.kind==SceneStep::Kind::Face||step.kind==SceneStep::Kind::Animation||step.kind==SceneStep::Kind::Walk;
            if(placed&&(step.actor.empty()||step.actor=="link"))return why="scene "+scene.id+": move/face requires a placed resident or mod actor",false;
            const bool timed=step.kind==SceneStep::Kind::Camera||step.kind==SceneStep::Kind::Say||step.kind==SceneStep::Kind::Move||step.kind==SceneStep::Kind::Wait||step.kind==SceneStep::Kind::Fade||step.kind==SceneStep::Kind::CameraPath||step.kind==SceneStep::Kind::CameraFollow||step.kind==SceneStep::Kind::Walk||step.kind==SceneStep::Kind::Choice;
            if(timed&&!step.frames)return why="scene "+scene.id+": timed step requires a duration",false;
            if(step.kind==SceneStep::Kind::CameraPath){std::vector<motion::Key> keys;
                for(const auto& key:step.camera_keys){const auto eye=motion_point(key.eye),target=motion_point(key.target);if(!distinct(eye,target))return why="scene camera eye and target coincide",false;keys.push_back({{eye,target},key.frames});}
                motion::Path path;if(!path.set(keys,step.easing=="smoothstep"?motion::Easing::Smoothstep:motion::Easing::Linear)||path.duration()!=step.frames||(step.easing!="linear"&&step.easing!="smoothstep"))return why="invalid scene camera path",false;
                for(size_t n=1;n<keys.size();++n){const auto& a=keys[n-1].camera;const auto& b=keys[n].camera;const motion::Point d{a.eye.x-a.target.x,a.eye.y-a.target.y,a.eye.z-a.target.z},v{b.eye.x-b.target.x-d.x,b.eye.y-b.target.y-d.y,b.eye.z-b.target.z-d.z};const double square=v.x*v.x+v.y*v.y+v.z*v.z;const double t=square>0?std::clamp(-(d.x*v.x+d.y*v.y+d.z*v.z)/square,0.,1.):0.;if(!distinct({d.x+v.x*t,d.y+v.y*t,d.z+v.z*t},{}))return why="scene camera path crosses its target",false;}
            }else if(step.kind==SceneStep::Kind::CameraFollow){if(step.actor.empty()||!motion::valid(motion_point(step.eye_offset))||!motion::valid(motion_point(step.target_offset))||!distinct(motion_point(step.eye_offset),motion_point(step.target_offset))||!std::isfinite(step.half_life)||step.half_life<0||step.half_life>10)return why="invalid scene follow camera",false;
            }else if(step.kind==SceneStep::Kind::Choice){
                if(step.choices.size()!=2||step.default_choice>1||step.successors.size()!=2||step.choices[0].next_step==step.choices[1].next_step)return why="scene choice requires two forward branches",false;
                for(const auto& option:step.choices){std::u16string text;if(!story_mail::append_utf16(option.text,false,text)||text.empty()||text.size()>32||std::find(step.successors.begin(),step.successors.end(),option.next_step)==step.successors.end())return why="invalid scene choice label or branch",false;}
            }else if(step.kind==SceneStep::Kind::Animation){if(step.clip.empty()||!std::isfinite(step.blend_seconds)||step.blend_seconds<0||step.blend_seconds>10)return why="invalid scene animation",false;
            }else if(step.kind==SceneStep::Kind::Walk){if(!std::isfinite(step.speed)||step.speed<=0||step.speed>1500||step.loop||(!step.quest_place.empty()&&!step.walk_points.empty())||(step.quest_place.empty()&&(step.walk_points.size()<2||step.walk_points.size()>16)))return why="invalid scene walk",false;for(const auto& p:step.walk_points)if(!motion::valid(motion_point(p)))return why="invalid scene walk point",false;
            }else if(step.kind==SceneStep::Kind::Music){if(step.cue.empty()||(step.music_action!="start"&&step.music_action!="stop")||step.fade_frames>600)return why="invalid scene music hook",false;}
            else if(step.kind==SceneStep::Kind::Particle){
                if((step.particle!="auction-sparkle"&&step.particle!="beacon-sparkle")||(step.actor.empty()!=step.particle_has_position)||
                   (step.particle_has_position&&!motion::valid(motion_point(step.position)))||
                   !std::isfinite(step.particle_scale)||step.particle_scale<0.1f||step.particle_scale>4||step.particle_alpha>255)
                    return why="invalid curated scene particle",false;
            }
            timing.push_back({step.kind==SceneStep::Kind::End?0:std::max(1u,step.frames),step.kind==SceneStep::Kind::End,step.successors});
        }
        if(!valid_graph(timing))return why="scene "+scene.id+": invalid branch graph or five minute limit",false;
    }return true;
}
bool declare(const std::string& mod,const std::vector<ManifestScene>& scenes,std::string& why) {
    if(g_player.active()||g_cleanup_pending||g_cpu)return why="scene declarations are immutable during execution",false;
    if(!validate(scenes,why))return false;
    for(const auto& scene:scenes)
        for(const auto& old:g_entries)if(old.mod==mod&&old.scene.id==scene.id)return why="scene "+scene.id+": already registered",false;
    for(const auto& scene:scenes)g_entries.push_back({mod,scene});
    return true;
}
size_t checkpoint(){return g_entries.size();}
bool rollback(size_t count) noexcept{if(g_cpu||g_player.active()||g_cleanup_pending||count>g_entries.size())return false;g_entries.resize(count);return true;}
bool request(const std::string& mod,const std::string& id){
    for(size_t i=0;i<g_entries.size();++i)if(g_entries[i].mod==mod&&g_entries[i].scene.id==id){
        const std::optional<size_t> active=(g_player.active()||g_cleanup_pending)?std::optional<size_t>(g_current):std::nullopt;
        if(!g_queue.push(i,active))return false;g_pending.insert(i);return true;
    }return false;
}
void trigger(const std::string& kind,const std::string& mod,const std::string& id,unsigned step){
    for(const auto& e:g_entries){const auto& s=e.scene;
        if(e.mod!=mod||s.trigger!=kind)continue;
        if((kind=="talk"&&s.actor==id)||(kind=="salvage"&&s.chart==id)||(kind=="quest_step"&&s.quest==id&&s.quest_step==step))request(e.mod,s.id);
    }
}
void install(void (*original_fn)(Cpu*,uint32_t),const std::function<std::string(uint32_t)>& replaced_by,const std::function<void(uint32_t,void(*)(Cpu*))>& replace) {
    if(g_entries.empty()||!original_fn)return;
    g_original=original_fn;
    const bool needs_choice=std::any_of(g_entries.begin(),g_entries.end(),[](const Entry& entry){return std::any_of(entry.scene.steps.begin(),entry.scene.steps.end(),[](const SceneStep& step){return step.kind==SceneStep::Kind::Choice;});});
    if(needs_choice&&!g_choice_installed){story_scene_choice::configure(original_fn,g_call,g_scratch+ChoiceScratch,scene_lease);g_choice_installed=story_scene_choice::install(replaced_by,replace);}
    if(g_trim_installed)return;
    const auto other=replaced_by(kCalcTrim);
    if(!other.empty()){LOG("[scenes] %s owns camera trim; scenes disabled this session",other.c_str());return;}
    g_original=original_fn;
    replace(kCalcTrim,[](Cpu* c) {
        const uint32_t camera=c->r[3];
        if((g_player.active()||g_cleanup_pending)&&g_scratch&&ld32(g_scratch)==kMarker&&camera==ld32(g_scratch+8)&&owns(ld32(g_scratch+4))) {
            // HD's normal trim intentionally shrinks during native events. For this lease,
            // use the game's own cinema-scope height and native camera scissor/projection.
            const float old=u32_as_f32(ld32(camera+0x5FC)),target=u32_as_f32(ld32(camera+0x73C+0x6C));
            if(std::isfinite(target)&&target>=0&&target<=180)st32(camera+0x5FC,f32_as_u32(old+(target-old)*0.25f));
            return;
        }
        g_original(c,kCalcTrim);
    });
    g_trim_installed=true;
}
void tick(Cpu* cpu,bool skip,unsigned frames){
    if(!cpu||!g_call||!g_scratch||g_entries.empty())return;g_cpu=cpu;g_frame_units=std::min(2u,std::max(1u,frames));
    if(g_abort_pending){g_abort_pending=false;g_restore_reason=StopReason::Aborted;if(g_player.active())g_player.stop(StopReason::Aborted);else restore(false,true);g_cpu=nullptr;return;}
    if(g_loaded){if(g_player.active())g_player.tick(g_generation);else restore(true);g_loaded=false;g_queue.clear();g_pending.clear();if(g_cleanup_pending){g_cpu=nullptr;return;}}
    else if(g_cleanup_pending){restore(g_cleanup_loaded);g_cpu=nullptr;return;}
    const uint32_t play=call(kPlay);if(!play){g_cpu=nullptr;return;}
    const uint32_t buttons=input::read().buttons;
    const bool skip_edge=(buttons&input::kPlus)&&!(g_buttons&input::kPlus);
    g_buttons=buttons;
    if(!(buttons&input::kPlus))g_swallow_plus.store(false,std::memory_order_relaxed);
    if(g_player.active()){
        const bool interrupted=!owns(play)||!same_room(play)||call(kCamera)!=ld32(g_scratch+8);
        // Requests belong to this uninterrupted native event/room generation.
        // A reload may retain the same textual room key, so discard immediately
        // rather than allowing a stale later owner to start in the new generation.
        if(interrupted){g_queue.clear();g_pending.clear();}
        if(skip_edge)g_swallow_plus.store(true,std::memory_order_relaxed);
        for(unsigned i=0;i<g_frame_units&&g_player.active();++i)g_player.tick(g_generation,interrupted,skip||skip_edge);
        g_cpu=nullptr;return;
    }
    const uint32_t link=ld32(play+kPlayer);
    if(link){
        const std::string stage(reinterpret_cast<const char*>(mem::ptr(play+kStage)),strnlen(reinterpret_cast<const char*>(mem::ptr(play+kStage)),8));
        const unsigned room=ld8(link+kRoom);const std::string room_key=stage+":"+std::to_string(room);
        if(room_key!=g_room){
            g_room=room_key;g_fired.clear();
            g_queue.discard([&](size_t i){const auto& s=g_entries[i].scene;const bool remove=(s.trigger=="room"||s.trigger=="area")&&(s.stage!=stage||s.room!=room);if(remove)g_pending.erase(i);return remove;});
        }
        if(g_suppress_loaded_room) {
            for(size_t i=0;i<g_entries.size();++i){const auto& s=g_entries[i].scene;if((s.trigger=="room"||s.trigger=="area")&&s.stage==stage&&s.room==room)g_fired.insert(i);}
            g_suppress_loaded_room=false;
        }
        for(size_t i=0;i<g_entries.size();++i){const auto& s=g_entries[i].scene;
            if(g_fired.count(i)||s.stage!=stage||s.room!=room)continue;
            bool match=s.trigger=="room";
            if(s.trigger=="area"){const WorldPoint p=position(link);const float dx=p.x-s.position.x,dy=p.y-s.position.y,dz=p.z-s.position.z;match=dx*dx+dy*dy+dz*dz<=s.radius*s.radius;}
            if(match){const bool queued=g_pending.count(i)!=0;if(queued||request(g_entries[i].mod,s.id))g_fired.insert(i);}
        }
    }
    if(g_queue.size()&&!ld8(play+kMode)&&!ld8(play+kOrderCount)){
        g_current=*g_queue.pop();g_pending.erase(g_current);std::vector<Timing> timing;
        for(const auto& s:g_entries[g_current].scene.steps)timing.push_back({s.kind==SceneStep::Kind::End?0:std::max(1u,s.frames),s.kind==SceneStep::Kind::End,s.successors});
        g_player.start(std::move(timing),g_driver,g_generation,false);
    }g_cpu=nullptr;
}
void mute(input::PadState& pad) {
    if(g_active_input.load(std::memory_order_relaxed)){pad.buttons&=~input::kPlus;if(!story_scene_choice::active())pad.lx=pad.ly=pad.rx=pad.ry=0;}
    else if(g_swallow_plus.load(std::memory_order_relaxed))pad.buttons&=~input::kPlus;
}
void state_loaded(){g_abort_pending=false;story_scene_choice::state_loaded();g_camera_follow.reset();g_restore_reason=StopReason::StateLoaded;++g_generation;g_loaded=true;g_suppress_loaded_room=true;g_queue.clear();g_pending.clear();g_fired.clear();g_room.clear();}
void abort(){g_queue.clear();g_pending.clear();if(g_cpu)g_player.stop();else g_abort_pending=true;}
bool active(){return g_player.active()||g_cleanup_pending;}
size_t current_step(){return g_player.step();}
} // namespace story_scenes
