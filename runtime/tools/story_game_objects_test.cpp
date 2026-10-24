// Game objects (story_game_objects_catalogue.h, story_world_manifest.h): the port's recipes, the recipe
// rules, and what a manifest may say about a game object (catalogued ids only; no params, flags, then,
// paths or dialogue; yaw-only turns and uniform scale within the entry's limits; the catalogue version
// gate; the per-room count and memory budget, also across packages; the variant in the fingerprint).
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "mods/story_actor_identity.h"
#include "mods/story_world_manifest.h"

using namespace story_notebook;
using mods::json::Value;

static Value manifest(const std::string& actors, const std::string& requires_ = R"({"game_objects": 1})") {
    return mods::json::parse(R"({"requires": )" + requires_ + R"(, "actors": [)" + actors + "]}");
}
static std::string palm(const std::string& id, const std::string& extra = "", int room = 11) {
    return R"({"id": ")" + id + R"(", "actor": "game:palm", "stage": "sea", "room": )" + std::to_string(room) +
           R"(, "position": {"x": 1, "y": 2, "z": 3})" + extra + "}";
}
static bool parses(const Value& m, std::string& why) {
    std::vector<ManifestActor> out;
    return parse_actors(m, out, why);
}

int main() {
    std::string why;
    // ---- the port's recipes
    const auto& c = story_game_objects::catalogue();
    assert(c.version == 6);  // the volcanic kit and the blocks came with version 6
    for (const auto& e : c.entries) assert(e.since == 1 || ((e.group == "volcanic" || e.group == "blocks" || e.id == "stall") && e.since == 6));
    {
        // the volcanic kit: always lit, no switch (0xFF fields), each the game's own stage actor name
        const auto* torch = c.find("torch");
        assert(torch && torch->native_name == "bonbori" && torch->archive == "Ep" && torch->variant("")->params == 0xFFFFFF03 && torch->variant("torch-stand")->params == 0xFFFFFF83 && torch->own_heap);
        const auto* flame = c.find("flame");
        assert(flame && flame->native_name == "bonbori" && flame->variant("")->params == 0xFFFFFF01 && !flame->own_heap && !flame->collision);
        const auto* jet = c.find("fire-jet");
        assert(jet && jet->native_name == "Yfire00" && jet->archive == "Yfire_00" && ((jet->variant("")->params >> 16) & 0xFF) == 0xFF && (jet->variant("")->params & 0xFF) == 0);
        assert(!c.find("steam-vent"));  // the game's steam vents use their own stage's particles: nothing shows elsewhere
        const auto* bubbles = c.find("lava-bubbles");
        assert(bubbles && bubbles->native_name == "Yboil00" && ((bubbles->variant("")->params >> 8) & 0xFF) == 0xFF);
        const auto* flower = c.find("bomb-flower");
        assert(flower && flower->native_name == "BFlower" && flower->archive == "VbakH" && ((flower->variant("")->params >> 8) & 0xFF) == 0xFF &&
               ((flower->variant("")->params >> 4) & 0xF) == 0 && flower->variant("")->params == 0x0000FF0F);
        // an archive name of eight characters (the game's "Yfire_00") is accepted, a longer name than 15 is not
        // a manifest names them with catalogue version 6
        const std::string flower_actor = R"({"id": "f", "actor": "game:bomb-flower", "stage": "sea", "room": 44, "position": {"x": 1, "y": 2, "z": 3}})";
        assert(parses(manifest(flower_actor, R"({"game_objects": 6})"), why));
        assert(!parses(manifest(flower_actor, R"({"game_objects": 5})"), why) && why.find("6") != std::string::npos);
        assert(story_game_objects::archive_name_ok("Yfire_00") && !story_game_objects::archive_name_ok("ABCDEFGHIJKLMNOP") && !story_game_objects::native_name_ok("Yfire_00"));
    }
    for (const char* id : {"palm", "tree-large", "table"}) assert(c.find(id));
    const auto* table = c.find("table");
    assert(table->native_name == "Table" && table->archive == "Table" && table->subtype == 255 && table->variants.size() == 3);
    assert(table->variant("")->params == 0xFFFFFF00 && table->variant("stool")->params == 0xFFFFFF01 && table->variant("stool-open")->params == 0xFFFFFF02);
    assert(!table->variant("chair") && table->scale_min == 1 && table->scale_max == 1 && table->yaw_only);
    const auto* tree = c.find("tree-large");
    assert(tree->scale_min == 0.5f && tree->scale_max == 1.5f && tree->variant("")->params == 0xFFFFFFFF);
    for (const auto& e : c.entries) assert(e.instance_bytes > 0 && !e.label.empty() && !e.category.empty());

    // ---- recipe rules: all or nothing per group
    const std::string good = R"({"game_objects_format": 1, "group": "test", "objects": [{"id": "rock", "since": 2, "category": "Rocks", "label": "Rock",
        "description": "a rock", "native": {"name": "Rock", "process": "Obj_Rock", "subtype": 0, "archive": "Rock"},
        "variants": [{"id": "rock", "label": "Rock", "params": "0x00000000"}], "transform": {"rotation": "yaw", "scale": [0.5, 2]},
        "collision": true, "memory": {"instance_bytes": 1000000, "archive_bytes": 600000}}]})";
    {
        story_game_objects::Catalogue t;
        assert(t.add_table(good, why) && t.version == 2 && t.find("rock") && t.find("rock")->scale_max == 2);
        assert(!t.add_table(good, why) && why.find("duplicate") != std::string::npos && t.entries.size() == 1);  // the same id twice
        auto bad = [&](const std::string& from, const std::string& to) {
            std::string text = good;
            const auto at = text.find(from);
            assert(at != std::string::npos);
            text.replace(at, from.size(), to);
            story_game_objects::Catalogue u;
            const bool ok = u.add_table(text, why);
            assert(!ok && u.entries.empty());
        };
        bad(R"("game_objects_format": 1)", R"("game_objects_format": 2)");
        bad(R"("id": "rock")", R"("id": "Rock!")");
        bad(R"("params": "0x00000000")", R"("params": "0")");
        bad(R"("params": "0x00000000")", R"("params": "0x0000000G")");
        bad(R"("scale": [0.5, 2])", R"("scale": [0.4, 2])");
        bad(R"("scale": [0.5, 2])", R"("scale": [1.2, 2])");
        bad(R"("rotation": "yaw")", R"("rotation": "any")");
        bad(R"("name": "Rock")", R"("name": "RockTooLong")");
        bad(R"("archive": "Rock")", R"("archive": "../Rock")");
        bad(R"("subtype": 0)", R"("subtype": 256)");
        bad(R"("collision": true)", R"("collision": 1)");
        bad(R"("instance_bytes": 1000000)", R"("instance_bytes": 0)");
        bad(R"("since": 2)", R"("since": 0)");
        assert(!t.add_table("not json", why));
    }

    // ---- manifests
    assert(parses(manifest(palm("p")), why));
    {
        std::vector<ManifestActor> out;
        assert(parse_actors(manifest(palm("p") + R"(, {"id": "t", "actor": "game:table", "variant": "stool", "stage": "sea", "room": 11, "position": {"x": 0, "y": 0, "z": 0}, "rotation": {"x": 0, "y": 90, "z": 0}})"), out, why));
        assert(out.size() == 2 && out[0].game_object() && out[0].variant == "palm" && out[1].variant == "stool" && out[1].rotation.y == 90);
        // the variant is part of the identity a state load checks
        auto other = out[1];
        other.variant = "table";
        assert(story_actors::identity::fingerprint("m", out[1]) != story_actors::identity::fingerprint("m", other));
    }
    // the catalogue version gate
    assert(!parses(manifest(palm("p"), "null"), why) && why.find("requires") != std::string::npos);
    // (game pieces share the version: story_game_pieces_catalogue.h)
    assert(!parses(manifest(palm("p"), R"({"game_objects": 99})"), why) && why.find("update the port") != std::string::npos);
    assert(!parses(manifest(palm("p"), R"({"game_objects": "1"})"), why));
    assert(!parses(manifest(palm("p"), R"({"game_objects": 1, "other": 1})"), why) && why.find("unknown requirement") != std::string::npos);
    // only catalogued ids, and nothing but a placement
    assert(!parses(manifest(R"({"id": "x", "actor": "game:castle", "stage": "sea", "room": 1, "position": {"x": 0, "y": 0, "z": 0}})"), why) &&
           why.find("not in the game objects catalogue") != std::string::npos);
    for (const char* field : {R"(, "params": 0)", R"(, "parameters": {})", R"(, "flag": "f")", R"(, "then": [])", R"(, "lines": ["hi"])",
                              R"(, "talks": [])", R"(, "path": {})", R"(, "when": {})", R"(, "kind": "mod")", R"(, "model": "m")", R"(, "drops": [])",
                              R"(, "switch_flag": "s")", R"(, "collision": {})"}) {
        assert(!parses(manifest(palm("p", field)), why));
        assert(why.find("a game object takes only") != std::string::npos || why.find("cannot") != std::string::npos);
    }
    // turns about the vertical axis only; uniform scale within the entry's limits
    assert(parses(manifest(palm("p", R"(, "rotation": {"x": 0, "y": 270, "z": 0})")), why));
    assert(!parses(manifest(palm("p", R"(, "rotation": {"x": 10, "y": 0, "z": 0})")), why) && why.find("vertical axis") != std::string::npos);
    assert(!parses(manifest(palm("p", R"(, "scale": {"x": 1.5, "y": 1.5, "z": 1.5})")), why) && why.find("scale 1 only") != std::string::npos);
    const std::string big = R"({"id": "t", "actor": "game:tree-large", "stage": "sea", "room": 11, "position": {"x": 0, "y": 0, "z": 0}, "scale": )";
    assert(parses(manifest(big + R"({"x": 1.5, "y": 1.5, "z": 1.5}})"), why));
    assert(parses(manifest(big + R"({"x": 0.5, "y": 0.5, "z": 0.5}})"), why));
    assert(!parses(manifest(big + R"({"x": 1.6, "y": 1.6, "z": 1.6}})"), why));
    assert(!parses(manifest(big + R"({"x": 1, "y": 1.2, "z": 1}})"), why));
    // variants: known ones only, and only on game objects
    assert(!parses(manifest(palm("p", R"(, "variant": "coconut")")), why) && why.find("has the variants palm") != std::string::npos);
    assert(!parses(mods::json::parse(R"({"actors": [{"id": "s", "actor": "sign", "flag": "read", "variant": "x", "stage": "sea", "room": 1, "position": {"x": 0, "y": 0, "z": 0}}]})"), why) &&
           why.find("variant is for game objects") != std::string::npos);
    // the per-room count (shared with the other actors)
    {
        std::string list;
        for (int i = 0; i < 17; i++) list += (i ? "," : "") + palm("p" + std::to_string(i));
        assert(!parses(manifest(list), why) && why.find("16 per room") != std::string::npos);
        list.clear();
        for (int i = 0; i < 16; i++) list += (i ? "," : "") + palm("p" + std::to_string(i));
        assert(parses(manifest(list), why));
    }
    // the room budget: every instance, each archive once (a test catalogue with heavy objects)
    {
        story_game_objects::Catalogue t;
        assert(t.add_table(good, why));
        std::vector<ManifestActor> list(2);
        for (auto& a : list) a.actor = "game:rock", a.stage = "sea", a.room = 3;
        assert(world_manifest::game_object_budget(list, why, t));  // 2 x 1000000 + 600000 once
        list.push_back(list[0]);
        assert(!world_manifest::game_object_budget(list, why, t) && why.find("memory budget") != std::string::npos);
        list.back().room = 4;  // another room has its own budget
        assert(world_manifest::game_object_budget(list, why, t));
        story_game_objects::RoomUse use;
        use.add(*t.find("rock"));
        use.add(*t.find("rock"));
        assert(use.bytes == 2600000 && use.count == 2);
    }
    // across enabled packages: the per-room count adds up
    {
        std::string a, b;
        for (int i = 0; i < 8; i++) a += (i ? "," : "") + palm("a" + std::to_string(i));
        for (int i = 0; i < 9; i++) b += (i ? "," : "") + palm("b" + std::to_string(i));
        std::vector<std::pair<std::string, Value>> packages = {{"one", manifest(a)}, {"two", manifest(b)}};
        assert(!world_manifest::validate_world_packages(packages, why) && why.find("16 actors") != std::string::npos);
        packages.pop_back();
        assert(world_manifest::validate_world_packages(packages, why));
    }
    // shared batches (grass, flowers, small trees): units per variant, scale 1, mods' share per stage
    {
        const auto* grass = c.find("grass");
        assert(grass && grass->batch() && grass->packet.kind == "grass" && grass->packet.capacity == 1500 && grass->packet.mod_share == 300);
        assert(grass->variant("grass-21")->units == 21 && grass->variant("")->params == 0 && (grass->variant("grass-7")->params & 0xFC0) == 0);  // item field 0: no drops
        for (const auto& e : c.entries)
            for (const auto& v : e.variants)
                if (e.batch()) assert(v.units > 0 && ((v.params >> 6) & 0x3F) < 0x20);  // no batch variant has a drop
        assert(!parses(manifest(R"({"id": "g", "actor": "game:grass", "variant": "grass-7", "stage": "sea", "room": 44, "position": {"x": 0, "y": 0, "z": 0}, "scale": {"x": 2, "y": 2, "z": 2}})"), why));
        std::string list;  // 15 patches of 21 over several rooms of one stage: 315 > 300
        for (int i = 0; i < 15; i++)
            list += std::string(i ? "," : "") + R"({"id": "g)" + std::to_string(i) + R"(", "actor": "game:grass", "variant": "grass-21", "stage": "sea", "room": )" + std::to_string(40 + i / 8) + R"(, "position": {"x": 0, "y": 0, "z": 0}})";
        assert(!parses(manifest(list), why) && why.find("mods may plant 300") != std::string::npos);
        list = list.substr(0, list.rfind(",{"));
        assert(parses(manifest(list), why));
        story_game_objects::Catalogue t;
        std::string bad = good;
        bad.replace(bad.find(R"("params": "0x00000000"})"), std::strlen(R"("params": "0x00000000"})"), R"("params": "0x00000000", "units": 3})");
        assert(!t.add_table(bad, why) && why.find("units") != std::string::npos);  // units only on a batch
    }
    // the stone block: the game's own block word and its home angle z "no path" (0xFFFF), catalogue version 6 (with the volcanic kit)
    {
        const auto* block = c.find("push-block");
        assert(block && block->native_name == "osiBLK0" && block->archive == "Osiblk" && block->since == 6 && block->collision && !block->batch());
        assert(block->variant("")->params == 0x8100FF00u && block->variant("stone")->angle_z == 0xFFFF && ((block->variant("")->params >> 8) & 0xFF) == 0xFF);  // no switch
        assert(c.version >= 6 && c.find("palm")->variant("")->angle_z == -1);
        assert(!parses(manifest(R"({"id": "b", "actor": "game:push-block", "stage": "sea", "room": 44, "position": {"x": 0, "y": 0, "z": 0}})", R"({"game_objects": 5})"), why));  // needs version 6
        assert(parses(manifest(R"({"id": "b", "actor": "game:push-block", "stage": "sea", "room": 44, "position": {"x": 0, "y": 0, "z": 0}})", R"({"game_objects": 6})"), why));
        // the block with a statue (type 2, the game's osiBLK1 word) and the heavy block (MpwrB, type 3)
        assert(block->variant("statue") && block->variant("statue")->params == 0x8200FF00u && block->variant("statue")->angle_z == 0xFFFF);
        const auto* heavy = c.find("heavy-block");
        assert(heavy && heavy->native_name == "MpwrB" && heavy->subtype == 10 && heavy->variant("")->params == 0x8300FF00u && heavy->variant("")->angle_z == 0xFFFF);
        // rocks: the small rock (STONE, type 0, no item 0x3F, no switch 0xFF) and the boulders and the stone head (Stone2,
        // their home angle z 0x00FF: no break event)
        const auto* rock = c.find("rock");
        assert(rock && rock->native_name == "koisi1" && rock->archive == "Always" && rock->variant("")->params == 0x707FFFFFu && ((rock->variant("")->params >> 24) & 7) == 0);
        for (const char* id : {"boulder", "boulder-small", "stone-head"}) {
            const auto* b = c.find(id);
            assert(b && b->process == "Stone2" && b->variant("")->angle_z == 0x00FF && ((b->variant("")->params >> 8) & 0xFF) == 0xFF && (b->variant("")->params & 0x3F) == 0x3F);
        }
        // the trader's stall (Obj_Roten, Zunari's on Windfall): three sizes by the parameter word's top byte
        const auto* stall = c.find("stall");
        assert(stall && stall->native_name == "Roten2" && stall->archive == "Roten" && stall->variant("stall-small")->params == 0 && stall->variant("stall")->params == 0x01000000u &&
               stall->variant("stall-large")->params == 0x02000000u && stall->collision);
        assert(c.find("boulder")->native_name == "Ebrock" && c.find("boulder-small")->native_name == "Ebrock2" && c.find("stone-head")->native_name == "Ekao");
        story_game_objects::Catalogue t;
        for (const char* bad_z : {"0xFFFFF", "65535", "0xZZZZ"}) {
            std::string bad = good;
            bad.replace(bad.find(R"("params": "0x00000000"})"), std::strlen(R"("params": "0x00000000"})"), std::string(R"("params": "0x00000000", "angle_z": ")") + bad_z + "\"}");
            assert(!t.add_table(bad, why) && why.find("angle_z") != std::string::npos);
        }
    }
    // a manifest without game objects never builds anything new: "requires" stays optional
    assert(parses(mods::json::parse(R"({"actors": [{"id": "s", "actor": "sign", "flag": "read", "stage": "sea", "room": 1, "position": {"x": 0, "y": 0, "z": 0}}]})"), why));
    std::cout << "story_game_objects_test: ok\n";
    return 0;
}
