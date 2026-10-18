// Original handmade W0 geometry. This does not convert models or game assets.
#pragma once
#include <array>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>
namespace story_world_collision::proof {
enum class Kind:uint32_t {None,Platform,Buoy,Raft};
struct Point {float x,y,z;};
struct Mesh {std::vector<Point> vertices;std::vector<std::array<uint16_t,3>> triangles;std::vector<uint16_t> attributes;};
inline void prism(Mesh&m,float half_x,float near_z,float far_z,float bottom,float near_top,float far_top){
    const auto base=uint16_t(m.vertices.size());
    m.vertices.insert(m.vertices.end(),{{-half_x,bottom,near_z},{half_x,bottom,near_z},{half_x,bottom,far_z},{-half_x,bottom,far_z},
        {-half_x,near_top,near_z},{half_x,near_top,near_z},{half_x,far_top,far_z},{-half_x,far_top,far_z}});
    constexpr uint16_t faces[][3]={{0,1,2},{0,2,3},{4,6,5},{4,7,6},{0,5,1},{0,4,5},{1,6,2},{1,5,6},{2,7,3},{2,6,7},{3,4,0},{3,7,4}};
    for(const auto&f:faces){m.triangles.push_back({uint16_t(base+f[0]),uint16_t(base+f[1]),uint16_t(base+f[2])});m.attributes.push_back(0);}
}
inline Mesh mesh(Kind kind){
    Mesh m;
    if(kind==Kind::Platform){prism(m,300,-300,300,-150,0,0);prism(m,150,-900,-300,-170,-150,0);}
    if(kind==Kind::Buoy)prism(m,70,-70,70,-100,100,100);
    if(kind==Kind::Raft){
        prism(m,250,-350,350,-50,0,0);
        // Native ship wall circles sample discrete heights. Original vertical
        // object walls span those samples; Link and camera pass through them.
        Mesh envelope;prism(envelope,250,-350,350,-300,200,200);
        const auto base=uint16_t(m.vertices.size());m.vertices.insert(m.vertices.end(),envelope.vertices.begin(),envelope.vertices.end());
        for(size_t i=4;i<envelope.triangles.size();++i){const auto&t=envelope.triangles[i];m.triangles.push_back({uint16_t(base+t[0]),uint16_t(base+t[1]),uint16_t(base+t[2])});m.attributes.push_back(1);}
    }
    return m;
}
// Native collision tables use big-endian guest addresses. All inputs here are
// original fixed proof geometry; the caller supplies the allocated guest base.
inline std::vector<uint8_t> encode(const Mesh&m,uint32_t base,uint16_t room){
    if(m.vertices.empty()||m.triangles.empty())return {};
    const uint32_t attributes=std::find(m.attributes.begin(),m.attributes.end(),1)!=m.attributes.end()?2:1;
    const uint32_t vertex=0x34,tri=vertex+uint32_t(m.vertices.size())*12;
    const uint32_t block=tri+uint32_t(m.triangles.size())*10,node=(block+2+3)&~3u,group=node+20,attribute=group+52,name=attribute+16*attributes;
    std::vector<uint8_t> out(name+4);
    auto u16=[&](uint32_t at,uint16_t value){out[at]=uint8_t(value>>8);out[at+1]=uint8_t(value);};
    auto u32=[&](uint32_t at,uint32_t value){for(unsigned i=0;i<4;++i)out[at+i]=uint8_t(value>>(24-8*i));};
    auto f32=[&](uint32_t at,float value){u32(at,std::bit_cast<uint32_t>(value));};
    const uint32_t count[]={uint32_t(m.vertices.size()),uint32_t(m.triangles.size()),1,1,1,attributes};
    const uint32_t offset[]={vertex,tri,block,node,group,attribute};
    for(unsigned i=0;i<6;++i){u32(i*8,count[i]);u32(i*8+4,base+offset[i]);}u32(0x30,0x80000000u);
    for(size_t i=0;i<m.vertices.size();++i){const auto&p=m.vertices[i];f32(vertex+uint32_t(i)*12,p.x);f32(vertex+uint32_t(i)*12+4,p.y);f32(vertex+uint32_t(i)*12+8,p.z);}
    for(size_t i=0;i<m.triangles.size();++i)for(unsigned j=0;j<3;++j)u16(tri+uint32_t(i)*10+2*j,m.triangles[i][j]);
    for(size_t i=0;i<m.attributes.size();++i)u16(tri+uint32_t(i)*10+6,m.attributes[i]);
    u16(node,1);u16(node+2,0xFFFF);for(unsigned i=1;i<8;++i)u16(node+4+i*2,0xFFFF);
    u32(group,base+name);for(unsigned i=0;i<3;++i)f32(group+4+i*4,1);
    u16(group+0x24,0xFFFF);u16(group+0x26,0xFFFF);u16(group+0x28,0xFFFF);u16(group+0x2A,room);
    // Unset native references are sentinels, not zero. Exit zero would trigger
    // the room's first transition as soon as Link stands on the mesh.
    u32(attribute,0x07FFE0FFu); // camera FF, exit 3F, polygon color FF; material 0
    u32(attribute+4,0x000000FFu); // no Link reference; ordinary wall/ground/attribute codes
    u32(attribute+8,0xFFFFFFFFu); // no moving camera, room camera, path or path point
    if(attributes==2){for(unsigned i=0;i<12;++i)out[attribute+16+i]=out[attribute+i];
        u32(attribute+16+12,0xED); // camera/Link/projectiles/rope through; object queries still collide
    }
    out[name]='w';out[name+1]='0';return out;
}
}
