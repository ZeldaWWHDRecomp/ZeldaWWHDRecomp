#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#include "mods/story_world_manifest.h"
#include "mods/story_actor_identity.h"
using namespace story_notebook;
using mods::json::Value;
static Value example() {
    return mods::json::parse(R"({"actors":[{"id":"guide","actor":"resident","stage":"town","room":0,"position":{"x":0,"y":0,"z":0},"lines":["Hello"],"then":[{"action":"scene","id":"intro"}]},{"id":"notice","actor":"sign","flag":"read","stage":"town","room":0,"position":{"x":1,"y":2,"z":3}}],"scenes":[{"id":"intro","trigger":{"type":"talk","actor":"guide"},"steps":[{"type":"camera","eye":{"x":0,"y":100,"z":100},"target_actor":"guide","frames":60},{"type":"say","actor":"guide","text":"Welcome","frames":600},{"type":"fade","direction":"out","frames":30},{"type":"end","then":[{"action":"flag","id":"seen"}]}]}]})");
}
int main() {
    std::string why;std::vector<ManifestActor> actors;std::vector<ManifestScene> scenes;
    { // Completion actions share the same strict parser in actor and scene lists.
        auto m=mods::json::parse(R"({"quests":[{"id":"delivery"}]})");
        auto action=mods::json::parse(R"([{"action":"quest_done","id":"delivery"}])");
        std::vector<StoryAction> parsed;
        assert(world_manifest::actions(m,action,parsed,why));
        assert(parsed.size()==1&&parsed[0].kind==StoryAction::Kind::QuestDone);
        action.array[0]["id"]="foreign:delivery";parsed.clear();
        assert(!world_manifest::actions(m,action,parsed,why));
        action.array[0]["id"]="missing";
        assert(!world_manifest::actions(m,action,parsed,why));
        action.array[0]["id"]="delivery";action.array[0]["step"]=1;
        assert(!world_manifest::actions(m,action,parsed,why));
    }
    { // "stage": into one of the package's own stages (mod_stages), room and point packed into value
        auto m=mods::json::parse(R"({"stages":[{"id":"sky"}]})");
        std::vector<StoryAction> parsed;
        assert(world_manifest::actions(m,mods::json::parse(R"([{"action":"stage","id":"sky","room":1,"point":3}])"),parsed,why));
        assert(parsed.size()==1&&parsed[0].kind==StoryAction::Kind::EnterStage&&parsed[0].id=="sky"&&parsed[0].value==(1u<<8|3u));
        parsed.clear();
        assert(world_manifest::actions(m,mods::json::parse(R"([{"action":"stage","id":"sky"}])"),parsed,why)&&parsed[0].value==0);
        for(const char* bad:{R"([{"action":"stage","id":"cave"}])",R"([{"action":"stage","id":"sky","room":64}])",
                             R"([{"action":"stage","id":"sky","point":100}])",R"([{"action":"stage","id":"sky","wipe":1}])"}){
            parsed.clear();assert(!world_manifest::actions(m,mods::json::parse(bad),parsed,why));
        }
    }
    {
        auto m=example();m["quests"]=mods::json::parse(R"([{"id":"a","steps":[{"n":1}]},{"id":"b","steps":[{"n":2}]}])");
        m["actors"].array[0]["when"]=mods::json::parse(R"({"flag":"read","not_flag":"seen","quests":[{"quest":"a","step":1},{"quest":"b","step":2}]})");
        assert(parse_actors(m,actors,why));assert(actors[0].when_flag=="read"&&actors[0].when_not_flag=="seen"&&actors[0].quests.size()==2);
        m["actors"].array[0]["when"]["flag"]="unknown";assert(!parse_actors(m,actors,why));
        m["actors"].array[0]["when"]["flag"]="other:read";assert(!parse_actors(m,actors,why));
        m["actors"].array[0]["when"]["flag"]="0x2D01";assert(!parse_actors(m,actors,why));
        m["actors"].array[0]["when"]["flag"]="read";
        m["actors"].array[0]["when"]["quests"].array[1]["step"]=3;assert(!parse_actors(m,actors,why));
    }
    {
        auto m=example();m["actors"].array[0].object.erase("lines");m["actors"].array[0].object.erase("then");
        m["actors"].array[0]["talks"]=mods::json::parse(R"([{"when":{"flag":"read"},"lines":"Read first","then":[{"action":"flag","id":"seen"}]},{"lines":"Fallback"}])");
        assert(parse_actors(m,actors,why));assert(actors[0].talks.size()==2&&actors[0].talks[0].lines=="Read first");
        auto t=actors[0].talks[0];t.quests={{"a",2},{"b",1}};
        auto quest=[](const std::string&q){return q=="a"?2:1;};auto item=[](const std::string&){return 1;};
        assert(condition_matches(t,quest,item,[](const std::string&){return true;},true));
        assert(!condition_matches(t,quest,item,[](const std::string&){return false;},true));
        t.quests[1].step=2;assert(!condition_matches(t,quest,item,[](const std::string&){return true;},true));
        m["actors"].array[0]["lines"]="Ambiguous";assert(!parse_actors(m,actors,why));m["actors"].array[0].object.erase("lines");
        auto sign=m;sign["actors"].array[0]["actor"]="kanban";sign["actors"].array[0]["flag"]="notice.read";
        assert(parse_actors(sign,actors,why)&&actors[0].actor=="sign"&&actors[0].talks.size()==2);
        auto&talks=m["actors"].array[0]["talks"].array;while(talks.size()<9)talks.push_back(talks.back());assert(!parse_actors(m,actors,why));
    }
    { // Conditions recognize every existing flag-producing actor spelling.
        for(const char*type:{"sign","kanban","PROC_KANBAN","chest","Tbox","takara","PROC_TBOX"}){
            auto m=example();m["actors"].array[1]["actor"]=type;
            m["actors"].array[0]["when"]=mods::json::parse(R"({"flag":"read"})");
            assert(parse_actors(m,actors,why)&&actors[0].when_flag=="read");
        }
        for(const char*type:{"pot","barrel","crate","chuchu","bokoblin","moblin","keese"}){
            auto m=example();auto&producer=m["actors"].array[1];producer["actor"]=type;producer.object.erase("flag");
            const bool prop=std::string(type)=="pot"||std::string(type)=="barrel"||std::string(type)=="crate";
            producer[prop?"broken_flag":"defeated_flag"]="produced";producer["drops"]=mods::json::parse("[]");
            m["actors"].array[0]["when"]=mods::json::parse(R"({"flag":"produced"})");
            assert(parse_actors(m,actors,why)&&actors[0].when_flag=="produced");
        }
        auto m=mods::json::parse(R"({"actors":[{"kind":"mod","flag":"custom"},{"kind":"mod","combat":{},"defeated_flag":"won"},{"actor":"switch","switch_flag":"pressed"}]})");
        for(const char*name:{"custom","won","pressed"})assert(world_manifest::known_flag(m,name));
        assert(!world_manifest::known_flag(m,"missing"));
        m=mods::json::parse(R"({"actors":[{"actor":"resident","when":{"flag":"read"}}]})");
        assert(!world_manifest::known_flag(m,"read")); // references never declare flags
        m["actors"].array[0]["then"]=mods::json::parse(R"([{"action":"flag","id":"read","value":"yes"}])");
        assert(!world_manifest::known_flag(m,"read"));
        m["actors"].array[0]["then"].array[0]["value"]=false;
        assert(world_manifest::known_flag(m,"read")); // clearing is an owned producer too
    }
    { // A captured native PID must never be rebound to a different declared reward/config.
        using namespace story_actors::identity;ManifestActor a;a.id="a";a.actor="pot";a.stage="sea";a.room=11;a.broken_flag="a.broken";a.drops={{"shard",1}};
        const auto expected=fingerprint("test",a);const auto saved=record(expected);assert(matches(saved.data(),expected));
        const auto different=[&](const ManifestActor&b){const auto hash=fingerprint("test",b);assert(hash!=expected&&!matches(saved.data(),hash));};
        auto b=a;b.id="b";different(b);const auto second=record(fingerprint("test",b));assert(!matches(second.data(),expected));
        std::array<std::array<uint8_t,kRecordBytes>,2> rows{saved,second};const auto second_hash=fingerprint("test",b);std::swap(rows[0],rows[1]);assert(!matches(rows[0].data(),expected)&&!matches(rows[1].data(),second_hash));
        b=a;b.actor="crate";different(b);b=a;b.stage="town";different(b);b=a;b.room=12;different(b);
        b=a;b.position.x=1;different(b);b=a;b.rotation.y=90;different(b);b=a;b.scale={2,2,2};different(b);
        b=a;b.broken_flag="other";different(b);b=a;b.defeated_flag="other";different(b);b=a;b.drops[0].quantity=2;different(b);b=a;b.drops[0].item="other";different(b);
        b=a;b.drops.push_back({"second",1});const auto ordered=fingerprint("test",b);std::swap(b.drops[0],b.drops[1]);assert(fingerprint("test",b)!=ordered);
        b=a;b.quest="quest";b.quest_step=2;different(b);b=a;b.item="item:key";different(b);b=a;b.time="night";different(b);
        b=a;b.flag="flag";different(b);b=a;b.params=1;different(b);b=a;b.lines="text";different(b);b=a;b.then.push_back({StoryAction::Kind::Flag,"flag",1});different(b);
        assert(fingerprint("foreign",a)!=expected);auto corrupt=saved;corrupt[0]^=1;assert(!matches(corrupt.data(),expected));corrupt=saved;corrupt[7]=2;assert(!matches(corrupt.data(),expected));corrupt=saved;corrupt.back()^=1;assert(!matches(corrupt.data(),expected));assert(!matches(nullptr,expected));
    }
    const auto prop_example=[](){return mods::json::parse(R"({"items":[{"id":"shard"}],"actors":[{"id":"vase","actor":"pot","stage":"town","room":0,"position":{"x":0,"y":0,"z":0},"broken_flag":"vase.broken","drops":[{"item":"item:shard","quantity":2}]}]})");};
    { // Validate all four curated native enemy declarations through the public parser.
        const auto fixture=[](){return mods::json::parse(R"({"items":[{"id":"shard"}],"actors":[{"id":"enemy","actor":"chuchu","stage":"sea","room":11,"position":{"x":0,"y":0,"z":0},"defeated_flag":"enemy.defeated","drops":[{"item":"item:shard","quantity":2}]}]})");};
        std::vector<std::string> enemy_types{"chuchu","bokoblin","moblin","keese","miniblin","peahat","darknut","rat_hole","bokobaba","bubble","octorok"};for(const char* more:story_enemy_variants::kMoreEnemies)enemy_types.push_back(more);
        for(const std::string& type_name:enemy_types){const char* type=type_name.c_str();
            auto data=fixture();
            if(story_enemy_variants::room_path_rule(type,"")){  // a wall cannon names its firing area (a mod room path)
                data["paths"]=mods::json::parse(R"([{"id":"area","stage":"sea","room":11,"closed":true,"points":[{"x":0,"y":0,"z":0},{"x":500,"y":0,"z":0},{"x":250,"y":0,"z":400}]}])");
                data["actors"].array[0]["path"]="area";}
            auto& value=data["actors"].array[0];value["actor"]=type;ManifestActor candidate;candidate.actor=type;
            assert(world_manifest::native_enemy_fields(data,value,candidate,why)&&candidate.defeated_flag=="enemy.defeated"&&candidate.drops.size()==1&&candidate.drops[0].quantity==2);
            assert(parse_actors(data,actors,why)&&actors.size()==1&&actors[0].actor==type&&actors[0].defeated_flag=="enemy.defeated"&&actors[0].drops[0].item=="shard");
            for(const char* field:{"params","parameters","flag","broken_flag","switch_flag","switch","lines","then","model","animation","look_at","path","collision","attention","kind"}){if((std::string(type)=="bokoblin"||std::string(type)=="moblin"||story_enemy_variants::takes_room_path(type))&&std::string(field)=="path")continue;auto bad=value;bad[field]=Value{};assert(!world_manifest::native_enemy_fields(data,bad,candidate,why));}
            for(int quantity:{0,255}){auto bad=value;bad["drops"].array[0]["quantity"]=quantity;assert(!world_manifest::native_enemy_fields(data,bad,candidate,why));}
            auto bad=value;bad["drops"].array[0]["item"]="item:foreign";assert(!world_manifest::native_enemy_fields(data,bad,candidate,why));
            bad=value;bad["defeated_flag"]="0x80";assert(!world_manifest::native_enemy_fields(data,bad,candidate,why));
            for(const char* field:{"defeated_flag","drops"}){auto bad=data;bad["actors"].array[0].object.erase(field);assert(!parse_actors(bad,actors,why)&&actors.empty());}
            for(const char* field:{"params","parameters","flag","broken_flag","switch_flag","switch","lines","then","model","animation","look_at","path","collision","attention","kind"}){auto bad=data;bad["actors"].array[0][field]=Value{};assert(!parse_actors(bad,actors,why)&&actors.empty());}
            {auto bad=data;auto duplicate=bad["actors"].array[0];duplicate["id"]="second";bad["actors"].array.push_back(duplicate);assert(!parse_actors(bad,actors,why)&&actors.empty());}
            {auto empty=data;empty["actors"].array[0]["drops"].array.clear();assert(parse_actors(empty,actors,why)&&actors[0].drops.empty());}
            {auto bad=data;auto drop=bad["actors"].array[0]["drops"].array[0];bad["actors"].array[0]["drops"].array.push_back(drop);assert(!parse_actors(bad,actors,why)&&actors.empty());}
            {auto bad=data;bad["actors"].array[0]["scale"]=mods::json::parse(R"({"x":2,"y":2,"z":2})");assert(!parse_actors(bad,actors,why)&&actors.empty());}
            candidate.scale={2,2,2};assert(!world_manifest::native_enemy_fields(data,value,candidate,why));candidate.scale={1,1,1};candidate.rotation.z=1;assert(!world_manifest::native_enemy_fields(data,value,candidate,why));
        }
        // declared variants only: ChuChu colours; never raw parameters, never another kind's variant
        for(const auto& v:story_enemy_variants::kVariants){auto data=fixture();auto& a=data["actors"].array[0];a["actor"]=v.kind;a["parameters"]=mods::json::parse(std::string(R"({"variant":")")+v.name+"\"}");
            if(v.path){data["paths"]=mods::json::parse(R"([{"id":"route","stage":"sea","room":11,"closed":true,"next":"spots","points":[{"x":0,"y":0,"z":0},{"x":500,"y":0,"z":0},{"x":250,"y":0,"z":400,"release":true}]},
                                                             {"id":"spots","stage":"sea","room":11,"closed":true,"points":[{"x":50,"y":0,"z":50},{"x":450,"y":0,"z":50},{"x":250,"y":0,"z":300}]}])");data["actors"].array[0]["path"]="route";}  // a route variant names its path (a rider's has a release point, a summoner's a next path)
            assert(parse_actors(data,actors,why)&&actors[0].variant==v.name);
            const auto fp=story_actors::identity::fingerprint("m",actors[0]);auto plain=actors[0];plain.variant.clear();assert(fp!=story_actors::identity::fingerprint("m",plain));}
        {auto data=fixture();assert(parse_actors(data,actors,why)&&actors[0].variant.empty()&&story_enemy_variants::find("chuchu","")->params==0xFF000000u);}
        for(const char* bad_variant:{R"({"variant":"purple"})",R"({"variant":""})",R"({"variant":3})",R"({"variant":"red","colour":1})",R"({"colour":"red"})",R"("red")"}){auto data=fixture();data["actors"].array[0]["parameters"]=mods::json::parse(bad_variant);assert(!parse_actors(data,actors,why)&&actors.empty());}
        std::vector<std::string> plain_kinds{"moblin","miniblin","bokobaba","octorok"};for(const char* more:story_enemy_variants::kMoreEnemies)if(!story_enemy_variants::has_variants(more))plain_kinds.push_back(more);
        for(const std::string& plain_name:plain_kinds){const char* plain_kind=plain_name.c_str();auto data=fixture();data["actors"].array[0]["actor"]=plain_kind;data["actors"].array[0]["parameters"]=mods::json::parse(R"({"variant":"red"})");assert(!parse_actors(data,actors,why)&&why.find("no variants")!=std::string::npos);}
        {auto data=fixture();data["actors"].array[0]["parameters"]=mods::json::parse(R"({"variant":"purple"})");assert(!parse_actors(data,actors,why)&&why.find("green, red, blue, yellow or black")!=std::string::npos);}
        for(const char* colour:{"red","blue","yellow"}){const auto*v=story_enemy_variants::find("chuchu",colour);assert(v&&(v->params&0xFFFF00FFu)==0xFF000000u&&((v->params>>8)&0xFF)>=1&&((v->params>>8)&0xFF)<=4);}
        assert(story_enemy_variants::find("chuchu","black")->params==0xFF000300u);
        // Fire Keese, the Seahat and the Bubble colours; the game has no Ice Keese and the bare-skull or path Bubble is never offered
        assert(story_enemy_variants::find("keese","")->params==0xFFFF0000u&&story_enemy_variants::find("keese","fire")->params==0xFFFF8000u&&!story_enemy_variants::find("keese","ice"));
        assert(story_enemy_variants::find("peahat","")->params==0xFFFFFF00u&&story_enemy_variants::find("peahat","seahat")->params==0xFFFFFF01u);
        assert(story_enemy_variants::find("bubble","")->params==0xFFFFFF00u&&story_enemy_variants::find("bubble","blue")->params==0xFFFFFF01u&&!story_enemy_variants::find("bubble","skull"));
        for(const auto& v:story_enemy_variants::kVariants)if(std::string(v.kind)=="bubble")assert((v.params&0xFEu)==0&&(v.params>>8)==0xFFFFFFu);
        {auto data=fixture();data["actors"].array[0]["actor"]="keese";data["actors"].array[0]["parameters"]=mods::json::parse(R"({"variant":"ice"})");assert(!parse_actors(data,actors,why)&&why.find("normal or fire")!=std::string::npos);}
        // a patrolling Bokoblin: points and mode (loop or back_and_forth), never speed or pauses; other enemies never take a path
        {auto data=fixture();auto& a=data["actors"].array[0];a["actor"]="bokoblin";a["path"]=mods::json::parse(R"({"points":[{"x":300,"y":0,"z":0},{"x":300,"y":0,"z":300}],"mode":"back_and_forth"})");
         assert(parse_actors(data,actors,why)&&actors[0].path.points.size()==2&&actors[0].path.back_and_forth);
         a["path"]["mode"]="loop";assert(parse_actors(data,actors,why)&&actors[0].path.loop);
         a["path"]["mode"]="once";assert(!parse_actors(data,actors,why));
         a["path"]["mode"]="loop";a["path"]["speed"]=100;assert(!parse_actors(data,actors,why));a["path"].object.erase("speed");
         a["path"]["pauses"]=true;assert(!parse_actors(data,actors,why));a["path"].object.erase("pauses");
         a["parameters"]=mods::json::parse(R"({"variant":"shield"})");assert(parse_actors(data,actors,why));
         assert(story_enemy_variants::bokoblin_patrol(0xFFFFFF24u)==0xFFFEFF20u&&story_enemy_variants::declared_params("bokoblin",0xFFFEFF20u)&&story_enemy_variants::bokoblin_patrols(0xFFFEFF20u)&&!story_enemy_variants::bokoblin_patrols(0xFFFFFF24u));
         a["actor"]="moblin";a.object.erase("parameters");assert(parse_actors(data,actors,why)&&actors[0].path.loop);
         assert(story_enemy_variants::moblin_patrol(0xFFFF0000u)==0xFFFE0000u&&story_enemy_variants::patrol_word(0xFFFE0000u));
         a["actor"]="chuchu";assert(!parse_actors(data,actors,why));}
        // a rat hole: the hole and three rats (four actors), its rats named "<id>.ratN" / "<flag>.ratN";
        // ids and flags at most 27 characters, no other actor with its rats' names, two holes per room
        {auto data=fixture();auto& a=data["actors"].array[0];a["actor"]="rat_hole";a["parameters"]=mods::json::parse(R"({"variant":"mixed"})");
         assert(parse_actors(data,actors,why)&&actors[0].variant=="mixed"&&placed_actors(actors[0])==4&&story_enemy_variants::kRatsPerHole==3);
         assert(!story_enemy_variants::rat_bomb("mixed",0)&&story_enemy_variants::rat_bomb("mixed",1)&&!story_enemy_variants::rat_bomb("mixed",2)&&story_enemy_variants::rat_bomb("bomb_rats",0)&&story_enemy_variants::rat_bomb("bomb_rats",2)&&!story_enemy_variants::rat_bomb("rats",1)&&!story_enemy_variants::rat_bomb("",1));
         for(const char* name:{"rats","bomb_rats","mixed"})assert(story_enemy_variants::find("rat_hole",name)&&story_enemy_variants::find("rat_hole",name)->params==0xFF010100u);
         assert(story_enemy_variants::find("rat_hole","")==story_enemy_variants::find("rat_hole","rats"));
         {auto bad=data;bad["actors"].array[0]["path"]=mods::json::parse(R"({"points":[{"x":300,"y":0,"z":0}],"mode":"loop"})");assert(!parse_actors(bad,actors,why));}
         {auto bad=data;bad["actors"].array[0]["id"]=std::string(28,'h');assert(!parse_actors(bad,actors,why)&&why.find("27")!=std::string::npos);}
         {auto ok=data;ok["actors"].array[0]["id"]=std::string(27,'h');ok["actors"].array[0]["defeated_flag"]=std::string(27,'f');assert(parse_actors(ok,actors,why));}
         {auto bad=data;bad["actors"].array[0]["defeated_flag"]=std::string(28,'f');assert(!parse_actors(bad,actors,why)&&why.find("27")!=std::string::npos);}
         const auto chu=[](const std::string& id,const std::string& flag){auto v=mods::json::parse(R"({"actor":"chuchu","stage":"sea","room":11,"position":{"x":0,"y":0,"z":0},"drops":[]})");v["id"]=id;v["defeated_flag"]=flag;return v;};
         {auto bad=data;bad["actors"].array.push_back(chu("enemy.rat2","other.defeated"));assert(!parse_actors(bad,actors,why)&&why.find("rats")!=std::string::npos);}
         {auto bad=data;bad["actors"].array.push_back(chu("other","enemy.defeated.rat3"));assert(!parse_actors(bad,actors,why)&&why.find("rats")!=std::string::npos);}
         {auto ok=data;ok["actors"].array.push_back(chu("enemy.rat4","enemy.defeated.rat4"));assert(parse_actors(ok,actors,why));}
         {auto two=data;auto hole=two["actors"].array[0];hole["id"]="h2";hole["defeated_flag"]="h2.closed";two["actors"].array.push_back(hole);assert(parse_actors(two,actors,why));
          hole["id"]="h3";hole["defeated_flag"]="h3.closed";two["actors"].array.push_back(hole);assert(!parse_actors(two,actors,why)&&why.find("two rat holes")!=std::string::npos);
          two["actors"].array.back()["room"]=12;assert(parse_actors(two,actors,why));}
         {auto full=data;for(int i=0;i<12;++i)full["actors"].array.push_back(chu("c"+std::to_string(i),"c"+std::to_string(i)+".d"));assert(parse_actors(full,actors,why));
          full["actors"].array.push_back(chu("c12","c12.d"));assert(!parse_actors(full,actors,why)&&why.find("16 per room")!=std::string::npos);}
         ManifestActor rat=actors.empty()?ManifestActor{}:actors[0];rat.actor="rat";rat.hole="enemy";const auto plain=story_actors::identity::fingerprint("m",rat);rat.rat_bomb=true;assert(plain!=story_actors::identity::fingerprint("m",rat));rat.hole="other";rat.rat_bomb=false;assert(plain!=story_actors::identity::fingerprint("m",rat));}
        // defeat_switch: an enemy names one of the mod's switch actors; anything else is refused
        {auto data=fixture();data["actors"].array.push_back(mods::json::parse(R"({"id":"gate-switch","actor":"switch","stage":"sea","room":11,"position":{"x":5,"y":0,"z":0},"switch_flag":"gate.open"})"));
         data["actors"].array[0]["defeat_switch"]="gate-switch";assert(parse_actors(data,actors,why)&&actors[0].defeat_switch=="gate-switch");
         auto plain=actors[0];plain.defeat_switch.clear();assert(story_actors::identity::fingerprint("m",actors[0])!=story_actors::identity::fingerprint("m",plain));
         data["actors"].array[0]["defeat_switch"]="missing";assert(!parse_actors(data,actors,why)&&why.find("defeat_switch")!=std::string::npos);
         data["actors"].array[0]["defeat_switch"]="enemy";assert(!parse_actors(data,actors,why));
         data["actors"].array[0]["defeat_switch"]=3;assert(!parse_actors(data,actors,why));
         data["actors"].array[0].object.erase("defeat_switch");data["actors"].array[1]["defeat_switch"]="gate-switch";assert(!parse_actors(data,actors,why)&&why.find("requires an enemy")!=std::string::npos);}
        // a Mighty Darknut and a Bokoblin naming the same switch: both are enemies for defeat_switch
        {auto data=fixture();data["actors"].array.push_back(mods::json::parse(R"({"id":"gate-switch","actor":"switch","stage":"sea","room":11,"position":{"x":5,"y":0,"z":0},"switch_flag":"gate.open"})"));
         auto& knight=data["actors"].array[0];knight["actor"]="darknut";knight["parameters"]=mods::json::parse(R"({"variant":"mighty"})");knight["defeat_switch"]="gate-switch";
         auto second=knight;second["id"]="guard";second["actor"]="bokoblin";second.object.erase("parameters");second["defeated_flag"]="guard.defeated";data["actors"].array.push_back(second);
         assert(parse_actors(data,actors,why)&&actors.size()==3&&actors[0].actor=="darknut"&&actors[0].variant=="mighty"&&actors[0].defeat_switch=="gate-switch"&&actors[2].defeat_switch=="gate-switch");}
        // Bokoblin: the stick is the default; the shield sets bit 0x20, the machete bit 0x40 (weapon 1)
        assert(story_enemy_variants::find("bokoblin","")->params==0xFFFFFF04u&&story_enemy_variants::bokoblin_weapon(0xFFFFFF04u)==0);
        assert(story_enemy_variants::find("bokoblin","shield")->params==0xFFFFFF24u&&story_enemy_variants::bokoblin_weapon(0xFFFFFF24u)==0);
        assert(story_enemy_variants::find("bokoblin","machete")->params==0xFFFFFF44u&&story_enemy_variants::bokoblin_weapon(0xFFFFFF44u)==1);
        {auto data=fixture();data["actors"].array[0]["actor"]="bokoblin";data["actors"].array[0]["parameters"]=mods::json::parse(R"({"variant":"red"})");assert(!parse_actors(data,actors,why)&&why.find("stick, shield or machete")!=std::string::npos);}
        // Darknut: guard behaviour 4, no path and no spawn switch in every variant; the equipment (x rotation
        // word bits 5..7) is the game's own pairing: none, shield, helmet and shield, helmet, shield and cape
        assert(story_enemy_variants::find("darknut","")->params==0xFFFF9604u&&story_enemy_variants::find("darknut","")->angle_x==0);
        for(const auto& v:story_enemy_variants::kVariants){if(std::string(v.kind)!="darknut")continue;assert((v.params&0xFFFF000Fu)==0xFFFF0004u&&!(v.angle_x&~0xE0u)&&story_enemy_variants::angle_x("darknut",v.params)==v.angle_x);}
        assert(story_enemy_variants::find("darknut","shield")->angle_x==2u<<5&&story_enemy_variants::find("darknut","helmet")->angle_x==3u<<5&&story_enemy_variants::find("darknut","mighty")->angle_x==5u<<5);
        for(const char* refused:{"frozen","patrol","boss","red"}){auto data=fixture();data["actors"].array[0]["actor"]="darknut";data["actors"].array[0]["parameters"]=mods::json::parse(std::string(R"({"variant":")")+refused+"\"}");assert(!parse_actors(data,actors,why)&&why.find("darknut, shield, helmet or mighty")!=std::string::npos);}
        {auto data=fixture();data["actors"].array[0]["actor"]="darknut";data["actors"].array[0]["rotation"]=mods::json::parse(R"({"x":45,"y":0,"z":0})");assert(!parse_actors(data,actors,why));}
    }
    for(const auto& name:{"pot","barrel","crate"}){auto p=prop_example();p["actors"].array[0]["actor"]=name;assert(parse_actors(p,actors,why)&&actors[0].broken_flag=="vase.broken"&&actors[0].drops[0].item=="shard"&&actors[0].drops[0].quantity==2);}
    for(const auto& name:{"pot","barrel","crate"}){auto p=prop_example();p["actors"].array[0]["actor"]=name;p["actors"].array[0]["texture"]="textures/blue.png";assert(parse_actors(p,actors,why)&&actors[0].texture=="textures/blue.png");const auto initial=story_actors::identity::fingerprint("test",actors[0]);actors[0].texture_digest="different";assert(story_actors::identity::fingerprint("test",actors[0])!=initial);}
    for(const auto& path:{"other.png","textures/../bad.png","textures/bad.jpg","textures//bad.png","assets/link\\bad.png"}){auto p=prop_example();p["actors"].array[0]["texture"]=path;assert(!parse_actors(p,actors,why));}
    {auto p=example();p["actors"].array[0]["texture"]="textures/blue.png";assert(!parse_actors(p,actors,why));}
    for(const auto& field:{"params","parameters","flag","lines","then","path","attention","collision","model"}){auto p=prop_example();p["actors"].array[0][field]=0;assert(!parse_actors(p,actors,why));}
    {auto p=prop_example();p["actors"].array[0].object.erase("broken_flag");assert(!parse_actors(p,actors,why));}
    {auto p=prop_example();p["actors"].array[0].object.erase("drops");assert(!parse_actors(p,actors,why));}
    {auto p=prop_example();p["actors"].array[0]["drops"].array.clear();assert(parse_actors(p,actors,why));}
    for(int n:{0,255}){auto p=prop_example();p["actors"].array[0]["drops"].array[0]["quantity"]=n;assert(!parse_actors(p,actors,why));}
    {auto p=prop_example();p["actors"].array[0]["drops"].array[0]["item"]="item:foreign";assert(!parse_actors(p,actors,why));}
    {auto p=prop_example();auto drop=p["actors"].array[0]["drops"].array[0];p["actors"].array[0]["drops"].array.push_back(drop);assert(!parse_actors(p,actors,why));}
    {auto p=prop_example();auto a=p["actors"].array[0];a["id"]="other";p["actors"].array.push_back(a);assert(!parse_actors(p,actors,why));}
    for(double scale:{.09,4.01}){auto p=prop_example();p["actors"].array[0]["scale"]=mods::json::parse("{\"x\":1,\"y\":1,\"z\":1}");for(const auto& axis:{"x","y","z"})p["actors"].array[0]["scale"][axis]=scale;assert(!parse_actors(p,actors,why));}
    {auto p=prop_example();p["actors"].array[0]["rotation"]=mods::json::parse("{\"x\":1,\"y\":0,\"z\":0}");assert(!parse_actors(p,actors,why));}
    { // Fixed native switch/door adapters: public fields never expose native IDs/events.
        const auto fixture=[](){return mods::json::parse(R"({"actors":[{"id":"gate","actor":"door","switch":"plate","stage":"sea","room":11,"position":{"x":300,"y":1040,"z":-201500}},{"id":"plate","actor":"switch","switch_flag":"plate-on","stage":"sea","room":11,"position":{"x":100,"y":1040,"z":-201500}}]})");};
        auto data=fixture();assert(parse_actors(data,actors,why)&&actors.size()==2);
        assert(actors[0].switch_actor=="plate"&&actors[0].switch_flag.empty()&&actors[1].switch_flag=="plate-on"&&actors[1].switch_actor.empty());
        const auto door_hash=story_actors::identity::fingerprint("own-mod",actors[0]);
        const auto switch_hash=story_actors::identity::fingerprint("own-mod",actors[1]);
        auto changed=actors[0];changed.switch_actor="other-plate";assert(story_actors::identity::fingerprint("own-mod",changed)!=door_hash);
        changed=actors[1];changed.switch_flag="other-on";assert(story_actors::identity::fingerprint("own-mod",changed)!=switch_hash);
        const auto invalid=[&](Value bad){assert(!parse_actors(bad,actors,why)&&actors.empty()&&!why.empty());};
        for(const char* field:{"params","parameters","flag","lines","then","broken_flag","drops","model","path","collision","attention","animation","look_at"}){
            for(unsigned index:{0u,1u}){auto bad=fixture();bad["actors"].array[index][field]=Value{};invalid(bad);}
        }
        for(const char* field:{"type","mode","event","key","destination","variant"}){auto bad=fixture();bad["actors"].array[0][field]="retail";invalid(bad);}
        for(const char* source:{"missing","gate","foreign:plate",""}){auto bad=fixture();bad["actors"].array[0]["switch"]=source;invalid(bad);}
        {auto bad=fixture();bad["actors"].array[0].object.erase("switch");invalid(bad);}
        {auto bad=fixture();bad["actors"].array[1].object.erase("switch_flag");invalid(bad);}
        {auto bad=fixture();bad["actors"].array[1]["switch_flag"]="game:4";invalid(bad);}
        {auto bad=fixture();bad["actors"].array[0]["switch_flag"]="wrong";invalid(bad);}
        {auto bad=fixture();bad["actors"].array[1]["switch"]="plate";invalid(bad);}
        {auto bad=fixture();auto other=bad["actors"].array[1];other["id"]="plate-two";bad["actors"].array.push_back(other);invalid(bad);}
        {auto good=fixture();auto other=good["actors"].array[0];other["id"]="gate-two";good["actors"].array.push_back(other);assert(parse_actors(good,actors,why)&&actors.size()==3);}
        for(unsigned index:{0u,1u}){
            auto bad=fixture();bad["actors"].array[index]["rotation"]=mods::json::parse(R"({"x":1,"y":0,"z":0})");invalid(bad);
            bad=fixture();bad["actors"].array[index]["scale"]=mods::json::parse(R"({"x":2,"y":2,"z":2})");invalid(bad);
            auto good=fixture();good["actors"].array[index]["rotation"]=mods::json::parse(R"({"x":0,"y":180,"z":0})");assert(parse_actors(good,actors,why));
        }
        {auto bad=example();bad["actors"].array[0]["switch_flag"]="wrong";invalid(bad);}
    }
    auto v=example();assert(parse_actors(v,actors,why)&&actors.size()==2);assert(parse_scenes(v,scenes,why)&&scenes[0].steps.size()==4);
    assert(parse_actors(Value{},actors,why)&&actors.empty());assert(parse_scenes(Value{},scenes,why)&&scenes.empty());
    v=example();v["actors"].array[0]["actor"]="bomb";assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[1].object.erase("flag");assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["position"]["x"]=350001;assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["scale"]["x"]=-1;assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["params"]=0;assert(!parse_actors(v,actors,why));
    v=example();auto actor=v["actors"].array[0];for(int i=0;i<15;++i){actor["id"]="extra"+std::to_string(i);v["actors"].array.push_back(actor);}assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["then"].array[0]["id"]="missing";assert(!parse_actors(v,actors,why));
    v=example();v["scenes"].array[0]["steps"].array[1]["actor"]="missing";assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array[0].object.erase("frames");assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array[0]["frames"]=18000;assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array.pop_back();assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array[0]["pan"]=true;assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["trigger"]["actor"]="missing";assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array[3]["then"].array[0]["id"]="0x1234";assert(!parse_scenes(v,scenes,why));
    v=example();v["actors"].array[1]["actor"]="Tbox";v["actors"].array[1]["params"]=0;v["actors"].array[1]["parameters"]["item"]="rupee_blue";assert(parse_actors(v,actors,why)&&actors[1].item_game==2);
    v["actors"].array[1]["parameters"]["item"]="triforce";assert(!parse_actors(v,actors,why)&&actors.empty());
    v=example();v["actors"].array[0]["parameters"]["resident"]=10;assert(!parse_actors(v,actors,why)&&actors.empty());
    for(const auto& name:{"uo2","um1","ym1"}){v=example();v["actors"].array[0]["parameters"]["resident"]=name;assert(parse_actors(v,actors,why));assert(actors[0].resident==(std::string(name)=="uo2"?1u:std::string(name)=="ym1"?19u:9u));}
    for(const auto& name:{"Uo2","ug1","1","","um3x"}){v=example();v["actors"].array[0]["parameters"]["resident"]=name;assert(!parse_actors(v,actors,why));}
    // the catalogue of placeable villagers: every declared code parses to its native subtype
    {const std::pair<const char*,unsigned> codes[]={{"uo1",0},{"uo2",1},{"uo3",2},{"ub1",3},{"ub2",4},{"ub3",5},{"ub4",6},{"uw1",7},{"uw2",8},{"um1",9},{"um2",10},{"sa1",12},{"sa2",13},{"sa3",14},{"sa4",15},{"ym1",19},{"rotena",20},{"rotenb",21},{"rotenc",22}};
     assert(std::size(codes)==std::size(story_resident_types::kTypes));
     for(const auto&[code,subtype]:codes){v=example();v["actors"].array[0]["parameters"]["resident"]=code;assert(parse_actors(v,actors,why));assert(actors[0].resident==subtype);
      const auto*t=story_resident_types::find(code);assert(t&&t->subtype==subtype&&story_resident_types::by_subtype(uint8_t(subtype))==t&&std::strlen(t->label)&&std::strlen(t->help));
      assert(story_resident_types::profile(uint8_t(subtype))==(subtype==19?0x13B:subtype>=20?0x174:0x175));
      // the story-gated pair skips its gate (stage argument 0xFF); everyone else keeps the ordinary 0;
      // a Goron merchant: its number in the top byte and no rail (0xFF)
      assert(story_resident_types::params(uint8_t(subtype))==(subtype==19?1u:subtype>=20?((subtype-20u)<<24)|0x00FFFFFFu:(subtype==8||subtype==10)?0x00FF00FFu:0x00FF0000u));
      assert(story_resident_types::create_argument(uint8_t(subtype))==(subtype==19?0xFFFFFFFFu:subtype>=20?subtype-20u:subtype));
      assert(story_resident_types::people(uint8_t(subtype))==(subtype<19));}}
    // the Goron merchants stand: a route is refused with the reason; talks and lines are theirs as any resident's
    for(const auto& name:{"rotena","rotenb","rotenc"}){v=example();v["actors"].array[0]["parameters"]["resident"]=name;
      v["actors"].array[0]["path"]=mods::json::parse(R"({"points":[{"x":300,"y":0,"z":0}]})");
      assert(!parse_actors(v,actors,why)&&why.find("Goron merchant")!=std::string::npos);}
    // a young Goron: a uniform scale 0.5..1.5
    v=example();v["actors"].array[0]["parameters"]["resident"]="rotena";v["actors"].array[0]["scale"]=mods::json::parse(R"({"x":0.7,"y":0.7,"z":0.7})");assert(parse_actors(v,actors,why)&&actors[0].resident==20);
    v["actors"].array[0]["scale"]=mods::json::parse(R"({"x":0.4,"y":0.4,"z":0.4})");assert(!parse_actors(v,actors,why)&&why.find("0.5 to 1.5")!=std::string::npos);
    v["actors"].array[0]["scale"]=mods::json::parse(R"({"x":1,"y":0.7,"z":1})");assert(!parse_actors(v,actors,why));
    // unsafe characters are refused with the reason, the rest with the list
    for(const auto& name:{"um3","sa5","ug1","ug2"}){v=example();v["actors"].array[0]["parameters"]["resident"]=name;assert(!parse_actors(v,actors,why)&&actors.empty());assert(why.find("not supported")!=std::string::npos&&why.find(name)!=std::string::npos);}
    v=example();v["actors"].array[0]["parameters"]["resident"]="rsh1";assert(!parse_actors(v,actors,why));assert(why.find("one of uo2, um1")!=std::string::npos&&why.find("ym1")!=std::string::npos);
    // ub4: never in the photo studio, where the game decides whether she appears
    v=example();v["actors"].array[0]["parameters"]["resident"]="ub4";v["actors"].array[0]["stage"]="Ocmera";v["actors"].array[0]["room"]=0;assert(!parse_actors(v,actors,why)&&why.find("Ocmera")!=std::string::npos);
    v["actors"].array[0]["stage"]="Opub";assert(parse_actors(v,actors,why)&&actors[0].resident==6);
    v["actors"].array[0]["parameters"]["resident"]="uo1";v["actors"].array[0]["stage"]="Ocmera";assert(parse_actors(v,actors,why));
    // the new characters are people residents (scene move/face), ym1 is not
    v=example();v["actors"].array[0]["parameters"]["resident"]="sa2";assert(story_notebook::world_manifest::resident_character(v,"guide"));
    v=example();v["actors"].array[0]["parameters"]["resident"]=19;assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["parameters"]["resident"]=1;assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["parameters"]["resident"]=9;assert(parse_actors(v,actors,why)&&actors[0].resident==9);
    // a resident's route ("path"): 1..15 points after its position, the walking characters only
    {auto route=[&](const char* code,const char* path_json){auto m=example();auto& a=m["actors"].array[0];a["parameters"]["resident"]=std::string(code);a["path"]=mods::json::parse(path_json);return m;};
     v=route("uo2",R"({"points":[{"x":100,"y":0,"z":0}]})");assert(parse_actors(v,actors,why));
     assert(actors[0].path.points.size()==1&&!actors[0].path.speed_given&&!actors[0].path.loop&&!actors[0].path.back_and_forth&&!actors[0].path.pauses&&std::string(actors[0].path.mode())=="once");
     v=route("um1",R"({"points":[{"x":100,"y":0,"z":0},{"x":100,"y":0,"z":100}],"mode":"loop","speed":90,"pauses":true})");assert(parse_actors(v,actors,why));
     assert(actors[0].path.loop&&actors[0].path.speed_given&&actors[0].path.speed==90&&actors[0].path.pauses);
     v=route("uo3",R"({"points":[{"x":100,"y":0,"z":0}],"mode":"back_and_forth"})");assert(parse_actors(v,actors,why)&&actors[0].path.back_and_forth);
     for(const char* walker:{"uo1","uo2","uo3","um1","um2"}){v=route(walker,R"({"points":[{"x":50,"y":0,"z":0}]})");assert(parse_actors(v,actors,why));}
     // the women and sailors walk with a borrowed walk (story_resident_walk_clip.h), at their own pace's limits
     for(const char* borrowed:{"ub1","ub2","ub3","ub4","uw1","uw2","sa1","sa2","sa3","sa4"}){v=route(borrowed,R"({"points":[{"x":50,"y":0,"z":0}],"mode":"loop"})");assert(parse_actors(v,actors,why)&&actors[0].path.loop);}
     v=route("ub1",R"({"points":[{"x":50,"y":0,"z":0}],"speed":72})");assert(parse_actors(v,actors,why));
     v=route("ub1",R"({"points":[{"x":50,"y":0,"z":0}],"speed":73})");assert(!parse_actors(v,actors,why)&&why.find("18 to 72")!=std::string::npos);
     v=route("sa4",R"({"points":[{"x":50,"y":0,"z":0}],"speed":29})");assert(!parse_actors(v,actors,why));
     v=route("ym1",R"({"points":[{"x":50,"y":0,"z":0}]})");assert(!parse_actors(v,actors,why)&&why.find("seated")!=std::string::npos);
     // speed: 0.5..2 times the character's own pace (uo2: 30 units a second)
     v=route("uo2",R"({"points":[{"x":50,"y":0,"z":0}],"speed":15})");assert(parse_actors(v,actors,why));
     v=route("uo2",R"({"points":[{"x":50,"y":0,"z":0}],"speed":60})");assert(parse_actors(v,actors,why));
     v=route("uo2",R"({"points":[{"x":50,"y":0,"z":0}],"speed":14})");assert(!parse_actors(v,actors,why)&&why.find("15 to 60")!=std::string::npos);
     v=route("uo2",R"({"points":[{"x":50,"y":0,"z":0}],"speed":61})");assert(!parse_actors(v,actors,why));
     v=route("um1",R"({"points":[{"x":50,"y":0,"z":0}],"speed":127})");assert(!parse_actors(v,actors,why));
     // bounds and shapes
     v=route("uo2",R"({"points":[]})");assert(!parse_actors(v,actors,why));
     {std::string many=R"({"points":[)";for(int i=0;i<16;i++)many+=std::string(i?",":"")+"{\"x\":"+std::to_string(10*(i+1))+",\"y\":0,\"z\":0}";v=route("uo2",(many+"]}").c_str());assert(!parse_actors(v,actors,why));}
     {std::string most=R"({"points":[)";for(int i=0;i<15;i++)most+=std::string(i?",":"")+"{\"x\":"+std::to_string(10*(i+1))+",\"y\":0,\"z\":0}";v=route("uo2",(most+"]}").c_str());assert(parse_actors(v,actors,why)&&actors[0].path.points.size()==15);}
     v=route("uo2",R"({"points":[{"x":0.5,"y":0,"z":0}]})");assert(!parse_actors(v,actors,why)&&why.find("position")!=std::string::npos);
     v=route("uo2",R"({"points":[{"x":50,"y":0,"z":0},{"x":50,"y":9,"z":0.5}]})");assert(!parse_actors(v,actors,why));
     v=route("uo2",R"({"points":[{"x":50,"y":0,"z":0},{"x":0.5,"y":0,"z":0}],"mode":"loop"})");assert(!parse_actors(v,actors,why)&&why.find("loop")!=std::string::npos);
     v=route("uo2",R"({"points":[{"x":50,"y":0,"z":0}],"mode":"circle"})");assert(!parse_actors(v,actors,why)&&why.find("back_and_forth")!=std::string::npos);
     v=route("uo2",R"({"points":[{"x":50,"y":0,"z":0}],"pauses":1})");assert(!parse_actors(v,actors,why));
     v=route("uo2",R"({"points":[{"x":50,"y":0,"z":0}],"walk":"walk"})");assert(!parse_actors(v,actors,why));  // a resident walks with its own walk
     v=route("uo2",R"({"points":[{"x":50,"y":0,"z":0}],"gait":"run"})");assert(!parse_actors(v,actors,why));
     // a sign has no route
     {auto m=example();m["actors"].array[1]["path"]=mods::json::parse(R"({"points":[{"x":50,"y":0,"z":0}]})");assert(!parse_actors(m,actors,why));}
     // the fingerprint follows every route field
     v=route("uo2",R"({"points":[{"x":50,"y":0,"z":0}]})");assert(parse_actors(v,actors,why));const auto base=story_actors::identity::fingerprint("m",actors[0]);
     for(const char* other:{R"({"points":[{"x":51,"y":0,"z":0}]})",R"({"points":[{"x":50,"y":0,"z":0}],"mode":"back_and_forth"})",R"({"points":[{"x":50,"y":0,"z":0}],"mode":"loop"})",
                            R"({"points":[{"x":50,"y":0,"z":0}],"pauses":true})",R"({"points":[{"x":50,"y":0,"z":0}],"speed":30})"}){
         v=route("uo2",other);assert(parse_actors(v,actors,why));assert(story_actors::identity::fingerprint("m",actors[0])!=base);}
    }
    v=example();v["actors"].array[0]["parameters"]["resident"]="ym1";v["actors"].array[0]["position"]["x"]=-202066;v["actors"].array[0]["position"]["z"]=320123;assert(!parse_actors(v,actors,why));
    v["actors"].array[0]["position"]["x"]=-202056;assert(parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["parameters"]["resident"]="ym1";assert(!story_notebook::world_manifest::resident_character(v,"guide"));


    v=example();v["actors"].array[0]["then"]=Value("bad");assert(!parse_actors(v,actors,why));
    v=example();v["scenes"].array.push_back(v["scenes"].array[0]);assert(!parse_scenes(v,scenes,why)&&scenes.empty());
    v=example();v["scenes"].array[0]["steps"].array[3]["then"]=true;assert(!parse_scenes(v,scenes,why)&&scenes.empty());
    v=example();v["scenes"].array[0]["steps"].array[0]["frames"]=1.5;assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array[0]["eye"]["x"]=350001;assert(!parse_scenes(v,scenes,why));
    v=example();auto& shot=v["scenes"].array[0]["steps"].array[0];shot.object.erase("frames");shot["seconds"]=0.5;assert(parse_scenes(v,scenes,why)&&scenes[0].steps[0].frames==30);shot["frames"]=30;assert(!parse_scenes(v,scenes,why));
    v=example();v["actors"].array[0]["rotation"]["x"]=0;v["actors"].array[0]["rotation"]["y"]=361;v["actors"].array[0]["rotation"]["z"]=0;assert(!parse_actors(v,actors,why));
    v=example();auto& move=v["scenes"].array[0]["steps"].array[0];move=mods::json::parse(R"({"type":"move","actor":"guide","position":{"x":0,"y":0,"z":0},"frames":60})");assert(parse_scenes(v,scenes,why));move["actor"]="link";assert(!parse_scenes(v,scenes,why));move["actor"]="notice";assert(!parse_scenes(v,scenes,why));
    v=example();auto& flag=v["scenes"].array[0]["steps"].array[3]["then"].array[0];flag["value"]=false;assert(parse_scenes(v,scenes,why)&&scenes[0].steps.back().then[0].value==0);flag["value"]=Value{};assert(!parse_scenes(v,scenes,why));flag["value"]=0;assert(!parse_scenes(v,scenes,why));flag["value"]=true;flag["switch"]=1;assert(!parse_scenes(v,scenes,why));
    v=example();v["items"]=mods::json::parse(R"([{"id":"token"}])");auto& give=v["actors"].array[0]["then"].array[0];give=mods::json::parse(R"({"action":"give","id":"item:token","count":254})");assert(parse_actors(v,actors,why)&&actors[0].then[0].value==254);
    for(const auto& bad:{Value{},Value(0),Value(255),Value(1.5),Value(true),Value("2")}){give["count"]=bad;assert(!parse_actors(v,actors,why));}
    give["count"]=2;give["action"]="take";assert(parse_actors(v,actors,why)&&actors[0].then[0].value==2);
    // a chart from dialogue: give chart:<id> of the manifest's charts, once (no count, never taken), in an
    // actor's then, a talk's then and a scene's end
    v=example();v["charts"]=mods::json::parse(R"([{"id":"keeper-map","name":"Keeper's map","place":{"x":0,"z":0}}])");
    {auto& chart=v["actors"].array[0]["then"].array[0];chart=mods::json::parse(R"({"action":"give","id":"chart:keeper-map"})");
     assert(parse_actors(v,actors,why)&&actors[0].then[0].kind==StoryAction::Kind::GiveChart&&actors[0].then[0].id=="chart:keeper-map");
     chart["count"]=1;assert(!parse_actors(v,actors,why)&&why.find("no count")!=std::string::npos);chart.object.erase("count");
     chart["action"]="take";assert(!parse_actors(v,actors,why)&&why.find("can't be taken")!=std::string::npos);chart["action"]="give";
     chart["id"]="chart:nowhere";assert(!parse_actors(v,actors,why)&&why.find("undeclared mod chart")!=std::string::npos);chart["id"]="chart:keeper-map";}
    {auto& end=v["scenes"].array[0]["steps"].array[3]["then"];end=mods::json::parse(R"([{"action":"give","id":"chart:keeper-map"}])");
     assert(parse_scenes(v,scenes,why)&&scenes[0].steps.back().then[0].kind==StoryAction::Kind::GiveChart);}
    {auto& a=v["actors"].array[0];a.object.erase("lines");a.object.erase("then");
     a["talks"]=mods::json::parse(R"([{"lines":["Take my map."],"then":[{"action":"give","id":"chart:keeper-map"}]}])");
     assert(parse_actors(v,actors,why)&&actors[0].talks[0].then[0].kind==StoryAction::Kind::GiveChart);}
    v.object.erase("charts");assert(!parse_actors(v,actors,why));
    // what a placed chest holds: the chart chests' game items, or one of the mod's items
    {auto w=example();w["items"]=mods::json::parse(R"([{"id":"crystal","name":"Crystal","description":"It glows.","icon":"c.png"}])");
     auto chest=mods::json::parse(R"({"id":"box","actor":"chest","stage":"sea","room":11,"position":{"x":0,"y":0,"z":0},"flag":"box-open"})");
     auto with=[&](const char* item){auto x=w;auto c=chest;if(item)c["parameters"]["item"]=item;x["actors"].array.push_back(c);return x;};
     auto holds=[&](const char* item,uint8_t game,const char* mod){if(!parse_actors(with(item),actors,why))return false;const auto& c=actors.back();return c.item_game==game&&c.item_mod==mod;};
     assert(holds(nullptr,0x06,""));  // as before: an orange rupee
     assert(holds("rupee_green",0x01,"")&&holds("rupee_red",0x04,"")&&holds("rupee_orange",0x06,""));
     assert(holds("rupee_silver",0x0F,"")&&holds("heart_piece",0x07,"")&&holds("heart_container",0x08,""));
     assert(holds("item:crystal",kChestModItem,"crystal"));
     for(const char* bad:{"item:nothing","triforce","game:heart_piece","rupee_gold","",})
         {assert(!parse_actors(with(bad),actors,why));}
     auto n=with(nullptr);n["actors"].array.back()["parameters"]["item"]=4;assert(!parse_actors(n,actors,why)&&why.find("chest item must be")!=std::string::npos);
     auto sign=with("rupee_red");sign["actors"].array.back()["actor"]="sign";assert(!parse_actors(sign,actors,why));}
    v=example();v["actors"].array[0]["switch"]=1;assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["parameters"]["switch"]=1;assert(!parse_actors(v,actors,why));
    v=example();v["scenes"].array[0]["kind"]="mod";assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["trigger"]["event_bit"]=1;assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array[0]["duration"]=100;assert(!parse_scenes(v,scenes,why));
    v=example();v["scenes"].array[0]["steps"].array[3]["event_bit"]=1;assert(!parse_scenes(v,scenes,why));
    auto package=[](unsigned count,unsigned stage_index){Value m;m["actors"].type=Value::Array;for(unsigned i=0;i<count;++i){auto a=mods::json::parse(R"({"id":"actor","actor":"resident","stage":"stage","room":0,"position":{"x":0,"y":0,"z":0}})");a["id"]="actor"+std::to_string(i);a["stage"]="stage"+std::to_string(stage_index);a["room"]=int(i/16);m["actors"].array.push_back(a);}return m;};
    auto nine=package(9,0),eight=package(8,0);assert(!world_manifest::validate_world_packages({{"one",nine},{"two",eight}},why)&&why.find("16 actors")!=std::string::npos);
    eight["actors"].array.pop_back();assert(world_manifest::validate_world_packages({{"one",nine},{"two",eight}},why));
    assert(!world_manifest::validate_world_packages({{"one",package(48,0)},{"two",package(48,1)},{"three",package(48,2)}},why)&&why.find("128 actors")!=std::string::npos);
    std::map<uint16_t,std::string> flag_names;std::string one_flag,two_flag;
    for(unsigned i=0;i<1000;++i){const std::string name="flag"+std::to_string(i);flag_names.emplace(story_mail::letter_key("one","actor-flag:"+name),name);}
    for(unsigned i=0;i<100000&&two_flag.empty();++i){const std::string name="flag"+std::to_string(i);auto found=flag_names.find(story_mail::letter_key("two","actor-flag:"+name));if(found!=flag_names.end()){one_flag=found->second;two_flag=name;}}
    assert(!two_flag.empty());auto one=package(1,0);one["actors"].array[0]["actor"]="sign";one["actors"].array[0]["flag"]=one_flag;
    auto two=mods::json::parse(R"({"scenes":[{"id":"scene","steps":[{"type":"end","then":[{"action":"flag","id":"flag"}]}]}]})");two["scenes"].array[0]["steps"].array[0]["then"].array[0]["id"]=two_flag;
    assert(!world_manifest::validate_world_packages({{"one",one},{"two",two}},why)&&why.find("hash collision")!=std::string::npos);
    two=package(1,1);two["actors"].array[0]["lines"]="Hello";two["actors"].array[0]["then"]=mods::json::parse(R"([{"action":"flag","id":"flag"}])");two["actors"].array[0]["then"].array[0]["id"]=two_flag;
    assert(!world_manifest::validate_world_packages({{"one",one},{"two",two}},why));
    assert(world_manifest::validate_world_packages({{"one",one}},why));
    v=example();auto& face=v["scenes"].array[0]["steps"].array[0];face=mods::json::parse(R"({"type":"face","actor":"guide","target_actor":"link"})");assert(parse_scenes(v,scenes,why));
    face["actor"]="link";assert(!parse_scenes(v,scenes,why));face["actor"]="notice";assert(!parse_scenes(v,scenes,why));
    v["actors"].array[1]["actor"]="chest";face["actor"]="notice";assert(!parse_scenes(v,scenes,why));
    face["actor"]="guide";face["target_actor"]="notice";assert(parse_scenes(v,scenes,why));
    auto mod=mods::json::parse(R"({"models":[{"id":"post","source":"models/post.glb"}],"actors":[{"id":"post","kind":"mod","model":"post","stage":"town","room":0,"position":{"x":0,"y":0,"z":0},"collision":{"radius":30,"height":120,"mode":"solid"},"attention":{"prompt":"check"},"lines":"Hello"}]})");
    assert(parse_actors(mod,actors,why)&&actors[0].kind=="mod"&&actors[0].model=="post"&&actors[0].collision_height==120&&!actors[0].pushable);
    auto valid_mod=mod;mod["actors"].array[0]["collision"]["mode"]="pushable";assert(parse_actors(mod,actors,why)&&actors[0].pushable);
    {auto talks=valid_mod;auto&a=talks["actors"].array[0];a.object.erase("lines");
        a["talks"]=mods::json::parse(R"([{"when":{"not_flag":"met"},"lines":"First meeting","then":[{"action":"flag","id":"met"}]},{"lines":"Welcome back"}])");
        assert(parse_actors(talks,actors,why)&&actors[0].talks.size()==2&&actors[0].flag.empty());
        a["lines"]="Ambiguous";assert(!parse_actors(talks,actors,why));a.object.erase("lines");
        a["then"]=mods::json::parse("[]");assert(!parse_actors(talks,actors,why));
    }

    { // Public static collision is explicit and cannot inherit mobile actor behaviour.
        auto island=valid_mod;auto&a=island["actors"].array[0];a.object.erase("attention");a.object.erase("lines");a["behaviour"]="static";a["collision"]=mods::json::parse(R"({"mesh":"post"})");
        assert(parse_actors(island,actors,why)&&actors[0].world_object()&&actors[0].collision_mesh=="post"&&actors[0].collision_shape.empty());
        auto rejected=[&](Value data){assert(!parse_actors(data,actors,why));};
        for(const char*behaviour:{"STATIC","unknown",""}){auto bad=island;bad["actors"].array[0]["behaviour"]=behaviour;rejected(bad);}
        for(const char*id:{"missing","other:post","../post"}){auto bad=island;bad["actors"].array[0]["collision"]["mesh"]=id;rejected(bad);}
        {auto bad=island;bad["models"].array[0]["kind"]="skinned";rejected(bad);}
        {auto bad=island;auto other=bad["models"].array[0];other["id"]="skin";other["kind"]="skinned";bad["models"].array.push_back(other);bad["actors"].array[0]["collision"]["mesh"]="skin";rejected(bad);}
        for(const char*field:{"carry","combat","path","animation","attention","talks","lines","then"}){auto bad=island;bad["actors"].array[0][field]=Value{};rejected(bad);}
        {auto bad=island;bad["actors"].array[0].object.erase("behaviour");rejected(bad);}
        {auto bad=island;bad["actors"].array[0].object.erase("collision");rejected(bad);}
        for(const char*shape:{"box","cylinder","sphere"})for(const char*surface:{"stone","grass","wood","sand","metal","wall"}){
            auto simple=island;auto&c=simple["actors"].array[0]["collision"];c=mods::json::parse(std::string(shape)=="box"?R"({"shape":"box","size":{"x":100000,"y":20,"z":100000}})":std::string(shape)=="cylinder"?R"({"shape":"cylinder","radius":50000,"height":100000})":R"({"shape":"sphere","radius":50000})");c["surface"]=surface;
            assert(parse_actors(simple,actors,why)&&actors[0].collision_shape==shape&&actors[0].collision_surface==surface&&actors[0].collision_mesh.empty());
            {auto bad=simple;bad["actors"].array[0]["collision"]["surface"]="col_grass";rejected(bad);}
            {auto bad=simple;bad["actors"].array[0]["collision"]["mesh"]="post";rejected(bad);}
            for(double dimension:{0.,-1.,100000.01}){auto bad=simple;auto&bc=bad["actors"].array[0]["collision"];if(std::string(shape)=="box")bc["size"]["x"]=dimension;else bc["radius"]=dimension/2;rejected(bad);}
            // one sea square (100000 units) along each axis, the old 20000 no longer a bound
            if(std::string(shape)=="box")for(const char*axis:{"y","z"}){auto bad=simple;bad["actors"].array[0]["collision"]["size"][axis]=100000.01;rejected(bad);auto good=simple;good["actors"].array[0]["collision"]["size"][axis]=20001;assert(parse_actors(good,actors,why));}
            if(std::string(shape)=="cylinder"){auto bad=simple;bad["actors"].array[0]["collision"]["height"]=100000.01;rejected(bad);}
            if(std::string(shape)!="box"){auto good=simple;good["actors"].array[0]["collision"]["radius"]=20001;assert(parse_actors(good,actors,why));}
            {auto bad=simple;auto&bc=bad["actors"].array[0]["collision"];if(std::string(shape)=="box")bc["radius"]=1;else bc["size"]=mods::json::parse(R"({"x":1,"y":1,"z":1})");rejected(bad);}
            if(std::string(shape)=="sphere"){auto bad=simple;bad["actors"].array[0]["collision"]["height"]=1;rejected(bad);}
        }
    }

    { // Floating and drifting use bounded own-format parameters, never native save bits.
        auto floating=mods::json::parse(R"({"models":[{"id":"raft","source":"assets/raft.glb"}],"actors":[{"id":"raft","kind":"mod","model":"raft","behaviour":"floating","stage":"sea","room":12,"position":{"x":100,"y":0,"z":200},"collision":{"shape":"box","size":{"x":100,"y":20,"z":200},"surface":"wood"}}]})");
        assert(parse_actors(floating,actors,why)&&actors[0].behaviour=="floating");
        auto rejected=[&](Value data){assert(!parse_actors(data,actors,why));};
        auto&fa=floating["actors"].array[0];fa["buoyancy"]=mods::json::parse(R"({"draft":-20,"damping":0.5,"max_tilt":12})");
        assert(parse_actors(floating,actors,why)&&actors[0].buoyancy.draft==-20&&actors[0].buoyancy.damping==0.5f&&actors[0].buoyancy.max_tilt==12);
        for(const char*setting:{R"({"draft":-1000,"damping":0,"max_tilt":0})",R"({"draft":1000,"damping":10,"max_tilt":30})","{}"}){auto data=floating;data["actors"].array[0]["buoyancy"]=mods::json::parse(setting);assert(parse_actors(data,actors,why));}
        for(const char*setting:{R"({"draft":-1001})",R"({"draft":1001})",R"({"damping":-0.1})",R"({"damping":10.1})",R"({"max_tilt":-1})",R"({"max_tilt":31})",R"({"damping":false})",R"({"max_tilt":"10"})",R"({"event_bit":1})"}){auto data=floating;data["actors"].array[0]["buoyancy"]=mods::json::parse(setting);rejected(data);}
        {auto data=floating;data["actors"].array[0]["stage"]="town";rejected(data);}
        for(const char*rotation:{R"({"x":1,"y":0,"z":0})",R"({"x":0,"y":0,"z":1})"}){auto data=floating;data["actors"].array[0]["rotation"]=mods::json::parse(rotation);rejected(data);}
        {auto data=floating;data["actors"].array[0]["rotation"]=mods::json::parse(R"({"x":0,"y":90,"z":0})");assert(parse_actors(data,actors,why));}
        {auto data=floating;data["actors"].array[0]["drift"]=mods::json::parse(R"({"wind_speed":1,"max_speed":1})");rejected(data);}
        auto drifting=floating;auto&da=drifting["actors"].array[0];da["behaviour"]="drifting";rejected(drifting);
        da["drift"]=mods::json::parse(R"({"wind_speed":4,"current":{"x":-3,"z":2},"max_speed":8})");da["anchor"]=mods::json::parse(R"({"point":{"x":100,"y":0,"z":200},"radius":10000})");
        assert(parse_actors(drifting,actors,why)&&actors[0].drift.wind_speed==4&&actors[0].drift.current.x==-3&&actors[0].drift.current.z==2&&actors[0].drift.max_speed==8&&actors[0].anchor.enabled&&actors[0].anchor.radius==10000);
        {auto data=floating;data["actors"].array[0]["anchor"]=da.get("anchor");rejected(data);}
        {auto data=drifting;data["actors"].array[0].object.erase("anchor");assert(parse_actors(data,actors,why)&&!actors[0].anchor.enabled);}
        for(const char*setting:{R"({"wind_speed":100,"max_speed":100})",R"({"current":{"x":-100,"z":100},"max_speed":100})"}){auto data=drifting;data["actors"].array[0]["drift"]=mods::json::parse(setting);assert(parse_actors(data,actors,why));}
        for(const char*setting:{"{}",R"({"wind_speed":0,"current":{"x":0,"z":0},"max_speed":10})",R"({"wind_speed":-1,"max_speed":10})",R"({"wind_speed":101,"max_speed":10})",R"({"wind_speed":1,"max_speed":0})",R"({"wind_speed":1,"max_speed":101})",R"({"current":{"x":101,"z":0},"max_speed":10})",R"({"current":{"x":0,"z":-101},"max_speed":10})",R"({"current":{"x":1,"y":0,"z":0},"max_speed":10})",R"({"wind_speed":1,"max_speed":10,"event_bit":1})"}){auto data=drifting;data["actors"].array[0]["drift"]=mods::json::parse(setting);rejected(data);}
        for(const char*setting:{R"({"point":{"x":100,"y":0,"z":200},"radius":0})",R"({"point":{"x":100,"y":0,"z":200},"radius":10001})",R"({"point":{"x":100,"z":200},"radius":100})",R"({"point":{"x":100,"y":0,"z":200},"radius":100,"event_bit":1})"}){auto data=drifting;data["actors"].array[0]["anchor"]=mods::json::parse(setting);rejected(data);}
        for(const char*field:{"buoyancy","drift","anchor"}){auto data=drifting;auto&a=data["actors"].array[0];a["behaviour"]="static";for(const char*other:{"buoyancy","drift","anchor"})if(std::string(other)!=field)a.object.erase(other);rejected(data);}
        for(const char*field:{"carry","combat","path","animation","attention","talks","lines","then"}){auto data=drifting;data["actors"].array[0][field]=Value{};rejected(data);}
    }

    { // Grapple points bind to original rigid world objects and saved identities.
        auto tower=mods::json::parse(R"({"models":[{"id":"tower","source":"assets/tower.glb"}],"actors":[{"id":"tower","kind":"mod","model":"tower","behaviour":"static","stage":"sea","room":12,"position":{"x":100,"y":500,"z":200},"collision":{"shape":"box","size":{"x":100,"y":1000,"z":100}},"grapple_points":[{"id":"beam","position":{"x":0,"y":800,"z":-330},"yaw":90}]}]})");
        assert(parse_actors(tower,actors,why)&&actors[0].grapple_points.size()==1&&actors[0].grapple_points[0].id=="beam"&&actors[0].grapple_points[0].position.y==800&&actors[0].grapple_points[0].yaw==90);
        auto rejected=[&](Value data){assert(!parse_actors(data,actors,why));};
        const auto first=actors[0];const auto hash=story_actors::identity::fingerprint("own",first);
        for(unsigned field=0;field<4;++field){auto changed=first;if(field==0)changed.grapple_points[0].id="other";if(field==1)changed.grapple_points[0].position.x=1;if(field==2)changed.grapple_points[0].position.y+=1;if(field==3)changed.grapple_points[0].yaw+=1;assert(story_actors::identity::fingerprint("own",changed)!=hash);}
        auto bare=first;bare.grapple_points.clear();auto missing=tower;missing["actors"].array[0].object.erase("grapple_points");assert(parse_actors(missing,actors,why)&&actors[0].grapple_points.empty());assert(story_actors::identity::fingerprint("own",actors[0])==story_actors::identity::fingerprint("own",bare));assert(story_actors::identity::fingerprint("own",bare)!=hash);
        auto empty=tower;empty["actors"].array[0]["grapple_points"]=mods::json::parse("[]");assert(parse_actors(empty,actors,why)&&actors[0].grapple_points.empty());assert(story_actors::identity::fingerprint("own",actors[0])==story_actors::identity::fingerprint("own",bare));
        auto eight=tower;auto&points=eight["actors"].array[0]["grapple_points"].array;while(points.size()<8){auto p=points[0];p["id"]="beam"+std::to_string(points.size());points.push_back(p);}assert(parse_actors(eight,actors,why)&&actors[0].grapple_points.size()==8);
        auto ordered=actors[0];const auto ordered_hash=story_actors::identity::fingerprint("own",ordered);std::swap(ordered.grapple_points[0],ordered.grapple_points[1]);assert(story_actors::identity::fingerprint("own",ordered)!=ordered_hash);points.push_back(points.back());rejected(eight);
        for(const char*definition:{R"([{"id":"beam","position":{"x":-100000,"y":100000,"z":100000},"yaw":-360}])",R"([{"id":"beam","position":{"x":100000,"y":-100000,"z":-100000},"yaw":360}])",R"([{"id":"beam","position":{"x":20001,"y":0,"z":-20001}}])",R"([{"id":"beam","position":{"x":0,"y":1,"z":0}}])"}){auto data=tower;data["actors"].array[0]["grapple_points"]=mods::json::parse(definition);assert(parse_actors(data,actors,why));}
        for(const char*definition:{"null","{}",R"([{"id":"beam","position":{"x":0,"y":1,"z":0}},{"id":"beam","position":{"x":1,"y":2,"z":3}}])",R"([{"id":"foreign:beam","position":{"x":0,"y":1,"z":0}}])",R"([{"id":"beam","position":{"x":100000.01,"y":0,"z":0}}])",R"([{"id":"beam","position":{"x":0,"y":-100000.01,"z":0}}])",R"([{"id":"beam","position":{"x":0,"y":0,"z":100000.01}}])",R"([{"id":"beam","position":{"x":0,"z":0}}])",R"([{"id":"beam","position":{"x":0,"y":0,"z":0},"yaw":361}])",R"([{"id":"beam","position":{"x":0,"y":0,"z":0},"yaw":-361}])",R"([{"id":"beam","position":{"x":0,"y":0,"z":0},"yaw":"90"}])",R"([{"id":"beam","position":{"x":0,"y":0,"z":0},"native_params":1}])"}){auto data=tower;data["actors"].array[0]["grapple_points"]=mods::json::parse(definition);rejected(data);}
        auto floating=tower;floating["actors"].array[0]["behaviour"]="floating";assert(parse_actors(floating,actors,why)&&actors[0].grapple_points.size()==1);
        floating["actors"].array[0]["behaviour"]="drifting";floating["actors"].array[0]["drift"]=mods::json::parse(R"({"current":{"x":1,"z":0}})");assert(parse_actors(floating,actors,why)&&actors[0].grapple_points.size()==1);
        auto ordinary=tower;ordinary["actors"].array[0].object.erase("behaviour");ordinary["actors"].array[0]["collision"]=mods::json::parse(R"({"radius":30,"height":100,"mode":"solid"})");rejected(ordinary);
        auto native=example();native["actors"].array[0]["grapple_points"]=tower.get("actors").array[0].get("grapple_points");rejected(native);
        auto skinned=tower;skinned["models"].array[0]["kind"]="skinned";rejected(skinned);
    }

    { // Island presentation is original, same-package, bounded and mod-flag owned.
        auto island=mods::json::parse(R"({"models":[{"id":"island","source":"assets/island.glb"},{"id":"low","source":"assets/low.glb"},{"id":"coast","source":"assets/coast.glb"},{"id":"skin","source":"assets/skin.glb","kind":"skinned"}],"actors":[{"id":"island","kind":"mod","model":"island","behaviour":"static","stage":"sea","room":12,"position":{"x":100,"y":0,"z":200},"collision":{"shape":"box","size":{"x":100,"y":100,"z":100}},"distant":{"model":"low"},"shoreline":{"model":"coast"},"chart_marker":{"label":"Horizon Isle","when_flag":"island.arrived"},"arrival":{"name":"Horizon Isle","radius":2200,"flag":"island.arrived"}}]})");
        assert(parse_actors(island,actors,why)&&actors[0].distant.model=="low"&&actors[0].shoreline.model=="coast"&&actors[0].chart_marker.enabled&&actors[0].chart_marker.when_flag=="island.arrived"&&actors[0].arrival.enabled&&actors[0].arrival.radius==2200&&actors[0].arrival.flag=="island.arrived");
        assert(visual_variant_count(actors[0])==2&&world_manifest::known_flag(island,"island.arrived"));
        auto rejected=[&](Value data){assert(!parse_actors(data,actors,why));};
        const auto original=actors[0];const auto hash=story_actors::identity::fingerprint("own",original);
        for(unsigned field=0;field<7;++field){auto changed=original;if(field==0)changed.distant.model="other";if(field==1)changed.shoreline.model="other";if(field==2)changed.chart_marker.label="Other";if(field==3)changed.chart_marker.when_flag="other";if(field==4)changed.arrival.name="Other";if(field==5)changed.arrival.radius+=1;if(field==6)changed.arrival.flag="other";assert(story_actors::identity::fingerprint("own",changed)!=hash);}
        auto bare=island;for(const char*key:{"distant","shoreline","chart_marker","arrival"})bare["actors"].array[0].object.erase(key);assert(parse_actors(bare,actors,why)&&visual_variant_count(actors[0])==0);auto cleared=original;cleared.distant={};cleared.shoreline={};cleared.chart_marker={};cleared.arrival={};assert(story_actors::identity::fingerprint("own",actors[0])==story_actors::identity::fingerprint("own",cleared));
        for(const char*key:{"distant","shoreline"})for(const char*id:{"missing","other:low","../low","skin"}){auto bad=island;bad["actors"].array[0][key]["model"]=id;rejected(bad);}
        for(const char*key:{"distant","shoreline","chart_marker","arrival"}){auto bad=island;bad["actors"].array[0][key]=Value{};rejected(bad);bad=island;bad["actors"].array[0][key]["native_params"]=1;rejected(bad);}
        // a shoreline may use the still-water material (a pond's surface), a river's or a waterfall's; only those or foam, and only on the shoreline
        {auto water=island;water["actors"].array[0]["shoreline"]["material"]="water";assert(parse_actors(water,actors,why)&&actors[0].shoreline.material=="water"&&actors[0].shoreline.model=="coast");
         const auto foam_identity=story_actors::identity::fingerprint("m",original);auto water_spec=actors[0];assert(story_actors::identity::fingerprint("m",water_spec)!=foam_identity);
         assert(actors[0].water_body());
         // pond, river and waterfall surfaces (the editor's water tool): each its own identity, each a body of water
         for(const char*flowing:{"pond","river","waterfall"}){auto f=island;f["actors"].array[0]["shoreline"]["material"]=flowing;assert(parse_actors(f,actors,why)&&actors[0].shoreline.material==flowing&&actors[0].water_body());assert(story_actors::identity::fingerprint("m",actors[0])!=foam_identity&&story_actors::identity::fingerprint("m",actors[0])!=story_actors::identity::fingerprint("m",water_spec));}
         water["actors"].array[0]["shoreline"]["material"]="foam";assert(parse_actors(water,actors,why)&&actors[0].shoreline.material.empty()&&!actors[0].water_body());
         for(const char*bad_material:{"magma","Water",""}){auto bad=island;bad["actors"].array[0]["shoreline"]["material"]=bad_material;rejected(bad);}
         auto bad=island;bad["actors"].array[0]["distant"]["material"]="water";rejected(bad);assert(parse_actors(island,actors,why));}
        for(const char*behaviour:{"floating","drifting"}){auto bad=island;bad["actors"].array[0]["behaviour"]=behaviour;if(std::string(behaviour)=="drifting")bad["actors"].array[0]["drift"]=mods::json::parse(R"({"wind_speed":1})");rejected(bad);}
        {auto bad=island;bad["actors"].array[0]["stage"]="town";rejected(bad);}
        {auto bad=island;auto&a=bad["actors"].array[0];a.object.erase("behaviour");a["collision"]=mods::json::parse(R"({"radius":30,"height":100,"mode":"solid"})");rejected(bad);}
        {auto bad=example();bad["actors"].array[0]["distant"]=mods::json::parse(R"({"model":"low"})");rejected(bad);}
        for(const char*key:{"label","name"}){
            const char*block=std::string(key)=="label"?"chart_marker":"arrival";
            for(const auto&text:{std::string(),std::string(97,'x'),std::string("two\nlines"),std::string(1,char(0xFF))}){auto bad=island;bad["actors"].array[0][block][key]=text;rejected(bad);}
            auto data=island;data["actors"].array[0][block][key]=std::string(96,'x');assert(parse_actors(data,actors,why));data["actors"].array[0][block][key]="Île des étoiles";assert(parse_actors(data,actors,why));
        }
        for(double radius:{100.,20001.,100000.}){auto data=island;data["actors"].array[0]["arrival"]["radius"]=radius;assert(parse_actors(data,actors,why));}
        for(double radius:{0.,99.,100000.01}){auto data=island;data["actors"].array[0]["arrival"]["radius"]=radius;rejected(data);}
        for(const char*flag:{"missing","other:island.arrived","0x3510"}){auto bad=island;bad["actors"].array[0]["chart_marker"]["when_flag"]=flag;rejected(bad);}
        for(const char*flag:{"","other:island.arrived","0x3510"}){auto bad=island;bad["actors"].array[0]["arrival"]["flag"]=flag;rejected(bad);}
        {auto data=island;auto&a=data["actors"].array[0];a["chart_marker"].object.erase("when_flag");a.object.erase("arrival");assert(parse_actors(data,actors,why)&&actors[0].chart_marker.enabled&&!actors[0].arrival.enabled);}
        {auto bad=island;bad["actors"].array[0].object.erase("arrival");rejected(bad);}
        auto many=bare;auto prototype=many["actors"].array[0];many["actors"].array.clear();prototype["chart_marker"]=mods::json::parse(R"({"label":"An island"})");
        for(unsigned n=0;n<8;++n){auto a=prototype;a["id"]="island"+std::to_string(n);a["position"]["x"]=int(1000*n);many["actors"].array.push_back(a);}assert(parse_actors(many,actors,why));
        auto nine=many;auto ninth=prototype;ninth["id"]="ninth";nine["actors"].array.push_back(ninth);rejected(nine);
        std::vector<std::pair<std::string,Value>> selected;for(unsigned n=0;n<4;++n){auto data=many;for(auto&a:data["actors"].array)a["room"]=int(n);selected.push_back({"owner"+std::to_string(n),data});}assert(world_manifest::validate_world_packages(selected,why));
        auto extra=many;extra["actors"].array.resize(1);extra["actors"].array[0]["room"]=4;selected.push_back({"fifth",extra});assert(!world_manifest::validate_world_packages(selected,why));
    }

    { // Calm water: a static sea world object's zone, bounded, at most eight per mod, not part of the object's identity.
        auto island=mods::json::parse(R"({"models":[{"id":"island","source":"assets/island.glb"}],"actors":[{"id":"island","kind":"mod","model":"island","behaviour":"static","stage":"sea","room":18,"position":{"x":100,"y":0,"z":-100000},"rotation":{"x":0,"y":90,"z":0},"collision":{"shape":"box","size":{"x":100,"y":100,"z":100}},"calm_water":{"radius":3000}}]})");
        auto rejected=[&](Value data){assert(!parse_actors(data,actors,why));};
        assert(parse_actors(island,actors,why)&&actors[0].calm_water.enabled&&actors[0].calm_water.radius==3000&&actors[0].calm_water.falloff==story_world_calm::kGameFalloff&&actors[0].calm_water.offset_x==0&&actors[0].calm_water.offset_z==0);
        const auto with=story_actors::identity::fingerprint("own",actors[0]);auto plain=actors[0];plain.calm_water={};assert(story_actors::identity::fingerprint("own",plain)==with);
        {auto data=island;data["actors"].array[0]["calm_water"]=mods::json::parse(R"({"radius":4500.5,"falloff":2000,"offset":{"x":-1200,"z":300}})");
         assert(parse_actors(data,actors,why)&&actors[0].calm_water.radius==4500.5f&&actors[0].calm_water.falloff==2000&&actors[0].calm_water.offset_x==-1200&&actors[0].calm_water.offset_z==300);}
        for(double radius:{100.,100000.}){auto data=island;data["actors"].array[0]["calm_water"]["radius"]=radius;assert(parse_actors(data,actors,why));}
        for(double radius:{0.,99.9,100000.5,-5.}){auto data=island;data["actors"].array[0]["calm_water"]["radius"]=radius;rejected(data);}
        for(double falloff:{800.,25600.}){auto data=island;data["actors"].array[0]["calm_water"]["falloff"]=falloff;assert(parse_actors(data,actors,why));}
        for(double falloff:{0.,799.,25601.}){auto data=island;data["actors"].array[0]["calm_water"]["falloff"]=falloff;rejected(data);}
        for(const char*text:{R"({})",R"({"falloff":1000})",R"({"radius":"3000"})",R"({"radius":3000,"height":2})",R"({"radius":3000,"offset":{"x":1}})",R"({"radius":3000,"offset":{"x":1,"y":2,"z":3}})",R"({"radius":3000,"offset":{"x":100001,"z":0}})",R"(true)"}){
            auto data=island;data["actors"].array[0]["calm_water"]=mods::json::parse(text);rejected(data);}
        {auto data=island;data["actors"].array[0]["calm_water"]=Value{};rejected(data);}
        // only on a static sea world object
        {auto bad=island;bad["actors"].array[0]["stage"]="Asoko";rejected(bad);}
        {auto bad=island;bad["actors"].array[0]["behaviour"]="floating";rejected(bad);}
        {auto bad=example();bad["actors"].array[0]["calm_water"]=mods::json::parse(R"({"radius":3000})");rejected(bad);}
        // eight zones per mod
        auto many=island;auto prototype=many["actors"].array[0];many["actors"].array.clear();
        for(unsigned n=0;n<8;++n){auto a=prototype;a["id"]="island"+std::to_string(n);a["position"]["x"]=int(1000*n);many["actors"].array.push_back(a);}assert(parse_actors(many,actors,why));
        auto nine=many;auto ninth=prototype;ninth["id"]="ninth";ninth["position"]["x"]=9000;nine["actors"].array.push_back(ninth);rejected(nine);
    }

    std::vector<ManifestModel> models;assert(parse_models(valid_mod,models,why)&&models[0].kind=="static");
    mod=valid_mod;mod["models"].array[0]["kind"]="skinned";assert(parse_models(mod,models,why)&&models[0].kind=="skinned");
    mod["models"].array[0]["kind"]="static";assert(parse_models(mod,models,why)&&models[0].kind=="static");
    for(const auto& kind:{"animated","", "native"}){mod["models"].array[0]["kind"]=kind;assert(!parse_models(mod,models,why));}
    mod["models"].array[0]["kind"]=mods::json::Value{};assert(!parse_models(mod,models,why));
    // a scene area trigger may play "again" once Link has left the area
    {
        auto v=mods::json::parse(R"({"scenes":[{"id":"bird","trigger":{"type":"area","stage":"sea","room":44,"position":{"x":-194790,"y":136,"z":314530},"radius":120,"again":true},
            "steps":[{"type":"wait","frames":10},{"type":"end"}]}]})");
        std::vector<ManifestScene> scenes;assert(parse_scenes(v,scenes,why)&&scenes.size()==1&&scenes[0].again&&scenes[0].trigger=="area");
        v["scenes"].array[0]["trigger"].object.erase("again");assert(parse_scenes(v,scenes,why)&&!scenes[0].again);
        v["scenes"].array[0]["trigger"]["again"]="yes";assert(!parse_scenes(v,scenes,why));
        auto room=mods::json::parse(R"({"scenes":[{"id":"r","trigger":{"type":"room","stage":"sea","room":44,"again":true},"steps":[{"type":"wait","frames":10},{"type":"end"}]}]})");
        assert(!parse_scenes(room,scenes,why));  // only an area has an "inside" to leave
    }
    // parts of one composition: the same mod's static world objects that name one "group"; the same origin alone is not
    // one, nor another group, room or a floating object; group is for static world objects only
    {
        auto pair=mods::json::parse(R"({"actors":[{"id":"ground","kind":"mod","model":"post","behaviour":"static","group":"isle","collision":{"shape":"box","size":{"x":100,"y":100,"z":100}},
            "stage":"sea","room":44,"position":{"x":-200512,"y":6640,"z":301774},"rotation":{"x":0,"y":198,"z":0}},
            {"id":"ruin","kind":"mod","model":"post","behaviour":"static","group":"isle","collision":{"shape":"box","size":{"x":100,"y":100,"z":100}},
            "stage":"sea","room":44,"position":{"x":-200000,"y":6000,"z":301000},"rotation":{"x":0,"y":0,"z":0}}],
            "models":[{"id":"post","source":"models/post.glb"}]})");
        assert(parse_actors(pair,actors,why)&&actors.size()==2&&actors[0].group=="isle"&&story_notebook::one_composition(actors[0],actors[1]));
        auto other=actors[1];other.group="other";assert(!story_notebook::one_composition(actors[0],other));
        auto none=actors[1];none.group.clear();none.position=actors[0].position;none.rotation=actors[0].rotation;assert(!story_notebook::one_composition(actors[0],none));
        auto elsewhere=actors[1];elsewhere.room=43;assert(!story_notebook::one_composition(actors[0],elsewhere));
        auto floating=actors[1];floating.behaviour="floating";assert(!story_notebook::one_composition(actors[0],floating));
        auto bad=pair;bad["actors"].array[0]["group"]="Not An Id";assert(!parse_actors(bad,actors,why));
        bad=pair;bad["actors"].array[0]["behaviour"]="floating";assert(!parse_actors(bad,actors,why));
    }
    // kit models: a package's model drawn with a kit room's materials, placed only as a static world object at scale 1
    {
        auto kit=mods::json::parse(R"({"actors":[{"id":"ground","kind":"mod","model":"island","behaviour":"static","collision":{"mesh":"island"},"stage":"sea","room":44,
            "position":{"x":-200512,"y":6640,"z":301774},"rotation":{"x":0,"y":198,"z":0}},
            {"id":"ruins","kind":"mod","model":"ruins","behaviour":"static","collision":{"mesh":"ruins"},"stage":"sea","room":44,
            "position":{"x":-200512,"y":6640,"z":301774},"rotation":{"x":0,"y":198,"z":0}}],
            "models":[{"id":"island","kind":"kit","source":"assets/island.glb","kit":{"stage":"sea","room":41},"materials":{"grass":"m_kusa_v","spray":"SC_00_m_2_"}},
            {"id":"ruins","kind":"kit","source":"assets/ruins.glb","kit":{"stage":"Hyroom","room":0},"material":"lambert9_v"}]})");
        assert(parse_models(kit,models,why)&&models.size()==2&&models[0].kind=="kit"&&models[0].kit_stage=="sea"&&models[0].kit_room==41&&models[0].kit_materials.size()==2&&
               models[1].kit_material=="lambert9_v"&&models[1].kit_materials.empty());
        assert(parse_actors(kit,actors,why)&&actors.size()==2&&actors[0].kit_model&&actors[0].background_drawn()&&!actors[0].game_piece()&&actors[0].collision_mesh=="island");
        assert(!story_notebook::one_composition(actors[0],actors[1]));  // the same origin alone: two objects
        auto bad=kit;bad["models"].array[0].object.erase("kit");assert(!parse_models(bad,models,why));
        for(const char*stage:{"","Stage_Too_Long","sea/41"}){bad=kit;bad["models"].array[0]["kit"]["stage"]=stage;assert(!parse_models(bad,models,why));}
        for(int room:{-1,64}){bad=kit;bad["models"].array[0]["kit"]["room"]=room;assert(!parse_models(bad,models,why));}
        bad=kit;bad["models"].array[0]["kit"]["extra"]=1;assert(!parse_models(bad,models,why));
        bad=kit;bad["models"].array[0]["materials"]["grass"]="has space";assert(!parse_models(bad,models,why));
        bad=kit;bad["models"].array[0]["materials"]["grass"]=std::string(65,'m');assert(!parse_models(bad,models,why));
        bad=kit;bad["models"].array[1]["kind"]="static";assert(!parse_models(bad,models,why));  // kit fields belong to kind kit
        // the placement: static only, its own collision, turned about the vertical axis at scale 1
        bad=kit;bad["actors"].array[0]["behaviour"]="floating";assert(!parse_actors(bad,actors,why));
        bad=kit;bad["actors"].array[0]["collision"]["mesh"]="ruins";assert(!parse_actors(bad,actors,why));
        bad=kit;bad["actors"].array[0]["scale"]=mods::json::parse(R"({"x":2,"y":2,"z":2})");assert(!parse_actors(bad,actors,why));
        bad=kit;bad["actors"].array[0]["rotation"]["x"]=10;assert(!parse_actors(bad,actors,why));
        bad=kit;bad["actors"].array[0].object.erase("behaviour");bad["actors"].array[0].object.erase("collision");assert(!parse_actors(bad,actors,why));
        // a static model's collision can't name a kit model
        auto other=valid_mod;other["models"].array.push_back(kit["models"].array[0]);other["actors"].array[0]["behaviour"]="static";
        other["actors"].array[0]["collision"]=mods::json::parse(R"({"mesh":"island"})");assert(!parse_actors(other,actors,why));
    }
    for(const auto& source:{"../post.glb","/post.glb","models//post.glb","models/./post.glb","models/post.bfres","C:post.glb"}){mod=valid_mod;mod["models"].array[0]["source"]=source;assert(!parse_actors(mod,actors,why));}
    mod=valid_mod;mod["actors"].array[0]["animation"]="wave";assert(!parse_actors(mod,actors,why));mod["models"].array[0]["kind"]="skinned";assert(parse_actors(mod,actors,why)&&actors[0].animation=="wave");mod["actors"].array[0]["animation"]="";assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["actor"]="sign";assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["model"]="missing";assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["params"]=0;assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["collision"]["radius"]=0;assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["collision"]["height"]=2001;assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["attention"]["prompt"]="attack";assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0].object.erase("kind");assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["models"].array.push_back(mod["models"].array[0]);assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["collision"]["radius"]=1e-300;assert(!parse_actors(mod,actors,why));
    mod=valid_mod;mod["actors"].array[0]["collision"]["height"]=1e-300;assert(!parse_actors(mod,actors,why));
    mod=example();mod["actors"].array[0]["actor"]="mod";assert(!parse_actors(mod,actors,why));
    auto moving=valid_mod;moving["models"].array[0]["kind"]="skinned";auto& ma=moving["actors"].array[0];
    ma["look_at"]=mods::json::parse(R"({"head":"Head","yaw_degrees":70,"pitch_degrees":30,"rate_degrees":180,"radius":500,"upper_body":{"bone":"Root","yaw_degrees":20,"fraction":0.3}})");
    ma["path"]=mods::json::parse(R"({"points":[{"x":0,"y":0,"z":0},{"x":100,"y":0,"z":0},{"x":100,"y":0,"z":100}],"speed":100,"mode":"loop"})");
    assert(parse_actors(moving,actors,why)&&actors[0].look_at.enabled&&actors[0].look_at.head=="Head"&&actors[0].look_at.upper_bone=="Root"&&actors[0].path.loop&&actors[0].path.points.size()==3);
    for(const auto& field:{"look_at","path"}){mod=moving;mod["models"].array[0]["kind"]="static";mod["actors"].array[0].object.erase(field==std::string("look_at")?"path":"look_at");assert(!parse_actors(mod,actors,why));mod=moving;mod["actors"].array[0][field]=Value{};assert(!parse_actors(mod,actors,why));}
    for(const auto& pair:{std::pair{"yaw_degrees",181.},std::pair{"pitch_degrees",91.},std::pair{"rate_degrees",0.},std::pair{"rate_degrees",1081.},std::pair{"rate_degrees",1e-300},std::pair{"radius",0.},std::pair{"radius",1000001.}}){mod=moving;mod["actors"].array[0]["look_at"][pair.first]=Value(pair.second);assert(!parse_actors(mod,actors,why));}
    for(const auto& bone:{"", "1Head", "Head/Bad", "Head Bad"}){mod=moving;mod["actors"].array[0]["look_at"]["head"]=bone;assert(!parse_actors(mod,actors,why));}
    mod=moving;mod["actors"].array[0]["look_at"]["upper_body"]["bone"]="Head";assert(!parse_actors(mod,actors,why));mod=moving;mod["actors"].array[0]["look_at"]["upper_body"]["fraction"]=1.01;assert(!parse_actors(mod,actors,why));mod=moving;mod["actors"].array[0]["look_at"]["upper_body"]=Value{};assert(!parse_actors(mod,actors,why));
    mod=moving;mod["actors"].array[0]["look_at"]["yaw_degrees"]=0;mod["actors"].array[0]["look_at"]["pitch_degrees"]=0;mod["actors"].array[0]["look_at"].object.erase("upper_body");assert(parse_actors(mod,actors,why)&&actors[0].look_at.upper_bone.empty());
    for(const auto& speed:{0.,-1.,1501.,1e-300}){mod=moving;mod["actors"].array[0]["path"]["speed"]=Value(speed);assert(!parse_actors(mod,actors,why));}
    mod=moving;mod["actors"].array[0]["path"]["mode"]="teleport";assert(!parse_actors(mod,actors,why));mod=moving;mod["actors"].array[0]["path"]["points"].array.resize(1);assert(!parse_actors(mod,actors,why));mod=moving;mod["actors"].array[0]["path"]["points"].array.resize(17);assert(!parse_actors(mod,actors,why));
    mod=moving;mod["actors"].array[0]["path"]["points"].array[1]=mod["actors"].array[0]["path"]["points"].array[0];assert(!parse_actors(mod,actors,why));mod=moving;mod["actors"].array[0]["path"]["points"].array.back()=mod["actors"].array[0]["path"]["points"].array.front();assert(!parse_actors(mod,actors,why));mod["actors"].array[0]["path"]["mode"]="once";assert(parse_actors(mod,actors,why));
    mod=moving;mod["actors"].array[0]["path"]["points"].array[0]["speed"]=1;assert(!parse_actors(mod,actors,why));mod=moving;mod["actors"].array[0]["look_at"]["extra"]=true;assert(!parse_actors(mod,actors,why));
    mod=example();mod["actors"].array[0]["look_at"]=ma.get("look_at");assert(!parse_actors(mod,actors,why));mod=example();mod["actors"].array[0]["path"]=ma.get("path");assert(!parse_actors(mod,actors,why));
    auto extended=moving;extended["scenes"]=mods::json::parse(R"([{"id":"extended","steps":[
      {"id":"opening","type":"camera_path","easing":"smoothstep","points":[{"eye":{"x":0,"y":100,"z":100},"target":{"x":0,"y":0,"z":0},"frames":30},{"eye":{"x":100,"y":100,"z":100},"target":{"x":0,"y":0,"z":0},"frames":60},{"eye":{"x":200,"y":100,"z":100},"target":{"x":0,"y":0,"z":0}}]},
      {"id":"follow","type":"camera_follow","actor":"post","eye_offset":{"x":0,"y":100,"z":100},"target_offset":{"x":0,"y":0,"z":0},"half_life":0.2,"frames":60},
      {"id":"animate","type":"animation","actor":"post","clip":"wave","blend_seconds":0.3,"loop":false},
      {"id":"walk","type":"walk","actor":"post","points":[{"x":0,"y":0,"z":0},{"x":100,"y":0,"z":0}],"speed":100,"frames":300},
      {"id":"question","type":"choice","text":"Which way?","options":[{"text":"Left","next":"left"},{"text":"Right","next":"right"}],"default":1,"frames":600},
      {"id":"left","type":"music","cue":"calm","action":"start","fade_frames":60,"next":"left-end"},{"id":"left-end","type":"end"},
      {"id":"right","type":"music","cue":"calm","action":"stop","next":"right-end"},{"id":"right-end","type":"end"}
    ]}])");
    assert(parse_scenes(extended,scenes,why)&&scenes[0].steps[0].frames==90&&scenes[0].steps[0].camera_keys.size()==3&&scenes[0].steps[4].successors==std::vector<size_t>({5,7})&&scenes[0].steps[4].choices[1].next_step==7&&scenes[0].steps[4].default_choice==1&&scenes[0].steps[5].successors==std::vector<size_t>({6}));
    auto bad_scene=[&](Value data){assert(!parse_scenes(data,scenes,why)&&scenes.empty()&&!why.empty());};
    v=extended;v["scenes"].array[0]["steps"].array[0]["points"].array.back()["frames"]=1;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[0]["points"].array[0]["frames"]=18000;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[0]["points"].array.resize(1);bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[0]["easing"]="bounce";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[0]["points"].array[1]["eye"]=mods::json::parse("{\"x\":0,\"y\":-100,\"z\":-100}");bad_scene(v); // crossing eye/target despite valid endpoints
    v=extended;v["scenes"].array[0]["steps"].array[1]["half_life"]=11;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[1]["target_offset"]=v["scenes"].array[0]["steps"].array[1]["eye_offset"];bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[2]["loop"]=1;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[2]["blend_seconds"]=11;bad_scene(v);
    v=extended;v["models"].array[0]["kind"]="static";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[3]["speed"]=1e-300;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[3]["quest_place"]="unknown";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[3].object.erase("points");v["scenes"].array[0]["steps"].array[3]["quest_place"]="destination";bad_scene(v);v["quests"]=mods::json::parse("[{\"id\":\"destination\"}]");assert(parse_scenes(v,scenes,why)&&scenes[0].steps[3].quest_place=="destination");
    v=extended;v["scenes"].array[0]["steps"].array[4]["options"].array[0]["text"]=std::string(33,'x');bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[4]["options"].array[0]["text"]="two\nlines";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[4]["default"]=2;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[4]["options"].array[1]["next"]="left";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[4]["options"].array[0]["next"]="opening";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[4]["next"]="left";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[0]["next"]="question";bad_scene(v); // skipped unreachable steps
    v=extended;v["scenes"].array[0]["steps"].array[1]["id"]="opening";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[5]["next"]="missing";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[6]["next"]="right";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[5]["fade_frames"]=601;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[5]["action"]="native-bgm-id";bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[1]["eye_offset"]["extra"]=0;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[0]["next"]=Value{};bad_scene(v);
    v=moving;v["scenes"]=mods::json::parse(R"([{"id":"branch-duration","steps":[{"id":"choose","type":"choice","text":"Choose","frames":1,"options":[{"text":"A","next":"a"},{"text":"B","next":"b"}]},{"id":"a","type":"wait","frames":10000,"next":"a-end"},{"id":"a-end","type":"end"},{"id":"b","type":"wait","frames":10000,"next":"b-end"},{"id":"b-end","type":"end"}]}])");assert(parse_scenes(v,scenes,why));v["scenes"].array[0]["steps"].array[1]["frames"]=18000;bad_scene(v);
    v=extended;v["scenes"].array[0]["steps"].array[1]["actor"]="link";assert(parse_scenes(v,scenes,why));
    v=moving;v["scenes"]=mods::json::parse(R"([{"id":"mod-move","steps":[{"type":"move","actor":"post","position":{"x":100,"y":0,"z":0},"frames":60},{"type":"face","actor":"post","target_actor":"link"},{"type":"end"}]}])");assert(parse_scenes(v,scenes,why));
    v=moving;v["scenes"]=mods::json::parse(R"([{"id":"sparkle","steps":[{"type":"particle","effect":"auction-sparkle","actor":"post","scale":1.5,"alpha":180},{"type":"particle","effect":"auction-sparkle","position":{"x":1,"y":2,"z":3}},{"type":"end"}]}])");
    assert(parse_scenes(v,scenes,why)&&scenes[0].steps[0].kind==SceneStep::Kind::Particle&&scenes[0].steps[1].particle_has_position);
    auto beacon_particle=v;beacon_particle["scenes"].array[0]["steps"].array[0]["effect"]="beacon-sparkle";assert(parse_scenes(beacon_particle,scenes,why)&&scenes[0].steps[0].particle=="beacon-sparkle");
    auto invalid_particle=v;invalid_particle["scenes"].array[0]["steps"].array[0]["effect"]="door-debris";bad_scene(invalid_particle);
    invalid_particle=v;invalid_particle["scenes"].array[0]["steps"].array[0]["position"]=mods::json::parse(R"({"x":1,"y":2,"z":3})");bad_scene(invalid_particle);
    invalid_particle=v;invalid_particle["scenes"].array[0]["steps"].array[0]["scale"]=5;bad_scene(invalid_particle);
    invalid_particle=v;invalid_particle["scenes"].array[0]["steps"].array[1].object.erase("position");bad_scene(invalid_particle);
    { // Generic combat is additive to kind:mod and cannot select native enemy variants.
        const auto combat_fixture=[](){return mods::json::parse(R"({"items":[{"id":"token"}],"models":[{"id":"creature","source":"assets/creature.glb","kind":"skinned"}],"actors":[{"id":"enemy","kind":"mod","model":"creature","stage":"sea","room":11,"position":{"x":100,"y":1040,"z":-201500},"collision":{"radius":30,"height":100,"mode":"solid"},"combat":{"health":10,"invulnerability_frames":15,"flinch":{"clip":"flinch","frames":8,"knockback_speed":300,"knockback_frames":6},"stun":{"clip":"stun","frames":30},"death":{"clip":"death","frames":30},"attack":{"damage":1,"radius":40,"height":100,"forward_offset":30,"y_offset":0,"active_frames":6,"cooldown_frames":30}},"defeated_flag":"enemy.defeated","drops":[{"item":"item:token","quantity":1}],"native_drops":[{"item":"heart","quantity":1}]}]})");};
        const auto rejects=[&](const Value& data){std::vector<ManifestActor> rejected;assert(!parse_actors(data,rejected,why));};
        auto data=combat_fixture();assert(parse_actors(data,actors,why)&&actors.size()==1&&actors[0].kind=="mod");
        for(const char* field:{"defeated_flag","drops"}){auto bad=data;bad["actors"].array[0].object.erase(field);rejects(bad);}
        for(const char* field:{"flag","lines","then","talks","attention","params","parameters"}){auto bad=data;bad["actors"].array[0][field]=Value{};rejects(bad);}
        for(const char* kind:{"chuchu","bokoblin","resident","pot","switch","door"}){auto bad=data;auto& a=bad["actors"].array[0];a.object.erase("kind");a.object.erase("model");a["actor"]=kind;rejects(bad);}
        {auto bad=data;bad["models"].array[0]["kind"]="static";rejects(bad);}
        for(const char* reaction:{"stun","death"}){auto bad=data;bad["actors"].array[0]["combat"][reaction].object.erase("clip");rejects(bad);bad=data;bad["actors"].array[0]["combat"][reaction]["clip"]="";rejects(bad);}
        for(const char* field:{"rotation","scale"}){auto bad=data;bad["actors"].array[0][field]=mods::json::parse(field==std::string("rotation")?R"({"x":1,"y":0,"z":0})":R"({"x":1,"y":2,"z":1})");rejects(bad);}
        for(const char* item:{"rupee","game:heart","item:token"}){auto bad=data;bad["actors"].array[0]["native_drops"].array[0]["item"]=item;rejects(bad);}
        for(int quantity:{0,2}){auto bad=data;bad["actors"].array[0]["native_drops"].array[0]["quantity"]=quantity;rejects(bad);}
        {auto bad=data;auto& a=bad["actors"].array[0];a.object.erase("combat");rejects(bad);}
        {auto valid=data;valid["actors"].array[0]["drops"].array.clear();valid["actors"].array[0]["native_drops"].array.clear();assert(parse_actors(valid,actors,why));}
        {auto bad=data;auto duplicate=bad["actors"].array[0];duplicate["id"]="second";bad["actors"].array.push_back(duplicate);rejects(bad);}
        {auto bad=data;bad["actors"].array.push_back(mods::json::parse(R"({"id":"native","actor":"chuchu","stage":"sea","room":11,"position":{"x":0,"y":0,"z":0},"defeated_flag":"enemy.defeated","drops":[]})"));rejects(bad);}
        {auto bad=data;bad["actors"].array[0]["drops"].array[0]["item"]="item:foreign";rejects(bad);}
        {auto bad=data;bad["actors"].array[0]["combat"]["native_type"]=2048;rejects(bad);}
    }
    { // Carry opts into native Link procedures; raw flags and native actors are excluded.
        auto data=mods::json::parse(R"({"items":[{"id":"token"}],"models":[{"id":"box","source":"assets/box.glb"}],"actors":[{"id":"box","kind":"mod","model":"box","stage":"sea","room":11,"position":{"x":0,"y":0,"z":0},"collision":{"radius":25,"height":60,"mode":"solid"},"carry":{}}]})");
        const auto rejects=[&](const Value&d){assert(!parse_actors(d,actors,why));};assert(parse_actors(data,actors,why)&&actors[0].carry_enabled&&!actors[0].carry_break_on_impact);
        for(const char*f:{"lines","then","talks","attention","flag","combat"}){auto bad=data;bad["actors"].array[0][f]=Value{};rejects(bad);}
        {auto bad=data;bad["actors"].array[0]["carry"]["native_flags"]=16;rejects(bad);}
        {auto bad=data;bad["actors"].array[0]["carry"]["break_on_impact"]=1;rejects(bad);}
        {auto bad=data;bad["actors"].array[0].object.erase("collision");rejects(bad);}
        {auto bad=data;bad["actors"].array[0]["collision"]["radius"]=101;rejects(bad);}
        {auto bad=data;bad["actors"].array[0]["collision"]["height"]=201;rejects(bad);}
        {auto bad=data;bad["actors"].array[0]["rotation"]=mods::json::parse(R"({"x":1,"y":0,"z":0})");rejects(bad);}
        {auto bad=data;bad["actors"].array[0]["scale"]=mods::json::parse(R"({"x":2,"y":2,"z":2})");rejects(bad);}
        {auto bad=data;auto&a=bad["actors"].array[0];a.object.erase("kind");a.object.erase("model");a["actor"]="pot";rejects(bad);}
        data["actors"].array[0]["carry"]["break_on_impact"]=true;rejects(data);data["actors"].array[0]["broken_flag"]="box.broken";rejects(data);data["actors"].array[0]["drops"]=mods::json::parse(R"([{"item":"item:token","quantity":1}])");assert(parse_actors(data,actors,why)&&actors[0].carry_break_on_impact&&actors[0].drops.size()==1);
        assert(world_manifest::known_flag(data,"box.broken"));
        {auto bad=data;bad["actors"].array[0]["drops"].array[0]["item"]="item:foreign";rejects(bad);}
        {auto bad=data;auto second=bad["actors"].array[0];second["id"]="other";bad["actors"].array.push_back(second);rejects(bad);}
    }
    std::cout<<"story world manifest tests passed\n";
}
