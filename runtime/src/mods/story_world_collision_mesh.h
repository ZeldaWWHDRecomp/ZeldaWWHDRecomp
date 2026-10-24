// Original bounded collision interchange and native table construction.
#pragma once
#include "story_world_collision_budget.h"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace story_world_collision::mesh {
// A body's triangles and vertices (each at most 8192; the 512 KiB storage budget of a body, mod_actor's
// collision_geometry, is the closer limit for a large mesh: about 6000 welded triangles).
inline constexpr uint32_t MaxVertices=8192,MaxTriangles=8192,LeafTriangles=32;
// A WCOL file's size (header, vertex and triangle records) and the largest valid one: 163880 bytes.
inline constexpr uint64_t wcol_bytes(uint64_t vertices,uint64_t triangles){return 40+12*vertices+8*triangles;}
inline constexpr uint64_t MaxWcolBytes=wcol_bytes(MaxVertices,MaxTriangles);
// A body's size: every collision-local coordinate within +/-MaxExtent and its extent along each axis at most
// MaxExtent, one sea square (100000 units, 1 km). The game's own collision has no size limit below that: cBgW
// keeps no grid, only per-node boxes it computes from the vertices (bounds start at +/-1e9, 1 unit margin),
// and the sea rooms' own bodies span a whole square (100000 x 100000, up to 45639 tall). Placement keeps a
// body inside its room's square (story_world_placement: room-boundary).
inline constexpr float MaxExtent=100000;
// WCOL surfaces (semantic): 0 stone, 1 grass, 2 wood, 3 sand, 4 metal, 5 wall, 6 objects, 7 climb,
// 8 hookshot, 9 water (a swimmable surface: its own depth-two group with the game's water bit 0x100, which
// only the game's water queries see; Link and the ship pass through it), 10 shallow water (wading),
// 11 waterfall, 12 ice, 13 void (a fall that sends Link back to the room's restart point), 14 lava (its surface:
// a depth-two group of its own with the game's lava bit 0x200, which only the game's lava checks see, plus the
// lava's bottom LavaDepth below it in the ordinary group, as the game's own lava floors are built).
inline constexpr uint16_t SurfaceWater=9,SurfaceVoid=13,SurfaceLava=14,Surfaces=15;
// The attribute rows of a body without lava: the layout before lava, byte for byte (a mesh without lava keeps the
// collision, and so the identity, it had; a full save state made before keeps working).
inline constexpr uint16_t SurfacesBeforeLava=14;
// The lava's bottom below its surface (game units): the game's own lava floors have theirs 85 to 500 units down
// (Fire Mountain's inside 0 to 100, Dragon Roost Cavern 85 to 500); dropped things sink to it and burn.
inline constexpr float LavaDepth=100;
// A lava row's info words, as every lava floor of the game has them (Dragon Roost Cavern, the inside of Fire
// Mountain, the lava caves, Ganon's Tower): no exit, material sound 23 (the lava's), attribute code 6, camera words
// 0xFFFFFF00. The attribute code is written from the game's conversion (class 6) as every other row's.
inline constexpr uint32_t LavaInfo0=0x07FFF7FF,LavaInfo2=0xFFFFFF00;
// Per surface, the attribute class the game gives its triangles (dBgS::GetAttributeCode, the game's raw-code
// conversion): stone 3, grass 4, wood 2, sand 11, metal 20, wall/objects/climb 0, hookshot 2 (wooden
// sounds), water 0 (as the islands' ponds), shallow 19, waterfall 23, ice 15, void 8, lava 6 (dBgS_Attr_LAVA: Link
// burns, jumps and is sent back; dropped things burn). The raw code written for a class is found in the game's own
// conversion.
inline constexpr std::array<uint8_t,Surfaces> NativeResultIds{3,4,2,11,20,0,0,0,2,0,19,23,15,8,6};
// The raw attribute code per surface from the game's conversion (32 entries, raw code -> class): the
// code that is its own class when there is one (metal: the code Islet of Steel uses), else the first
// code of that class. False when a class has no code.
template<size_t N> inline bool native_attributes(const uint32_t (&conversion)[32],const std::array<uint8_t,N>& classes,std::array<uint8_t,N>& raw){
    for(size_t i=0;i<N;++i){const uint32_t want=classes[i];int code=want<32&&conversion[want]==want?int(want):-1;
        for(unsigned c=0;c<32&&code<0;++c)if(conversion[c]==want)code=int(c);
        if(code<0)return false;raw[i]=uint8_t(code);}
    return true;
}
struct Point {float x=0,y=0,z=0;};
struct Triangle {std::array<uint16_t,3> indices{};uint16_t semantic=0;};
struct Mesh {std::vector<Point> vertices;std::vector<Triangle> triangles;Point minimum,maximum;};
// Shapes are centered at the origin; box size is full dimensions. Cylinder: 16 sides; sphere: 16 sectors, 8 latitude divisions.
bool make_shape(const std::string& shape,Point size,float radius,float height,uint16_t semantic,Mesh&,std::string& why);
bool validate(const Mesh&,std::string& why);  // the native counts (lava's bottom counts again) within the limits too
// How many vertices and triangles the native collision has: the mesh's, plus the lava's bottom (a copy of every lava
// triangle and of its vertices, LavaDepth lower).
struct NativeSize {size_t vertices=0,triangles=0,lava=0;};
NativeSize native_size(const Mesh&);
bool decode(std::span<const uint8_t>,Mesh&,std::string& why);
Counts counts(const Mesh&);
uint64_t allocation_bytes(const Mesh&); // source + native arrays/body, cookies/padding
// raw_attributes: one per surface (Surfaces); a mesh without lava writes the first SurfacesBeforeLava of them.
bool build_native(const Mesh&,const std::array<uint8_t,Surfaces>& raw_attributes,uint16_t room,std::vector<uint8_t>& relative_dzb,std::string& why);
bool relocate(std::vector<uint8_t>& relative_dzb,uint32_t guest_base,std::string& why);
// The first native layout (nine attributes, no water group), byte for byte: a full save state made before the
// surfaces water to void holds collision built this way. False for a mesh using a later surface.
bool build_native_v1(const Mesh&,const std::array<uint8_t,9>& raw_attributes,uint16_t room,std::vector<uint8_t>& relative_dzb,std::string& why);
}
