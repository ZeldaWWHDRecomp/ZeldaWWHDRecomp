// SPDX-License-Identifier: MPL-2.0
// Game pieces (story_game_pieces_catalogue.h, story_world_manifest.h, mod_actor_budget.h): the port's
// recipes, the recipe rules, what a manifest may say about a piece (catalogued ids only; id, stage,
// room, position and a turn only; the catalogue version gate), what a piece becomes (a static world
// object with the piece's model and collision), and the room budget for buildings (story_game_pieces_budget.h), in manifests,
// across packages and at run time.
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <bit>
#include <cstring>
#include <vector>

#include "mods/mod_actor_budget.h"
#include "mods/story_world_placement.h"
#include "mods/story_world_manifest.h"

using namespace story_notebook;
using mods::json::Value;

static Value manifest(const std::string& actors, const std::string& requires_ = R"({"game_objects": 2})") {
    return mods::json::parse(R"({"requires": )" + requires_ + R"(, "actors": [)" + actors + "]}");
}
static std::string house(const std::string& id, const std::string& piece = "outset-house-small", const std::string& extra = "", int room = 12) {
    return R"({"id": ")" + id + R"(", "actor": "piece:)" + piece + R"(", "stage": "sea", "room": )" + std::to_string(room) +
           R"(, "position": {"x": 100, "y": 20, "z": -300})" + extra + "}";
}
static bool parses(const Value& m, std::string& why, std::vector<ManifestActor>* got = nullptr) {
    std::vector<ManifestActor> out;
    const bool ok = parse_actors(m, out, why);
    if (got) *got = out;
    return ok;
}

