// SPDX-License-Identifier: MPL-2.0
// World editor: the water tool's geometry (docs/mod-editor.md "Water", docs/world-objects.md "Water").
//
// A pond (a closed outline at a water level, a floor `depth` below it, banks and an optional foam ring), a river (a
// centre line with a width that follows the ground and steps down where it drops; the point order is the flow) and a
// waterfall (a falling sheet from a top edge down to the ground) each become two original models:
//   - the main model: what is drawn of the water's ground (the pond's floor and banks, the river's bed, the rock
//     behind the waterfall and its splash pad), with the collision as `_col` meshes: col_water (swim), col_shallow
//     (wade), col_waterfall (the splash under the fall), col_sand (a pond's floor) and col_stone / col_wall (banks);
//   - the surface model: the water itself, drawn by the game's own water, river or waterfall material (the model is
//     the actor's "shoreline"; the converter fades it out towards its open edges).
// Each model is a GLB that the package keeps under models/water/ and the usual converter prepares.
//
// The bytes are the same on every platform: every input is a whole game unit, every output coordinate is a multiple
// of 1/1024 metre written as such (no float formatting, no locale), PNG textures use stored (uncompressed) DEFLATE
// blocks, and the JSON is written by hand. The tool's own parameters ride along in the main model's
// asset.extras.wwhd_water, so edit_water can rebuild the body from what the package holds.
//
// Pure code, no game dependencies (runtime/tools/editor_water_test.cpp).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../mods/mod_json.h"

