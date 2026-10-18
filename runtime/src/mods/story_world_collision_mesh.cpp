#include "story_world_collision_mesh.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
namespace story_world_collision::mesh { namespace {
uint16_t u16(std::span<const uint8_t>b,size_t p){return uint16_t((uint16_t(b[p])<<8)|b[p+1]);}
uint32_t u32(std::span<const uint8_t>b,size_t p){return (uint32_t(b[p])<<24)|(uint32_t(b[p+1])<<16)|(uint32_t(b[p+2])<<8)|b[p+3];}
float f32(std::span<const uint8_t>b,size_t p){return std::bit_cast<float>(u32(b,p));}
void put16(std::vector<uint8_t>&b,size_t p,uint16_t v){b[p]=uint8_t(v>>8);b[p+1]=uint8_t(v);}
void put32(std::vector<uint8_t>&b,size_t p,uint32_t v){for(unsigned i=0;i<4;++i)b[p+i]=uint8_t(v>>(24-8*i));}
void putf(std::vector<uint8_t>&b,size_t p,float v){put32(b,p,std::bit_cast<uint32_t>(v));}
float component(Point p,unsigned k){return k==0?p.x:k==1?p.y:p.z;}
bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
uint32_t leaves(uint32_t t){if(!t)return 0;return t<=LeafTriangles?1:leaves(t/2)+leaves(t-t/2);}
void bounds(Mesh&m){m.minimum=m.maximum=m.vertices.front();for(auto p:m.vertices){m.minimum={std::min(m.minimum.x,p.x),std::min(m.minimum.y,p.y),std::min(m.minimum.z,p.z)};m.maximum={std::max(m.maximum.x,p.x),std::max(m.maximum.y,p.y),std::max(m.maximum.z,p.z)};}}
}
bool validate(const Mesh&m,std::string&why){
    why.clear();if(m.vertices.empty()||m.vertices.size()>MaxVertices||m.triangles.empty()||m.triangles.size()>MaxTriangles){why="collision vertex or triangle limit exceeded";return false;}
    if(!finite(m.minimum)||!finite(m.maximum)){why="non-finite collision bounds";return false;}
    Mesh actual;actual.vertices=m.vertices;bounds(actual);
    for(auto p:m.vertices)if(!finite(p)||std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})>MaxExtent){why="non-finite or out-of-range collision-local vertex";return false;}
    for(unsigned k=0;k<3;++k){const float lo=component(m.minimum,k),hi=component(m.maximum,k);if(lo!=component(actual.minimum,k)||hi!=component(actual.maximum,k)||double(hi)-lo>MaxExtent){why="collision bounds mismatch or extent exceeds limit";return false;}}
    for(const auto&t:m.triangles){if(t.semantic>=9||t.indices[0]>=m.vertices.size()||t.indices[1]>=m.vertices.size()||t.indices[2]>=m.vertices.size()){why="invalid collision triangle index or surface";return false;}
        const auto a=m.vertices[t.indices[0]],b=m.vertices[t.indices[1]],c=m.vertices[t.indices[2]];
        const double x=double(b.x)-a.x,y=double(b.y)-a.y,z=double(b.z)-a.z,xx=double(c.x)-a.x,yy=double(c.y)-a.y,zz=double(c.z)-a.z;
        const double nx=y*zz-z*yy,ny=z*xx-x*zz,nz=x*yy-y*xx,norm=nx*nx+ny*ny+nz*nz;
        if(!std::isfinite(norm)||norm<=1e-12||std::max({std::abs(nx),std::abs(ny),std::abs(nz)})>std::numeric_limits<float>::max()){why="degenerate or numerically unsafe collision triangle";return false;}
    }return true;
}
bool make_shape(const std::string&shape,Point size,float radius,float height,uint16_t semantic,Mesh&out,std::string&why){
    out={};Mesh m;auto positive=[](float v){return std::isfinite(v)&&v>0&&v<=MaxExtent;};
    if(semantic>=6){why="invalid collision surface";return false;}
    auto triangle=[&](uint16_t a,uint16_t b,uint16_t c){m.triangles.push_back({{a,b,c},semantic});};
    constexpr unsigned sectors=16,rings=8;constexpr double pi=3.14159265358979323846;
    if(shape=="box"){
        if(!positive(size.x)||!positive(size.y)||!positive(size.z)){why="invalid box dimensions";return false;}
        const float x=size.x/2,y=size.y/2,z=size.z/2;m.vertices={{-x,-y,-z},{x,-y,-z},{x,-y,z},{-x,-y,z},{-x,y,-z},{x,y,-z},{x,y,z},{-x,y,z}};
        constexpr uint16_t f[][3]={{0,1,2},{0,2,3},{4,6,5},{4,7,6},{0,5,1},{0,4,5},{1,6,2},{1,5,6},{2,7,3},{2,6,7},{3,4,0},{3,7,4}};for(auto&t:f)triangle(t[0],t[1],t[2]);
    }else if(shape=="cylinder"){
        if(!positive(radius)||radius>MaxExtent/2||!positive(height)){why="invalid cylinder dimensions";return false;}
        for(unsigned layer=0;layer<2;++layer)for(unsigned k=0;k<sectors;++k){const double a=2*pi*k/sectors;m.vertices.push_back({float(radius*std::cos(a)),layer?height/2:-height/2,float(radius*std::sin(a))});}
        m.vertices.push_back({0,-height/2,0});m.vertices.push_back({0,height/2,0});
        for(unsigned k=0;k<sectors;++k){const uint16_t next=(k+1)%sectors;triangle(k,k+sectors,next);triangle(next,k+sectors,next+sectors);triangle(32,k,next);triangle(33,next+sectors,k+sectors);}
    }else if(shape=="sphere"){
        if(!positive(radius)||radius>MaxExtent/2){why="invalid sphere radius";return false;}
        m.vertices.push_back({0,radius,0});for(unsigned ring=1;ring<rings;++ring){const double a=pi*ring/rings;for(unsigned k=0;k<sectors;++k){const double b=2*pi*k/sectors;m.vertices.push_back({float(radius*std::sin(a)*std::cos(b)),float(radius*std::cos(a)),float(radius*std::sin(a)*std::sin(b))});}}
        const uint16_t bottom=uint16_t(m.vertices.size());m.vertices.push_back({0,-radius,0});
        for(unsigned k=0;k<sectors;++k){const uint16_t next=(k+1)%sectors;triangle(0,1+next,1+k);for(unsigned r=0;r<rings-2;++r){const uint16_t a=1+r*sectors+k,b=1+r*sectors+next,c=a+sectors,d=b+sectors;triangle(a,b,c);triangle(b,d,c);}triangle(bottom,1+(rings-2)*sectors+k,1+(rings-2)*sectors+next);}
    }else{why="unknown collision shape";return false;}
    bounds(m);if(!validate(m,why))return false;out=std::move(m);return true;
}
bool decode(std::span<const uint8_t>b,Mesh&out,std::string&why){
    out={};if(b.size()<40||u32(b,0)!=0x57434F4C||u32(b,4)!=1){why="invalid WCOL header";return false;}
    const uint32_t v=u32(b,8),t=u32(b,12);if(!v||v>MaxVertices||!t||t>MaxTriangles||b.size()!=40ull+12ull*v+8ull*t){why="invalid WCOL counts or byte length";return false;}
    Mesh m;m.minimum={f32(b,16),f32(b,20),f32(b,24)};m.maximum={f32(b,28),f32(b,32),f32(b,36)};
    for(uint32_t i=0;i<v;++i){const size_t p=40+12*i;m.vertices.push_back({f32(b,p),f32(b,p+4),f32(b,p+8)});}
    for(uint32_t i=0;i<t;++i){const size_t p=40+12*v+8*i;m.triangles.push_back({{u16(b,p),u16(b,p+2),u16(b,p+4)},u16(b,p+6)});}
    if(!validate(m,why))return false;out=std::move(m);return true;
}
Counts counts(const Mesh&m){if(m.vertices.empty()||m.vertices.size()>MaxVertices||m.triangles.empty()||m.triangles.size()>MaxTriangles)return {};const uint32_t b=leaves(uint32_t(m.triangles.size()));return {uint32_t(m.vertices.size()),uint32_t(m.triangles.size()),b,b?2*b-1:0,2,9};}
uint64_t allocation_bytes(const Mesh&m){const auto c=counts(m);if(!c.vertices)return 0;const auto s=storage(c);return s.payload()+32+((4-(c.blocks*2)%4)%4)+4;}
bool build_native(const Mesh&m,const std::array<uint8_t,9>&raw,uint16_t room,std::vector<uint8_t>&out,std::string&why){
    out.clear();if(!validate(m,why))return false;for(auto a:raw)if(a>31){why="invalid native surface attribute";return false;}
    struct Node{uint16_t parent=0xFFFF,block=0xFFFF;std::array<uint16_t,8> children;Node(){children.fill(0xFFFF);}};
    auto ordered=m.triangles;std::vector<Node> nodes;std::vector<uint16_t> blocks;
    std::function<uint16_t(size_t,size_t,uint16_t)> split=[&](size_t first,size_t end,uint16_t parent){const auto id=uint16_t(nodes.size());nodes.emplace_back();nodes[id].parent=parent;
        if(end-first<=LeafTriangles){nodes[id].block=uint16_t(blocks.size());blocks.push_back(uint16_t(first));return id;}
        Point low=m.vertices[ordered[first].indices[0]],high=low;for(size_t i=first;i<end;++i)for(auto v:ordered[i].indices){auto p=m.vertices[v];low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};}
        unsigned axis=0;for(unsigned k=1;k<3;++k)if(component(high,k)-component(low,k)>component(high,axis)-component(low,axis))axis=k;
        auto center=[&](const Triangle&t){return (double(component(m.vertices[t.indices[0]],axis))+component(m.vertices[t.indices[1]],axis)+component(m.vertices[t.indices[2]],axis))/3;};
        std::stable_sort(ordered.begin()+first,ordered.begin()+end,[&](const Triangle&a,const Triangle&b){return center(a)<center(b);});
        const size_t middle=first+(end-first)/2;const auto left=split(first,middle,id),right=split(middle,end,id);nodes[id].children[0]=left;nodes[id].children[1]=right;return id;};
    split(0,ordered.size(),0xFFFF);
    const uint32_t vertex=0x34,tri=vertex+uint32_t(m.vertices.size())*12,block=tri+uint32_t(ordered.size())*10,node=(block+uint32_t(blocks.size())*2+3)&~3u,group=node+uint32_t(nodes.size())*20,attribute=group+104,name=attribute+144;
    out.resize(name+4);const uint32_t n[]={uint32_t(m.vertices.size()),uint32_t(ordered.size()),uint32_t(blocks.size()),uint32_t(nodes.size()),2,9},offset[]={vertex,tri,block,node,group,attribute};
    for(unsigned i=0;i<6;++i){put32(out,i*8,n[i]);put32(out,i*8+4,offset[i]);}
    for(size_t i=0;i<m.vertices.size();++i)for(unsigned k=0;k<3;++k)putf(out,vertex+i*12+k*4,component(m.vertices[i],k));
    for(size_t i=0;i<ordered.size();++i){for(unsigned k=0;k<3;++k)put16(out,tri+i*10+k*2,ordered[i].indices[k]);put16(out,tri+i*10+6,ordered[i].semantic);put16(out,tri+i*10+8,1);}
    for(size_t i=0;i<blocks.size();++i)put16(out,block+i*2,blocks[i]);
    for(size_t i=0;i<nodes.size();++i){const auto&n=nodes[i];put16(out,node+i*20,n.block==0xFFFF?0:1);put16(out,node+i*20+2,n.parent);for(unsigned k=0;k<8;++k)put16(out,node+i*20+4+k*2,n.block!=0xFFFF&&k==0?n.block:n.children[k]);}
    // Native special-group filters run at depth two. Keep the root empty and
    // place the normal collision tree in its child; otherwise lava/water-only
    // queries incorrectly return ordinary walkable ground.
    for(unsigned i=0;i<2;++i){const uint32_t g=group+i*52;put32(out,g,name);for(unsigned k=0;k<3;++k)putf(out,g+4+k*4,1);put16(out,g+0x24,i?0:0xFFFF);put16(out,g+0x26,0xFFFF);put16(out,g+0x28,i?0xFFFF:1);put16(out,g+0x2A,room);put16(out,g+0x2E,i?0:0xFFFF);}
    for(unsigned i=0;i<9;++i){put32(out,attribute+i*16,0x07FFE0FF);put32(out,attribute+i*16+4,0xFFu|(i==7?0x100u:0)|(uint32_t(raw[i])<<16));put32(out,attribute+i*16+8,0xFFFFFFFF);if(i==6)put32(out,attribute+i*16+12,0xED);if(i==8)put32(out,attribute+i*16+12,0x10);}
    out[name]='w';out[name+1]='1';return true;
}
bool relocate(std::vector<uint8_t>&b,uint32_t base,std::string&why){
    if(b.size()<0x34||u32(b,0x30)!=0||!base||uint64_t(base)+b.size()>uint64_t(UINT32_MAX)+1){why="invalid native collision relocation";return false;}
    const uint32_t v=u32(b,0),t=u32(b,8);
    if(!v||v>MaxVertices||!t||t>MaxTriangles){why="invalid native collision counts";return false;}
    const uint32_t blocks=leaves(t),nodes=2*blocks-1;
    if(u32(b,16)!=blocks||u32(b,24)!=nodes||u32(b,32)!=2||u32(b,40)!=9){why="invalid native collision counts";return false;}
    const uint32_t vertex=0x34,tri=vertex+v*12,block=tri+t*10,node=(block+blocks*2+3)&~3u,grp=node+nodes*20,attribute=grp+104,name=attribute+144;
    const uint32_t expected[]={vertex,tri,block,node,grp,attribute};
    if(b.size()!=name+4||u32(b,grp)!=name||u32(b,grp+52)!=name||b[name]!='w'||b[name+1]!='1'||b[name+2]||b[name+3]){why="invalid native collision layout";return false;}
    for(unsigned i=0;i<6;++i)if(u32(b,i*8+4)!=expected[i]){why="invalid native collision table";return false;}
    for(unsigned i=0;i<2;++i){const size_t g=grp+i*52;if(u16(b,g+0x24)!=(i?0:0xFFFF)||u16(b,g+0x26)!=0xFFFF||u16(b,g+0x28)!=(i?0xFFFF:1)||u16(b,g+0x2E)!=(i?0:0xFFFF)||u32(b,g+0x30)!=0){why="invalid native collision group hierarchy";return false;}}
    for(uint32_t i=0;i<t;++i)if(u16(b,tri+i*10+8)!=1){why="invalid native collision triangle group";return false;}
    const uint32_t group=u32(b,0x24),n=u32(b,0x20);for(uint32_t i=0;i<n;++i){const auto p=u32(b,group+i*52);if(p>=b.size()||std::find(b.begin()+p,b.end(),0)==b.end()){why="invalid native collision name";return false;}}
    for(uint32_t i=0;i<n;++i){const size_t p=group+i*52;put32(b,p,base+u32(b,p));}for(unsigned i=0;i<6;++i)put32(b,i*8+4,base+u32(b,i*8+4));put32(b,0x30,0x80000000);why.clear();return true;
}
}
