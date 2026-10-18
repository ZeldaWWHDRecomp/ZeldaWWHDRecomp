// Synthetic guest memory: proves native call contracts and lease cleanup, not game rendering.
#include "mods/story_scenes.h"
#include "mods/story_mail.h"
#include "mods/story_scene_choice.h"
#include "mods/story_particles.h"
#include "mods/mod_actor.h"
#include <map>
#include <stdexcept>
#include "mods/story_notebook_game.h"
#include "runtime.h"
#include "input.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <cstdlib>
#include <string>
#include <optional>
#include <cstdint>
#include <utility>
#include <string_view>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <cstdlib>
#include <string>
#include <optional>
#include <cstdint>
#include <utility>
#include <string_view>
#endif
void log_msg(const char*,...){}
namespace {
constexpr uint32_t kArena=0x20000000,kPlay=kArena,kCamera=kArena+0x10000,kManager=kArena+0x20000,kScratch=kArena+0x30000,kActor=kArena+0x40000;
std::map<uint32_t,void(*)(Cpu*)> choice_hooks;bool native_choice=false;unsigned choice_answer=0;
bool fake_mod=false,walk_finished=false;unsigned actor_claims=0,actor_releases=0,actor_paths=0,actor_animations=0;uint32_t actor_lease=0;std::vector<story_notebook::WorldPoint> last_path;
std::vector<std::string> reward_flags;
unsigned native_calls=0,end_calls=0,finished=0,sounds=0;
unsigned particle_calls=0;bool particle_accept=true,particle_identity=true;story_particles::Placement last_particle;
uint32_t pad_buttons=0;void(*trim_hook)(Cpu*)=nullptr;bool armed=false;unsigned message_status=0,close_ticks=0,reset_calls=0;float fade_rate=0;bool admit_ok=true,advance_close=true;
std::vector<unsigned> mode_calls;
void* map(uint32_t at,uint32_t bytes){
#ifdef _WIN32
return VirtualAlloc(mem::ptr(at),bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
return mmap(mem::ptr(at),bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
#endif
}
void sound(Cpu*,const char*){++sounds;}
uint32_t game_call(Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){
    ++native_calls;std::vector<uint32_t> a(args);
    switch(fn){
    case 0x025200D4:return kPlay;
    case 0x024F8044:return kCamera;
    case 0x0253EC0C:assert(a.size()==9&&a[0]==kPlay+0x51D0&&a[1]==3&&a[2]==1&&a[5]==0&&a[6]==0&&a[7]==0xFFFFFFFF&&a[8]==0xFF);st8(kPlay+0x5290,1);return 1;
    case 0x0253FD1C:if(!admit_ok)return 0;assert(ld8(kPlay+0x5290)==1);st8(kPlay+0x5290,0);st8(kPlay+0x5292,3);st16(kPlay+0x51D0+0xD8,0xFFFF);st32(kPlay+0x51D0+0xC4,0xFFFFFFFF);st32(kPlay+0x51D0+0xC8,0xFFFFFFFF);return 1;
    case 0x0253EFA8:assert(a[0]==kPlay+0x51D0);++end_calls;st8(kPlay+0x5292,0);return 1;
    case 0x02514F2C:st8(kCamera+4,0);return 0;
    case 0x02514F38:st8(kCamera+4,1);return 0;
    case 0x02515280:st32(kCamera+0x600,a[1]);return 1;
    case 0x02515048:return 1;
    case 0x02514F50:assert(a.size()==3);std::memcpy(mem::ptr(kCamera+0x10),mem::ptr(a[1]),12);std::memcpy(mem::ptr(kCamera+0x1C),mem::ptr(a[2]),12);return 1;
    case 0x025F7DB0:assert(armed&&a[0]==kManager&&a[1]==0x2CF3&&a[2]==0);if(native_choice){Cpu nested=*c;nested.r[3]=kManager;nested.r[4]=0x2CF3;nested.r[5]=0;choice_hooks.at(0x025F7980)(&nested);if(nested.r[3]==0xFFFFFFFFu)return nested.r[3];st32(kManager+0x948,choice_answer);}message_status=14;return 1;
    case 0x025F50E8:assert(a.size()==4&&a[0]==kArena+0x1000&&a[1]==kManager+0x14);assert(std::strcmp(reinterpret_cast<char*>(mem::ptr(ld32(a[2]))),"dummy")==0);st32(kManager+0x240,kArena+0x2000);st32(kManager+0x1C,kArena+0x1800);st32(kArena+0x1800+4,8);st32(kManager+0x12C,7);return 1;
    case 0x026AE194:assert(a==std::vector<uint32_t>({kArena+0x6000,7,0x2CF3,1,0}));st32(kArena+0x6000+0x5C,0x2CF3);return 1;
    case 0x025F795C:return message_status;
    case 0x025F74D0:
        if(a[1]==19)assert(message_status==18||message_status==19);
        message_status=a[1];if(a[1]==16)close_ticks=3;return 0;
    case 0x025F78C4:++reset_calls;message_status=0;close_ticks=0;return 0;
    case 0x025916C4:st8(kPlay+0x5C22,0);return 0;
    case 0x0259169C:st8(kPlay+0x5C22,1);return 0;
    case 0x025F069C:fade_rate=float(c->f[1].ps0);return 0;
    case 0x025D54C4:st32(a[1],kActor);return 1;
    case 0x022BED70:st32(a[0]+0x89C,a[1]);return 0;
    case 0x022C2D6C:case 0x022C2CC0:case 0x022BF3A0:case 0x024F08A8:return 0;
    case 0x022BF480:assert(a[0]==kActor&&a[1]==kActor+0x690&&a[2]==kActor+0x314);return 0;
    case 0x025D6870:{float speed=u32_as_f32(ld32(a[0]+0x370));double angle=double(int16_t(ld16(a[0]+0x322)))*3.14159265358979323846/32768.0;
        st32(a[0]+0x314,f32_as_u32(u32_as_f32(ld32(a[0]+0x314))+float(std::sin(angle))*speed));
        st32(a[0]+0x31C,f32_as_u32(u32_as_f32(ld32(a[0]+0x31C))+float(std::cos(angle))*speed));return 0;}

    default:assert(false);return 0;
    }
}
void test_tick(Cpu* cpu,bool skip=false,unsigned frames=1){
    // The native message window updates asynchronously once per base game pass.
    if(advance_close&&close_ticks){--close_ticks;message_status=close_ticks?17:18;}
    story_scenes::tick(cpu,skip,frames);
}
void clear_guest(){std::memset(mem::ptr(kArena),0,0x50000);st32(0x101F4B5C,kManager);st8(kCamera+4,1);st8(kPlay+0x5C22,1);st32(kCamera+0x73C+0x6C,f32_as_u32(60));st32(kCamera+0x600,0);st32(kCamera+0x10,f32_as_u32(123));st32(kPlay+0x5B34,kActor);std::memcpy(mem::ptr(kPlay+0x5134),"sea",4);}
}
namespace story_particles {
bool burst_named(Cpu*,std::string_view effect,const Placement& placement,Options options){
    assert((effect=="auction-sparkle"||effect=="beacon-sparkle")&&options.scale==1.5f&&options.alpha==180);
    ++particle_calls;last_particle=placement;return particle_accept;
}
}
namespace mod_actor {
bool identify(Cpu*,uint32_t actor,Identity& out){if(!fake_mod||actor!=kActor)return false;out={"mod","sprite",1,ld32(kActor+4)};return true;}
bool has_animation(Cpu*,const Identity&,const std::string& clip){return clip=="walk"||clip=="talk"||clip=="idle";}
bool snapshot(Cpu*,const Identity&,State& out){out.position={u32_as_f32(ld32(kActor+0x314)),u32_as_f32(ld32(kActor+0x318)),u32_as_f32(ld32(kActor+0x31C))};return true;}
bool scene_rotation(Cpu*,const Identity&,uint32_t lease,story_notebook::WorldPoint point){assert(lease==actor_lease);st16(kActor+0x32A,uint16_t(point.y*65536.f/360.f));return true;}
bool begin_scene(Cpu*,const Identity& id,uint32_t lease){assert(id.mod=="mod"&&id.token==1&&id.process_id==ld32(kActor+4));if(actor_lease&&actor_lease!=lease)return false;actor_lease=lease;++actor_claims;return true;}
bool end_scene(Cpu*,const Identity& id,uint32_t lease){assert(id.process_id==ld32(kActor+4)&&lease==actor_lease);actor_lease=0;++actor_releases;return true;}
bool scene_path(Cpu*,const Identity&,uint32_t lease,const std::vector<story_notebook::WorldPoint>& points,float speed){assert(lease==actor_lease&&points.size()>=2&&speed>0);++actor_paths;last_path=points;walk_finished=false;return true;}
bool scene_animation(Cpu*,const Identity&,uint32_t lease,const std::string& clip,float blend,bool loop){assert(lease==actor_lease&&clip=="talk"&&std::fabs(blend-6)<1e-6&&!loop);++actor_animations;return true;}
bool get_motion(Cpu*,const Identity&,MotionState& out){out.finished=walk_finished;return true;}
}
namespace input {PadState read(){PadState p;p.buttons=pad_buttons;return p;}}
namespace story_mail {void say(uint32_t,const std::u16string*){armed=true;}void unsay(){armed=false;}bool can_say(){return true;}}
namespace story_notebook {const GameUi& game_ui(){static GameUi ui=[] {GameUi u;u.sound=sound;return u;}();return ui;}}
int main(){
    assert(map(kArena,0x50000)==mem::ptr(kArena));assert(map(0x101F0000,0x10000)==mem::ptr(0x101F0000));clear_guest();st16(kActor+8,0x175);
    Cpu cpu{};story_scenes::set_game_calls(game_call);story_scenes::set_scratch_memory(kScratch);
    test_tick(&cpu);assert(native_calls==0); // no declarations means no guest calls/writes
    using namespace story_notebook;ManifestScene s;s.id="intro";SceneStep camera;camera.kind=SceneStep::Kind::Camera;camera.frames=2;camera.eye={1,2,3};camera.target={4,5,6};camera.pan=true;camera.eye_to={10,20,30};camera.target_to={40,50,60};
    SceneStep say;say.kind=SceneStep::Kind::Say;say.frames=5;say.text="Hello.";
    SceneStep fade;fade.kind=SceneStep::Kind::Fade;fade.frames=2;
    SceneStep end;end.kind=SceneStep::Kind::End;end.then.push_back({StoryAction::Kind::Flag,"done",1});
    SceneStep face;face.kind=SceneStep::Kind::Face;face.actor="resident";face.position={10,0,0};face.frames=1;
    SceneStep se;se.kind=SceneStep::Kind::Sound;se.sound="SE_MENU_CANCEL";se.frames=1;
    s.steps={camera,say,fade,face,se,end};std::string why;
    story_scenes::set_callbacks([](Cpu*,const std::string&,const std::string&){return kActor;},[](const std::string&,const std::vector<StoryAction>& actions){++finished;for(const auto& a:actions)reward_flags.push_back(a.id);return true;});
    story_scenes::set_particle_resolver([](Cpu*,const std::string& mod,const std::string& id,unsigned room,WorldPoint& out){
        if(!particle_identity||mod!="mod"||id!="sprite"||room!=ld8(kActor+0x326)||!ld32(kActor+4))return false;
        out={u32_as_f32(ld32(kActor+0x314)),u32_as_f32(ld32(kActor+0x318)),u32_as_f32(ld32(kActor+0x31C))};return true;
    });
    assert(story_scenes::declare("mod",{s},why));
    const unsigned declaration_calls=native_calls;ManifestScene copied;
    assert(story_scenes::declared_scene("mod","intro",copied)&&copied.steps.size()==s.steps.size()&&native_calls==declaration_calls);
    copied.steps[0].frames=999;copied.id="mutated";assert(story_scenes::declared_scene("mod","intro",copied)&&copied.id=="intro"&&copied.steps[0].frames==2);
    assert(!story_scenes::declared_scene("foreign","intro",copied)&&copied.id.empty()&&copied.steps.empty()&&native_calls==declaration_calls);
    const auto declaration_checkpoint=story_scenes::checkpoint();auto transient=s;transient.id="transient";assert(story_scenes::declare("mod",{transient},why));assert(story_scenes::rollback(declaration_checkpoint));assert(!story_scenes::declared_scene("mod","transient",copied)&&native_calls==declaration_calls);

    // Enumeration is a pure copy, includes only optional-native declarations,
    // preserves module identity, and rolls back with its actual source registry.
    assert(story_scenes::native_scene_ids().empty());
    auto native_enum=s;native_enum.id="native-enumeration";native_enum.native.emplace();
    assert(story_scenes::declare("mod",{native_enum},why));
    assert(story_scenes::declare("other",{native_enum},why));
    const std::vector<story_scenes::NativeSceneId> expected_native_ids{{"mod","native-enumeration"},{"other","native-enumeration"}};
    auto native_ids=story_scenes::native_scene_ids();assert(native_ids==expected_native_ids&&native_calls==declaration_calls);
    native_ids[0].first="mutated";assert(story_scenes::native_scene_ids()==expected_native_ids);
    assert(story_scenes::rollback(declaration_checkpoint)&&story_scenes::native_scene_ids().empty());

    story_scenes::install([](Cpu*,uint32_t){},[](uint32_t){return std::string();},[](uint32_t address,void(*hook)(Cpu*)){assert(address==0x024FF8A0);trim_hook=hook;});assert(trim_hook);
    admit_ok=false;assert(story_scenes::request("mod","intro"));test_tick(&cpu);assert(!story_scenes::active()&&ld8(kPlay+0x5290)==0&&ld8(kPlay+0x5292)==0);admit_ok=true;
    assert(story_scenes::request("mod","intro"));assert(!story_scenes::request("mod","intro"));
    st8(kPlay+0x5292,1);test_tick(&cpu);assert(!story_scenes::active()&&end_calls==0);
    st8(kPlay+0x5292,0);test_tick(&cpu);assert(story_scenes::active()&&ld8(kPlay+0x5292)==3&&ld32(kCamera+0x600)==2);
    assert(ld8(kPlay+0x5C22)==0);Cpu trim_cpu{};trim_cpu.r[3]=kCamera;trim_hook(&trim_cpu);assert(u32_as_f32(ld32(kCamera+0x5FC))==15);
    uint8_t mid_state[story_scenes::kScratchBytes];std::memcpy(mid_state,mem::ptr(kScratch),sizeof(mid_state));
    for(unsigned i=0;i<20&&story_scenes::active();++i)test_tick(&cpu);
    assert(sounds==1&&ld16(kActor+0x322)==0x4000&&ld16(kActor+0x32A)==0x4000);
    assert(!story_scenes::active()&&finished==1&&end_calls==1&&!armed&&ld8(kPlay+0x5292)==0&&ld8(kCamera+4)==1&&ld8(kPlay+0x5C22)==1&&u32_as_f32(ld32(kCamera+0x10))==123&&fade_rate==-1);
    // Load a state captured during that now-finished scene: guest marker restores control even
    // though no host scene is active. The loaded camera snapshot is used, not stale live state.
    std::memcpy(mem::ptr(kScratch),mid_state,sizeof(mid_state));st8(kPlay+0x5292,3);st8(kCamera+4,0);story_scenes::state_loaded();test_tick(&cpu);
    assert(end_calls==2&&finished==1&&ld8(kPlay+0x5292)==0&&ld8(kCamera+4)==1);
    assert(story_scenes::request("mod","intro"));test_tick(&cpu);assert(story_scenes::active());test_tick(&cpu,true);assert(!story_scenes::active()&&finished==1&&end_calls==3);
    assert(story_scenes::request("mod","intro"));test_tick(&cpu);assert(story_scenes::active());pad_buttons=input::kPlus;test_tick(&cpu);assert(!story_scenes::active()&&finished==1);pad_buttons=0;test_tick(&cpu);
    assert(story_scenes::request("mod","intro"));test_tick(&cpu);assert(story_scenes::active());story_scenes::state_loaded();test_tick(&cpu);assert(!story_scenes::active()&&finished==1&&end_calls==5);
    // Interruption by a different native event never resets the new owner's control/camera.
    assert(story_scenes::request("mod","intro"));test_tick(&cpu);st8(kPlay+0x5292,1);test_tick(&cpu);assert(!story_scenes::active()&&end_calls==5&&ld8(kPlay+0x5292)==1);
    SceneStep move;move.kind=SceneStep::Kind::Move;move.frames=4;s.steps={move,end};assert(!story_scenes::declare("unsupported",{s},why));assert(why.find("placed resident")!=std::string::npos);
    st8(kPlay+0x5292,0);move.actor="resident";move.position={10,0,20};s.id="walking";s.steps={move,end};
    st16(kActor+8,0x175);st32(kActor+0x8A0,kActor+0x2000);st32(kActor+0x8C0,f32_as_u32(30));st32(kActor+0x2000+0x40,f32_as_u32(100));
    assert(story_scenes::declare("mod",{s},why));assert(story_scenes::request("mod","walking"));test_tick(&cpu);
    for(unsigned i=0;i<10&&story_scenes::active();++i)test_tick(&cpu);
    assert(!story_scenes::active()&&finished==2&&std::fabs(u32_as_f32(ld32(kActor+0x314))-10)<1&&std::fabs(u32_as_f32(ld32(kActor+0x31C))-20)<1);
    // Skip while a real message is waiting for input: native FINISH16 alone can
    // require another A press. Non-natural exits reset only the owned message core
    // immediately and restore control even when the window never progresses.
    advance_close=false;
    const unsigned before_abort_resets=reset_calls;
    for(unsigned cycle=0;cycle<5;++cycle){
        assert(story_scenes::request("mod","intro"));test_tick(&cpu);test_tick(&cpu);test_tick(&cpu);test_tick(&cpu);
        assert(message_status==14&&ld8(kScratch+13)==0&&ld32(kScratch+160)==0);
        const unsigned prior_ends=end_calls;
        if(cycle%2){story_scenes::abort();test_tick(&cpu);}else test_tick(&cpu,true);
        assert(!story_scenes::active()&&message_status==0&&end_calls==prior_ends+1);
        assert(reset_calls==before_abort_resets+cycle+1&&ld8(kPlay+0x5292)==0);
    }
    // A normal dialogue that receives no further confirm expires at its declared
    // scene deadline, resets immediately and cannot apply end rewards.
    const unsigned before_deadline_resets=reset_calls,before_deadline_rewards=finished;
    assert(story_scenes::request("mod","intro"));test_tick(&cpu);
    for(unsigned i=0;i<10&&story_scenes::active();++i)test_tick(&cpu);
    assert(!story_scenes::active()&&reset_calls==before_deadline_resets+1&&message_status==0&&finished==before_deadline_rewards);
    // A full-state abort during an owned dialogue follows the same no-input path.
    assert(story_scenes::request("mod","intro"));test_tick(&cpu);test_tick(&cpu);test_tick(&cpu);test_tick(&cpu);
    const unsigned before_state_resets=reset_calls;story_scenes::state_loaded();test_tick(&cpu);
    assert(!story_scenes::active()&&reset_calls==before_state_resets+1&&message_status==0&&ld8(kPlay+0x5292)==0);
    advance_close=true;
    // Walking cleanup also runs if a different event takes over the event control.
    st32(kActor+0x370,f32_as_u32(19));assert(story_scenes::request("mod","walking"));test_tick(&cpu);st32(kActor+0x314,0);test_tick(&cpu);st8(kPlay+0x5292,1);test_tick(&cpu);assert(!story_scenes::active()&&u32_as_f32(ld32(kActor+0x370))==19&&ld8(kPlay+0x5292)==1);
    // A queued room trigger belongs to its room; loading there aborts without retriggering.
    s.id="room-scene";s.trigger="room";s.stage="sea";s.room=1;s.steps={camera,end};assert(story_scenes::declare("mod",{s},why));st8(kActor+0x326,1);test_tick(&cpu);assert(!story_scenes::active());st8(kActor+0x326,2);st8(kPlay+0x5292,0);test_tick(&cpu);assert(!story_scenes::active());st8(kActor+0x326,1);test_tick(&cpu);assert(story_scenes::active());story_scenes::state_loaded();test_tick(&cpu);assert(!story_scenes::active());test_tick(&cpu);assert(!story_scenes::active());
    face.actor="link";s.id="link-face";s.trigger.clear();s.steps={face,end};assert(!story_scenes::declare("mod",{s},why));
    face.actor="resident";s.id="resident-face";s.steps={face,end};assert(story_scenes::declare("mod",{s},why));
    const auto before_face_rewards=finished;const auto prior_angle=ld16(kActor+0x322);
    for(uint16_t process:{uint16_t(0xB6),uint16_t(0x125)}){st16(kActor+8,process);assert(story_scenes::request("mod","resident-face"));test_tick(&cpu);assert(!story_scenes::active()&&finished==before_face_rewards&&ld16(kActor+0x322)==prior_angle);}
    st16(kActor+8,0x175);
    // Smooth multi-point camera uses authored endpoints and easing; no eye/target singularities.
    SceneStep path;path.kind=SceneStep::Kind::CameraPath;path.frames=4;path.easing="smoothstep";
    path.camera_keys={{{0,10,20},{0,0,0},2},{{10,10,20},{10,0,0},2},{{20,10,20},{20,0,0},0}};
    s.id="camera-path";s.steps={path,end};assert(story_scenes::declare("mod",{s},why));const unsigned before_camera=finished;
    assert(story_scenes::request("mod",s.id));test_tick(&cpu);test_tick(&cpu);assert(std::fabs(u32_as_f32(ld32(kCamera+0x10))-5)<1e-6);test_tick(&cpu);assert(u32_as_f32(ld32(kCamera+0x10))==10);test_tick(&cpu);assert(u32_as_f32(ld32(kCamera+0x10))==15);test_tick(&cpu);assert(u32_as_f32(ld32(kCamera+0x10))==20);test_tick(&cpu);assert(!story_scenes::active()&&finished==before_camera+1);
    auto bad_path=path;bad_path.camera_keys[1].eye=bad_path.camera_keys[1].target;s.id="bad-path";s.steps={bad_path,end};assert(!story_scenes::declare("mod",{s},why));
    SceneStep follow;follow.kind=SceneStep::Kind::CameraFollow;follow.actor="resident";follow.frames=4;follow.eye_offset={0,50,100};follow.target_offset={0,20,0};follow.half_life=0;
    s.id="follow";s.steps={follow,end};assert(story_scenes::declare("mod",{s},why));st32(kActor+4,33);st32(kActor+0x314,f32_as_u32(10));assert(story_scenes::request("mod",s.id));test_tick(&cpu);test_tick(&cpu);assert(u32_as_f32(ld32(kCamera+0x10))==10);st32(kActor+0x314,f32_as_u32(30));test_tick(&cpu);assert(u32_as_f32(ld32(kCamera+0x10))==30);
    const unsigned before_identity=finished;st32(kActor+4,34);test_tick(&cpu);assert(!story_scenes::active()&&finished==before_identity); // Reused address cannot silently change followed identity.

    // Real choice adapter selects only one forward branch and uses that branch's end actions.
    assert(map(0x10480000,0x10000)==mem::ptr(0x10480000));st32(0x101F4AE8,kArena+0x1000);st32(0x1048D774,1);st32(0x1048D760,kArena+0x3000);st32(kManager+0x960,kArena+0x6000);st32(kArena+0x6000+0x8C,kManager+0x14);
    SceneStep choice;choice.kind=SceneStep::Kind::Choice;choice.text="Which way?";choice.frames=20;choice.choices={{"Walk","left",1},{"Stay","right",3}};choice.successors={1,3};
    auto left_sound=se;left_sound.successors={2};auto right_sound=se;right_sound.successors={4};auto left_end=end;left_end.then={{StoryAction::Kind::Flag,"branch-left",1}};auto right_end=end;right_end.then={{StoryAction::Kind::Flag,"branch-right",1}};
    s.id="branch";s.steps={choice,left_sound,left_end,right_sound,right_end};assert(story_scenes::declare("mod",{s},why));
    story_scenes::install([](Cpu*,uint32_t){},[](uint32_t){return std::string();},[](uint32_t address,void(*hook)(Cpu*)){assert(address==0x025F7980||address==0x027046D4);choice_hooks[address]=hook;});
    native_choice=true;
    for(unsigned selected=0;selected<2;++selected){message_status=0;choice_answer=selected;const auto previous_sounds=sounds;const auto previous_rewards=finished;assert(story_scenes::request("mod","branch"));test_tick(&cpu);test_tick(&cpu);assert(story_scene_choice::active());input::PadState pad;pad.lx=.7f;pad.buttons=input::kPlus;story_scenes::mute(pad);assert(pad.lx==.7f&&!(pad.buttons&input::kPlus));for(unsigned n=0;n<20&&story_scenes::active();++n)test_tick(&cpu);assert(!story_scenes::active()&&sounds==previous_sounds+1&&finished==previous_rewards+1&&reward_flags.back()==(selected?"branch-right":"branch-left"));}
    message_status=0;assert(story_scenes::request("mod","branch"));test_tick(&cpu);test_tick(&cpu);const auto before_choice_load=finished;story_scenes::state_loaded();test_tick(&cpu);assert(!story_scenes::active()&&!story_scene_choice::active()&&message_status==0&&finished==before_choice_load);
    native_choice=false;

    // Music has a real owned callback; missing/failed hooks cannot silently award rewards.
    SceneStep music;music.kind=SceneStep::Kind::Music;music.cue="harbor";music.music_action="start";music.fade_frames=12;music.frames=1;
    s.id="music";s.steps={music,end};assert(story_scenes::declare("music-owner",{s},why));const auto before_music=finished;assert(story_scenes::request("music-owner","music"));test_tick(&cpu);assert(!story_scenes::active()&&finished==before_music);
    std::vector<story_scenes::MusicPhase> phases;std::vector<story_scenes::StopReason> reasons;bool music_restore=true;
    assert(story_scenes::register_music_hook("music-owner",[&](Cpu*,const std::string& cue,story_scenes::MusicPhase phase,unsigned fade,story_scenes::StopReason reason){assert(cue=="harbor"&&fade==12);phases.push_back(phase);reasons.push_back(reason);return phase!=story_scenes::MusicPhase::Restore||music_restore;}));
    assert(!story_scenes::register_music_hook("music-owner",[](Cpu*,const std::string&,story_scenes::MusicPhase,unsigned,story_scenes::StopReason){return true;}));
    assert(story_scenes::request("music-owner","music"));test_tick(&cpu);test_tick(&cpu);test_tick(&cpu);assert(!story_scenes::active()&&finished==before_music+1&&phases==std::vector<story_scenes::MusicPhase>({story_scenes::MusicPhase::Start,story_scenes::MusicPhase::Restore}));
    phases.clear();assert(story_scenes::request("music-owner","music"));test_tick(&cpu);test_tick(&cpu,true);assert(phases==std::vector<story_scenes::MusicPhase>({story_scenes::MusicPhase::Start,story_scenes::MusicPhase::Restore})&&reasons.back()==story_scenes::StopReason::Skipped);
    assert(story_scenes::request("music-owner","music"));test_tick(&cpu);story_scenes::abort();test_tick(&cpu);assert(reasons.back()==story_scenes::StopReason::Aborted&&!story_scenes::active());
    music_restore=false;const auto before_failed_music=finished;assert(story_scenes::request("music-owner","music"));test_tick(&cpu);test_tick(&cpu);test_tick(&cpu);assert(!story_scenes::active()&&finished==before_failed_music);music_restore=true;

    // Generic actors use their own native scene lease/path/clip contract, never resident offsets.
    fake_mod=true;SceneStep clip;clip.kind=SceneStep::Kind::Animation;clip.actor="sprite";clip.clip="talk";clip.blend_seconds=.2f;clip.loop=false;clip.frames=1;
    SceneStep walk;walk.kind=SceneStep::Kind::Walk;walk.actor="sprite";walk.frames=5;walk.speed=100;walk.loop=false;walk.walk_points={{0,0,0},{10,0,0}};
    s.id="sprite-scene";s.steps={clip,walk,end};assert(story_scenes::declare("mod",{s},why));const auto before_actor_rewards=finished;assert(story_scenes::request("mod",s.id));test_tick(&cpu);test_tick(&cpu);assert(actor_claims==1&&actor_animations==1&&actor_paths==1);test_tick(&cpu);assert(story_scenes::active());walk_finished=true;test_tick(&cpu);test_tick(&cpu);assert(!story_scenes::active()&&actor_releases==1&&finished==before_actor_rewards+1);
    assert(story_scenes::request("mod","sprite-scene"));test_tick(&cpu);test_tick(&cpu);const auto before_blocked=finished;for(unsigned n=0;n<8&&story_scenes::active();++n)test_tick(&cpu);assert(!story_scenes::active()&&actor_releases==2&&finished==before_blocked); // Blocked walk expires, no branch/end reward.
    auto missing_clip=clip;missing_clip.clip="missing";s.id="missing-clip";s.steps={missing_clip,end};assert(story_scenes::declare("mod",{s},why));const auto preflight_ends=end_calls,preflight_claims=actor_claims;assert(story_scenes::request("mod",s.id));test_tick(&cpu);assert(!story_scenes::active()&&end_calls==preflight_ends&&actor_claims==preflight_claims);
    assert(story_scenes::request("mod","sprite-scene"));test_tick(&cpu);uint8_t actor_state[story_scenes::kScratchBytes];std::memcpy(actor_state,mem::ptr(kScratch),sizeof(actor_state));const uint32_t saved_actor_lease=actor_lease;
    test_tick(&cpu);walk_finished=true;test_tick(&cpu);test_tick(&cpu);assert(!story_scenes::active());const auto before_actor_load_rewards=finished,before_actor_load_release=actor_releases;
    std::memcpy(mem::ptr(kScratch),actor_state,sizeof(actor_state));actor_lease=saved_actor_lease;st8(kPlay+0x5292,3);story_scenes::state_loaded();test_tick(&cpu);assert(!story_scenes::active()&&actor_releases==before_actor_load_release+1&&finished==before_actor_load_rewards);
    auto quest_walk=walk;quest_walk.quest_place="harbor-place";quest_walk.walk_points.clear();quest_walk.speed=1500;s.id="quest-walk";s.steps={quest_walk,end};assert(story_scenes::declare("mod",{s},why));
    bool far_place=false;story_scenes::set_place_resolver([&](Cpu*,const std::string& owner,const std::string& id)->std::optional<WorldPoint>{assert(owner=="mod"&&id=="harbor-place");return WorldPoint{far_place?20000.f:40.f,0,50};});st32(kActor+0x318,f32_as_u32(1040));assert(story_scenes::request("mod",s.id));test_tick(&cpu);assert(last_path.size()==2&&last_path[0].y==1040&&last_path[1].y==1040);walk_finished=true;test_tick(&cpu);test_tick(&cpu);assert(!story_scenes::active());
    far_place=true;const auto before_far_place_ends=end_calls;assert(story_scenes::request("mod",s.id));test_tick(&cpu);assert(!story_scenes::active()&&end_calls==before_far_place_ends);st32(kActor+0x318,0);
    fake_mod=false;

    // A full snapshot restores the music lease marker even after its host scene finished.
    assert(story_scenes::request("music-owner","music"));test_tick(&cpu);uint8_t music_state[story_scenes::kScratchBytes];std::memcpy(music_state,mem::ptr(kScratch),sizeof(music_state));test_tick(&cpu);test_tick(&cpu);assert(!story_scenes::active());const auto before_music_load_rewards=finished;
    std::memcpy(mem::ptr(kScratch),music_state,sizeof(music_state));st8(kPlay+0x5292,3);story_scenes::state_loaded();test_tick(&cpu);assert(!story_scenes::active()&&finished==before_music_load_rewards&&reasons.back()==story_scenes::StopReason::StateLoaded);
    s.id="throwing-music";s.steps={music,end};assert(story_scenes::declare("throwing-owner",{s},why));assert(story_scenes::register_music_hook("throwing-owner",[](Cpu*,const std::string&,story_scenes::MusicPhase,unsigned,story_scenes::StopReason)->bool{throw std::runtime_error("test hook");}));const auto before_throw_rewards=finished;assert(story_scenes::request("throwing-owner",s.id));test_tick(&cpu);assert(!story_scenes::active()&&finished==before_throw_rewards);

    // Restore owner A's guest snapshot while host owner B is active. Cleanup
    // must dispatch from restored SceneIndex, never stale host g_current.
    std::vector<std::pair<std::string,story_scenes::MusicPhase>> owner_music;
    std::vector<std::pair<std::string,story_scenes::StopReason>> owner_reasons;
    for(const std::string& owner:{std::string("restore-a"),std::string("restore-b")}){auto owned_music=music;owned_music.cue=owner;s.id="cross-owner";s.steps={owned_music,end};assert(story_scenes::declare(owner,{s},why));assert(story_scenes::register_music_hook(owner,[&,owner](Cpu*,const std::string& cue,story_scenes::MusicPhase phase,unsigned,story_scenes::StopReason reason){assert(cue==owner);owner_music.emplace_back(owner,phase);owner_reasons.emplace_back(owner,reason);return true;}));}
    assert(story_scenes::request("restore-a","cross-owner"));test_tick(&cpu);uint8_t owner_a_state[story_scenes::kScratchBytes];std::memcpy(owner_a_state,mem::ptr(kScratch),sizeof(owner_a_state));test_tick(&cpu);test_tick(&cpu);assert(!story_scenes::active());
    assert(story_scenes::request("restore-b","cross-owner"));test_tick(&cpu);assert(story_scenes::active());assert(owner_music.back()==std::make_pair(std::string("restore-b"),story_scenes::MusicPhase::Start));const unsigned before_cross_owner_rewards=finished;
    std::memcpy(mem::ptr(kScratch),owner_a_state,sizeof(owner_a_state));st8(kPlay+0x5292,3);story_scenes::state_loaded();test_tick(&cpu);assert(!story_scenes::active()&&finished==before_cross_owner_rewards);assert(owner_music.back()==std::make_pair(std::string("restore-a"),story_scenes::MusicPhase::Restore));assert(owner_reasons.back()==std::make_pair(std::string("restore-a"),story_scenes::StopReason::StateLoaded));

    // Sixteen different mods' queued scenes survive FIFO execution, and reload clears pending work.
    SceneStep short_wait;short_wait.kind=SceneStep::Kind::Wait;short_wait.frames=1;s.id="queued";s.steps={short_wait,end};
    for(unsigned n=0;n<17;++n)assert(story_scenes::declare("queue-"+std::to_string(n),{s},why));
    for(unsigned n=0;n<16;++n)assert(story_scenes::request("queue-"+std::to_string(n),"queued"));assert(!story_scenes::request("queue-16","queued"));assert(!story_scenes::request("queue-0","queued"));
    const auto before_queue=finished;for(unsigned n=0;n<50;++n)test_tick(&cpu);assert(finished==before_queue+16&&!story_scenes::active());
    for(unsigned n=0;n<16;++n)assert(story_scenes::request("queue-"+std::to_string(n),"queued"));test_tick(&cpu);story_scenes::state_loaded();test_tick(&cpu);for(unsigned n=0;n<50;++n)test_tick(&cpu);assert(finished==before_queue+16&&!story_scenes::active());
    // Native interruption clears later owners even when stage/room strings
    // stay identical, as with a same-room progress reload. Plus skip deliberately
    // retains the later owners; it is not a native-generation change.
    assert(story_scenes::request("queue-0","queued"));assert(story_scenes::request("queue-1","queued"));assert(story_scenes::request("queue-2","queued"));test_tick(&cpu);assert(story_scenes::active());const auto before_interrupt_rewards=finished;
    st8(kPlay+0x5292,1);test_tick(&cpu);assert(!story_scenes::active());st8(kPlay+0x5292,0);for(unsigned n=0;n<12;++n)test_tick(&cpu);assert(!story_scenes::active()&&finished==before_interrupt_rewards);
    assert(story_scenes::request("queue-0","queued"));assert(story_scenes::request("queue-1","queued"));assert(story_scenes::request("queue-2","queued"));test_tick(&cpu);assert(story_scenes::active());test_tick(&cpu,true);for(unsigned n=0;n<12;++n)test_tick(&cpu);assert(!story_scenes::active()&&finished==before_interrupt_rewards+2);
    // An instant particle step emits once at its copied world position. A
    // missing native bank aborts the scene before its end reward.
    SceneStep particle;particle.kind=SceneStep::Kind::Particle;particle.particle="auction-sparkle";
    particle.position={11,22,33};particle.particle_has_position=true;particle.particle_scale=1.5f;particle.particle_alpha=180;
    s.id="particle";s.steps={particle,end};assert(story_scenes::declare("mod",{s},why));
    const auto before_particle_rewards=finished;assert(story_scenes::request("mod","particle"));test_tick(&cpu);
    assert(particle_calls==1&&last_particle.position.x==11&&last_particle.position.y==22&&last_particle.position.z==33);
    test_tick(&cpu);test_tick(&cpu);assert(!story_scenes::active()&&finished==before_particle_rewards+1);
    particle_accept=false;assert(story_scenes::request("mod","particle"));test_tick(&cpu);
    assert(particle_calls==2&&!story_scenes::active()&&finished==before_particle_rewards+1);particle_accept=true;
    particle.actor="sprite";particle.particle_has_position=false;s.id="actor-particle";s.steps={particle,end};
    assert(story_scenes::declare("mod",{s},why));st32(kActor+4,33);st8(kActor+0x326,1);
    st32(kActor+0x314,f32_as_u32(45));st32(kActor+0x318,f32_as_u32(46));st32(kActor+0x31C,f32_as_u32(47));
    assert(story_scenes::request("mod","actor-particle"));test_tick(&cpu);
    assert(particle_calls==3&&last_particle.position.x==45&&last_particle.position.y==46&&last_particle.position.z==47);
    test_tick(&cpu);test_tick(&cpu);assert(!story_scenes::active());
    particle_identity=false;const auto before_stale_actor=particle_calls;
    assert(story_scenes::request("mod","actor-particle"));test_tick(&cpu);
    assert(!story_scenes::active()&&particle_calls==before_stale_actor);particle_identity=true;
    particle.particle="beacon-sparkle";particle.actor.clear();particle.particle_has_position=true;s.id="beacon-particle";s.steps={particle,end};assert(story_scenes::declare("mod",{s},why));const auto before_beacon=particle_calls;assert(story_scenes::request("mod","beacon-particle"));test_tick(&cpu);assert(particle_calls==before_beacon+1);test_tick(&cpu);test_tick(&cpu);assert(!story_scenes::active());
    std::puts("story scene native contract tests passed (synthetic memory)");
    return 0;
}
