// Conservative placement queries. Native resource data stays on the player's machine.
#pragma once
#include <functional>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace story_world_placement {
struct Point {float x=0,y=0,z=0;};
struct Bounds {Point minimum,maximum;};
// a room collision triangle (kind "collision") also keeps its corners (`triangle`), so an object's own mesh can be
// checked against the triangle itself rather than its bounds
struct ProtectedBounds {std::string stage;int room=-1;std::string kind,id;Bounds bounds;bool triangle=false;std::array<Point,3> corners{};};
// user: who follows the route when the layout says so ("boat": a boat of the island sails it, d_a_obj_ikada), else "";
// radius: the corridor's half width (kRouteHalfWidth for a walker, kBoatHalfWidth for a boat)
struct ProtectedRoute {std::string stage;int room=-1;std::string id;std::vector<Point> points;float radius=0;bool closed=false;std::string user;};
struct Coverage {std::string stage;int room=-1;bool collision=false,routes=false,events=false;std::string reason;};
struct CoverageRegion {std::string stage;int room=-1;Bounds bounds;};
// One entry of the room's own layout (add_native_layout): what world objects and island buildings keep clear of
// (check_building). The position is in world space; scale is the layout's three bytes (scaled tags only).
struct LayoutEntry {std::string stage;int room=-1;std::string tag,name;Point position;uint8_t scale[3]{};bool scaled=false;unsigned index=0;uint32_t params=0;int16_t angle_x=0;};
// Everything is in world space, as the game stores it: bounds (the room's collision triangles, and what the
// caller adds: other world objects, the ship), routes (the layout's walking routes, kRouteHalfWidth wide),
// layout (actors, tags, doors, chests, start points). One index serves world objects and island buildings.
struct Index {std::vector<ProtectedBounds> bounds;std::vector<ProtectedRoute> routes;std::vector<Coverage> coverage;std::vector<CoverageRegion> regions;std::vector<LayoutEntry> layout;};
// message: plain words for mod makers ("a door of the island"); native: the game's own name when known ("KNOB00D")
// move: for a single small thing, how far (game units, horizontally) the bounds must move away from it to be clear;
// 0 when it can't be said (a route crossing it, collision, coverage)
// at/reach/path: where the thing in the way is, for the editor to draw it (a layout entry: its place and reach;
// a route: its points)
// triangles: with an object's own collision (check() with a solid), its triangles that are in the way (indices into the
// Solid's triangles, ascending): those crossing the island's walls or ground, or rising into a layout entry's or a
// route's space; the editor tints them on the model (issue #281)
struct Conflict {std::string kind,id,message,native;float move=0;Point at{};float reach=0;std::vector<Point> path;std::vector<uint32_t> triangles;};
bool valid(const Bounds&);
// Degrees, with the native actor's RY * RX * RZ rotation order.
bool transform_bounds(const Bounds&,Point position,Point rotation,Point scale,Bounds&);
// Everything in the way of world bounds: coverage and the room's square, the room's walls and ground (one
// finding; resting on a floor is fine), other bounds the caller added, and what the layout needs kept clear
// (check_building), each with `clearance` around it.
// With `solid` (the object's own collision in world space, its bounds the Bounds given; issue #281), what the bounds
// reach is checked against the object's own triangles: other bounds by their distance to its triangles (the same
// clearance); the room's walls and ground by a real crossing above the object's base plus kGroundStep, as
// solids_overlap, where new ground that meets the island's ground at its height (kFlushStep) doesn't count; a
// layout entry and a route by the space they need above their floor (layout_space, route_space). An L-shaped object
// whose box covers a villager it doesn't reach is then clear, and so is new ground joined flush to an island's hill
// next to a villager. Coverage and the room's square stay checked by the bounds.
inline constexpr float kClearance=50;
struct Solid;
std::vector<Conflict> check(const std::string& stage,int room,const Bounds&,const Index&,float clearance=kClearance,const Solid* solid=nullptr);
struct SweepResult {Point allowed_delta;std::vector<Conflict> conflicts;bool blocked=false;};
// Continuous horizontal motion of a fixed conservative wave/tilt envelope.
// A nonzero vertical delta is refused. Earliest contact stops the blocked axis;
// a second continuous query permits sliding along an unblocked axis. Unknown
// coverage, initial overlap and malformed dynamic obstacles stop all movement.
SweepResult sweep(const std::string& stage,int room,const Bounds& start,Point desired_delta,const Index&,float clearance=kClearance);
// ---- two world objects' own collision against each other (mod_actor's placement query): an object may stand on
// another's walkable top (an island ground, a platform, a pier) and lean against its slopes as the islands' own
// buildings do; only a real overlap of their solid geometry counts. Triangles in world space.
struct Solid {std::vector<Point> vertices;std::vector<std::array<uint16_t,3>> triangles;Bounds bounds;};
// how far an object may reach into the ground it stands on (a slope under a house's floor, a step)
inline constexpr float kGroundStep=150;
// how far two solids' triangles must go through each other to count as crossing (solids_overlap, water_blocked, and the
// room's walls and ground in check()): geometry that only touches, as new ground whose corners are exactly the island's
// own corners (issue #281), is not in the way. A quarter unit (a few millimetres in the game) is 8 float roundings
// (ulp 1/32) of coordinates out to 262144 and 4 out to 524288, beyond every sea square, so a model placed by its own
// origin and rotation, or exported with float coordinates, still meets the island's corners within it; real overlaps are
// tens of units (a wall circle is 30 above the feet, kFlushStep).
inline constexpr float kTouch=.25f;
// true when a triangle of one crosses a triangle of the other above the higher of the two bases plus `step`:
// resting on (or slightly into) the other's top doesn't count, a cliff, pillar, wall or roof through it does.
// A solid wholly inside the other without any crossing is not found (the caller's bounds rules still apply).
bool solids_overlap(const Solid& a,const Solid& b,float step=kGroundStep);
// ---- a body of water (the editor's pond, river or waterfall) against an object it lies on or in (an island ground, a
// terrace): `water` is the body's open water only (its swimming, wading and splash surfaces). The object's ground under
// the water and its ground and walls where the body's floor and banks meet them are where the water lies, as on an
// island's own ground; in the way is only what rises through the water (a wall, a pillar, a house, ground higher than
// the water inside its shore): a crossing of the open water raised by `step`.
inline constexpr float kWaterStep=50;
bool water_blocked(const Solid& water,const Solid& other,float step=kWaterStep);
// The highest ground of a solid under `above` (a triangle facing up, at or below above.y): its height. False: none.
inline constexpr float kGroundFacing=0.25f;  // the least upward share of a ground triangle's normal (about 75 degrees)
// ---- flush ground (issue #281): the game's own measures of what is walked onto
// a floor triangle: the upward share of its normal at least 0.5 (60 degrees), as the game sorts its collision
// (cBgW_CheckBGround: ground ny >= 0.5, roof ny < -0.8, the rest wall)
inline constexpr float kWalkableFacing=0.5f;
// how far new ground may stand above the island's ground (or an actor's floor) and still meet it flush: the step that
// Link and the villagers walk over without a wall in their way (their lowest wall circles are 30 units above their
// feet: d_a_player SetWallH(30.1), d_a_npc_people and d_a_npc_tt SetWall(30, ...); the ground probe starting 60 above the
// feet, dBgS_Acch m_ground_check_offset, then steps them onto it)
inline constexpr float kFlushStep=30;
// a boat's route (d_a_obj_ikada, Beedle's shop ship "Terry" around Windfall): the ship's own reach, its hit sphere of
// 300 (l_HIO.m9C, the sphere's centre on the water line) to either side and under the water, and up to where the game
// marks it (the attention point 900 above it, l_HIO.mShipOffsY_Attention)
inline constexpr float kBoatHalfWidth=300,kBoatDraft=300,kBoatHeight=900;
bool ground_under(const Solid&,Point above,float& height);
// a local collision mesh placed in the world (position, rotation in degrees as transform_bounds, scale)
Solid place_solid(const std::vector<Point>& local,const std::vector<std::array<uint16_t,3>>& triangles,Point position,Point rotation,Point scale);
// Bounded readers of original stage resources. Invalid or unsupported input never grants coverage.
// The data is world space as stored (a stage's MULT entries place rooms on the sea chart and turn the
// minimap; they do not move a room's collision or layout), so the readers move nothing.
bool add_native_collision(Index&,const std::string& stage,int room,const std::vector<uint8_t>& dzb,std::string* why=nullptr);
bool add_native_layout(Index&,const std::string& stage,int room,const std::vector<uint8_t>& dzr,std::string* why=nullptr);
// Bytes per entry of a layout table (dStage chunk handlers): 32 for ACTR/ACT0-b, TGOB, TRES/TRE0-b and PLYR;
// 36 for SCOB/SCO0-b, TGSC, TGDR and DOOR (a u8[3] scale at 32); 0 for a table that is not one of these.
unsigned table_stride(const std::string& tag);
// Load loose or packed stage archives and their BFRES embedded DZB/DZR files.
// Never writes, exports or caches native bytes. A returned index may have incomplete coverage.
Index load_native_room(const std::string& game_directory,const std::string& stage,int room);
// Stage archives that are not in the game's files (the mod stages, mod_stages.h): a stage file name
// ("<name>_Room0.szs") -> a local file, or "". Consulted before the game's files.
void set_stage_archive_source(std::function<std::string(const std::string&)> source);
// ---- what the room's layout needs kept clear, by its real extent where it is known (a tag area's own
// radius and height from its scale; a door, chest, villager, item, switch or start point by its kind) and a
// reasonable default for any other object. Grass, flowers, trees, palms, birds, crabs and the light and weather
// tags (kytag) are ignored: a world object or a building may stand over them.
enum class Keep {None,Villager,Chest,Door,Item,Switch,Event,Object,Start};
struct Extent {Keep keep=Keep::Object;float radius=150,below=50,above=400;};
inline constexpr float kRouteHalfWidth=60;    // a walking route's corridor half width (a villager follows its line; ~35 units wide)
inline constexpr float kBuildingMargin=50;    // added around the footprint (the same as kClearance)
Extent building_extent(const LayoutEntry&);
// ---- what an actor of the layout (a villager, a start point, a door, ...) and a route need with an object's own
// triangles (check() and check_building() with a solid): the space above their floor, within their reach (the extent's
// radius, the route's half width, plus the clearance) and up to their height (plus the clearance). New geometry is in
// the way where it rises more than kFlushStep above that floor and is not buried in the island's ground (at most
// kFlushStep above an up-facing room triangle near the floor's height: kGroundStep above it, the extent's `below`
// under it). Walkable ground flush with an actor's floor, or with the hill it stands on, beside it is fine; a wall, a
// raised part or a roof within its reach is not. A boat's route keeps its whole hull clear (kBoatDraft under the water
// line to kBoatHeight above), as the boat sails through whatever stands there.
const char* keep_words(Keep);
// Conflicts of world bounds with the room's layout entries and routes: kind is what is in the way in plain
// words ("villager", "villager route", "chest", "door", "item", "switch", "cutscene area", "start point",
// "object"), id the layout entry ("TGDR:KNOB00D:3"), message plain words, native the game's name. check()
// includes these; coverage is checked by check().
std::vector<Conflict> check_building(const std::string& stage,int room,const Bounds& footprint,const Index&,float margin=kBuildingMargin,const Solid* solid=nullptr);
}