int main() {
    std::string why;
    // ---- the port's recipes
    const auto& c = story_game_pieces::catalogue();
    assert(c.version >= 2);
    const char* outset[] = {"outset-house-ladder", "outset-house-lookout", "outset-house-terrace", "outset-house-small", "outset-lookout-tower", "outset-well-shelter"};
    for (const char* id : outset) {
        const auto* p = c.find(id);
        assert(p && p->set == "outset" && p->category == "Buildings" && !p->label.empty() && !p->description.empty());
        assert(!p->parts.empty() && p->parts[0].model == 0 && p->parts[0].light == 1 && p->parts[0].nodes == std::vector<std::string>{"ie"});
        assert(p->instance_bytes > 0 && p->collision_triangles > 0 && !p->collision_groups.empty());
    }
    for (const char* id : {"outset-house-ladder", "outset-house-lookout", "outset-house-terrace", "outset-house-small"}) {
        const auto* p = c.find(id);
        assert(p->parts.size() == 2 && p->parts[1].model == 3 && p->parts[1].light == 4);  // the window glow: BG3
    }
    const auto* set = c.set("outset");
    assert(set && set->archive == "sea_Room44.szs" && set->room == 44 && set->pieces.size() == 6);
    assert(set->archive_bytes > 1000000 && set->archive_bytes < story_game_pieces::kRoomBudgetBytes);
    for (const auto& p : c.pieces) assert(story_game_pieces::neutral_id(p.id) && c.set(p.set));
    // the island parts: Dragon Roost's shrine, torii, walkways, rope posts and sea rocks, Windfall's windmill cap and
    // the Forsaken Fortress shipwrecks, each with collision of its own, BG0 light, since catalogue version 3
    {
        const std::pair<const char*, const char*> parts[] = {
            {"dragon-roost-torii", "dragon-roost-kit"}, {"dragon-roost-shrine", "dragon-roost-kit"}, {"dragon-roost-walkway", "dragon-roost-kit"},
            {"dragon-roost-walkway-turn", "dragon-roost-kit"}, {"dragon-roost-rope-posts", "dragon-roost-kit"}, {"sea-rock-spire", "dragon-roost-kit"},
            {"sea-rock-pillar", "dragon-roost-kit"}, {"sea-rock-stack", "dragon-roost-kit"}, {"windfall-windmill-cap", "windfall-kit"},
            {"shipwreck-hull", "fortress-kit"}, {"shipwreck-bow", "fortress-kit"}};
        uint64_t all_sets = 0;
        for (const auto& [id, set_id] : parts) {
            const auto* p = c.find(id);
            assert(p && p->set == set_id && p->category == "Island parts" && p->since == 3 && p->parts.size() == 1 && p->parts[0].light == 1);
            assert(p->collision_triangles > 0 && !p->collision_groups.empty() && p->instance_bytes > 0);
        }
        assert(c.set("dragon-roost-kit")->room == 13 && c.set("windfall-kit")->room == 11 && c.set("fortress-kit")->room == 1);
        for (const char* set_id : {"dragon-roost-kit", "windfall-kit", "fortress-kit"}) all_sets += c.set(set_id)->archive_bytes;
        // every island part of the three sets fits one room together
        assert(all_sets < story_game_pieces::kRoomBudgetBytes);
        assert(c.version >= 3);
        // more of Dragon Roost's sea rocks and Fire Mountain's two volcanic rocks (their own set), since version 6
        for (const char* id : {"sea-rock-broad", "sea-rock-low", "sea-rock-small", "sea-rock-mid", "fire-mountain-rock", "fire-mountain-rock-small"}) {
            const auto* p = c.find(id);
            assert(p && p->category == "Island parts" && p->since == 6 && p->parts.size() == 1 && p->parts[0].light == 1 && p->collision_triangles > 0);
        }
        assert(c.find("fire-mountain-rock")->set == "fire-mountain-kit" && c.set("fire-mountain-kit")->room == 20 && c.find("sea-rock-low")->set == "dragon-roost-kit");
        assert(c.version >= 6);
    }
    assert(story_game_pieces::reference("piece:outset-house-small") == "outset-house-small" && story_game_pieces::reference("game:palm").empty());
    assert(story_game_pieces::model_key("x") == "piece:x");
    assert(story_game_pieces::light_type("bg0") == 1 && story_game_pieces::light_type("bg3") == 4 && !story_game_pieces::light_type("bg4") &&
           !story_game_pieces::light_type("actor"));

    // ---- island grounds: sets in the Islands category, each with the room's own distant view
    {
        const char* isles[][2] = {{"crescent-moon-island", "5"}, {"triangle-island", "18"}, {"five-star-isles", "49"},
                                  {"star-island", "2"},           {"bomb-island", "34"},    {"fairy-island", "3"},
                                  {"shark-island", "38"},         {"horseshoe-island", "43"}, {"rock-spire-isle", "16"},
                                  {"thorned-fairy-island", "28"}, {"seven-star-isles", "6"}, {"overlook-island", "7"},
                                  {"stone-watcher-island", "31"}, {"cliff-plateau-isles", "42"}, {"angular-isles", "47"},
                                  {"boating-course", "48"},       {"gale-isle", "4"},       {"headstone-island", "45"},
                                  {"greatfish-isle", "23"},       {"cyclops-reef", "24"},   {"two-eye-reef", "46"},
                                  {"diamond-steppe-island", "36"}, {"mother-and-child-isles", "9"}, {"fire-mountain", "20"}};
        unsigned islands = 0;
        for (const auto& p : c.pieces) islands += p.category == "Islands";
        assert(islands == sizeof isles / sizeof isles[0]);
        assert(!c.find("birds-peak-rock"));  // its textures are over the 12 MiB archive cap
        for (const auto& [id, room] : isles) {
            const auto* p = c.find(id);
            assert(p && p->category == "Islands" && p->since >= 4 && p->set == id && !p->parts.empty() && p->parts[0].light == 1);
            // the islands with water: their water model (model1) as an animated second part with the island's second light
            const bool water = std::string(id) == "diamond-steppe-island" || std::string(id) == "mother-and-child-isles";
            assert(p->parts.size() == (water ? 2u : 1u) && !p->parts[0].animated);
            if (water) assert(p->parts[1].model == 1 && p->parts[1].light == 2 && p->parts[1].animated);
            char lod[16];
            std::snprintf(lod, sizeof lod, "sea_LOD%02d.szs", std::stoi(room));
            assert(p->distant_triangles > 0 && p->distant_triangles <= 2048 && p->distant_archive == lod && c.set(id)->room == unsigned(std::stoi(room)));
            assert(p->collision_triangles > 0 && p->collision_triangles <= 4096 && p->instance_bytes > 0);
            for (unsigned k = 0; k < 3; ++k) assert(p->volume[k + 3] - p->volume[k] <= 20000);
        }
        assert(story_game_pieces::distant_key("star-island") == "piece:star-island.distant");
        for (const auto& p : c.pieces) assert((p.category == "Islands") == (p.distant_triangles > 0));
        // a recipe's distant view must be the room's own archive with a sane count
        const std::string isle = R"({"game_pieces_format": 1, "set": "isle", "label": "Isle", "source": {"archive": "sea_Room5.szs", "room": 5},
            "memory": {"archive_bytes": 1000000}, "pieces": [{"id": "isle", "since": 4, "category": "Islands", "label": "Isle", "description": "an isle",
              "volume": [-100, -100, -100, 100, 200, 100], "origin_y": 0, "parts": [{"model": 0, "nodes": ["a"], "light": "bg0"}],
              "collision": {"groups": ["a", "h"], "void": ["h"], "strip_exits": true}, "distant": {"archive": "sea_LOD05.szs", "triangles": 120},
              "expect": {"triangles": [12], "collision_triangles": 4}, "memory": {"instance_bytes": 40000}}]})";
        story_game_pieces::Catalogue t;
        assert(t.add_set(isle, why) && t.find("isle")->distant_triangles == 120);
        for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{{"sea_LOD05.szs", "sea_LOD06.szs"}, {"sea_LOD05.szs", "../LOD05.szs"},
                                                                                       {R"("triangles": 120)", R"("triangles": 2049)"}, {R"("triangles": 120)", R"("triangles": 0)"}}) {
            std::string text = isle;
            text.replace(text.find(from), from.size(), to);
            story_game_pieces::Catalogue u;
            assert(!u.add_set(text, why) && u.pieces.empty() && why.find("distant") != std::string::npos);
        }
        // only a second part may keep its texture animation (an island's water), and only as true or false
        {
            std::string text = isle;
            const std::string from = R"("parts": [{"model": 0, "nodes": ["a"], "light": "bg0"}])";
            text.replace(text.find(from), from.size(), R"("parts": [{"model": 0, "nodes": ["a"], "light": "bg0"}, {"model": 1, "nodes": ["w"], "light": "bg1", "animated": true}])");
            const std::string tris = R"("triangles": [12])";
            text.replace(text.find(tris), tris.size(), R"("triangles": [12, 6])");
            story_game_pieces::Catalogue u;
            assert(u.add_set(text, why) && u.find("isle")->parts[1].animated && !u.find("isle")->parts[0].animated);
            for (const auto& [a, b] : std::vector<std::pair<std::string, std::string>>{
                     {R"("light": "bg0"})", R"("light": "bg0", "animated": true})"}, {R"("animated": true)", R"("animated": 1)"}}) {
                std::string bad = text;
                bad.replace(bad.find(a), a.size(), b);
                story_game_pieces::Catalogue v;
                assert(!v.add_set(bad, why) && why.find("animated") != std::string::npos);
            }
        }
        // a placed island ground on the sea gets its distant view; elsewhere it has none
        std::vector<ManifestActor> got;
        assert(parses(manifest(house("i", "star-island"), R"({"game_objects": 4})"), why, &got));
        assert(got[0].distant.model == "piece:star-island.distant" && got[0].model == "piece:star-island" && got[0].shoreline.model.empty());
        assert(visual_variant_count(got[0]) == 1);
        assert(!parses(manifest(house("i", "star-island"), R"({"game_objects": 3})"), why) && why.find("version 4") != std::string::npos);
        assert(parses(manifest(house("o", "outset-house-small"), R"({"game_objects": 4})"), why, &got) && got[0].distant.model.empty());
        // the islands added later need catalogue version 5
        assert(c.find("gale-isle")->since == 5 && c.version >= 5);
        assert(!parses(manifest(house("g", "gale-isle"), R"({"game_objects": 4})"), why) && why.find("version 5") != std::string::npos);
        assert(parses(manifest(house("g", "gale-isle"), R"({"game_objects": 5})"), why, &got) && got[0].distant.model == "piece:gale-isle.distant");
    }

    // ---- recipe rules: all or nothing per set
    const std::string good = R"({"game_pieces_format": 1, "set": "test", "label": "Test", "source": {"archive": "sea_Room12.szs", "room": 12},
        "memory": {"archive_bytes": 3000000},
        "pieces": [{"id": "hut", "since": 3, "category": "Buildings", "label": "Hut", "description": "a hut",
          "volume": [-100, 0, -100, 100, 200, 100], "parts": [{"model": 0, "nodes": ["ie"], "light": "bg0"}],
          "collision": {"groups": ["hut"]}, "expect": {"triangles": [12], "collision_triangles": 4}, "memory": {"instance_bytes": 40000}}]})";
    {
        story_game_pieces::Catalogue t;
        assert(t.add_set(good, why) && t.version == 3 && t.find("hut") && t.set("test")->recipe == good);
        assert(!t.add_set(good, why) && why.find("duplicate") != std::string::npos && t.pieces.size() == 1);
        auto bad = [&](const std::string& from, const std::string& to) {
            std::string text = good;
            const auto at = text.find(from);
            assert(at != std::string::npos);
            text.replace(at, from.size(), to);
            story_game_pieces::Catalogue u;
            const bool ok = u.add_set(text, why);
            assert(!ok && u.pieces.empty() && u.sets.empty());
        };
        bad(R"("game_pieces_format": 1)", R"("game_pieces_format": 2)");
        bad(R"("set": "test")", R"("set": "Test")");
        bad(R"("archive": "sea_Room12.szs")", R"("archive": "sea_Room13.szs")");  // not the room's archive
        bad(R"("archive": "sea_Room12.szs")", R"("archive": "../sea_Room12.szs")");
        bad(R"("room": 12})", R"("room": 50})");
        bad(R"("archive_bytes": 3000000)", "\"archive_bytes\": " + std::to_string(story_game_pieces::kRoomBudgetBytes + 1));  // over the room budget alone
        bad(R"("id": "hut")", R"("id": "Hut")");
        bad(R"("volume": [-100, 0, -100, 100, 200, 100])", R"("volume": [100, 0, -100, -100, 200, 100])");
        bad(R"("volume": [-100, 0, -100, 100, 200, 100])", R"("volume": [-100, 0, -100, 100, 200])");
        bad(R"("model": 0)", R"("model": 4)");
        bad(R"("light": "bg0")", R"("light": "actor")");
        bad(R"("nodes": ["ie"])", R"("nodes": ["../ie"])");
        bad(R"("triangles": [12])", R"("triangles": [12, 4])");  // one count per part
        bad(R"("groups": ["hut"])", R"("groups": []})");           // groups and their count go together
        bad(R"("instance_bytes": 40000)", R"("instance_bytes": 0)");
    }

    // ---- manifests
    {
        std::vector<ManifestActor> got;
        assert(parses(manifest(house("a")), why, &got));
        const auto& a = got.at(0);
        assert(a.game_piece() && a.piece == "outset-house-small" && a.actor == "piece:outset-house-small" && !a.game_object());
        assert(a.kind == "mod" && a.model == "piece:outset-house-small" && a.collision_mesh == a.model && a.behaviour == "static" && a.world_object());
        assert(a.scale.x == 1 && a.position.x == 100 && a.room == 12);
        assert(parses(manifest(house("b", "outset-lookout-tower", R"(, "rotation": {"x": 0, "y": 90, "z": 0})")), why, &got) && got[0].rotation.y == 90);
    }
    assert(!parses(manifest(house("a", "outset-house-big")), why) && why.find("not in the game pieces catalogue") != std::string::npos);
    assert(!parses(manifest(house("a"), "null"), why) && why.find("requires") != std::string::npos);
    assert(!parses(manifest(house("a"), R"({"game_objects": 1})"), why) && why.find("version 2") != std::string::npos);
    assert(!parses(manifest(house("a"), R"({"game_objects": 99})"), why) && why.find("update the port") != std::string::npos);
    for (const char* extra : {R"(, "scale": {"x": 2, "y": 2, "z": 2})", R"(, "lines": "hello")", R"(, "flag": "f")", R"(, "params": 1)",
                              R"(, "collision": {"radius": 1})", R"(, "model": "m")", R"(, "variant": "v")", R"(, "then": [])"})
        assert(!parses(manifest(house("a", "outset-house-small", extra)), why) && why.find("a game piece takes only") != std::string::npos);
    assert(!parses(manifest(house("a", "outset-house-small", R"(, "rotation": {"x": 10, "y": 0, "z": 0})")), why) && why.find("vertical axis") != std::string::npos);
    assert(!parses(manifest(house("a") + "," + house("a")), why) && why.find("duplicate") != std::string::npos);
    // the room budget: one set's archive once and every placement (the Outset set leaves room for its own pieces)
    {
        std::string many;
        for (int i = 0; i < 12; ++i) many += (i ? "," : "") + house("h" + std::to_string(i), outset[i % 6], "", 12 + i / 6);
        assert(parses(manifest(many), why));
    }
    {
        // a second set in the same room goes over the budget
        story_game_pieces::Catalogue t = c;
        std::string other = good;
        other.replace(other.find("3000000"), 7, std::to_string(story_game_pieces::kRoomBudgetBytes - 5000000));
        assert(t.add_set(other, why));
        std::vector<ManifestActor> list(2);
        list[0].piece = "outset-house-small", list[0].stage = "sea", list[0].room = 12;
        list[1].piece = "hut", list[1].stage = "sea", list[1].room = 12;
        assert(!world_manifest::game_piece_budget(list, why, t) && why.find("memory budget for buildings") != std::string::npos);
        list[1].room = 13;
        assert(world_manifest::game_piece_budget(list, why, t));
    }
    // across packages: each package within budget, together over it
    {
        story_game_pieces::Catalogue t = c;
        // (the port's sets fit; the cross-package check reuses game_piece_budget over every enabled package's pieces)
        std::vector<std::pair<std::string, Value>> packages{{"one", manifest(house("a"))}, {"two", manifest(house("b", "outset-well-shelter"))}};
        assert(world_manifest::validate_world_packages(packages, why));
    }

    // ---- the run-time budget (mod_actor_budget.h): a piece set may take up to the buildings' budget, a model 2 MiB;
    // buildings don't count against a mod's own 8 MiB for its models
    {
        using namespace mod_actor::budget;
        Usage u;
        std::vector<Record> r{{"m", "gp-set", 12, 5400000, 300000, 30000, 4400, true, true, true}};
        assert(check(r, 12, u, why) && u.pieces > 5700000);
        r[0].piece = false;
        assert(!check(r, 12, u, why) && why.find("2 MiB") != std::string::npos);
        r[0].piece = true;
        r.push_back({"m", "gp-other", 12, uint32_t(PieceRoomLimit - 5700000), 200000, 30000, 4400, true, true, true});
        assert(!check(r, 12, u, why) && why.find("game pieces in this room exceed " + mib(PieceRoomLimit)) != std::string::npos);
        r[1].backing = 2600000;
        assert(check(r, 12, u, why) && !u.mods.count("m"));
        r[1].room = 13, r[1].active = false, r[1].loaded = false;
        assert(check(r, 12, u, why));
    }
    // ---- where a building may stand (story_world_placement.h check_building, also part of check()): an Outset-sized house
    // (1340 x 1430 x 1390) on open ground is accepted; over a villager's route, a chest or a door it is
    // refused with that reason; grass, flowers and trees under it don't count; a tag area by its own size
    {
        using namespace story_world_placement;
        // a sea room's layout as the game stores it (world space): TGDR door, TRES chest, ACTR grass and a
        // villager, SCOB event trigger (scale 30 -> radius and height 300), RPAT route of two points
        using Bytes = std::vector<uint8_t>;
        Bytes b(4 + 6 * 12, 0);
        auto u16 = [](Bytes& d, size_t p, uint16_t v) { d[p] = uint8_t(v >> 8), d[p + 1] = uint8_t(v); };
        auto u32 = [](Bytes& d, size_t p, uint32_t v) { for (unsigned k = 0; k < 4; ++k) d[p + k] = uint8_t(v >> (24 - k * 8)); };
        auto f32 = [&](Bytes& d, size_t p, float v) { u32(d, p, std::bit_cast<uint32_t>(v)); };
        auto chunk = [&](unsigned j, const char* tag, unsigned count, size_t size) {
            const size_t at = b.size();
            std::memcpy(&b[4 + j * 12], tag, 4);
            u32(b, 4 + j * 12 + 4, count), u32(b, 4 + j * 12 + 8, uint32_t(at));
            b.resize(at + count * size, 0);
            return at;
        };
        u32(b, 0, 6);
        auto entry = [&](size_t at, const char* name, float x, float y, float z) {
            std::memcpy(&b[at], name, std::strlen(name));
            f32(b, at + 12, x), f32(b, at + 16, y), f32(b, at + 20, z);
        };
        size_t at = chunk(0, "TGDR", 1, 36);
        entry(at, "KNOB00D", -205000, 500, 316000), b[at + 32] = b[at + 33] = b[at + 34] = 10;
        at = chunk(1, "TRES", 1, 32);
        entry(at, "takara3", -195000, 300, 320000);
        at = chunk(2, "ACTR", 3, 32);
        entry(at, "kusax21", -200000, 180, 316300), entry(at + 32, "Oyashi", -200100, 180, 316400), entry(at + 64, "Ko1", -210000, 100, 330000);
        at = chunk(3, "SCOB", 1, 36);
        entry(at, "TagEv", -190000, 100, 310000), b[at + 32] = 30, b[at + 33] = 30, b[at + 34] = 30;
        at = chunk(4, "RPAT", 1, 12);
        u16(b, at, 2), u32(b, at + 8, 0);
        at = chunk(5, "RPPN", 2, 16);
        f32(b, at + 4, -198000), f32(b, at + 8, 150), f32(b, at + 12, 324000);
        f32(b, at + 20, -198000), f32(b, at + 24, 150), f32(b, at + 28, 328000);
        Index sea;
        // the layout is world space as stored: nothing is moved
        assert(add_native_layout(sea, "sea", 44, b, &why));
        assert(sea.layout.size() == 6 && sea.routes.size() == 1);
        assert(sea.layout[0].position.x == -205000 && sea.routes[0].points[0].z == 324000);
        auto house = [](float x, float y, float z) { return Bounds{{x - 670, y, z - 695}, {x + 670, y + 1430, z + 695}}; };
        // open ground (over grass and a palm): accepted
        assert(check_building("sea", 44, house(-200000, 178, 316300), sea).empty());
        auto reason = [&](Bounds where) { auto c = check_building("sea", 44, where, sea); return c.size() == 1 ? c[0].kind + ": " + c[0].message + " [" + c[0].native + "]" : std::to_string(c.size()); };
        assert(reason(house(-205200, 400, 316200)) == "door: a door of the island [KNOB00D]");
        assert(reason(house(-195300, 200, 320300)) == "chest: a chest of the island [takara3]");
        assert(reason(house(-198300, 100, 326000)) == "villager route: a villager route of the island [" + sea.routes[0].id + "]");
        assert(reason(house(-210000, 0, 330400)) == "villager: a villager of the island [Ko1]");
        // the tag area's radius 300: 1000 units off is clear, 900 is not (its edge plus the margin)
        assert(check_building("sea", 44, house(-190000 + 670 + 400, 0, 310000), sea).empty());
        assert(reason(house(-190000 + 670 + 300, 0, 310000)) == "cutscene area: a cutscene area of the island [TagEv]");
        // another room's layout and another stage don't count
        assert(check_building("sea", 45, house(-205200, 400, 316200), sea).empty());
        // a dungeon room's layout is world space too
        Index dungeon;
        assert(add_native_layout(dungeon, "M_NewD2", 2, b, &why) && dungeon.layout[0].position.x == -205000);
        // what the kinds are and what a building may cover
        LayoutEntry e;
        e.name = "kusax7";
        assert(building_extent(e).keep == Keep::None);
        e.name = "kytag05", e.scaled = true;
        assert(building_extent(e).keep == Keep::None);
        e.name = "item", e.params = 0x07;  // a piece of heart
        assert(building_extent(e).keep == Keep::Item);
        e.name = "Unknown", e.scaled = false, e.params = 0;
        assert(building_extent(e).keep == Keep::Object && building_extent(e).radius == 120);
        // things that need no place of their own (they refused most open ground before): message, hint, sound,
        // attention, pig and island-name tags, the sea stage's island LOD markers, GBA switches (HD: bomb switches
        // only), invisible walls, pots, barrels and signs
        for (const char* free : {"TagMsg2", "TagHt", "TagSo", "AttTagB", "TagKb", "TagIsl", "LOD44", "agbMARK", "agbF", "Akabe", "kotubo", "Ktaru", "Kanban", "Salvage", "ITat00", "ky_tag1"}) {
            e.name = free, e.scaled = true;
            assert(building_extent(e).keep == Keep::None);
        }
        // rupees and hearts lying about may be covered; a piece of heart (item 07) or a heart container (08) not
        e.name = "item", e.scaled = false, e.params = 0x00000001;
        assert(building_extent(e).keep == Keep::None);
        e.params = 0x0000FF07;
        assert(building_extent(e).keep == Keep::Item);
        // a wide event trigger counts by its core of 300 units; a small one whole
        e.name = "TagEv", e.scaled = true, e.scale[0] = e.scale[2] = 190, e.scale[1] = 20;
        assert(building_extent(e).keep == Keep::Event && building_extent(e).radius == 300);
        e.scale[0] = e.scale[2] = 15;
        assert(building_extent(e).radius == 150);
        // start points keep 100 units, props 60
        e.tag = "PLYR", e.name = "Link", e.scaled = false;
        assert(building_extent(e).keep == Keep::Start && building_extent(e).radius == 100);
        e.tag = "ACTR", e.name = "bonbori";
        assert(building_extent(e).keep == Keep::Object && building_extent(e).radius == 60);
        // a single small thing says how far to move: the chest (radius 120, margin 50) 100 units inside its reach
        {
            Index one;
            LayoutEntry chest{"sea", 44, "TRES", "takara3", {0, 0, 0}, {}, false, 0, 0};
            one.layout.push_back(chest);
            auto c = check_building("sea", 44, Bounds{{70, 0, -100}, {370, 100, 100}}, one);
            assert(c.size() == 1 && c[0].kind == "chest" && c[0].move == 101 && c[0].reach == 120 && c[0].at.x == 0);
            // its centre under the bounds: past the nearest side (70) and its reach (170)
            c = check_building("sea", 44, Bounds{{-70, 0, -100}, {370, 100, 100}}, one);
            assert(c.size() == 1 && c[0].move == 241);
        }
        // a route at sea level is the sea's (boats, swimmers): not in the way of a building on the island, in the
        // way of what reaches down to the water
        {
            Index sea_routes;
            sea_routes.routes.push_back({"sea", 44, "RPAT:9", {{-1000, 0, 0}, {1000, 0, 0}}, kRouteHalfWidth, false});
            assert(check_building("sea", 44, Bounds{{-100, 30, -100}, {100, 500, 100}}, sea_routes).empty());
            assert(check_building("sea", 44, Bounds{{-100, -50, -100}, {100, 500, 100}}, sea_routes).size() == 1);
            sea_routes.routes[0].points = {{-1000, 150, 0}, {1000, 170, 0}};  // on the island: a villager's walk
            auto c = check_building("sea", 44, Bounds{{-100, 150, -100}, {100, 500, 100}}, sea_routes);
            assert(c.size() == 1 && c[0].kind == "villager route" && c[0].path.size() == 2);
        }
    }
    std::puts("story_game_pieces_test: ok");
    return 0;
}
