// SPDX-License-Identifier: MPL-2.0
#include "story_native_scene_request.h"
#include "story_scenes.h"
#include "mod_hash.h"
#include <bit>
#include <thread>
#include <tuple>
#include <type_traits>
namespace story_native_scene_request {
namespace detail {
struct StartupState {std::thread::id thread=std::this_thread::get_id();bool active=true;};
thread_local std::shared_ptr<StartupState>active;
}
namespace {
namespace M=story_notebook;namespace N=story_native_scene_manifest;
bool same(const story_actors::ResidentEventDeclaration&a,const story_actors::ResidentEventDeclaration&b){return a.token==b.token&&a.room==b.room&&a.subtype==b.subtype&&a.stage==b.stage&&a.fingerprint==b.fingerprint;}
struct Encoding {
 std::string bytes;
 void word(uint64_t v){for(int n=56;n>=0;n-=8)bytes.push_back(char(v>>n));}
 void tuple(auto&&v){std::apply([this](const auto&...item){(this->emit(item),...);},v);}
 template<class T>void emit(const T&v){
  if constexpr(std::is_same_v<T,std::string>){word(v.size());bytes+=v;}
  else if constexpr(std::is_enum_v<T>||std::is_integral_v<T>)word(uint64_t(v));
  else if constexpr(std::is_same_v<T,float>)word(std::bit_cast<uint32_t>(v));
  else if constexpr(std::is_same_v<T,M::WorldPoint>||std::is_same_v<T,N::Point>)tuple(std::tie(v.x,v.y,v.z));
  else if constexpr(std::is_same_v<T,M::StoryAction>)tuple(std::tie(v.kind,v.id,v.value));
  else if constexpr(std::is_same_v<T,M::SceneCameraKey>)tuple(std::tie(v.eye,v.target,v.frames));
  else if constexpr(std::is_same_v<T,M::SceneChoiceOption>)tuple(std::tie(v.text,v.next,v.next_step));
  else if constexpr(std::is_same_v<T,M::SceneStep>)tuple(std::tie(v.kind,v.actor,v.target_actor,v.text,v.sound,v.id,v.next,v.clip,v.quest_place,v.cue,v.music_action,v.particle,v.easing,v.successors,v.camera_keys,v.eye_offset,v.target_offset,v.half_life,v.blend_seconds,v.speed,v.loop,v.walk_points,v.choices,v.default_choice,v.fade_frames,v.eye,v.target,v.position,v.eye_to,v.target_to,v.frames,v.particle_alpha,v.particle_scale,v.pan,v.fade_in,v.particle_has_position,v.then));
  else if constexpr(std::is_same_v<T,M::ManifestScene>)tuple(std::tie(v.id,v.steps,v.native,v.trigger,v.stage,v.actor,v.quest,v.chart,v.room,v.quest_step,v.position,v.radius));
  else if constexpr(std::is_same_v<T,N::ActorTurn>)tuple(std::tie(v.id,v.turn_to));
  else if constexpr(std::is_same_v<T,N::Camera>)tuple(std::tie(v.hold_ticks,v.move_ticks,v.start_eye,v.start_target,v.end_eye,v.end_target,v.fovy));
  else if constexpr(std::is_same_v<T,N::Definition>)tuple(std::tie(v.actors,v.camera,v.wait_ticks,v.timekeeper_ticks,v.watchdog_ticks,v.stage,v.room));
  else if constexpr(std::is_same_v<T,story_actors::ResidentEventDeclaration>)tuple(std::tie(v.token,v.room,v.subtype,v.stage,v.fingerprint));
  else if constexpr(requires{v.has_value();}){emit(v.has_value());if(v)emit(*v);}
  else if constexpr(requires{v.begin();v.end();v.size();}){word(v.size());for(const auto&item:v)emit(item);}
  else static_assert(sizeof(T)==0,"native scene digest needs an explicit typed encoding");
 }
};
std::array<uint8_t,32>digest(const std::string&mod,const std::string&scene,const M::ManifestScene&definition,const story_native_scene_compile::Compiled&compiled,const std::array<story_actors::ResidentEventDeclaration,2>&actors){
 Encoding e;e.emit(std::string("sdk-declared-native-request-v1"));e.tuple(std::tie(mod,scene,definition,actors));e.emit(compiled.program().kind());e.emit(compiled.program().frames());e.emit(compiled.program().watchdog());e.emit(compiled.program().pristine());
 const auto hex=mods::hash::sha256_text(e.bytes);std::array<uint8_t,32>out{};auto nibble=[](char c){return uint8_t(c<='9'?c-'0':c-'a'+10);};for(unsigned n=0;n<32;++n)out[n]=uint8_t(nibble(hex[n*2])*16+nibble(hex[n*2+1]));return out;
}
}
StartupWindow::StartupWindow(){if(detail::active)return;state_=std::make_shared<detail::StartupState>();detail::active=state_;}
StartupWindow::~StartupWindow(){if(!state_)return;if(state_->thread!=std::this_thread::get_id())std::terminate();state_->active=false;if(detail::active==state_)detail::active.reset();}
bool startup_window_current()noexcept{return detail::active&&detail::active->active&&detail::active->thread==std::this_thread::get_id();}
StartupWitness::StartupWitness(const std::shared_ptr<detail::StartupState>&state):state_(state){}
bool StartupWitness::current()const noexcept{return startup_window_current()&&state_.lock()==detail::active;}
std::optional<StartupWitness>current_startup(){if(!startup_window_current())return {};return StartupWitness(detail::active);}
DeclaredRequest::DeclaredRequest(std::string mod,std::string scene,story_native_scene_compile::Compiled compiled,M::ManifestScene definition,std::array<story_actors::ResidentEventDeclaration,2>actors,std::shared_ptr<detail::StartupState>startup):mod_(std::move(mod)),scene_(std::move(scene)),compiled_(std::move(compiled)),definition_(std::move(definition)),actors_(std::move(actors)),startup_(std::move(startup)){hash_=digest(mod_,scene_,definition_,compiled_,actors_);}
bool DeclaredRequest::startup_current()const noexcept{if(!startup_window_current())return false;return startup_.lock()==detail::active;}
bool DeclaredRequest::current()const{try{
 M::ManifestScene before,after;if(!story_scenes::declared_scene(mod_,scene_,before)||before!=definition_)return false;
 std::array<story_actors::ResidentEventDeclaration,2>fresh;
 for(unsigned n=0;n<2;++n)if(!story_actors::resident_event_declaration(mod_,compiled_.actor_ids()[n],fresh[n])||!same(fresh[n],actors_[n]))return false;
 if(!story_scenes::declared_scene(mod_,scene_,after)||after!=definition_)return false;
 for(unsigned n=0;n<2;++n){story_actors::ResidentEventDeclaration final;if(!story_actors::resident_event_declaration(mod_,compiled_.actor_ids()[n],final)||!same(final,fresh[n]))return false;}
 // Typed equality is checked independently; this digest is configuration data,
 // never a substitute for actual registry reads or a live native lease.
 return digest(mod_,scene_,before,compiled_,fresh)==hash_;
 }catch(...){return false;}}
std::shared_ptr<const DeclaredRequest>resolve(const std::string&mod,const std::string&scene,std::string&why){
 why.clear();try{
  if(!startup_window_current()){why="native scene request requires the actual committed startup window";return {};}
  if(mod.empty()||mod.size()>128||!story_mail::valid_id(scene)){why="invalid native scene module or scene ID";return {};}
  M::ManifestScene definition;if(!story_scenes::declared_scene(mod,scene,definition)||definition.id!=scene||!definition.native){why="committed native scene declaration not found";return {};}
  auto compiled=story_native_scene_compile::compile(*definition.native,why);if(!compiled)return {};
  std::array<story_actors::ResidentEventDeclaration,2>actors;
  for(unsigned n=0;n<2;++n)if(!story_actors::resident_event_declaration(mod,compiled->actor_ids()[n],actors[n])||!actors[n].token||actors[n].subtype!=1||actors[n].stage!=compiled->stage()||actors[n].room!=compiled->room()){why="native scene actor declaration does not match same-module Uo2 placement";return {};}
  if(actors[0].token==actors[1].token){why="native scene actors require distinct declaration tokens";return {};}
  auto result=std::shared_ptr<const DeclaredRequest>(new DeclaredRequest(mod,scene,std::move(*compiled),std::move(definition),std::move(actors),detail::active));
  if(!result->startup_current()||!result->current()){why="native scene declarations changed during startup capture";return {};}
  return result;
 }catch(...){why="native scene request capture failed";return {};}
}
}
