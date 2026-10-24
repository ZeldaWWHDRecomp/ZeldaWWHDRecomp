#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <vector>
#include "mods/story_world_collision_mesh.h"
using namespace story_world_collision::mesh;
namespace {
uint32_t word(const std::vector<uint8_t>&b,size_t p){return (uint32_t(b[p])<<24)|(uint32_t(b[p+1])<<16)|(uint32_t(b[p+2])<<8)|b[p+3];}
uint16_t half(const std::vector<uint8_t>&b,size_t p){return uint16_t((b[p]<<8)|b[p+1]);}
void put(std::vector<uint8_t>&b,size_t p,uint32_t v){for(unsigned k=0;k<4;++k)b[p+k]=uint8_t(v>>(24-k*8));}
std::vector<uint8_t> wcol(const Mesh&m){std::vector<uint8_t>b(40+m.vertices.size()*12+m.triangles.size()*8);put(b,0,0x57434F4C);put(b,4,1);put(b,8,uint32_t(m.vertices.size()));put(b,12,uint32_t(m.triangles.size()));const float bounds[]={m.minimum.x,m.minimum.y,m.minimum.z,m.maximum.x,m.maximum.y,m.maximum.z};for(unsigned k=0;k<6;++k)put(b,16+4*k,std::bit_cast<uint32_t>(bounds[k]));for(size_t i=0;i<m.vertices.size();++i){auto p=m.vertices[i];const float values[]={p.x,p.y,p.z};for(unsigned k=0;k<3;++k)put(b,40+i*12+k*4,std::bit_cast<uint32_t>(values[k]));}for(size_t i=0;i<m.triangles.size();++i){const auto&t=m.triangles[i];for(unsigned k=0;k<4;++k){const uint16_t v=k<3?t.indices[k]:t.semantic;const size_t p=40+m.vertices.size()*12+i*8+k*2;b[p]=uint8_t(v>>8);b[p+1]=uint8_t(v);}}return b;}
void hierarchy(const std::vector<uint8_t>&b){const auto triangles=word(b,8),blocks=word(b,16),nodes=word(b,24),bp=word(b,20),np=word(b,28);std::set<unsigned> visited,seenblocks;std::function<void(unsigned,unsigned,unsigned)> visit=[&](unsigned id,unsigned parent,unsigned depth){assert(depth<=8&&id<nodes&&visited.insert(id).second);assert(half(b,np+20*id+2)==parent);if(half(b,np+20*id)&1){const unsigned block=half(b,np+20*id+4);assert(block<blocks&&seenblocks.insert(block).second);const unsigned begin=half(b,bp+2*block),end=block+1==blocks?triangles:half(b,bp+2*(block+1));assert(begin<end&&end-begin<=LeafTriangles);for(unsigned k=1;k<8;++k)assert(half(b,np+20*id+4+2*k)==0xFFFF);}else{unsigned children=0;for(unsigned k=0;k<8;++k){auto next=half(b,np+20*id+4+2*k);if(next!=0xFFFF){++children;visit(next,id,depth+1);}}assert(children==2);}};visit(0,0xFFFF,0);assert(visited.size()==nodes&&seenblocks.size()==blocks);}
// The native filter applies only at depth 2; an empty root must never expose
// geometry before the normal child has been tested against the query mask.
bool normal_group_visible(const std::vector<uint8_t>&b,unsigned mask){const auto group=word(b,0x24);unsigned hits=0;std::function<void(unsigned,unsigned)> visit=[&](unsigned id,unsigned depth){const auto g=group+52*id;if(depth==2&&!(mask&1))return;if(half(b,g+0x2E)!=0xFFFF)++hits;for(unsigned child=half(b,g+0x28);child!=0xFFFF;child=half(b,group+52*child+0x26))visit(child,depth+1);};visit(0,1);return hits!=0;}

// Every group's own tree (dBgW_SplGrpChkGrpRp walks a group's tree at +0x2E): its nodes and blocks hold only
// triangles of that group; all trees together cover every node and block once.
void group_trees(const std::vector<uint8_t>&b){const auto triangles=word(b,8),blocks=word(b,16),nodes=word(b,24),bp=word(b,20),np=word(b,28),tp=word(b,12),gp=word(b,0x24),groups=word(b,32);std::set<unsigned> visited,seenblocks;
    for(unsigned g=0;g<groups;++g){const unsigned root=half(b,gp+52*g+0x2E);if(root==0xFFFF)continue;std::function<void(unsigned,unsigned)> visit=[&](unsigned id,unsigned parent){assert(id<nodes&&visited.insert(id).second&&half(b,np+20*id+2)==parent);if(half(b,np+20*id)&1){const unsigned block=half(b,np+20*id+4);assert(block<blocks&&seenblocks.insert(block).second);const unsigned begin=half(b,bp+2*block),end=block+1==blocks?triangles:half(b,bp+2*(block+1));assert(begin<end&&end-begin<=LeafTriangles);for(unsigned t=begin;t<end;++t)assert(half(b,tp+10*t+8)==g);}else for(unsigned k=0;k<8;++k){auto next=half(b,np+20*id+4+2*k);if(next!=0xFFFF)visit(next,id);}};visit(root,0xFFFF);}
    assert(visited.size()==nodes&&seenblocks.size()==blocks);}
// The groups whose trees a query with `mask` reaches (dBgW_ChkGrpThrough at depth 2: 1 normal, 2 water, 4 lava).
std::set<unsigned> reached(const std::vector<uint8_t>&b,unsigned mask){const auto group=word(b,0x24);std::set<unsigned> out;std::function<void(unsigned,unsigned)> visit=[&](unsigned id,unsigned depth){const auto g=group+52*id;const uint32_t info=word(b,g+0x30);
    if(depth==2){bool through=true;if(!(info&0x80700)&&(mask&1))through=false;if((info&0x100)&&(mask&2))through=false;if((info&0x200)&&(mask&4))through=false;if(through)return;}
    if(half(b,g+0x2E)!=0xFFFF)out.insert(id);for(unsigned child=half(b,g+0x28);child!=0xFFFF;child=half(b,group+52*child+0x26))visit(child,depth+1);};visit(0,1);return out;}

}
int main(){std::string why;Mesh m,out;std::array<uint8_t,Surfaces> raw{2,4,1,11,20,0,0,0,1,0,19,23,15,8};
    for(const auto*shape:{"box","cylinder","sphere"}){
        assert(make_shape(shape,{100,80,120},50,80,1,m,why));assert(validate(m,why));
        double volume=0;for(auto&t:m.triangles){auto a=m.vertices[t.indices[0]],b=m.vertices[t.indices[1]],c=m.vertices[t.indices[2]];volume+=(double(a.x)*(double(b.y)*c.z-double(b.z)*c.y)+double(a.y)*(double(b.z)*c.x-double(b.x)*c.z)+double(a.z)*(double(b.x)*c.y-double(b.y)*c.x))/6;}assert(volume>0);
        const auto original=wcol(m);assert(decode(original,out,why)&&out.vertices.size()==m.vertices.size()&&out.triangles.size()==m.triangles.size());
        std::vector<uint8_t> native;assert(build_native(out,raw,12,native,why));hierarchy(native);assert(normal_group_visible(native,1));assert(!normal_group_visible(native,0xE));assert(word(native,32)==2);const auto gp=word(native,0x24),tp=word(native,12);for(unsigned i=0;i<word(native,8);++i)assert(half(native,tp+10*i+8)==1);for(unsigned i=0;i<2;++i)assert(word(native,gp+52*i+0x30)==0);const auto c=counts(out);assert(word(native,16)==c.blocks&&word(native,24)==c.nodes);assert(allocation_bytes(out)>=native.size()+0xBC);
        const uint32_t attribute=word(native,0x2C),group=word(native,0x24),name=word(native,group);assert(word(native,40)==SurfacesBeforeLava);for(unsigned i=0;i<SurfacesBeforeLava;++i){assert(word(native,attribute+16*i)==0x07FFE0FF);assert(word(native,attribute+16*i+4)==(0xFFu|(i==7?0x100u:0)|(uint32_t(raw[i])<<16)|(i==SurfaceVoid?4u<<21:0)));assert(word(native,attribute+16*i+8)==0xFFFFFFFF);assert(word(native,attribute+16*i+12)==(i==6?0xEDu:i==8?0x10u:0));}
        const auto before=native;for(unsigned field:{group+0x28,word(native,12)+8}){auto broken=before;broken[field]=0;broken[field+1]=0;assert(!relocate(broken,0x20000000,why));}assert(!relocate(native,0xFFFFFFF0,why)&&native==before);assert(relocate(native,0x20000000,why));for(unsigned i=0;i<6;++i)assert(word(native,i*8+4)==word(before,i*8+4)+0x20000000);assert(word(native,group)==name+0x20000000&&word(native,group+52)==name+0x20000000&&word(native,0x30)==0x80000000);const auto relocated=native;assert(!relocate(native,0x20000000,why)&&native==relocated);
        for(auto mutate:{0,1,2,3,4,5,6}){auto bad=original;if(mutate==0)bad.push_back(0);if(mutate==1)put(bad,4,2);if(mutate==2)put(bad,8,4097);if(mutate==3)put(bad,16,std::bit_cast<uint32_t>(m.minimum.x-1));if(mutate==4)put(bad,40,0x7FC00000);if(mutate==6)put(bad,40,std::bit_cast<uint32_t>(std::nextafter(MaxExtent,INFINITY)));if(mutate==5){const size_t p=40+m.vertices.size()*12;bad[p+6]=0;bad[p+7]=uint8_t(Surfaces);}out=m;assert(!decode(bad,out,why)&&out.vertices.empty()&&!why.empty());}
    }
    assert(make_shape("box",{100,100,100},0,0,0,m,why));const auto initial=m.triangles;while(m.triangles.size()+initial.size()<=MaxTriangles)m.triangles.insert(m.triangles.end(),initial.begin(),initial.end());while(m.triangles.size()<MaxTriangles)m.triangles.push_back(initial[0]);std::vector<uint8_t> native;assert(build_native(m,raw,0,native,why));hierarchy(native);assert(MaxTriangles==8192&&counts(m).blocks==256&&counts(m).nodes==511);{auto over=m;over.triangles.push_back(initial[0]);assert(!validate(over,why));std::vector<uint8_t> none;assert(!build_native(over,raw,0,none,why));}
    // a large island's ground: 6000 triangles on 3600 welded vertices stays inside a body's 512 KiB budget; the whole 8192/8192 does not
    {Mesh big;big.vertices.resize(3600);for(size_t i=0;i<big.vertices.size();++i)big.vertices[i]={float(i%60)*10.f,float((i*7)%13),float(i/60)*10.f};for(uint32_t i=0;i<6000;++i){const uint16_t r=uint16_t((i/2)/59),q=uint16_t((i/2)%59),v=uint16_t(r*60+q);big.triangles.push_back(i%2?Triangle{{v,uint16_t(v+60),uint16_t(v+61)},0}:Triangle{{v,uint16_t(v+61),uint16_t(v+1)},0});}
     big.minimum=big.maximum=big.vertices[0];for(auto p:big.vertices){big.minimum={std::min(big.minimum.x,p.x),std::min(big.minimum.y,p.y),std::min(big.minimum.z,p.z)};big.maximum={std::max(big.maximum.x,p.x),std::max(big.maximum.y,p.y),std::max(big.maximum.z,p.z)};}
     assert(validate(big,why));assert(allocation_bytes(big)<=512*1024);Mesh full=big;full.vertices.resize(8192,big.vertices.back());while(full.triangles.size()<8192)full.triangles.push_back(big.triangles[full.triangles.size()%6000]);assert(counts(full).triangles==8192&&allocation_bytes(full)>512*1024);}
    auto bad=m;bad.triangles[0].indices[1]=bad.triangles[0].indices[0];assert(!validate(bad,why));bad=m;bad.triangles[0].indices[0]=4096;assert(!validate(bad,why));bad=m;bad.vertices[0].x=std::numeric_limits<float>::infinity();assert(!validate(bad,why));raw[0]=32;assert(!build_native(m,raw,0,native,why)&&native.empty());
    assert(make_shape("box",{100,100,100},0,0,0,m,why));m.triangles[0].semantic=6;assert(decode(wcol(m),out,why)&&out.triangles[0].semantic==6);assert(!make_shape("box",{100,100,100},0,0,6,out,why));
    for(uint16_t semantic:{7,8}){m.triangles[0].semantic=semantic;assert(decode(wcol(m),out,why)&&out.triangles[0].semantic==semantic);assert(!make_shape("box",{100,100,100},0,0,semantic,out,why));}
    assert(!make_shape("box",{0,1,1},1,1,0,out,why));assert(!make_shape("sphere",{},std::nextafter(MaxExtent/2,INFINITY),0,0,out,why));assert(!make_shape("capsule",{},1,1,0,out,why));
    // Metal is the game's metal class (20, as Islet of Steel), never damage (9): with a conversion where two
    // raw codes give class 20 (17 and 20) and code 9 stays damage, metal writes 20 (its own class), stone 3.
    {uint32_t conversion[32];for(unsigned c=0;c<32;++c)conversion[c]=c;conversion[17]=20;conversion[7]=1;conversion[12]=2;
     std::array<uint8_t,Surfaces> got{};assert(native_attributes(conversion,NativeResultIds,got));assert(NativeResultIds[4]==20&&got[4]==20&&got[4]!=9);
     assert(got[0]==3&&got[1]==4&&got[2]==2&&got[3]==11&&got[5]==0&&got[8]==2);
     conversion[20]=21;assert(native_attributes(conversion,NativeResultIds,got)&&got[4]==17); // no own code: the first code of the class
     conversion[17]=9;assert(!native_attributes(conversion,NativeResultIds,got));} // no metal code at all: refused
    // Water: its own depth-two group with the game's water bit and its own tree; ordinary ground queries never
    // see it and water queries see only it. Void writes the game's void ground code (4) with the void class.
    {raw[0]=2;Mesh ground;assert(make_shape("box",{400,100,400},0,0,0,ground,why));Mesh mixed=ground;const uint16_t v0=uint16_t(mixed.vertices.size());
     for(float y:{80.f,90.f})for(unsigned k=0;k<40;++k){const float x=-150+float(k)*7;mixed.vertices.push_back({x,y,-150});mixed.vertices.push_back({x+5,y,150});mixed.vertices.push_back({x+5,y,-150});}
     for(uint16_t i=0;i<80;++i)mixed.triangles.push_back({{uint16_t(v0+3*i),uint16_t(v0+3*i+1),uint16_t(v0+3*i+2)},SurfaceWater});
     mixed.triangles[0].semantic=SurfaceVoid;mixed.triangles[1].semantic=12;mixed.minimum=ground.minimum;mixed.maximum=ground.maximum;
     for(auto p:mixed.vertices){mixed.minimum={std::min(mixed.minimum.x,p.x),std::min(mixed.minimum.y,p.y),std::min(mixed.minimum.z,p.z)};mixed.maximum={std::max(mixed.maximum.x,p.x),std::max(mixed.maximum.y,p.y),std::max(mixed.maximum.z,p.z)};}
     assert(decode(wcol(mixed),out,why));std::vector<uint8_t> native;assert(build_native(out,raw,12,native,why));
     assert(word(native,32)==3&&word(native,40)==SurfacesBeforeLava);const auto c=counts(out);assert(c.groups==3&&word(native,16)==c.blocks&&word(native,24)==c.nodes&&allocation_bytes(out)>=native.size()+0xBC);
     const auto gp=word(native,0x24),tp=word(native,12);assert(word(native,gp+52*2+0x30)==0x100&&word(native,gp+52+0x30)==0&&half(native,gp+52+0x26)==2&&half(native,gp+52*2+0x24)==0);
     unsigned in_water=0;for(unsigned i=0;i<word(native,8);++i){const bool w=half(native,tp+10*i+6)==SurfaceWater;assert(half(native,tp+10*i+8)==(w?2:1));in_water+=w;}assert(in_water==80);
     group_trees(native);assert((reached(native,1)==std::set<unsigned>{1}));assert((reached(native,2)==std::set<unsigned>{2}));assert(reached(native,4).empty());
     const uint32_t attribute=word(native,0x2C);assert(((word(native,attribute+16*SurfaceVoid+4)>>21)&0x1F)==4&&((word(native,attribute+16*SurfaceVoid+4)>>16)&0x1F)==raw[SurfaceVoid]);
     assert(((word(native,attribute+16*12+4)>>16)&0x1F)==15&&((word(native,attribute+16*10+4)>>16)&0x1F)==19&&((word(native,attribute+16*11+4)>>16)&0x1F)==23);
     for(unsigned i=0;i<SurfacesBeforeLava;++i)if(i!=SurfaceVoid)assert(((word(native,attribute+16*i+4)>>21)&0x1F)==0);
     const auto before=native;assert(relocate(native,0x20000000,why));auto broken=before;broken[gp+52*2+0x30+2]=0;assert(!relocate(broken,0x20000000,why));
     // water only: the ordinary group has no tree
     Mesh pond;pond.vertices={{0,0,0},{0,0,100},{100,0,0},{100,0,100}};pond.triangles={{{0,1,2},SurfaceWater},{{2,1,3},SurfaceWater}};pond.minimum={0,0,0};pond.maximum={100,0,100};
     assert(build_native(pond,raw,3,native,why));group_trees(native);assert(half(native,word(native,0x24)+52+0x2E)==0xFFFF);assert(reached(native,1).empty()&&(reached(native,2)==std::set<unsigned>{2}));assert(relocate(native,0x20000000,why));
     // no water: two groups as before
     assert(build_native(ground,raw,3,native,why)&&word(native,32)==2);group_trees(native);assert(relocate(native,0x20000000,why));
     auto bad=pond;bad.triangles[0].semantic=Surfaces;assert(!validate(bad,why));}
    // Lava: its surface in a depth-two group of its own with the game's lava bit (0x200), which only lava queries
    // (Link's mLavaGndChk, a bomb's, an item's: mask 4) reach; its bottom LavaDepth lower in the ordinary group; both
    // with the game's lava row (attribute class 6, material sound 23, camera words 0xFFFFFF00), as Dragon Roost
    // Cavern's lava floors have it. A body with lava has the lava row; one without keeps the earlier layout.
    {std::array<uint8_t,Surfaces> lraw{3,4,2,11,20,0,0,0,2,0,19,23,15,8,6};Mesh ground;assert(make_shape("box",{400,100,400},0,0,0,ground,why));
     for(bool with_water:{false,true}){Mesh mixed=ground;const uint16_t v0=uint16_t(mixed.vertices.size());
      mixed.vertices.insert(mixed.vertices.end(),{{-100,60,-100},{-100,60,100},{100,60,-100},{100,60,100}});
      mixed.triangles.push_back({{v0,uint16_t(v0+1),uint16_t(v0+2)},SurfaceLava});mixed.triangles.push_back({{uint16_t(v0+2),uint16_t(v0+1),uint16_t(v0+3)},SurfaceLava});
      if(with_water){const uint16_t w=uint16_t(mixed.vertices.size());mixed.vertices.insert(mixed.vertices.end(),{{150,40,150},{150,40,190},{190,40,150}});mixed.triangles.push_back({{w,uint16_t(w+1),uint16_t(w+2)},SurfaceWater});}
      mixed.minimum={-200,-50,-200};mixed.maximum={200,60,200};assert(validate(mixed,why));
      const auto size=native_size(mixed);assert(size.lava==2&&size.vertices==mixed.vertices.size()+4&&size.triangles==mixed.triangles.size()+2);
      assert(decode(wcol(mixed),out,why));std::vector<uint8_t> native;assert(build_native(out,lraw,12,native,why));
      const unsigned groups=with_water?4:3,lava_group=groups-1;
      assert(word(native,32)==groups&&word(native,40)==Surfaces&&word(native,0)==size.vertices&&word(native,8)==size.triangles);
      const auto c=counts(out);assert(c.groups==groups&&c.attributes==Surfaces&&c.vertices==size.vertices&&c.triangles==size.triangles&&word(native,16)==c.blocks&&word(native,24)==c.nodes&&allocation_bytes(out)>=native.size()+0xBC);
      const auto gp=word(native,0x24),tp=word(native,12),vp=word(native,4);
      assert(word(native,gp+52*lava_group+0x30)==0x200&&half(native,gp+52*lava_group+0x24)==0&&half(native,gp+52*lava_group+0x26)==0xFFFF);
      assert(half(native,gp+52+0x26)==(with_water?2:lava_group));if(with_water)assert(half(native,gp+52*2+0x26)==lava_group&&word(native,gp+52*2+0x30)==0x100);
      unsigned surface=0,bottom=0;auto y=[&](unsigned v){return std::bit_cast<float>(word(native,vp+12*v+4));};
      for(unsigned i=0;i<word(native,8);++i){const unsigned g=half(native,tp+10*i+8),sem=half(native,tp+10*i+6);
       if(sem!=SurfaceLava)continue;
       if(g==lava_group){++surface;for(unsigned k=0;k<3;++k)assert(y(half(native,tp+10*i+2*k))==60);}
       else{assert(g==1);++bottom;for(unsigned k=0;k<3;++k)assert(y(half(native,tp+10*i+2*k))==60-LavaDepth);}}
      assert(surface==2&&bottom==2);
      group_trees(native);assert((reached(native,1)==std::set<unsigned>{1}));assert((reached(native,4)==std::set<unsigned>{lava_group}));
      if(with_water)assert((reached(native,2)==std::set<unsigned>{2}));
      // Link's lava check (water, lava and poison: 0xE) sees the lava's surface, and the water when there is some
      assert(reached(native,0xE).count(lava_group)&&!reached(native,0xE).count(1));
      const uint32_t attribute=word(native,0x2C);
      assert(word(native,attribute+16*SurfaceLava)==0x07FFF7FF&&word(native,attribute+16*SurfaceLava+4)==0x000600FF&&word(native,attribute+16*SurfaceLava+8)==0xFFFFFF00&&word(native,attribute+16*SurfaceLava+12)==0);
      assert(((word(native,attribute+16*SurfaceLava)>>8)&0x1F)==23&&((word(native,attribute+16*SurfaceLava)>>13)&0x3F)==0x3F);
      const auto before=native;assert(relocate(native,0x20000000,why));auto broken=before;broken[gp+52*lava_group+0x30+2]=1;assert(!relocate(broken,0x20000000,why));
      broken=before;broken[tp+8+1]=uint8_t(lava_group);assert(!relocate(broken,0x20000000,why));}
     // lava alone (no ordinary ground of its own besides its bottom) and the limits: lava counts twice
     Mesh pool;pool.vertices={{0,0,0},{0,0,100},{100,0,0},{100,0,100}};pool.triangles={{{0,1,2},SurfaceLava},{{2,1,3},SurfaceLava}};pool.minimum={0,0,0};pool.maximum={100,0,100};
     std::vector<uint8_t> native;assert(build_native(pool,lraw,3,native,why)&&word(native,32)==3);group_trees(native);assert(relocate(native,0x20000000,why));
     Mesh big;big.vertices={{0,0,0},{0,0,100},{100,0,0}};big.minimum={0,0,0};big.maximum={100,0,100};big.triangles.assign(MaxTriangles/2+1,{{0,1,2},SurfaceLava});
     assert(!validate(big,why)&&counts(big).vertices==0); // MaxTriangles/2+1 lava triangles: MaxTriangles+2 native ones
     big.triangles.resize(MaxTriangles/2);assert(validate(big,why)&&native_size(big).triangles==MaxTriangles&&native_size(big).vertices==6&&counts(big).triangles==MaxTriangles);}
    // The layout without lava is the one before lava, byte for byte (a world object's identity is its collision's
    // digest, which full save states keep): a fixture with water and void, its size and FNV-1a digest from that build.
    {Mesh ground;assert(make_shape("box",{400,100,400},0,0,0,ground,why));Mesh mixed=ground;const uint16_t v0=uint16_t(mixed.vertices.size());
     for(unsigned k=0;k<40;++k){const float x=-150+float(k)*7;mixed.vertices.push_back({x,80,-150});mixed.vertices.push_back({x+5,80,150});mixed.vertices.push_back({x+5,80,-150});}
     for(uint16_t i=0;i<40;++i)mixed.triangles.push_back({{uint16_t(v0+3*i),uint16_t(v0+3*i+1),uint16_t(v0+3*i+2)},SurfaceWater});
     mixed.triangles[0].semantic=SurfaceVoid;mixed.triangles[1].semantic=12;mixed.minimum={-200,-50,-200};mixed.maximum={200,80,200};
     std::array<uint8_t,Surfaces> nraw{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14};std::vector<uint8_t> now;assert(build_native(mixed,nraw,12,now,why));
     unsigned long long h=1469598103934665603ull;for(auto c:now){h^=c;h*=1099511628211ull;}assert(now.size()==2580&&h==0x3fc5d84f6d10c0ecull);}
    // The first native layout, byte for byte as written before water and the later surfaces (a full save state of
    // that time keeps collision built this way, recognised by its digest): a fixture's size and FNV-1a digest from
    // that build, and a mesh using a later surface is refused.
    {Mesh old;assert(make_shape("box",{100,80,120},0,0,1,old,why));old.triangles[3].semantic=4;old.triangles[5].semantic=7;
     std::array<uint8_t,9> v1raw{0,1,2,3,4,5,6,7,8};std::vector<uint8_t> v1;assert(build_native_v1(old,v1raw,12,v1,why));
     unsigned long long h=1469598103934665603ull;for(auto c:v1){h^=c;h*=1099511628211ull;}assert(v1.size()==544&&h==0x6826a7b239b63809ull);
     assert(word(v1,32)==2&&word(v1,40)==9);std::vector<uint8_t> now;std::array<uint8_t,Surfaces> nraw{0,1,2,3,4,5,6,7,8,9,10,11,12,13};assert(build_native(old,nraw,12,now,why)&&now!=v1);
     old.triangles[0].semantic=SurfaceWater;assert(!build_native_v1(old,v1raw,12,v1,why));}
    // A body's size: one sea square (100000 units) per axis. A whole-square ground (a 40 x 40 grid, 1 km wide,
    // 400 m tall at one corner) validates, builds and relocates; one float above the bound on any axis or any
    // coordinate is refused, the shapes too.
    {assert(MaxExtent==100000.f);Mesh ground;const float h=MaxExtent/2;
     for(unsigned j=0;j<=40;++j)for(unsigned i=0;i<=40;++i)ground.vertices.push_back({-h+MaxExtent*i/40,i==40&&j==40?40000.f:float((i*j)%7)*10,-h+MaxExtent*j/40});
     for(uint16_t j=0;j<40;++j)for(uint16_t i=0;i<40;++i){const uint16_t a=uint16_t(j*41+i);ground.triangles.push_back({{a,uint16_t(a+41),uint16_t(a+1)},1});ground.triangles.push_back({{uint16_t(a+1),uint16_t(a+41),uint16_t(a+42)},1});}
     auto bound=[](Mesh&m){m.minimum=m.maximum=m.vertices[0];for(auto p:m.vertices){m.minimum={std::min(m.minimum.x,p.x),std::min(m.minimum.y,p.y),std::min(m.minimum.z,p.z)};m.maximum={std::max(m.maximum.x,p.x),std::max(m.maximum.y,p.y),std::max(m.maximum.z,p.z)};}};
     bound(ground);assert(ground.maximum.x-ground.minimum.x==MaxExtent&&ground.maximum.z-ground.minimum.z==MaxExtent);
     assert(validate(ground,why));assert(decode(wcol(ground),out,why));std::vector<uint8_t> native;assert(build_native(out,raw,26,native,why));hierarchy(native);group_trees(native);assert(relocate(native,0x20000000,why));
     // the old 20000 bound is gone: 200 m and 400 m wide grounds both pass
     for(float width:{20000.f,40000.f}){Mesh m=ground;for(auto&p:m.vertices){p.x*=width/MaxExtent;p.z*=width/MaxExtent;}bound(m);assert(validate(m,why));}
     const float above=std::nextafter(MaxExtent,INFINITY);
     for(unsigned axis=0;axis<3;++axis){Mesh wide=ground;auto&p=wide.vertices.back();(axis==0?p.x:axis==1?p.y:p.z)=axis==1?ground.minimum.y+above:h+(above-MaxExtent);bound(wide);
         const float extent=axis==0?wide.maximum.x-wide.minimum.x:axis==1?wide.maximum.y-wide.minimum.y:wide.maximum.z-wide.minimum.z;assert(extent>MaxExtent);assert(!validate(wide,why));assert(!decode(wcol(wide),out,why));}
     {Mesh far=ground;for(auto&p:far.vertices)p.x+=1;far.vertices[0].x=-h;bound(far);assert(!validate(far,why));}  // extent above the bound inside +/-MaxExtent
     {Mesh corner=ground;for(auto&p:corner.vertices)p.x+=h;bound(corner);assert(validate(corner,why));corner.vertices.back().x=above;bound(corner);assert(!validate(corner,why));}  // coordinates up to the bound
     assert(make_shape("box",{MaxExtent,MaxExtent,MaxExtent},0,0,0,out,why)&&make_shape("cylinder",{},MaxExtent/2,MaxExtent,0,out,why)&&make_shape("sphere",{},MaxExtent/2,0,0,out,why));
     assert(!make_shape("box",{above,1,1},0,0,0,out,why)&&!make_shape("box",{1,1,above},0,0,0,out,why)&&!make_shape("cylinder",{},1,above,0,out,why)&&!make_shape("cylinder",{},std::nextafter(MaxExtent/2,INFINITY),1,0,out,why));}
}
