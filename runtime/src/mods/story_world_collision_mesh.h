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
inline constexpr std::array<uint8_t,9> NativeResultIds{3,4,2,11,9,0,0,0,2};
struct Point {float x=0,y=0,z=0;};
struct Triangle {std::array<uint16_t,3> indices{};uint16_t semantic=0;};
struct Mesh {std::vector<Point> vertices;std::vector<Triangle> triangles;Point minimum,maximum;};
// Shapes are centered at the origin; box size is full dimensions. Cylinder: 16 sides; sphere: 16 sectors, 8 latitude divisions.
bool make_shape(const std::string& shape,Point size,float radius,float height,uint16_t semantic,Mesh&,std::string& why);
bool validate(const Mesh&,std::string& why);
bool decode(std::span<const uint8_t>,Mesh&,std::string& why);
Counts counts(const Mesh&);
uint64_t allocation_bytes(const Mesh&); // source + native arrays/body, cookies/padding
bool build_native(const Mesh&,const std::array<uint8_t,9>& raw_attributes,uint16_t room,std::vector<uint8_t>& relative_dzb,std::string& why);
bool relocate(std::vector<uint8_t>& relative_dzb,uint32_t guest_base,std::string& why);
}
