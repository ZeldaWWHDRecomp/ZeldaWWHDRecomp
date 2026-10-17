// SPDX-License-Identifier: MPL-2.0
#include "mods/story_mod_combat_manifest.h"
#include <cassert>
#include <limits>
using namespace story_mod_combat;
static Value fixture(){return mods::json::parse(R"({"health":10,"invulnerability_frames":15,"flinch":{"clip":"flinch","frames":8,"knockback_speed":300,"knockback_frames":6},"stun":{"clip":"stun","frames":30},"death":{"clip":"death","frames":30},"attack":{"damage":1,"radius":40,"height":100,"forward_offset":35,"y_offset":0,"active_frames":6,"cooldown_frames":30}})");}
int main(){Config c;std::string why;auto v=fixture();assert(parse(v,c,why));assert(c.health==10&&c.attack.damage==1&&c.flinch.knockback_frames==6&&c.death.clip=="death");
 for(const char* field:{"health","attack"}){auto b=v;b.object.erase(field);assert(!parse(b,c,why));}
 for(const char* field:{"health","invulnerability_frames"})for(double n:{-1.,256.5,901.}){auto b=v;b[field]=n;assert(!parse(b,c,why));}
 for(const char* field:{"damage","radius","height","active_frames","cooldown_frames"}){auto b=v;b["attack"][field]=0;assert(!parse(b,c,why));}
 for(auto value:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){auto b=v;b["attack"]["radius"]=value;assert(!parse(b,c,why));}
 {auto b=v;b["attack"]["damage"]=5;assert(!parse(b,c,why));b=v;b["attack"]["active_frames"]=151;assert(!parse(b,c,why));b=v;b["attack"]["cooldown_frames"]=5;assert(!parse(b,c,why));}
 for(const char* unknown:{"native_type","health_offset"}){auto b=v;b[unknown]=1;assert(!parse(b,c,why));}
 {auto b=v;b["flinch"]["knockback_frames"]=0;assert(!parse(b,c,why));b=v;b["stun"]["knockback_speed"]=1;assert(!parse(b,c,why));b=v;b["death"]["frames"]=0;assert(!parse(b,c,why));b=v;b["death"]["clip"]="../foreign";assert(!parse(b,c,why));b=v;b["attack"]["raw_flags"]=1;assert(!parse(b,c,why));}
 {auto b=v;b["health"]=255;b["invulnerability_frames"]=900;b["attack"]["damage"]=4;b["attack"]["active_frames"]=150;b["attack"]["cooldown_frames"]=900;assert(parse(b,c,why));assert(c.health==255);auto bad=b;bad["health"]=0;assert(!parse(bad,c,why)&&c.health==255);}
 {auto b=v;b["attack"]["forward_offset"]=300;b["attack"]["y_offset"]=300;assert(parse(b,c,why));b["attack"]["forward_offset"]=301;assert(!parse(b,c,why));b=v;b["attack"]["y_offset"]=301;assert(!parse(b,c,why));b=v;b["attack"]["y_offset"]=-201;assert(!parse(b,c,why));}
 {for(const auto& [name,expected]:{std::pair{"low",0u},std::pair{"normal",1u},std::pair{"high",2u}}){auto b=v;b["target_priority"]=name;assert(parse(b,c,why)&&c.target_priority==expected);}auto b=v;b["target_priority"]="nearest";assert(!parse(b,c,why));b["target_priority"]=2;assert(!parse(b,c,why));}
 bool heart=false;assert(parse_native_drops(mods::json::parse(R"([{"item":"heart","quantity":1}])"),heart,why)&&heart);assert(parse_native_drops(mods::json::parse("[]"),heart,why)&&!heart);
 for(const char* bad:{R"([{"item":"heart","quantity":2}])",R"([{"item":"rupee","quantity":1}])",R"([{"item":"heart","quantity":1,"raw_bit":255}])",R"([{"item":"heart","quantity":1},{"item":"heart","quantity":1}])","null"})assert(!parse_native_drops(mods::json::parse(bad),heart,why));
}