namespace editor::water {
using mods::json::Value;

struct XZ {
    int x = 0, z = 0;
    bool operator==(const XZ&) const = default;
};
struct Colour {
    uint8_t r = 0x7a, g = 0x8c, b = 0x5c;  // a mossy floor
    bool operator==(const Colour&) const = default;
};
// The ground the game reported at a world place (x, z) (its height, whole units): the cache that keeps a rebuilt
// body where it was (a moved point is asked again, an unmoved one keeps its height).
struct Sample {
    int x = 0, z = 0, h = 0;
};

// Limits (docs/world-objects.md "Water")
inline constexpr unsigned kMaxPondPoints = 48, kMaxRiverPoints = 32;
inline constexpr int kMinDepth = 20, kMaxDepth = 480, kMinWidth = 100, kMaxWidth = 2000, kMaxRim = 300, kMaxFall = 6000;
inline constexpr int kShallowDepth = 30;   // a shallow river section's water above its bed
inline constexpr int kStepDrop = 60;       // a river steps down where its water would drop more than this between samples
inline constexpr int kSampleSpacing = 150; // a river's water is drawn in rows this far apart (and at each step)
inline constexpr int kFineSpacing = 25;    // its ground is asked of the game this often along the centre line
inline constexpr int kBedLift = 4;         // a river's bed (its wading ground) above the ground at the samples
inline constexpr unsigned kMaxSurfaceTriangles = 1024;  // a shoreline model's limit
inline constexpr unsigned kMaxCollisionVertices = 4096, kMaxCollisionTriangles = 4096;

// A body's parameters, as the tool keeps them (local: relative to the actor's position, which is the origin).
struct Params {
    std::string kind;          // "pond", "river" or "waterfall"
    std::vector<XZ> points;    // pond: the outline; river: the centre line (flow from first to last); waterfall: the top edge (2)
    int depth = 200;           // pond: floor below the level; river: water above the bed in its deep sections
    int rim = 0;               // pond: banks above the level
    int width = 400;           // river
    int top = 0, bottom = -600;  // waterfall: the top edge's and the ground's height (relative to the origin)
    int side = 1;              // waterfall: +1 the water falls to the right of points[0] -> points[1], -1 to the left
    bool foam = false;         // pond: a foam ring at the shore
    std::vector<bool> shallow; // river: per section (points[i] -> points[i+1]), wading water instead of swimming water
    Colour floor;
    std::vector<Sample> ground;  // world places and heights asked of the game (river beds, pond banks)
    int room = 0;                // the sea square (not stored: where the banks read the island's ground texture from)
};
// the shoreline material of a pond's surface (its kind in the game's own water, tools/story_mods/shoreline_model.py)
inline constexpr const char* kPondMaterial = "pond";
// What the tool found wrong (shown red when an error, else yellow).
struct Problem {
    std::string message, field, severity = "error";  // "error" or "warning"
    Value where;  // what is in the way: {"at": {x, y, z}, "reach"} or {"path": [...]} (placement findings)
};
// A river step (where a waterfall goes): the step's two bank points, its top and bottom height (local), the flow.
struct Step {
    XZ left, right;
    int top = 0, bottom = 0;  // the water's level above and below the step
    int ground = 0;           // the ground at the step's foot
    int section = 0;
};
struct Built {
    std::vector<uint8_t> main, surface;  // the GLBs
    std::string material;                // the surface's shoreline material: "pond", "river" or "waterfall"
    unsigned main_vertices = 0, main_triangles = 0, surface_vertices = 0, surface_triangles = 0;
    unsigned collision_vertices = 0, collision_triangles = 0;
    int min[3] = {0, 0, 0}, max[3] = {0, 0, 0};  // the main model's local bounds, whole units (rounded outwards)
    std::vector<Step> steps;             // river
    std::vector<int> levels;             // river: the water level at each row (local)
    uint64_t texture_bytes = 0;          // decoded RGBA of the main model's textures
    // the main model's collision as the game gets it (local, game units): what the placement check compares with
    // the world around it; each triangle's collision material ("col_water", "col_sand", ...)
    std::vector<std::array<float, 3>> collision_points;
    std::vector<std::array<uint16_t, 3>> collision_faces;
    std::vector<std::string> collision_materials;
};

// ---- parameters
// From editor command parameters (world points, `origin` the actor's place) into local Params, and back.
// `world` points are {x, z} or {x, y, z} objects (y ignored); waterfall's top / bottom are world heights.
bool parse(const std::string& kind, const Value& p, const XZ& origin, int origin_y, Params& out, std::string& why);
Value to_value(const Params& p, const XZ& origin, int origin_y);  // world form (get_water)
// The canonical text stored in a GLB (asset.extras.wwhd_water) and read back.
std::string params_text(const Params& p);
bool params_from_text(const std::string& text, Params& out, std::string& why);
// A GLB of the water tool: its parameters (false: not one of the tool's models).
bool params_from_glb(const std::vector<uint8_t>& glb, Params& out, std::string& why);

// ---- geometry
// Where the game's ground is needed (world places): pond banks, the river's centre line samples, the waterfall's foot.
std::vector<XZ> ground_places(const Params& p, const XZ& origin);
// Of those, a pond's shore (its rounded outline, where the water meets the bank): its level when none is given is its
// depth above the lowest ground there (not where the banks reach out to, which may be down a cliff or the sea floor).
std::vector<XZ> shore_places(const Params& p, const XZ& origin);
// Problems with the parameters themselves (shape, limits), before anything is built.
std::vector<Problem> check(const Params& p);
// Builds the two models. `ground` holds heights (world) for ground_places (missing: none). Problems are added to
// `problems` (an error: nothing is built, false).
bool build(const Params& p, const XZ& origin, int origin_y, Built& out, std::vector<Problem>& problems);

// ---- pieces (unit-tested on their own)
int64_t area2(const std::vector<XZ>& polygon);         // twice the signed area (x, z)
bool simple_polygon(const std::vector<XZ>& polygon);   // no repeated point, no two edges crossing or touching
// Ear clipping (exact integer predicates); triangles as index triples, each with the polygon's own orientation.
bool triangulate(const std::vector<XZ>& polygon, std::vector<unsigned>& triangles);
std::vector<uint8_t> png(unsigned width, unsigned height, const std::vector<uint8_t>& pixels, bool alpha);  // stored DEFLATE
uint32_t crc32(const uint8_t* data, size_t size);
std::string metres(int64_t k);  // k / 1024, exactly, as decimal text
}  // namespace editor::water
