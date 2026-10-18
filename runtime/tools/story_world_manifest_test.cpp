#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
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
        for(const char* type:{"chuchu","bokoblin","moblin","keese"}){
            auto data=fixture();auto& value=data["actors"].array[0];value["actor"]=type;ManifestActor candidate;candidate.actor=type;
            assert(world_manifest::native_enemy_fields(data,value,candidate,why)&&candidate.defeated_flag=="enemy.defeated"&&candidate.drops.size()==1&&candidate.drops[0].quantity==2);
            assert(parse_actors(data,actors,why)&&actors.size()==1&&actors[0].actor==type&&actors[0].defeated_flag=="enemy.defeated"&&actors[0].drops[0].item=="shard");
            for(const char* field:{"params","parameters","flag","broken_flag","switch_flag","switch","lines","then","model","animation","look_at","path","collision","attention","kind"}){auto bad=value;bad[field]=Value{};assert(!world_manifest::native_enemy_fields(data,bad,candidate,why));}
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
    v["actors"].array[1]["parameters"]["item"]="heart_piece";assert(!parse_actors(v,actors,why)&&actors.empty());
    v=example();v["actors"].array[0]["parameters"]["resident"]=10;assert(!parse_actors(v,actors,why)&&actors.empty());
    for(const auto& name:{"uo2","um1","ym1"}){v=example();v["actors"].array[0]["parameters"]["resident"]=name;assert(parse_actors(v,actors,why));assert(actors[0].resident==(std::string(name)=="uo2"?1u:std::string(name)=="ym1"?19u:9u));}
    for(const auto& name:{"Uo2","uo1","ug1","1",""}){v=example();v["actors"].array[0]["parameters"]["resident"]=name;assert(!parse_actors(v,actors,why));}
    v=example();v["actors"].array[0]["parameters"]["resident"]=19;assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["parameters"]["resident"]=1;assert(!parse_actors(v,actors,why));
    v=example();v["actors"].array[0]["parameters"]["resident"]=9;assert(parse_actors(v,actors,why)&&actors[0].resident==9);
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
            auto simple=island;auto&c=simple["actors"].array[0]["collision"];c=mods::json::parse(std::string(shape)=="box"?R"({"shape":"box","size":{"x":20000,"y":20,"z":40}})":std::string(shape)=="cylinder"?R"({"shape":"cylinder","radius":10000,"height":20000})":R"({"shape":"sphere","radius":10000})");c["surface"]=surface;
            assert(parse_actors(simple,actors,why)&&actors[0].collision_shape==shape&&actors[0].collision_surface==surface&&actors[0].collision_mesh.empty());
            {auto bad=simple;bad["actors"].array[0]["collision"]["surface"]="col_grass";rejected(bad);}
            {auto bad=simple;bad["actors"].array[0]["collision"]["mesh"]="post";rejected(bad);}
            for(int dimension:{0,-1,20001}){auto bad=simple;auto&bc=bad["actors"].array[0]["collision"];if(std::string(shape)=="box")bc["size"]["x"]=dimension;else bc["radius"]=dimension;rejected(bad);}
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
        for(const char*definition:{R"([{"id":"beam","position":{"x":-20000,"y":20000,"z":20000},"yaw":-360}])",R"([{"id":"beam","position":{"x":20000,"y":-20000,"z":-20000},"yaw":360}])",R"([{"id":"beam","position":{"x":0,"y":1,"z":0}}])"}){auto data=tower;data["actors"].array[0]["grapple_points"]=mods::json::parse(definition);assert(parse_actors(data,actors,why));}
        for(const char*definition:{"null","{}",R"([{"id":"beam","position":{"x":0,"y":1,"z":0}},{"id":"beam","position":{"x":1,"y":2,"z":3}}])",R"([{"id":"foreign:beam","position":{"x":0,"y":1,"z":0}}])",R"([{"id":"beam","position":{"x":20001,"y":0,"z":0}}])",R"([{"id":"beam","position":{"x":0,"y":-20001,"z":0}}])",R"([{"id":"beam","position":{"x":0,"y":0,"z":20001}}])",R"([{"id":"beam","position":{"x":0,"z":0}}])",R"([{"id":"beam","position":{"x":0,"y":0,"z":0},"yaw":361}])",R"([{"id":"beam","position":{"x":0,"y":0,"z":0},"yaw":-361}])",R"([{"id":"beam","position":{"x":0,"y":0,"z":0},"yaw":"90"}])",R"([{"id":"beam","position":{"x":0,"y":0,"z":0},"native_params":1}])"}){auto data=tower;data["actors"].array[0]["grapple_points"]=mods::json::parse(definition);rejected(data);}
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
        for(const char*behaviour:{"floating","drifting"}){auto bad=island;bad["actors"].array[0]["behaviour"]=behaviour;if(std::string(behaviour)=="drifting")bad["actors"].array[0]["drift"]=mods::json::parse(R"({"wind_speed":1})");rejected(bad);}
        {auto bad=island;bad["actors"].array[0]["stage"]="town";rejected(bad);}
        {auto bad=island;auto&a=bad["actors"].array[0];a.object.erase("behaviour");a["collision"]=mods::json::parse(R"({"radius":30,"height":100,"mode":"solid"})");rejected(bad);}
        {auto bad=example();bad["actors"].array[0]["distant"]=mods::json::parse(R"({"model":"low"})");rejected(bad);}
        for(const char*key:{"label","name"}){
            const char*block=std::string(key)=="label"?"chart_marker":"arrival";
            for(const auto&text:{std::string(),std::string(97,'x'),std::string("two\nlines"),std::string(1,char(0xFF))}){auto bad=island;bad["actors"].array[0][block][key]=text;rejected(bad);}
            auto data=island;data["actors"].array[0][block][key]=std::string(96,'x');assert(parse_actors(data,actors,why));data["actors"].array[0][block][key]="Île des étoiles";assert(parse_actors(data,actors,why));
        }
        for(int radius:{100,20000}){auto data=island;data["actors"].array[0]["arrival"]["radius"]=radius;assert(parse_actors(data,actors,why));}
        for(int radius:{0,99,20001}){auto data=island;data["actors"].array[0]["arrival"]["radius"]=radius;rejected(data);}
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

    std::vector<ManifestModel> models;assert(parse_models(valid_mod,models,why)&&models[0].kind=="static");
    mod=valid_mod;mod["models"].array[0]["kind"]="skinned";assert(parse_models(mod,models,why)&&models[0].kind=="skinned");
    mod["models"].array[0]["kind"]="static";assert(parse_models(mod,models,why)&&models[0].kind=="static");
    for(const auto& kind:{"animated","", "native"}){mod["models"].array[0]["kind"]=kind;assert(!parse_models(mod,models,why));}
    mod["models"].array[0]["kind"]=mods::json::Value{};assert(!parse_models(mod,models,why));
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
