// SPDX-License-Identifier: MPL-2.0
// The water tool's geometry (runtime/src/editor/water.h): polygons, the stored-DEFLATE PNG, exact metres, ponds,
// rivers (levels, steps, shallow sections), waterfalls, the stored parameters, the models as the game's own
// validator takes them, and the bytes themselves (the same digest on every platform).
// Usage: editor_water_test [--write <dir>]  (--write: the sample models, for the converter by hand)
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "editor/water.h"
#include "mods/static_model_validation.h"

using namespace editor::water;
using mods::json::Value;

namespace {
int g_failures = 0;
#define CHECK(x)                                                                    \
    do {                                                                            \
        if (!(x)) {                                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

constexpr uint64_t POND_MAIN = 0xd793489655682debULL, POND_SURFACE = 0xc636fe6488f047e4ULL, RIVER_MAIN = 0x05f0c05e04830be9ULL, RIVER_SURFACE = 0x9905f734aeda6bccULL,
                   FALL_MAIN = 0x0ac6154a1354baf7ULL, FALL_SURFACE = 0x59abc62557077e82ULL;  // the digests every platform makes
uint64_t fnv(const std::vector<uint8_t>& b) {
    uint64_t h = 1469598103934665603ULL;
    for (uint8_t c : b) h = (h ^ c) * 1099511628211ULL;
    return h;
}
Value json(const char* s) { return mods::json::parse(s); }
bool errors(const std::vector<Problem>& ps) {
    for (const auto& p : ps)
        if (p.severity == "error") return true;
    return false;
}
// the model as the game takes it: the strict validator, with collision when it has some
bool valid(const std::vector<uint8_t>& glb, bool collision, static_models::ModelInfo& info, std::string& why) {
    return static_models::validate_glb(std::span<const uint8_t>(glb.data(), glb.size()), info, why, collision);
}
Value doc_of(const std::vector<uint8_t>& b) {
    const uint32_t jl = uint32_t(b[12]) | uint32_t(b[13]) << 8 | uint32_t(b[14]) << 16 | uint32_t(b[15]) << 24;
    return mods::json::parse(std::string(reinterpret_cast<const char*>(b.data() + 20), jl));
}
std::vector<std::string> material_names(const Value& doc) {
    std::vector<std::string> out;
    for (const auto& m : doc.get("materials").array) out.push_back(m.get("name").string());
    return out;
}


void pieces() {
    CHECK(metres(0) == "0" && metres(1536) == "1.5" && metres(-1) == "-0.0009765625" && metres(1024) == "1" && metres(-2048) == "-2" && metres(1) == "0.0009765625");
    // polygons: a square either way round, a concave L, a bow tie, a repeated point
    const std::vector<XZ> square = {{0, 0}, {100, 0}, {100, 100}, {0, 100}}, back = {{0, 100}, {100, 100}, {100, 0}, {0, 0}};
    CHECK(area2(square) == 20000 && area2(back) == -20000 && simple_polygon(square) && simple_polygon(back));
    const std::vector<XZ> ell = {{0, 0}, {300, 0}, {300, 100}, {100, 100}, {100, 300}, {0, 300}};
    std::vector<unsigned> t;
    CHECK(triangulate(ell, t) && t.size() == 3 * 4);
    int64_t filled = 0;
    for (size_t i = 0; i < t.size(); i += 3) {
        const XZ a = ell[t[i]], b = ell[t[i + 1]], c = ell[t[i + 2]];
        const int64_t o = int64_t(b.x - a.x) * (c.z - a.z) - int64_t(b.z - a.z) * (c.x - a.x);
        CHECK(o > 0);  // the polygon's own orientation
        filled += o;
    }
    CHECK(filled == area2(ell));  // the triangles cover it exactly
    CHECK(!simple_polygon({{0, 0}, {100, 100}, {100, 0}, {0, 100}}));
    CHECK(!simple_polygon({{0, 0}, {100, 0}, {100, 100}, {100, 0}}));
    CHECK(!simple_polygon({{0, 0}, {100, 0}}));
    // a stored-DEFLATE PNG: signature, IHDR, the zlib stored block, the CRCs the validator checks
    std::vector<uint8_t> px(64 * 8 * 3, 0x55);
    const auto p = png(64, 8, px, false);
    CHECK(p.size() > 33 && !std::memcmp(p.data(), "\x89PNG\r\n\x1a\n", 8) && !std::memcmp(p.data() + 12, "IHDR", 4) && p[24] == 8 && p[25] == 2);
    CHECK(p[33 + 8] == 0x78 && p[33 + 9] == 0x01 && p[33 + 10] == 1);  // zlib header, one final stored block
    const uint8_t abc[] = {'a', 'b', 'c'};
    CHECK(crc32(abc, 3) == 0x352441C2u);
}

Params pond_params() {
    Params p;
    p.kind = "pond";
    p.points = {{-500, -400}, {500, -400}, {600, 300}, {0, 600}, {-600, 300}};
    p.depth = 250;
    p.rim = 30;
    p.foam = true;
    p.floor = {0x40, 0x80, 0x60};
    p.ground = {{-500 + 1000, -400 + 2000, 90}, {500 + 1000, -400 + 2000, 110}};  // two banks known, three not
    return p;
}
Params river_params() {
    Params p;
    p.kind = "river";
    p.points = {{0, 0}, {600, 0}, {1200, 300}, {1800, 300}};
    p.width = 300;
    p.depth = 150;
    p.shallow = {false, true, false};
    return p;
}

void pond() {
    const Params p = pond_params();
    Built b;
    std::vector<Problem> problems;
    CHECK(build(p, {1000, 2000}, 100, b, problems) && problems.empty());
    CHECK(b.material == kPondMaterial && b.surface_triangles > 0 && b.collision_triangles > 0 && b.main_triangles > 0);
    // the surface sits at the level (local 0), the floor `depth` below, the banks' crest `rim` above, down to the ground
    // (and beyond the outline: the outline's corners rounded off, the banks sloping out to where they meet the ground)
    CHECK(b.min[1] <= -250 && b.max[1] >= 30 && b.max[1] <= 31 && b.max[0] > 600 && b.min[0] < -600);
    static_models::ModelInfo info;
    std::string why;
    CHECK(valid(b.main, true, info, why));
    if (!why.empty()) std::fprintf(stderr, "pond main: %s\n", why.c_str());
    // the bowl and the bank in four arcs share one texture (the converter puts the island's own in where it can)
    CHECK(info.material_count == 5 && info.cutout_count == 0 && info.texture_bytes == 64 * 64 * 4);
    // the surface: the water, its wave lines and the coast foam along the shore
    CHECK(valid(b.surface, false, info, why) && info.triangle_count == b.surface_triangles && info.material_count == 3);
    CHECK((material_names(doc_of(b.surface)) == std::vector<std::string>{"surface", "waves", "edge"}));
    const Value doc = doc_of(b.main);
    CHECK((material_names(doc) == std::vector<std::string>{"bed", "bank1", "bank2", "bank3", "bank4", "col_water", "col_sand", "col_grass", "col_stone"}));
    // no room: no request for the island's texture; on a sea room each bank asks for the ground where it meets it
    CHECK(doc.get("materials").array[1].get("extras").type != Value::Object);
    Params on_island = p;
    on_island.room = 44;
    Built island;
    problems.clear();
    CHECK(build(on_island, {1000, 2000}, 100, island, problems));
    const Value ask = doc_of(island.main).get("materials").array[1].get("extras").get("wwhd_island");
    CHECK(ask.get("room").number == 44 && !ask.get("points").array.empty() && ask.get("points").array.size() <= 64);
    CHECK(doc.get("nodes").array.size() == 2 && doc.get("nodes").array[1].get("name").string() == "pond_col");
    // the collision the placement check sees: the GLB's collision triangles, each with its material; the open water at
    // the level (local 0), the floor `depth` below it
    CHECK(b.collision_faces.size() == b.collision_triangles && b.collision_materials.size() == b.collision_faces.size() && b.collision_points.size() == b.collision_vertices);
    {
        size_t water = 0;
        float lowest = 0;
        for (size_t t = 0; t < b.collision_faces.size(); t++) {
            for (auto i : b.collision_faces[t]) {
                CHECK(i < b.collision_points.size());
                if (i < b.collision_points.size()) lowest = std::min(lowest, b.collision_points[i][1]);
            }
            if (b.collision_materials[t] != "col_water") continue;
            ++water;
            for (auto i : b.collision_faces[t]) CHECK(b.collision_points[i][1] == 0);
        }
        CHECK(water > 0 && lowest == -float(p.depth));
    }
    // the shore: the rounded outline's points (where the level comes from), each the first of its ground places
    {
        const auto shore = shore_places(p, {1000, 2000}), places = ground_places(p, {1000, 2000});
        CHECK(!shore.empty() && places.size() == shore.size() * 6);
        for (size_t i = 0; i < shore.size(); i++) CHECK(places[i * 6] == shore[i]);
        Params river_like = p;
        river_like.kind = "river";
        CHECK(shore_places(river_like, {}).empty());
    }
    // the parameters ride along and come back as they were
    Params back;
    CHECK(params_from_glb(b.main, back, why) && params_text(back) == params_text(p));
    CHECK(!params_from_glb(b.surface, back, why));
    // without foam: no coast foam on the surface
    Params plain = p;
    plain.foam = false;
    Built c;
    problems.clear();
    CHECK(build(plain, {1000, 2000}, 100, c, problems) && valid(c.surface, false, info, why) && info.material_count == 2);
    // the same input, the same bytes; a changed depth, other bytes
    Built again;
    problems.clear();
    CHECK(build(p, {1000, 2000}, 100, again, problems) && again.main == b.main && again.surface == b.surface);
    Params deeper = p;
    deeper.depth = 300;
    problems.clear();
    CHECK(build(deeper, {1000, 2000}, 100, again, problems) && again.main != b.main && again.surface == b.surface);
    // the bytes every platform makes (water.h: whole 1/1024 metres, stored DEFLATE, JSON by hand)
    std::printf("pond main %016llx surface %016llx (%zu, %zu bytes)\n", (unsigned long long)fnv(b.main), (unsigned long long)fnv(b.surface), b.main.size(), b.surface.size());
    CHECK(fnv(b.main) == POND_MAIN && fnv(b.surface) == POND_SURFACE);
    // problems: too few points, a crossing outline, too small, too many points
    Params bad = p;
    bad.points = {{0, 0}, {100, 0}};
    problems.clear();
    CHECK(!build(bad, {}, 0, again, problems) && errors(problems) && problems[0].field == "points");
    bad.points = {{0, 0}, {1000, 1000}, {1000, 0}, {0, 1000}};
    problems.clear();
    CHECK(!build(bad, {}, 0, again, problems) && problems[0].message.find("crosses") != std::string::npos);
    bad.points = {{0, 0}, {50, 0}, {50, 50}};
    problems.clear();
    CHECK(!build(bad, {}, 0, again, problems) && problems[0].message.find("too small") != std::string::npos);
    bad.points.clear();
    for (int i = 0; i < 60; i++) bad.points.push_back({int(1000 * std::cos(i * 0.1047)), int(1000 * std::sin(i * 0.1047))});
    problems.clear();
    CHECK(!build(bad, {}, 0, again, problems) && problems[0].message.find("at most 48") != std::string::npos);
    // a small star-shaped outline (clicked round a little hollow) has no room for wave rings: its surface leaves the empty
    // "waves" part out with its material (the converter reads each material as a part: an unused one failed it)
    {
        Params star = p;
        star.foam = false;
        star.rim = 6;
        star.points = {{210, 40}, {101, 81}, {15, 135}, {-60, 118}, {-64, 36}, {-215, -7}, {-69, -50}, {-39, -162}, {72, -152}, {122, -79}};
        Built sb;
        problems.clear();
        CHECK(build(star, {}, 0, sb, problems) && valid(sb.surface, false, info, why));
        const Value sd = doc_of(sb.surface);
        CHECK((material_names(sd) == std::vector<std::string>{"surface"}));
        std::vector<bool> used(sd.get("materials").array.size(), false);
        for (const auto& mesh : sd.get("meshes").array)
            for (const auto& prim : mesh.get("primitives").array) used[size_t(prim.get("material").number)] = true;
        for (bool u : used) CHECK(u);
    }
    // a concave outline (a bean) builds, with its shore ring inside
    Params bean = p;
    bean.points = {{-800, -300}, {800, -300}, {800, 300}, {200, 300}, {0, 50}, {-200, 300}, {-800, 300}};
    problems.clear();
    CHECK(build(bean, {}, 0, again, problems) && valid(again.main, true, info, why) && valid(again.surface, false, info, why));
}

void river() {
    Params p = river_params();
    const XZ origin{5000, -3000};
    // the ground: 100 along the first section, falling 300 at once after x 1100 (a cliff), then flat
    const auto places = ground_places(p, origin);
    // the centre line first (every 25 units), then at each of those both edges and five places beyond each bank
    CHECK(places.front() == XZ({5000, -3000}) && places.size() % (1 + 2 * 6) == 0);
    for (const auto& q : places) p.ground.push_back({q.x, q.z, q.x - origin.x > 1100 ? -200 : 100});
    Built b;
    std::vector<Problem> problems;
    CHECK(build(p, origin, 100, b, problems));
    for (const auto& e : problems) std::fprintf(stderr, "river: %s %s\n", e.severity.c_str(), e.message.c_str());
    CHECK(problems.empty() && b.material == "river");
    // levels: deep water 150 above the bed (local: ground 100 - 100 = 0), shallow 30 in the second section, never up
    CHECK(b.levels.size() < places.size() && b.levels.size() >= 13 && b.levels.front() == 150);  // rows every 150 units and at the steps
    for (size_t k = 1; k < b.levels.size(); k++) CHECK(b.levels[k] <= b.levels[k - 1]);
    // two steps: where the deep water meets the shallow section (150 -> 30 above the same ground), and at the cliff
    // (found to 25 units: the ground is asked every 25 units along the line; the drop is between x 1100 and 1125)
    CHECK(b.steps.size() == 2 && b.steps[0].top == 150 && b.steps[0].bottom == 30 && b.steps[0].ground == 0 && b.steps[0].section == 0);
    CHECK(b.steps.size() == 2 && b.steps[1].top == 30 && b.steps[1].bottom == -270 && b.steps[1].ground == -300 && b.steps[1].section == 1);
    CHECK(b.steps.size() == 2 && std::abs((b.steps[1].left.x + b.steps[1].right.x) / 2 - 1100) <= 25);
    static_models::ModelInfo info;
    std::string why;
    CHECK(valid(b.main, true, info, why) && info.material_count == 3);
    CHECK(valid(b.surface, false, info, why) && info.triangle_count == b.surface_triangles && info.material_count == 2);
    const Value doc = doc_of(b.main);
    // the bed and banks of each section (each painted with the island's ground along it), then the collision
    CHECK((material_names(doc) == std::vector<std::string>{"bank1", "bank2", "bank3", "col_water", "col_shallow", "col_sand", "col_grass", "col_stone"}));
    CHECK((material_names(doc_of(b.surface)) == std::vector<std::string>{"surface", "edge"}));
    std::printf("river main %016llx surface %016llx\n", (unsigned long long)fnv(b.main), (unsigned long long)fnv(b.surface));
    CHECK(fnv(b.main) == RIVER_MAIN && fnv(b.surface) == RIVER_SURFACE);  // the same bytes on every platform
    // a missing ground answer: refused with the place
    Params lost = p;
    lost.ground.erase(lost.ground.begin() + 1);  // a place on the centre line
    problems.clear();
    CHECK(!build(lost, origin, 100, b, problems) && problems.back().message.find("no ground under the river") != std::string::npos);
    // a ledge in a cliff (two drops 50 units apart): one step, from the upper water past the ledge to the lower
    Params ledge = river_params();
    ledge.shallow = {false, false, false};
    for (const auto& q : places) ledge.ground.push_back({q.x, q.z, q.x - origin.x > 1150 ? -400 : q.x - origin.x > 1100 ? -150 : 100});
    problems.clear();
    CHECK(build(ledge, origin, 100, b, problems) && b.steps.size() == 1 && b.steps[0].top == 150 && b.steps[0].bottom == -350 && b.steps[0].ground == -500);
    // uphill: a warning (the water stays level)
    Params up = river_params();
    for (const auto& q : places) up.ground.push_back({q.x, q.z, 100 + (q.x - origin.x) / 2});
    problems.clear();
    CHECK(build(up, origin, 100, b, problems) && problems.size() == 1 && problems[0].severity == "warning" && problems[0].message.find("uphill") != std::string::npos);
    // too sharp a turn, points too close
    Params sharp = river_params();
    sharp.points = {{0, 0}, {600, 0}, {0, 100}};
    CHECK(errors(check(sharp)));
    sharp.points = {{0, 0}, {20, 0}};
    CHECK(errors(check(sharp)));
}

void waterfall() {
    Params p;
    p.kind = "waterfall";
    p.points = {{-300, 0}, {300, 0}};
    p.top = 0, p.bottom = -900, p.side = 1;
    Built b;
    std::vector<Problem> problems;
    CHECK(build(p, {}, 0, b, problems) && problems.empty() && b.material == "waterfall");
    std::printf("waterfall main %016llx surface %016llx\n", (unsigned long long)fnv(b.main), (unsigned long long)fnv(b.surface));
    CHECK(fnv(b.main) == FALL_MAIN && fnv(b.surface) == FALL_SURFACE);
    static_models::ModelInfo info;
    std::string why;
    // the sheet (2 x 3 quads), the foam over the lip, the ring of foam round its foot (12 quads)
    CHECK(valid(b.main, true, info, why) && valid(b.surface, false, info, why) && b.surface_triangles == 12 + 2 + 24);
    CHECK((material_names(doc_of(b.main)) == std::vector<std::string>{"bank", "col_waterfall", "col_wall"}));
    CHECK((material_names(doc_of(b.surface)) == std::vector<std::string>{"surface", "edge"}));
    // it spans the fall (the sheet must stay inside the main model's bounds plus 512)
    CHECK(b.min[1] <= -900 && b.max[1] >= 2);
    // the foot to ask the ground at: in front of the edge, on the falling side
    const auto places = ground_places(p, {});
    CHECK(places.size() == 3 && places[2].x == 0 && places[2].z == -150);
    p.side = -1;
    CHECK(ground_places(p, {})[2].z == 150);
    p.bottom = -40;
    CHECK(errors(check(p)));
    p.bottom = -900;
    p.points = {{0, 0}, {50, 0}};
    CHECK(errors(check(p)));
}

void parameters() {
    // world parameters into local ones and back
    Params p;
    std::string why;
    CHECK(parse("pond", json(R"({"points": [{"x": 1000, "y": 5, "z": 2000}, {"x": 1400, "z": 2000}, [1400, 2400]], "depth": 120, "rim": 10, "foam": true, "floor": "#102030"})"), {1200, 2100}, 300, p, why));
    CHECK(p.points.size() == 3 && p.points[0] == XZ({-200, -100}) && p.points[2] == XZ({200, 300}) && p.depth == 120 && p.rim == 10 && p.foam && p.floor == Colour({0x10, 0x20, 0x30}));
    const Value w = to_value(p, {1200, 2100}, 300);
    CHECK(w.get("level").number == 300 && w.get("points").array[2].get("x").number == 1400 && w.get("floor").string() == "#102030");
    CHECK(!parse("lake", json(R"({"points": []})"), {}, 0, p, why));
    CHECK(!parse("pond", json(R"({"points": [{"x": 1}]})"), {}, 0, p, why));
    CHECK(!parse("pond", json(R"({"points": [], "depth": 5000})"), {}, 0, p, why) && why.find("depth") != std::string::npos);
    CHECK(!parse("pond", json(R"({"points": [], "floor": "green"})"), {}, 0, p, why));
    CHECK(parse("river", json(R"({"points": [[0,0],[100,0],[200,0]], "shallow": true})"), {}, 0, p, why) && p.shallow == std::vector<bool>({true, true}));
    CHECK(parse("river", json(R"({"points": [[0,0],[100,0],[200,0]], "shallow": [true]})"), {}, 0, p, why) && p.shallow == std::vector<bool>({true, false}));
    CHECK(parse("waterfall", json(R"({"points": [[0,0],[300,0]], "top": 500, "bottom": -100, "side": -1})"), {150, 0}, 500, p, why) && p.top == 0 && p.bottom == -600 && p.side == -1);
    CHECK(!parse("waterfall", json(R"({"points": [[0,0],[300,0]], "side": 2})"), {}, 0, p, why));
    // the stored text reads back
    Params back;
    CHECK(params_from_text(params_text(p), back, why) && params_text(back) == params_text(p));
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--write") {
        Built b;
        std::vector<Problem> problems;
        auto save = [&](const std::string& name, const std::vector<uint8_t>& bytes) {
            std::ofstream f(std::string(argv[2]) + "/" + name, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        };
        Params pond = pond_params();
        pond.room = 44;  // on Outset's beach: its banks read the island's sand
        if (build(pond, {-204100, 314550}, 250, b, problems)) save("pond-main.glb", b.main), save("pond-surface.glb", b.surface);
        Params r = river_params();
        for (const auto& q : ground_places(r, {})) r.ground.push_back({q.x, q.z, q.x > 1100 ? -200 : 100});
        if (build(r, {}, 100, b, problems)) save("river-main.glb", b.main), save("river-surface.glb", b.surface);
        Params w;
        w.kind = "waterfall", w.points = {{-300, 0}, {300, 0}}, w.top = 0, w.bottom = -900;
        if (build(w, {}, 0, b, problems)) save("waterfall-main.glb", b.main), save("waterfall-surface.glb", b.surface);
        for (const auto& e : problems) std::fprintf(stderr, "%s: %s\n", e.severity.c_str(), e.message.c_str());
        return 0;
    }
    pieces();
    pond();
    river();
    waterfall();
    parameters();
    if (g_failures) {
        std::fprintf(stderr, "editor_water_test: %d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("editor_water_test: ok");
    return 0;
}
