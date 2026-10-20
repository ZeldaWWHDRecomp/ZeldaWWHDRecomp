// Original bounded collision interchange and native table construction.
#pragma once
#include "story_world_collision_budget.h"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace story_world_collision::mesh {
inline constexpr uint32_t MaxVertices=4096,MaxTriangles=4096,LeafTriangles=32;
inline constexpr float MaxExtent=20000;
// WCOL surfaces (semantic): 0 stone, 1 grass, 2 wood, 3 sand, 4 metal, 5 wall, 6 objects, 7 climb,
// 8 hookshot, 9 water (a swimmable surface: its own depth-two group with the game's water bit 0x100, which
// only the game's water queries see; Link and the ship pass through it), 10 shallow water (wading),
// 11 waterfall, 12 ice, 13 void (a fall that sends Link back to the room's restart point).
inline constexpr uint16_t SurfaceWater=9,SurfaceVoid=13,Surfaces=14;
// Per surface, the attribute class the game gives its triangles (dBgS::GetAttributeCode, the game's raw-code
// conversion): stone 3, grass 4, wood 2, sand 11, metal 20, wall/objects/climb 0, hookshot 2 (wooden
// sounds), water 0 (as the islands' ponds), shallow 19, waterfall 23, ice 15, void 8. The raw code written
// for a class is found in the game's own conversion.
inline constexpr std::array<uint8_t,Surfaces> NativeResultIds{3,4,2,11,20,0,0,0,2,0,19,23,15,8};
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
bool validate(const Mesh&,std::string& why);
bool decode(std::span<const uint8_t>,Mesh&,std::string& why);
Counts counts(const Mesh&);
uint64_t allocation_bytes(const Mesh&); // source + native arrays/body, cookies/padding
bool build_native(const Mesh&,const std::array<uint8_t,Surfaces>& raw_attributes,uint16_t room,std::vector<uint8_t>& relative_dzb,std::string& why);
bool relocate(std::vector<uint8_t>& relative_dzb,uint32_t guest_base,std::string& why);
// The first native layout (nine attributes, no water group), byte for byte: a full save state made before the
// surfaces water to void holds collision built this way. False for a mesh using a later surface.
bool build_native_v1(const Mesh&,const std::array<uint8_t,9>& raw_attributes,uint16_t room,std::vector<uint8_t>& relative_dzb,std::string& why);
}
