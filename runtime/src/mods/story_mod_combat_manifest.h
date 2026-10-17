// SPDX-License-Identifier: MPL-2.0
// Pure generic combat schema. Native attack/attention/drop admission is separate.
#pragma once
#include "mod_json.h"
#include "story_mail_book.h"
#include <cmath>
#include <initializer_list>
#include <string>
#include <tuple>
namespace story_mod_combat {
struct Reaction {std::string clip;unsigned frames=0;float knockback_speed=0;unsigned knockback_frames=0;};
struct Attack {unsigned damage=1,active_frames=1,cooldown_frames=1;float radius=30,height=100,forward_offset=0,y_offset=0;};
struct Config {unsigned health=1,invulnerability_frames=15,target_priority=1;Reaction flinch{"",8,0,0},stun{"",30,0,0},death{"",15,0,0};Attack attack;};
using mods::json::Value;
inline bool keys(const Value&v,std::initializer_list<const char*> allowed){if(v.type!=Value::Object)return false;for(const auto& [key,value]:v.object){bool found=false;for(auto name:allowed)found|=key==name;if(!found)return false;}return true;}
inline bool integer(const Value&v,unsigned lo,unsigned hi,unsigned&out){if(v.type!=Value::Number||!std::isfinite(v.number)||v.number<lo||v.number>hi||std::floor(v.number)!=v.number)return false;out=unsigned(v.number);return true;}
inline bool scalar(const Value&v,float lo,float hi,float&out){if(v.type!=Value::Number||!std::isfinite(v.number)||v.number<lo||v.number>hi)return false;out=float(v.number);return std::isfinite(out);}
inline bool optional_frames(const Value&v,const char*key,unsigned&out,unsigned maximum=900){return !v.object.contains(key)||integer(v.get(key),0,maximum,out);}
inline bool reaction(const Value&v,Reaction&out,bool knockback,std::string&why){
 if(!keys(v,knockback?std::initializer_list<const char*>{"clip","frames","knockback_speed","knockback_frames"}:std::initializer_list<const char*>{"clip","frames"}))return why="unknown combat reaction field",false;
 if(v.object.contains("clip")){const auto&clip=v.get("clip");if(clip.type!=Value::String||!story_mail::valid_id(clip.text))return why="invalid combat reaction clip",false;out.clip=clip.text;}
 if(!optional_frames(v,"frames",out.frames))return why="combat reaction frames must be 0..900",false;
 if(knockback){if(v.object.contains("knockback_speed")&&!scalar(v.get("knockback_speed"),0,1500,out.knockback_speed))return why="invalid combat knockback speed",false;if(!optional_frames(v,"knockback_frames",out.knockback_frames,30))return why="invalid combat knockback frames",false;if((out.knockback_speed>0)!=(out.knockback_frames>0))return why="knockback speed and frames must both be positive or zero",false;}
 if(!out.clip.empty()&&!out.frames)return why="combat clip needs positive reaction frames",false;
 return true;
}
inline bool parse(const Value&v,Config&out,std::string&why){
 Config c;
 if(!keys(v,{"health","invulnerability_frames","flinch","stun","death","attack","target_priority"}))return why="unknown combat field",false;
 if(!integer(v.get("health"),1,255,c.health)||!optional_frames(v,"invulnerability_frames",c.invulnerability_frames))return why="invalid combat health or invulnerability frames",false;
 for(const auto& [name,destination,knockback]:{std::tuple{"flinch",&c.flinch,true},std::tuple{"stun",&c.stun,false},std::tuple{"death",&c.death,false}})if(v.object.contains(name)&&!reaction(v.get(name),*destination,knockback,why))return false;
 if(v.object.contains("target_priority")){const auto&p=v.get("target_priority");if(p.type!=Value::String||(p.text!="low"&&p.text!="normal"&&p.text!="high"))return why="target_priority must be low, normal or high",false;c.target_priority=p.text=="low"?0:p.text=="high"?2:1;}
 const auto&a=v.get("attack");if(!keys(a,{"damage","radius","height","forward_offset","y_offset","active_frames","cooldown_frames"}))return why="invalid combat attack fields",false;
 if(!integer(a.get("damage"),1,4,c.attack.damage)||!scalar(a.get("radius"),1,200,c.attack.radius)||!scalar(a.get("height"),1,400,c.attack.height)||!integer(a.get("active_frames"),1,150,c.attack.active_frames)||!integer(a.get("cooldown_frames"),c.attack.active_frames,900,c.attack.cooldown_frames))return why="invalid bounded physical attack",false;
 if((a.object.contains("forward_offset")&&!scalar(a.get("forward_offset"),0,300,c.attack.forward_offset))||(a.object.contains("y_offset")&&!scalar(a.get("y_offset"),-200,300,c.attack.y_offset)))return why="invalid combat attack offset",false;
 out=std::move(c);why.clear();return true;
}
// Only one curated native heart. Queued native pickup is distinct from atomic mod rewards.
inline bool parse_native_drops(const Value&v,bool&heart,std::string&why){
 if(v.type!=Value::Array||v.array.size()>1)return why="native_drops permits at most one heart",false;
 bool parsed=false;if(!v.array.empty()){const auto&d=v.array[0];unsigned quantity=0;if(!keys(d,{"item","quantity"})||d.get("item").type!=Value::String||d.get("item").text!="heart"||!integer(d.get("quantity"),1,1,quantity))return why="native_drops supports heart quantity 1 only",false;parsed=true;}
 heart=parsed;why.clear();return true;
}
}
