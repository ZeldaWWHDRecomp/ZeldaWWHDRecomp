// The mod's room paths (story_room_path.h, story_room_paths.cpp, the manifest's "paths") with synthetic
// guest memory and no game: parsing and its refusals, the record the game reads, the game's own area test
// for both windings, the numbers and records a mod's paths keep across edits, the answer only for the
// path the actor names, and the records written again after a full state load.
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "mods/story_room_path.h"
#include "mods/story_room_paths.h"
#include "mods/story_world_manifest.h"
#include "guest_addr.h"
#include "runtime.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

void log_msg(const char*, ...) {}

namespace {
namespace path = story_room_path;
namespace paths = story_room_paths;
using mods::json::Value;
constexpr uint32_t kRecords = 0x60100000;
void map_guest(uint32_t at, uint32_t size) {
#ifdef _WIN32
    assert(VirtualAlloc(mem::ptr(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
    assert(mmap(mem::ptr(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
#endif
}
float f(uint32_t a) { return u32_as_f32(ld32(a)); }
Value manifest(const std::string& text) {
    return mods::json::parse(text);
}
bool parse(const std::string& text, std::vector<path::Path>& out, std::string& why) { return story_notebook::parse_room_paths(manifest(text), out, why); }
std::string refusal(const std::string& text) {
    std::vector<path::Path> out;
    std::string why;
    assert(!parse(text, out, why));
    return why;
}
path::Path square(const std::string& id, unsigned room, bool ccw) {
    path::Path p;
    p.id = id, p.stage = "sea", p.room = room, p.closed = true;
    p.points = {{0, 0, 0}, {1000, 0, 0}, {1000, 0, 1000}, {0, 0, 1000}};
    if (!ccw) std::swap(p.points[1], p.points[3]);
    return p;
}
}  // namespace

int main() {
    // ---- the manifest
    std::vector<path::Path> out;
    std::string why;
    assert(parse(R"({"paths":[{"id":"fort","stage":"sea","room":11,"closed":true,"points":[{"x":0,"y":5,"z":0},{"x":900,"y":5,"z":0},{"x":450,"y":5,"z":800}]},
                             {"id":"line","stage":"sea","room":11,"points":[{"x":0,"y":0,"z":0},{"x":10,"y":0,"z":0}]}]})", out, why));
    assert(out.size() == 2 && out[0].closed && !out[1].closed && out[0].room == 11 && out[0].points.size() == 3 && out[0].points[1].x == 900);
    assert(parse(R"({})", out, why) && out.empty());
    assert(refusal(R"({"paths":[{"id":"a","stage":"sea","room":1,"points":[{"x":0,"y":0,"z":0}]}]})").find("2 to 32") != std::string::npos);
    assert(refusal(R"({"paths":[{"id":"a","stage":"sea","room":1,"closed":true,"points":[{"x":0,"y":0,"z":0},{"x":9,"y":0,"z":0}]}]})").find("at least 3") != std::string::npos);
    assert(refusal(R"({"paths":[{"id":"a","stage":"sea","room":1,"closed":true,"points":[{"x":0,"y":0,"z":0},{"x":100,"y":0,"z":100},{"x":100,"y":0,"z":0},{"x":0,"y":0,"z":100}]}]})").find("cross") != std::string::npos);
    assert(refusal(R"({"paths":[{"id":"a","stage":"sea","room":1,"points":[{"x":0,"y":0,"z":0},{"x":0.5,"y":0,"z":0}]}]})").find("one unit") != std::string::npos);
    assert(refusal(R"({"paths":[{"id":"a","stage":"sea","room":1,"closed":true,"points":[{"x":0,"y":0,"z":0},{"x":10,"y":0,"z":0},{"x":20,"y":0,"z":0}]}]})").find("enclose") != std::string::npos);
    assert(refusal(R"({"paths":[{"id":"a","stage":"sea","room":1,"points":[{"x":0,"y":0,"z":0},{"x":9,"y":0,"z":0}]},{"id":"a","stage":"sea","room":1,"points":[{"x":0,"y":0,"z":0},{"x":9,"y":0,"z":0}]}]})").find("duplicate") != std::string::npos);
    assert(refusal(R"({"paths":[{"id":"a","stage":"sea","room":64,"points":[{"x":0,"y":0,"z":0},{"x":9,"y":0,"z":0}]}]})").find("room") != std::string::npos);
    assert(refusal(R"({"paths":[{"id":"a","stage":"sea","room":1,"speed":3,"points":[{"x":0,"y":0,"z":0},{"x":9,"y":0,"z":0}]}]})").find("takes id") != std::string::npos);
    {
        std::string many = R"({"paths":[)";
        for (int i = 0; i < 17; i++) many += std::string(i ? "," : "") + R"({"id":"p)" + std::to_string(i) + R"(","stage":"sea","room":1,"points":[{"x":0,"y":0,"z":0},{"x":9,"y":0,"z":0}]})";
        assert(refusal(many + "]}").find("up to 16") != std::string::npos);
    }
    // a refused path refuses the whole world manifest (parse_actors checks the paths too)
    {
        std::vector<story_notebook::ManifestActor> actors;
        assert(!story_notebook::parse_actors(manifest(R"({"paths":[{"id":"a","stage":"sea","room":1,"points":[]}],"actors":[]})"), actors, why));
    }
    // more paths in one room than 24 across the enabled mods
    {
        std::vector<std::pair<std::string, Value>> packages;
        for (int m = 0; m < 2; m++) {
            std::string text = R"({"paths":[)";
            for (int i = 0; i < 13; i++) text += std::string(i ? "," : "") + R"({"id":"p)" + std::to_string(i) + R"(","stage":"sea","room":1,"points":[{"x":0,"y":0,"z":0},{"x":9,"y":0,"z":0}]})";
            packages.push_back({"mod" + std::to_string(m), manifest(text + "]}")});
        }
        assert(!story_notebook::world_manifest::validate_world_packages(packages, why) && why.find("24 paths") != std::string::npos);
    }

    // ---- a wall cannon names its firing area: a closed path of its own stage and room, required
    {
        const std::string paths_part = R"("paths":[{"id":"yard","stage":"sea","room":11,"closed":true,"points":[{"x":0,"y":0,"z":0},{"x":900,"y":0,"z":0},{"x":450,"y":0,"z":800}]},
                                                 {"id":"line","stage":"sea","room":11,"points":[{"x":0,"y":0,"z":0},{"x":900,"y":0,"z":0}]},
                                                 {"id":"far","stage":"sea","room":12,"closed":true,"points":[{"x":0,"y":0,"z":0},{"x":900,"y":0,"z":0},{"x":450,"y":0,"z":800}]}])";
        auto cannon = [&](const std::string& path) {
            return manifest("{" + paths_part + R"(,"actors":[{"id":"wc","actor":"wall_cannon","stage":"sea","room":11,"position":{"x":450,"y":0,"z":-500},"defeated_flag":"wc.dead","drops":[])" +
                            (path.empty() ? std::string() : ",\"path\":" + path) + "}]}");
        };
        std::vector<story_notebook::ManifestActor> actors;
        assert(story_notebook::parse_actors(cannon("\"yard\""), actors, why) && actors.size() == 1 && actors[0].room_path == "yard");
        assert(!story_notebook::parse_actors(cannon(""), actors, why) && why.find("needs \"path\"") != std::string::npos);
        assert(!story_notebook::parse_actors(cannon("\"nowhere\""), actors, why) && why.find("no path") != std::string::npos);
        assert(!story_notebook::parse_actors(cannon("\"far\""), actors, why) && why.find("another stage or room") != std::string::npos);
        assert(!story_notebook::parse_actors(cannon("\"line\""), actors, why) && why.find("must be an area") != std::string::npos);
        assert(!story_notebook::parse_actors(cannon(R"({"points":[{"x":1,"y":0,"z":1}]})"), actors, why));
        // a warship on a route and a wandering Floormaster: a path (a line or an area) of their room, required;
        // the anchored warship and the ordinary Floormaster take none
        auto enemy = [&](const std::string& type, const std::string& variant, const std::string& path) {
            return manifest("{" + paths_part + R"(,"actors":[{"id":"e","actor":")" + type + R"(","stage":"sea","room":11,"position":{"x":450,"y":0,"z":-500},"defeated_flag":"e.dead","drops":[])" +
                            (variant.empty() ? std::string() : R"(,"parameters":{"variant":")" + variant + "\"}") + (path.empty() ? std::string() : ",\"path\":" + path) + "}]}");
        };
        for (const char* type : {"warship", "floormaster"}) {
            const std::string variant = std::string(type) == "warship" ? "route" : "wandering";
            assert(story_notebook::parse_actors(enemy(type, variant, "\"line\""), actors, why) && actors[0].room_path == "line");
            assert(story_notebook::parse_actors(enemy(type, variant, "\"yard\""), actors, why) && actors[0].room_path == "yard");
            assert(!story_notebook::parse_actors(enemy(type, variant, ""), actors, why) && why.find("needs \"path\"") != std::string::npos);
            assert(!story_notebook::parse_actors(enemy(type, variant, "\"far\""), actors, why));
            assert(story_notebook::parse_actors(enemy(type, "", ""), actors, why) && actors[0].room_path.empty());
            assert(!story_notebook::parse_actors(enemy(type, "", "\"line\""), actors, why) && why.find("takes no path") != std::string::npos);
        }
        // a Kargaroc with a rider: a path of its room with a release point; its rider's id is its own
        {
            const std::string with_release = R"("paths":[{"id":"flight","stage":"sea","room":11,"closed":true,"points":[{"x":0,"y":0,"z":0},{"x":900,"y":0,"z":0,"release":true},{"x":450,"y":0,"z":800}]},
                                                       {"id":"plain","stage":"sea","room":11,"closed":true,"points":[{"x":0,"y":0,"z":0},{"x":900,"y":0,"z":0},{"x":450,"y":0,"z":800}]}])";
            auto bird = [&](const std::string& path, const std::string& extra) {
                return manifest("{" + with_release + R"(,"actors":[{"id":"bird","actor":"kargaroc","parameters":{"variant":"moblin_rider"},"stage":"sea","room":11,"position":{"x":450,"y":500,"z":-500},"defeated_flag":"bird.dead","drops":[],"path":")" + path + "\"}" + extra + "]}");
            };
            assert(story_notebook::parse_actors(bird("flight", ""), actors, why) && actors.size() == 1 && actors[0].room_path == "flight" && story_notebook::placed_actors(actors[0]) == 2);
            assert(!story_notebook::parse_actors(bird("plain", ""), actors, why) && why.find("release") != std::string::npos);
            assert(!story_notebook::parse_actors(bird("flight", R"(,{"id":"bird.rider","actor":"chuchu","stage":"sea","room":11,"position":{"x":0,"y":0,"z":0},"defeated_flag":"x.dead","drops":[]})"), actors, why) && why.find("rider") != std::string::npos);
        }
        // a Wizzrobe summoner: a path with a next path (its summon points), one per room; its pool is its own allowance
        {
            const std::string two = R"("paths":[{"id":"hall","stage":"sea","room":11,"closed":true,"next":"spots","points":[{"x":0,"y":0,"z":0},{"x":900,"y":0,"z":0},{"x":450,"y":0,"z":800}]},
                                                {"id":"spots","stage":"sea","room":11,"closed":true,"points":[{"x":100,"y":0,"z":100},{"x":800,"y":0,"z":100},{"x":450,"y":0,"z":600}]}])";
            auto wizard = [&](const std::string& id, const std::string& path, const std::string& extra) {
                return manifest("{" + two + R"(,"actors":[{"id":")" + id + R"(","actor":"wizzrobe","parameters":{"variant":"summoner_morths"},"stage":"sea","room":11,"position":{"x":450,"y":0,"z":300},"defeated_flag":"w.dead","drops":[],"path":")" + path + "\"}" + extra + "]}");
            };
            assert(story_notebook::parse_actors(wizard("wiz", "hall", ""), actors, why) && actors[0].room_path == "hall" && story_notebook::placed_actors(actors[0]) == 1);
            assert(!story_notebook::parse_actors(wizard("wiz", "spots", ""), actors, why) && why.find("next path") != std::string::npos);
            assert(!story_notebook::parse_actors(wizard("wiz", "hall", R"(,{"id":"wiz2","actor":"wizzrobe","parameters":{"variant":"summoner_chuchus"},"stage":"sea","room":11,"position":{"x":0,"y":0,"z":0},"defeated_flag":"w2.dead","drops":[],"path":"hall"})"), actors, why) && why.find("one Wizzrobe summoner per room") != std::string::npos);
            assert(!story_notebook::parse_actors(wizard("a-very-long-wizzrobe-id-here", "hall", ""), actors, why) && why.find("summon") != std::string::npos);
            assert(story_enemies_summons::size(3) == 20 && story_enemies_summons::size(4) == 10 && story_enemies_summons::size(0) == 10 && !story_enemies_summons::pool(7));
            assert(story_enemies_summons::variant("chuchu", 5) == "red" && story_enemies_summons::variant("bubble", 1) == "blue" && story_enemies_summons::variant("morth", 3).empty());
            assert(story_enemies_summons::wanted_variant(0xCE, 0xFFFF0C00u) == "blue" && story_enemies_summons::wanted_variant(0xCF, 0x0100FF01u) == "blue" && story_enemies_summons::wanted_variant(0xCD, 3).empty());
            assert(story_enemy_variants::declared_params("wizzrobe", 0xC2FFFF01u) && !story_enemy_variants::declared_params("wizzrobe", 0xC2FFFF02u));
            // the overall capacity counts the pool
            std::vector<std::pair<std::string, Value>> packages{{"m", wizard("wiz", "hall", "")}};
            assert(story_notebook::world_manifest::validate_world_packages(packages, why));
        }
        // other enemies take no room path
        assert(!story_notebook::parse_actors(manifest("{" + paths_part + R"(,"actors":[{"id":"cc","actor":"chuchu","stage":"sea","room":11,"position":{"x":0,"y":0,"z":0},"defeated_flag":"cc.dead","drops":[],"path":"yard"}]})"), actors, why));
    }

    // ---- the game's area test: the inside of either winding once in game order; outside stays out
    for (bool ccw : {true, false}) {
        const auto sq = square("s", 1, ccw);
        assert(path::inside(sq, 500, 500) && path::inside(sq, 10, 990) && path::inside(sq, 990, 10));
        assert(!path::inside(sq, -10, 500) && !path::inside(sq, 500, 1010) && !path::inside(sq, 2000, 2000) && !path::inside(sq, 500, -300));
        assert(path::winding(path::game_order(sq)) < 0 && path::game_order(sq).front() == sq.points.front());
    }
    {
        auto line = square("l", 1, true);
        line.closed = false;
        assert(!path::inside(line, 500, 500) && path::game_order(line) == line.points);  // a line is no area; its order stays
    }
    assert(path::convex(square("s", 1, true).points) && path::self_crossing({{0, 0, 0}, {100, 0, 100}, {100, 0, 0}, {0, 0, 100}}));

    // ---- the record
    {
        const auto sq = square("s", 7, true);
        const auto b = path::encode(sq, 0xC3, kRecords);
        assert(path::get32(b.data(), 0) == 0x0004FFFFu && b[4] == 0xFF && b[5] == 1 && b[6] == 0xFF && path::get32(b.data(), 8) == kRecords + path::kPoints);
        assert(path::get32(b.data(), path::kMagicAt) == path::kMagic && b[path::kNumber] == 0xC3 && b[path::kRoom] == 7 && std::memcmp(b.data() + path::kStage, "sea", 4) == 0);
        assert(path::get32(b.data(), path::kPoints) == 0xFFFFFFFFu);
        const auto order = path::game_order(sq);
        for (unsigned i = 0; i < 4; i++) {
            float x;
            const uint32_t u = path::get32(b.data(), path::kPoints + i * 16 + 4);
            std::memcpy(&x, &u, 4);
            assert(x == order[i].x);
        }
    }
    // a rider's release point: argument 3 = 5 at that point, kept with its point when an area is turned to game order
    {
        auto sq = square("r", 7, true);
        sq.release = {false, false, true, false};
        const auto b = path::encode(sq, 0xC0, kRecords);
        const auto order = path::game_indices(sq);
        for (unsigned i = 0; i < 4; i++) assert(path::get32(b.data(), path::kPoints + i * 16) == (order[i] == 2 ? 0xFFFFFF05u : 0xFFFFFFFFu));
        auto plain = sq;
        plain.release.clear();
        assert(path::hash(sq, 0xC0) != path::hash(plain, 0xC0));
        std::vector<path::Path> parsed_release;
        assert(parse(R"({"paths":[{"id":"p","stage":"sea","room":1,"points":[{"x":0,"y":0,"z":0},{"x":9,"y":0,"z":0,"release":true}]}]})", parsed_release, why) &&
               parsed_release[0].releases(1) && !parsed_release[0].releases(0));
        assert(refusal(R"({"paths":[{"id":"p","stage":"sea","room":1,"points":[{"x":0,"y":0,"z":0},{"x":9,"y":0,"z":0,"release":1}]}]})").find("release") != std::string::npos);
    }
    assert(path::free_number({}) == 0xC0 && path::free_number({0xC0, 0xC2}) == 0xC1 && path::is_number(0xFD) && !path::is_number(0xFE) && !path::is_number(0xBF));

    // ---- the records in the game
    map_guest(kRecords, 0x20000);
    paths::reset();
    auto a = square("a", 3, true), b = square("b", 3, false), c = square("c", 4, true);
    assert(paths::declare("m1", {a, b}, why) && paths::declare("m2", {c}, why));
    assert(paths::declared() && paths::number("m1", "a") == 0xC0 && paths::number("m1", "b") == 0xC1 && paths::number("m2", "c") == 0xC0);
    assert(!paths::record("m1", "a"));  // no memory yet: numbers, but no records
    paths::set_memory(kRecords);
    const uint32_t ra = paths::record("m1", "a"), rb = paths::record("m1", "b");
    assert(ra && rb && ra != rb && ld16(ra) == 4 && ld32(ra + 8) == ra + path::kPoints);
    for (const auto& i : paths::list()) assert(i.valid && i.address);
    // the answer: only that path, its number, its room, its stage
    assert(paths::answer("m1", "a", 0xC0, 3, "sea") == ra);
    assert(!paths::answer("m1", "a", 0xC1, 3, "sea") && !paths::answer("m1", "a", 0xC0, 4, "sea") && !paths::answer("m1", "a", 0xC0, 3, "Asoko") && !paths::answer("m2", "a", 0xC0, 3, "sea"));
    assert(!paths::answer("m1", "a", 0xC0, -1, "sea"));  // the stage's own paths (room -1) are never a mod path
    // an edit keeps the record and number of a path that stays in its room (an actor holding it sees the change)
    a.points[2] = {1500, 0, 1200};
    assert(paths::declare("m1", {b, a}, why) && paths::record("m1", "a") == ra && paths::number("m1", "a") == 0xC0 && f(ra + path::kPoints + 2 * 16 + 4) == 1500);
    // a path that went frees its record; a new one in the room takes the lowest free number
    auto d = square("d", 3, true);
    assert(paths::declare("m1", {a, d}, why) && !paths::number("m1", "b") && paths::number("m1", "d") == 0xC1);
    assert(paths::record("m1", "d") == rb && ld8(rb + path::kNumber) == 0xC1 && ld32(rb + path::kHash) == path::hash(d, 0xC1));  // b's record, now d's
    // a path that moves to another room gets that room's lowest free number
    a.room = 4;
    assert(paths::declare("m1", {a, d}, why) && paths::number("m1", "a") == 0xC1 && paths::number("m2", "c") == 0xC0);
    // a full state load: every record written again from the declarations (a state from before an edit)
    std::memset(mem::ptr(kRecords), 0, path::kMemoryBytes);
    paths::state_loaded();
    assert(ld32(paths::record("m1", "a") + path::kMagicAt) == path::kMagic && ld32(paths::record("m2", "c") + path::kMagicAt) == path::kMagic);
    // the room's numbers run out at 24 paths
    {
        std::vector<path::Path> many;
        for (int i = 0; i < 25; i++) {
            auto p = square("n" + std::to_string(i), 9, true);
            many.push_back(p);
        }
        assert(!paths::declare("m3", many, why) && why.find("24 paths") != std::string::npos && !paths::number("m3", "n0"));
    }
    // a next path: its number in the record's header; the next-path lookup finds it in the same room
    {
        auto first = square("first", 5, true), second = square("second", 5, true);
        first.next = "second";
        assert(paths::declare("m4", {first, second}, why));
        const uint32_t r1 = paths::record("m4", "first"), r2 = paths::record("m4", "second");
        assert(ld16(r1 + 2) == paths::number("m4", "second") && ld16(r2 + 2) == 0xFFFF);
        assert(paths::is_record(r1) && !paths::is_record(r1 + 4) && !paths::is_record(0x20000000));
        assert(paths::next_record(r1, 5) == r2 && !paths::next_record(r2, 5) && !paths::next_record(r1, 6));
        assert(paths::declare("m4", {}, why));
        std::vector<path::Path> bad;
        assert(!parse(R"({"paths":[{"id":"a","stage":"sea","room":1,"next":"b","points":[{"x":0,"y":0,"z":0},{"x":9,"y":0,"z":0}]},{"id":"b","stage":"sea","room":2,"points":[{"x":0,"y":0,"z":0},{"x":9,"y":0,"z":0}]}]})", bad, why) &&
               why.find("same stage and room") != std::string::npos);
        assert(!parse(R"({"paths":[{"id":"a","stage":"sea","room":1,"next":"a","points":[{"x":0,"y":0,"z":0},{"x":9,"y":0,"z":0}]}]})", bad, why));
    }
    // a mod with no paths left: its records go
    const uint32_t rc = paths::record("m2", "c");
    assert(paths::declare("m2", {}, why) && !paths::number("m2", "c") && ld32(rc + path::kMagicAt) == 0);
    std::puts("story_room_paths_test: ok");
    return 0;
}
