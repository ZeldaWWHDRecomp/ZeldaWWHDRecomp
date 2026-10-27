#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include "mods/story_world_placement.h"
using namespace story_world_placement;
using Bytes=std::vector<uint8_t>;
static void u16(Bytes&b,size_t p,uint16_t v){b[p]=uint8_t(v>>8);b[p+1]=uint8_t(v);}
static void u32(Bytes&b,size_t p,uint32_t v){for(unsigned k=0;k<4;++k)b[p+k]=uint8_t(v>>(24-k*8));}
static void f32(Bytes&b,size_t p,float v){u32(b,p,std::bit_cast<uint32_t>(v));}
static void xyz(Bytes&b,size_t p,Point v){f32(b,p,v.x);f32(b,p+4,v.y);f32(b,p+8,v.z);}
static void tag(Bytes&b,size_t p,const char*s){for(unsigned k=0;k<4;++k)b[p+k]=uint8_t(s[k]);}
static Bytes mesh(){Bytes b(0x34+36+10);u32(b,0,3);u32(b,4,0x34);u32(b,8,1);u32(b,12,0x34+36);xyz(b,0x34,{-10,0,-10});xyz(b,0x40,{10,0,-10});xyz(b,0x4c,{0,0,10});u16(b,0x34+36,0);u16(b,0x34+38,2);u16(b,0x34+40,1);return b;}
static Bytes layout(){Bytes b(4+36+32+12+32);u32(b,0,3);tag(b,4,"ACTR");u32(b,8,1);u32(b,12,40);tag(b,16,"RPAT");u32(b,20,1);u32(b,24,72);tag(b,28,"RPPN");u32(b,32,2);u32(b,36,84);tag(b,40,"orig");xyz(b,52,{30000,0,30000});u16(b,72,2);u32(b,80,0);xyz(b,88,{-100,10000,0});xyz(b,104,{100,10000,0});return b;}
static Bytes sarc(const std::vector<std::pair<std::string,Bytes>>&files){const size_t nodes=0x20,names=nodes+files.size()*16+8;size_t ns=0;for(auto&f:files)ns+=(f.first.size()+1+3)&~size_t(3);size_t data=(names+ns+15)&~size_t(15),bytes=data;for(auto&f:files)bytes+=f.second.size();Bytes b(bytes);tag(b,0,"SARC");u16(b,4,0x14);u16(b,6,0xfeff);u32(b,8,uint32_t(bytes));u32(b,12,uint32_t(data));tag(b,0x14,"SFAT");u16(b,0x18,12);u16(b,0x1a,uint16_t(files.size()));tag(b,nodes+files.size()*16,"SFNT");u16(b,nodes+files.size()*16+4,8);size_t name=names,pos=data;for(size_t j=0;j<files.size();++j){auto&[n,v]=files[j];size_t p=nodes+j*16;u32(b,p+4,0x01000000|uint32_t((name-names)/4));u32(b,p+8,uint32_t(pos-data));u32(b,p+12,uint32_t(pos+v.size()-data));std::copy(n.begin(),n.end(),b.begin()+name);std::copy(v.begin(),v.end(),b.begin()+pos);name+=(n.size()+1+3)&~size_t(3);pos+=v.size();}return b;}
static Bytes bfres(const std::vector<std::pair<std::string,Bytes>>&files){const size_t dict=0x70,entries=dict+24;size_t pos=entries+files.size()*16;Bytes b(pos);tag(b,0,"FRES");u16(b,8,0xfeff);u32(b,0x4c,uint32_t(dict-0x4c));u32(b,dict+4,uint32_t(files.size()));for(size_t j=0;j<files.size();++j){auto&[n,v]=files[j];size_t at=entries+j*16,name=pos;b.insert(b.end(),n.begin(),n.end());b.push_back(0);pos=b.size();while(pos%4){b.push_back(0);++pos;}size_t ext=pos;b.resize(pos+8);pos+=8;size_t data=pos;b.insert(b.end(),v.begin(),v.end());pos=b.size();u32(b,at+8,uint32_t(name-at-8));u32(b,at+12,uint32_t(ext-at-12));u32(b,ext,uint32_t(data-ext));u32(b,ext+4,uint32_t(v.size()));}return b;}
static bool near(float a,float b){return std::fabs(a-b)<.001f;}
int main(){
 Bounds local{{-1,-2,-3},{1,2,3}},result;assert(transform_bounds(local,{10,20,30},{0,90,0},{2,3,4},result));assert(near(result.minimum.x,-2)&&near(result.maximum.x,22)&&near(result.minimum.z,28)&&near(result.maximum.z,32));
 assert(transform_bounds(local,{0,0,0},{90,0,90},{1,1,1},result));assert(near(result.maximum.x,2)&&near(result.maximum.y,3)&&near(result.maximum.z,1));
 assert(transform_bounds(local,{0,0,0},{},{-1,1,1},result));assert(near(result.minimum.x,-1));assert(!transform_bounds(local,{},{},{0,1,1},result));assert(!transform_bounds(local,{},{std::numeric_limits<float>::infinity(),0,0},{1,1,1},result));
 Index i;assert(check("test",0,local,i).front().kind=="coverage");i.coverage.push_back({"test",0,true,true,true,{}});assert(check("test",0,local,i).empty());
 i.regions.push_back({"test",0,{{-10,-10,-10},{10,10,10}}});assert(check("test",0,{{-11,0,0},{0,1,1}},i,0).front().kind=="coverage");i.regions.clear();
 i.bounds.push_back({"test",0,"event","a",{{101,-1,-1},{102,1,1}}});assert(check("test",0,local,i,99).empty());assert(check("test",0,local,i,100).size()==1);assert(check("test",1,local,i,0).front().kind=="coverage");
 i.bounds.clear();
 // a route 100 m above is not in the way (a cliff path); one through the body is; a corridor by its radius
 i.routes.push_back({"test",0,"cross",{{-100,10000,0},{100,10000,0}},0,false});assert(check("test",0,local,i,0).empty());i.routes[0].points={{-100,0,0},{100,0,0}};assert(check("test",0,local,i,0).front().kind=="route");i.routes[0].points={{-100,0,50},{100,0,50}};assert(check("test",0,local,i,0).empty());i.routes[0].radius=50;assert(check("test",0,local,i,0).size()==1);i.routes[0].id="RPAT:0";assert(check("test",0,local,i,0).front().kind=="villager route");
 Index ground;ground.coverage.push_back({"test",0,true,true,true,{}});ground.bounds.push_back({"test",0,"collision","floor",{{-100,0,-100},{100,0,100}}});
 assert(check("test",0,{{-1,0,-1},{1,2,1}},ground).empty()); // Floor contact is intentional.
 assert(!check("test",0,{{-1,-.05f,-1},{1,2,1}},ground).empty()); // Penetration remains invalid.
 ground.bounds[0].bounds.maximum.y=10;assert(!check("test",0,{{-1,0,-1},{1,2,1}},ground).empty()); // Sloping walls are protected.
 Index parsed;std::string why;assert(add_native_collision(parsed,"test",0,mesh(),&why));assert(parsed.bounds.size()==1);assert(near(parsed.bounds[0].bounds.minimum.x,-10)); /* world space as stored */auto bad=mesh();u16(bad,0x34+36,3);size_t count=parsed.bounds.size();assert(!add_native_collision(parsed,"test",0,bad,&why));assert(parsed.bounds.size()==count);
 assert(add_native_layout(parsed,"test",0,layout(),&why));assert(parsed.routes.size()==1&&parsed.routes[0].points.size()==2&&parsed.routes[0].radius==kRouteHalfWidth);assert(near(parsed.routes[0].points[0].x,-100));assert(parsed.layout.size()==1&&parsed.layout[0].name=="orig"&&near(parsed.layout[0].position.x,30000));
 {auto c=check("test",0,local,parsed,0);assert(c.size()==1&&c[0].kind=="collision"&&c[0].message=="the island's walls or ground (1 triangle)");} // the floor triangle it goes into
 bad=layout();u32(bad,80,1);assert(!add_native_layout(parsed,"test",0,bad,&why));assert(parsed.routes.size()==1);
 { // a water (or lava) group's triangles are not ground or walls: the sea plane every island square carries
  Bytes water=mesh();const size_t groups=water.size();water.resize(groups+0x34);u32(water,0x20,1);u32(water,0x24,uint32_t(groups));u32(water,groups+0x30,0x12C);u16(water,0x34+36+8,0);
  Index sea;assert(add_native_collision(sea,"test",0,water,&why)&&sea.bounds.empty());
  u32(water,groups+0x30,0x02C);Index ground;assert(add_native_collision(ground,"test",0,water,&why)&&ground.bounds.size()==1);
  u32(water,groups+0x30,0x22C);Index lava;assert(add_native_collision(lava,"test",0,water,&why)&&lava.bounds.empty());
 }
 // the table strides of the game's chunk handlers (dStage: actor 0x20, tgsc and door 0x24, treasure 0x20)
 for(const char*t:{"ACTR","ACT0","ACT9","ACTa","ACTb","TGOB","TRES","TRE3","PLYR"})assert(table_stride(t)==32);
 for(const char*t:{"SCOB","SCO0","SCOb","TGSC","TGDR","DOOR"})assert(table_stride(t)==36);
 for(const char*t:{"RPAT","MULT","ACTc","SCOx","ACT","FILI"})assert(table_stride(t)==0);
 { // a door table of two 36-byte entries: the second door is read at 36, with its scale; a 32-byte chest table after it
  Bytes b(4+3*12+2*36+32);u32(b,0,3);tag(b,4,"TGDR");u32(b,8,2);u32(b,12,40);tag(b,16,"TRES");u32(b,20,1);u32(b,24,112);tag(b,28,"PLYR");u32(b,32,0);u32(b,36,144);
  tag(b,40,"KNOB");tag(b,44,"00D\0");xyz(b,52,{100,0,0});b[72]=10;tag(b,76,"KNOB");tag(b,80,"01D\0");xyz(b,88,{200,0,0});b[108]=20;b[109]=21;b[110]=22;
  tag(b,112,"taka");tag(b,116,"ra3\0");xyz(b,124,{300,0,0});
  Index doors;assert(add_native_layout(doors,"test",0,b,&why));assert(doors.layout.size()==3);
  assert(doors.layout[1].name=="KNOB01D"&&near(doors.layout[1].position.x,200)&&doors.layout[1].scaled&&doors.layout[1].scale[0]==20&&doors.layout[1].scale[2]==22);
  assert(doors.layout[2].name=="takara3"&&near(doors.layout[2].position.x,300)&&!doors.layout[2].scaled);
  doors.coverage[0].collision=true; // the layout gave routes and events; no collision in this test
  // world bounds next to the second door are refused with it; open ground 1000 units away is clear
  auto c=check("test",0,{{150,0,-50},{250,100,50}},doors);assert(c.size()==3&&c[1].kind=="door"&&c[1].native=="KNOB01D"&&c[1].message=="a door of the island"&&c[2].kind=="chest");
  assert(check("test",0,{{1200,0,-50},{1300,100,50}},doors).empty());
  // a raft drifting toward the doors stops short of the nearer door's reach (radius 250 plus the clearance)
  auto drift=sweep("test",0,{{900,0,-10},{920,10,10}},{-1000,0,0},doors);assert(drift.blocked&&drift.conflicts[0].kind=="door"&&drift.allowed_delta.x<-399&&drift.allowed_delta.x>-401);
  // a route far below the raft does not stop it
  doors.layout.clear();doors.routes.push_back({"test",0,"RPAT:0",{{0,-5000,-100},{0,-5000,100}},kRouteHalfWidth,false});assert(!sweep("test",0,{{900,0,-10},{920,10,10}},{-1000,0,0},doors).blocked);
  doors.routes[0].points={{0,0,-100},{0,0,100}};assert(sweep("test",0,{{900,0,-10},{920,10,10}},{-1000,0,0},doors).conflicts[0].kind=="villager route");
 }
 { // Continuous drift cannot tunnel through thin walls or a moving ship footprint.
  Index motion;motion.coverage.push_back({"test",0,true,true,true,{}});const Bounds body{{-1,0,-1},{1,2,1}};
  auto free=sweep("test",0,body,{1000,0,400},motion,0);assert(!free.blocked&&free.allowed_delta.x==1000&&free.allowed_delta.z==400);
  motion.bounds.push_back({"test",0,"ship","live-ship",{{20,-100,-1000},{20.01f,100,1000}}});
  auto hit=sweep("test",0,body,{1000,0,0},motion,0);assert(hit.blocked&&hit.allowed_delta.x>18&&hit.allowed_delta.x<19&&hit.allowed_delta.z==0&&hit.conflicts[0].kind=="ship");
  auto slide=sweep("test",0,body,{100,0,100},motion,0);assert(slide.blocked&&slide.allowed_delta.x<19&&slide.allowed_delta.x>18&&near(slide.allowed_delta.z,100));
  motion.bounds.push_back({"test",0,"object","other-raft",{{-1000,-100,20},{1000,100,20.01f}}});
  auto corner=sweep("test",0,body,{100,0,100},motion,0);assert(corner.blocked&&corner.allowed_delta.x<19&&corner.allowed_delta.z<19&&corner.conflicts.size()==2);
  auto below=motion;below.bounds[0].kind="collision";below.bounds[0].bounds.maximum.y=0;below.bounds.resize(1);assert(!sweep("test",0,body,{100,0,0},below,0).blocked);
  assert(sweep("test",0,body,{1,1,0},motion,0).conflicts[0].kind=="invalid");assert(sweep("test",1,body,{100,0,0},motion,0).allowed_delta.x==0);
  motion.bounds.clear();motion.regions.push_back({"test",0,{{-10,-10,-10},{10,10,10}}});auto edge=sweep("test",0,body,{100,0,2},motion,0);assert(edge.blocked&&edge.allowed_delta.x<9&&edge.allowed_delta.x>8&&near(edge.allowed_delta.z,2));
  motion.regions.clear();motion.routes.push_back({"test",0,"diagonal",{{20,0,-100},{120,0,100}},1,false});auto route=sweep("test",0,body,{1000,0,0},motion,0);assert(route.blocked&&route.allowed_delta.x>60&&route.allowed_delta.x<70&&route.conflicts[0].kind=="route");
  motion.routes[0].points={{-100,0,30},{100,0,30}};assert(!sweep("test",0,body,{100,0,0},motion,0).blocked);
  motion.routes.clear();motion.bounds.push_back({"test",0,"object","left",{{-20.01f,-100,-1000},{-20,100,1000}}});auto left=sweep("test",0,body,{-1000,0,100},motion,0);assert(left.blocked&&left.allowed_delta.x>-19&&left.allowed_delta.x<-18&&near(left.allowed_delta.z,100));
  motion.bounds[0].bounds={{-.5f,0,-.5f},{.5f,1,.5f}};auto overlap=sweep("test",0,body,{100,0,100},motion,0);assert(overlap.blocked&&overlap.allowed_delta.x==0&&overlap.allowed_delta.z==0);
 }
 // Only original synthetic archives; no native resource fixtures are redistributed.
 const auto temporary=std::filesystem::temp_directory_path()/("world-placement-"+std::to_string(reinterpret_cast<uintptr_t>(&i)));std::filesystem::create_directories(temporary/"content/Common/Stage");auto write_fixture=[&](const char*name,const Bytes&b){std::ofstream f(temporary/"content/Common/Stage"/name,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));};
 Bytes stage(4+12+12);u32(stage,0,1);tag(stage,4,"MULT");u32(stage,8,1);u32(stage,12,16);f32(stage,16,100);f32(stage,20,200);stage[26]=0;
 write_fixture("test_Stage.szs",sarc({{"stage.dzs",stage}}));write_fixture("test_Room0.szs",sarc({{"Room0.bfres",bfres({{"room.dzb",mesh()},{"room.dzr",layout()}})}}));auto loaded=load_native_room(temporary.string(),"test",0);bool complete=false;for(auto&c:loaded.coverage)if(c.room==0)complete=c.collision&&c.routes&&c.events;assert(complete);
 // a stage room: its MULT entry (100, 200) moves nothing: collision, layout and routes stay where they are stored
 assert(loaded.bounds.size()==1&&near(loaded.bounds[0].bounds.minimum.x,-10)&&loaded.layout.size()==1&&near(loaded.layout[0].position.x,30000)&&near(loaded.routes[0].points[0].x,-100));assert(loaded.regions.empty());assert(check("test",0,{{-5000,0,-5000},{-4999,1,-4999}},loaded,0).empty());
 { // a sea room: the MULT grid gives its square; its own data stays in world space (room 2 at x 100000)
  Bytes grid(4+12+3*12);u32(grid,0,1);tag(grid,4,"MULT");u32(grid,8,3);u32(grid,12,16);f32(grid,16,0);f32(grid,20,0);grid[26]=1;f32(grid,28,100000);f32(grid,32,0);grid[38]=2;f32(grid,40,100000);f32(grid,44,100000);grid[50]=3;
  Bytes room=layout();xyz(room,52,{100500,0,200});
  write_fixture("sea_Stage.szs",sarc({{"stage.dzs",grid}}));write_fixture("sea_Room2.szs",sarc({{"Room2.bfres",bfres({{"room.dzb",mesh()},{"room.dzr",room}})}}));
  auto sea=load_native_room(temporary.string(),"sea",2);assert(sea.regions.size()==1&&near(sea.regions[0].bounds.minimum.x,50000)&&near(sea.regions[0].bounds.maximum.x,150000));
  assert(sea.layout.size()==1&&near(sea.layout[0].position.x,100500)&&near(sea.bounds[0].bounds.minimum.x,-10));
  // next to the object at 100500 (an unknown object: radius 150) is refused, open sea in its square is not
  assert(check("sea",2,{{100600,0,150},{100700,100,250}},sea).front().kind=="object");assert(check("sea",2,{{110000,0,0},{110100,100,100}},sea).empty());
  assert(check("sea",2,{{149000,0,0},{151000,100,100}},sea).front().id=="room-boundary");
 }
 write_fixture("test_Room0.szs",{0,1,2});loaded=load_native_room(temporary.string(),"test",0);assert(check("test",0,local,loaded,0).front().kind=="coverage");assert(check("../test",0,local,load_native_room(temporary.string(),"../test",0)).front().kind=="coverage");std::filesystem::remove_all(temporary);
 // ---- solids: an island ground (a sloped hill with a cliff wall) and houses (closed boxes) on and against it
 {
  auto box=[](Point lo,Point hi){std::vector<Point> v{{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},{lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}};
   std::vector<std::array<uint16_t,3>> t{{0,1,2},{0,2,3},{4,6,5},{4,7,6},{0,5,1},{0,4,5},{1,6,2},{1,5,6},{2,7,3},{2,6,7},{3,4,0},{3,7,4}};return std::make_pair(v,t);};
  // the hill: its top rises 100 units over 2000 (a slope), a cliff stands at x 1500..1600 up to 900
  std::vector<Point> hv{{-2000,0,-2000},{2000,100,-2000},{2000,100,2000},{-2000,0,2000},{1500,100,-500},{1600,100,-500},{1600,900,-500},{1500,900,-500},{1500,100,500},{1600,100,500},{1600,900,500},{1500,900,500},{-2000,-1000,-2000},{2000,-1000,-2000},{2000,-1000,2000},{-2000,-1000,2000}};
  std::vector<std::array<uint16_t,3>> ht{{0,2,1},{0,3,2},{4,7,11},{4,11,8},{5,9,10},{5,10,6},{4,5,6},{4,6,7},{8,10,9},{8,11,10},{7,6,10},{7,10,11},{12,13,14},{12,14,15}};  // and its base under the sea, as an island's
  const Solid hill=place_solid(hv,ht,{0,0,0},{0,0,0},{1,1,1});
  auto [bv,bt]=box({-200,0,-200},{200,500,200});
  assert(!solids_overlap(hill,place_solid(bv,bt,{0,60,0},{0,222.5f,0},{1,1,1})));     // on the slope, its floor a little in it
  assert(!solids_overlap(hill,place_solid(bv,bt,{-1000,10,0},{0,0,0},{1,1,1})));     // a house on the low side
  assert(solids_overlap(hill,place_solid(bv,bt,{0,-300,0},{0,0,0},{1,1,1})));        // sunk 300: the top goes through its walls
  assert(solids_overlap(hill,place_solid(bv,bt,{1500,100,0},{0,0,0},{1,1,1})));      // into the cliff
  assert(!solids_overlap(hill,place_solid(bv,bt,{1250,100,0},{0,0,0},{1,1,1})));     // right beside the cliff (its bounds touch, the solids don't)
  // two houses: apart, touching corners after a turn, overlapping
  const Solid a=place_solid(bv,bt,{0,100,0},{0,0,0},{1,1,1});
  assert(!solids_overlap(a,place_solid(bv,bt,{450,100,0},{0,0,0},{1,1,1})));
  assert(!solids_overlap(a,place_solid(bv,bt,{520,100,0},{0,45,0},{1,1,1})));        // its turned bounds overlap, it doesn't
  assert(solids_overlap(a,place_solid(bv,bt,{300,100,100},{0,30,0},{1,1,1})));
  assert(!solids_overlap(a,Solid{}));
 }
 // ---- a route in the way says where: its point nearest the bounds, not its start
 {
  Index idx;idx.coverage.push_back({"sea",12,true,true,true,{}});idx.routes.push_back({"sea",12,"RPAT:3",{{0,100,0},{1000,100,0},{2000,100,0},{3000,100,0}},kRouteHalfWidth,false});
  const auto found=check_building("sea",12,{{1900,50,-100},{2100,300,100}},idx);
  assert(found.size()==1&&found[0].kind=="villager route"&&found[0].at.x==2000&&found[0].path.size()==4);
 }
 // ---- a body of water on a terraced island ground (the mod's own world object): three square terraces, each a closed
 // block from the sea floor (-1000) up to its top (200, 400, 600; 3000, 2000 and 1000 wide), a pillar on the top one
 {
  std::vector<Point> v;std::vector<std::array<uint16_t,3>> t;
  auto block=[&](Point lo,Point hi){const auto b=uint16_t(v.size());
   for(Point p:std::initializer_list<Point>{{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},{lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}})v.push_back(p);
   for(auto f:std::initializer_list<std::array<uint16_t,3>>{{0,1,2},{0,2,3},{4,6,5},{4,7,6},{0,5,1},{0,4,5},{1,6,2},{1,5,6},{2,7,3},{2,6,7},{3,4,0},{3,7,4}})t.push_back({uint16_t(b+f[0]),uint16_t(b+f[1]),uint16_t(b+f[2])});};
  block({-1500,-1000,-1500},{1500,200,1500});block({-1000,-1000,-1000},{1000,400,1000});block({-500,-1000,-500},{500,600,500});
  const Solid isle=place_solid(v,t,{0,0,0},{0,0,0},{1,1,1});
  block({200,600,-50},{300,900,50});const Solid pillared=place_solid(v,t,{0,0,0},{0,0,0},{1,1,1});
  // the ground under a point: the top terrace, the middle one, the lowest, nothing beside the island; from below a top
  float h=0;
  assert(ground_under(isle,{0,5000,0},h)&&h==600);assert(ground_under(isle,{800,5000,-300},h)&&h==400);assert(ground_under(isle,{1300,5000,1300},h)&&h==200);
  assert(!ground_under(isle,{1600,5000,0},h));assert(ground_under(isle,{0,500,0},h)&&h==400);assert(ground_under(isle,{500,5000,0},h)&&h==600);  // an edge is the top's
  assert(ground_under(isle,{0,600,0},h)&&h==600);  // standing on it
  // a pond's open water: a square (two triangles) at its level
  auto water=[](float x0,float z0,float x1,float z1,float y){return place_solid({{x0,y,z0},{x1,y,z0},{x1,y,z1},{x0,y,z1}},{{0,2,1},{0,3,2}},{0,0,0},{0,0,0},{1,1,1});};
  assert(!water_blocked(water(-300,-300,300,300,600),isle));   // on the top terrace, at its height (its floor dug into it)
  assert(!water_blocked(water(-300,-300,300,300,800),isle));   // raised above it (its banks a mound on it)
  assert(!water_blocked(water(-300,-300,300,300,630),isle));   // a little over it
  assert(!water_blocked(water(100,-300,700,300,600),isle));    // over the edge of the top: the terrace below is lower
  assert(water_blocked(water(100,-300,700,300,400),isle));     // on the middle terrace, cutting into the top one's wall
  assert(!water_blocked(water(550,-300,900,300,400),isle));    // on the middle terrace, beside the wall
  assert(water_blocked(water(-300,-300,300,300,600),pillared)); // a pillar standing in it
  assert(!water_blocked(water(-300,-300,100,300,600),pillared)); // the pillar beside it
  assert(!water_blocked(Solid{},isle)&&!water_blocked(water(-300,-300,300,300,600),Solid{}));
 }
 { // issue #281: an L-shaped extension by its own triangles. Its box (0..2000 x 0..2000) covers its open corner, where a
  // villager, Link's start point, an object, a route, another bounds entry (an event area) and a wall of the room are;
  // its arms (300 wide along x and along z, 400 high) reach none of them: clear. Each of them on an arm: in the way.
  std::vector<Point> v;std::vector<std::array<uint16_t,3>> t;
  auto box=[&](Point lo,Point hi){const uint16_t o=uint16_t(v.size());for(unsigned k=0;k<8;++k)v.push_back({k&1?hi.x:lo.x,k&2?hi.y:lo.y,k&4?hi.z:lo.z});
   for(auto f:{std::array<uint16_t,4>{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}}){t.push_back({uint16_t(o+f[0]),uint16_t(o+f[1]),uint16_t(o+f[2])});t.push_back({uint16_t(o+f[0]),uint16_t(o+f[2]),uint16_t(o+f[3])});}};
  box({0,0,0},{2000,400,300});box({0,0,300},{300,400,2000});
  const Solid ell=place_solid(v,t,{0,0,0},{0,0,0},{1,1,1});const Bounds b=ell.bounds;
  Index idx;idx.coverage.push_back({"sea",11,true,true,true,{}});
  auto entry=[](const char*tag,const char*name,Point at){LayoutEntry e;e.stage="sea";e.room=11;e.tag=tag;e.name=name;e.position=at;return e;};
  idx.layout.push_back(entry("ACTR","Uo1",{1400,0,1400}));   // a villager
  idx.layout.push_back(entry("PLYR","Link",{1100,0,1700}));  // a start point
  idx.layout.push_back(entry("ACTR","Hbox",{1700,0,1000}));  // an object of the island (a name of no villager)
  idx.routes.push_back({"sea",11,"RPAT:40",{{900,0,2100},{2100,0,900}},kRouteHalfWidth,true,"boat"});  // across the corner
  idx.bounds.push_back({"sea",11,"event","area",{{1200,0,1200},{1300,100,1300}}});
  // a wall of the room standing in the corner (two triangles up to 600)
  auto wall=[&](float x0,float z0,float x1,float z1){for(int k=0;k<2;++k){std::array<Point,3> c=k?std::array<Point,3>{Point{x0,-50,z0},Point{x1,600,z1},Point{x0,600,z0}}:std::array<Point,3>{Point{x0,-50,z0},Point{x1,-50,z1},Point{x1,600,z1}};
   Bounds wb{{std::min(x0,x1),-50,std::min(z0,z1)},{std::max(x0,x1),600,std::max(z0,z1)}};idx.bounds.push_back({"sea",11,"collision","wall-"+std::to_string(idx.bounds.size()),wb,true,c});}};
  wall(1500,1300,1500,1700);
  const auto boxed=check("sea",11,b,idx);
  std::vector<std::string> kinds;for(const auto&c:boxed)kinds.push_back(c.kind);
  for(const char*k:{"villager","start point","object","boat route","event","collision"})assert(std::find(kinds.begin(),kinds.end(),k)!=kinds.end());
  assert(check("sea",11,b,idx,kClearance,&ell).empty());
  // each of them where an arm is: in the way again, one at a time
  auto only=[&](Index x){const auto c=check("sea",11,b,x,kClearance,&ell);return c.size()==1?c[0].kind:std::to_string(c.size());};
  {Index x=idx;x.layout[0].position={1400,0,200};assert(only(x)=="villager");}
  {Index x=idx;x.layout[1].position={150,0,1700};assert(only(x)=="start point");}
  {Index x=idx;x.layout[2].position={1700,0,380};assert(only(x)=="object");}             // its 120 reach (+50) from the arm's side at 300
  {Index x=idx;x.routes[0].points={{-100,0,1000},{900,0,1000}};assert(only(x)=="boat route");}  // through the z arm
  {Index x=idx;x.routes[0].points={{-100,0,1000},{900,0,1000}};x.routes[0].points[0].y=x.routes[0].points[1].y=2000;assert(check("sea",11,b,x,kClearance,&ell).empty());}  // high above it
  {Index x=idx;x.bounds[0].bounds={{1000,0,100},{1100,100,200}};assert(only(x)=="event");}
  const Index base=idx;
  {Index x=idx;x.bounds.resize(1);wall(1000,-100,1000,700);x.bounds.push_back(idx.bounds.back());x.bounds.push_back(idx.bounds[idx.bounds.size()-2]);
   x.bounds[0].bounds={{5000,0,5000},{5001,1,5001}};const auto c=check("sea",11,b,x,kClearance,&ell);assert(c.size()==1&&c[0].kind=="collision");}  // a wall through the x arm
  // ground it stands on (a floor under it, and a slope reaching 100 into it) is not in the way
  {Index x=idx;x.bounds.clear();x.layout.clear();x.routes.clear();
   x.bounds.push_back({"sea",11,"collision","floor",{{-500,0,-500},{2500,100,2500}},true,{Point{-500,0,-500},Point{2500,100,-500},Point{-500,0,2500}}});
   assert(check("sea",11,b,x,kClearance,&ell).empty());assert(check("sea",11,b,x).size()==1);}
  // the boat's route is said as a boat route; a villager's as before
  {Index x=base;x.routes[0].user.clear();x.routes[0].points={{-100,0,1000},{900,0,1000}};assert(only(x)=="villager route");}
 }
 { // issue #281: new ground joined flush to an island's hill. The island: a plateau (two floor triangles) at y 1000 over
  // x -2000..0, z -1000..1000, its cliff (two wall triangles, facing +x) at x 0 from the sea floor at -1000 up to it.
  // The extension: blocks (outward faces) from the sea floor up, reaching 200 into the plateau.
  Index isle;isle.coverage.push_back({"sea",11,true,true,true,{}});
  auto tri=[&](Point a,Point b,Point c){Bounds wb{{std::min({a.x,b.x,c.x}),std::min({a.y,b.y,c.y}),std::min({a.z,b.z,c.z})},{std::max({a.x,b.x,c.x}),std::max({a.y,b.y,c.y}),std::max({a.z,b.z,c.z})}};
   isle.bounds.push_back({"sea",11,"collision","t"+std::to_string(isle.bounds.size()),wb,true,{a,b,c}});};
  // the plateau, sloping by `slope` per unit toward -x (0: flat)
  auto island=[&](float slope){isle.bounds.clear();const float w=1000+2000*slope;
   tri({-2000,w,-1000},{-2000,w,1000},{0,1000,-1000});tri({0,1000,-1000},{-2000,w,1000},{0,1000,1000});
   tri({0,-1000,-1000},{0,1000,-1000},{0,-1000,1000});tri({0,1000,-1000},{0,1000,1000},{0,-1000,1000});};
  island(0);
  struct Mesh {std::vector<Point> v;std::vector<std::array<uint16_t,3>> t;
   // a block with outward faces; its top may slope (top + slope * -x) and lie `lift` higher
   void block(Point lo,Point hi,float slope=0){const uint16_t o=uint16_t(v.size());
    for(unsigned k=0;k<8;++k){const float x=k&1?hi.x:lo.x,z=k&4?hi.z:lo.z;v.push_back({x,k&2?hi.y-slope*x:lo.y,z});}
    for(auto f:{std::array<uint16_t,4>{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6},{0,4,6,2},{1,3,7,5}}){t.push_back({uint16_t(o+f[0]),uint16_t(o+f[1]),uint16_t(o+f[2])});t.push_back({uint16_t(o+f[0]),uint16_t(o+f[2]),uint16_t(o+f[3])});}}
   Solid solid()const{return place_solid(v,t,{0,0,0},{0,0,0},{1,1,1});}};
  auto run=[&](const Mesh&m,const Index&x){const Solid s=m.solid();return check("sea",11,s.bounds,x,kClearance,&s);};
  auto kinds=[](const std::vector<Conflict>&c){std::string k;for(const auto&x:c)k+=(k.empty()?"":",")+x.kind;return k;};
  // the block's top faces up (the own floor the wall rule looks for)
  {Mesh m;m.block({-200,-1000,-300},{2000,1000,300});const Solid s=m.solid();float h=0;assert(ground_under(s,{500,5000,0},h)&&h==1000);}
  // 1. flush ground: its top at the plateau's height (and up to kFlushStep above it) is not in the way; its sides go into
  //    the cliff under that floor
  {Mesh m;m.block({-200,-1000,-300},{2000,1000,300});assert(run(m,isle).empty());}
  {Mesh m;m.block({-200,-1000,-300},{2000,1000+kFlushStep-1,300});assert(run(m,isle).empty());}
  // 2. standing out of it is: 100 above the plateau (a step at the join), a parapet on the join, a block against the
  //    cliff whose top is lower than the plateau's (its sides cut the cliff where no floor of its own meets the island's)
  {Mesh m;m.block({-200,-1000,-300},{2000,1100,300});const auto c=run(m,isle);assert(kinds(c)=="collision");
   assert(c[0].message.find("the model rises out of the ground or goes through a wall there")!=std::string::npos&&c[0].message.find("up to 30 units above it, is fine")!=std::string::npos);}
  {Mesh m;m.block({-200,-1000,-300},{2000,1000,300});m.block({-100,990,-300},{-50,1300,300});const auto c=run(m,isle);assert(kinds(c)=="collision");
   // the triangles in the way, for the editor to mark: the parapet's (the second block's, from 12), not the new ground's
   assert(!c[0].triangles.empty()&&std::is_sorted(c[0].triangles.begin(),c[0].triangles.end()));for(uint32_t t:c[0].triangles)assert(t>=12&&t<24);}
  // (a parapet only resting on the island's ground, touching it, is not in its way: kTouch)
  {Mesh m;m.block({-200,-1000,-300},{2000,1000,300});m.block({-100,1000,-300},{-50,1300,300});assert(run(m,isle).empty());}
  {Mesh m;m.block({-100,-1000,-300},{2000,500,300});assert(kinds(run(m,isle))=="collision");}
  // 3. a villager on the plateau 100 from where the new ground joins: flush ground beside him is fine; a step of 100, or
  //    the parapet (standing 10 into the ground), within his reach is in his way (and says how high it rises)
  Index with=isle;{LayoutEntry e;e.stage="sea";e.room=11;e.tag="ACT0";e.name="Ko1";e.position={-300,1000,0};with.layout.push_back(e);}
  {Mesh m;m.block({-200,-1000,-300},{2000,1000,300});assert(run(m,with).empty());}
  {Mesh m;m.block({-200,-1000,-300},{2000,1100,300});const auto c=run(m,with);assert(kinds(c)=="villager,collision");
   assert(c[0].message=="a villager of the island (the model rises 100 units above its floor within 150 of it; ground flush with its floor, up to 30 above, is fine)");}
  {Mesh m;m.block({-200,-1000,-300},{2000,1000,300});m.block({-190,990,-300},{-170,1300,300});const auto c=run(m,with);assert(kinds(c)=="villager,collision");
   assert(!c[0].triangles.empty());for(uint32_t t:c[0].triangles)assert(t>=12&&t<24);}
  // a raised block beside him, out of his reach (200 away from the join, past 100 + the clearance)
  {Index far=with;far.layout[0].position={-400,1000,0};Mesh m;m.block({-200,-1000,-300},{2000,1000,300});m.block({-190,990,-300},{-170,1300,300});assert(kinds(run(m,far))=="collision");}
  // he would be inside it: the block's top over his head
  {Index under=with;under.layout[0].position={-100,1000,0};Mesh m;m.block({-200,-1000,-300},{2000,1100,300});assert(kinds(run(m,under))=="villager,collision");}
  // the same for Link's start point
  {Index start=isle;LayoutEntry e;e.stage="sea";e.room=11;e.tag="PLYR";e.name="Link";e.position={-300,1000,0};start.layout.push_back(e);
   Mesh m;m.block({-200,-1000,-300},{2000,1000,300});assert(run(m,start).empty());m.block({-190,990,-300},{-170,1300,300});assert(kinds(run(m,start))=="start point,collision");}
  // 4. on a slope: the plateau rises 0.3 per unit toward -x; a villager 50 from the cliff stands at 1015, the new ground
  //    continues the slope (up to 1060 where it reaches into the hill, 45 above his feet but buried in the hill): fine;
  //    the same 50 higher is in his way
  island(.3f);
  {Index slope=isle;LayoutEntry e;e.stage="sea";e.room=11;e.tag="ACT0";e.name="Ko1";e.position={-50,1015,0};slope.layout.push_back(e);
   Mesh m;m.block({-200,-1000,-300},{2000,1000,300},.3f);assert(run(m,slope).empty());
   Mesh up;up.block({-200,-1000,-300},{2000,1050,300},.3f);assert(kinds(run(up,slope))=="villager,collision");}
  island(0);
  // 5. route points the same: a villager route along the plateau 100 from the join, one walking across the join onto the
  //    new ground
  for(const auto&points:{std::vector<Point>{{-300,1000,-500},{-300,1000,500}},std::vector<Point>{{-800,1000,0},{800,1000,0}}}){
   Index walk=isle;walk.routes.push_back({"sea",11,"RPAT:7",points,kRouteHalfWidth,false});
   Mesh m;m.block({-200,-1000,-300},{2000,1000,300});assert(run(m,walk).empty());
   Mesh up;up.block({-200,-1000,-300},{2000,1100,300});const auto c=run(up,walk);assert(kinds(c)=="villager route,collision");
   assert(c[0].message=="a villager route of the island (the model rises 100 units above the route within 110 of it; ground flush with it, up to 30 above, is fine)");
   Mesh wall;wall.block({-200,-1000,-300},{2000,1000,300});wall.block({-190,990,-300},{-170,1300,300});assert(kinds(run(wall,walk))=="villager route,collision");}
  // 6. a boat's route at sea, 1000 out: an extension standing in the water across it is in the boat's way; an arch over it
  //    is not when its underside clears the boat's height (kBoatHeight plus the clearance), the boat's width to either side
  {Index sea=isle;sea.routes.push_back({"sea",11,"RPAT:40",{{1000,0,-3000},{1000,0,3000}},kBoatHalfWidth,true,"boat"});
   Mesh m;m.block({-200,-1000,-300},{2000,1000,300});const auto c=run(m,sea);assert(kinds(c)=="boat route");
   assert(c[0].message=="a boat route of the island (the model reaches into the boat's way: 350 to either side of its route, from 350 under the water line to 950 above it; the boat sails through whatever stands there)");
   // the arch: its deck from 960 up over x 500..1500, its feet in the water outside 1000 +- 350
   Mesh arch;arch.block({-200,-1000,-300},{500,1000,300});arch.block({500,960,-300},{1500,1000,300});arch.block({1500,-1000,-300},{2000,1000,300});assert(run(arch,sea).empty());
   Mesh low;low.block({-200,-1000,-300},{500,1000,300});low.block({500,900,-300},{1500,1000,300});low.block({1500,-1000,-300},{2000,1000,300});assert(kinds(run(low,sea))=="boat route");
   Mesh narrow;narrow.block({-200,-1000,-300},{700,1000,300});narrow.block({700,960,-300},{1300,1000,300});narrow.block({1300,-1000,-300},{2000,1000,300});assert(kinds(run(narrow,sea))=="boat route");
   // a walker's route at sea level keeps only its own height clear (a deck at 400 over the swimming fish's route)
   Index fish=isle;fish.routes.push_back({"sea",11,"RPAT:18",{{1000,-35,-3000},{1000,-35,3000}},kRouteHalfWidth,false});
   Mesh deck;deck.block({-200,-1000,-300},{800,1000,300});deck.block({800,400,-300},{1200,1000,300});deck.block({1200,-1000,-300},{2000,1000,300});assert(run(deck,fish).empty());
   Mesh deep;deep.block({-200,-1000,-300},{800,1000,300});deep.block({800,250,-300},{1200,1000,300});deep.block({1200,-1000,-300},{2000,1000,300});assert(kinds(run(deep,fish))=="villager route");}
 }
 { // issue #281: geometry that only touches is not in the way (kTouch). Two blocks side by side: face to face, a little apart
  // or a little into each other (less than kTouch) don't overlap; a unit into each other does
  auto box=[](Point lo,Point hi){std::vector<Point> v;std::vector<std::array<uint16_t,3>> t;
   for(unsigned k=0;k<8;++k)v.push_back({k&1?hi.x:lo.x,k&2?hi.y:lo.y,k&4?hi.z:lo.z});
   for(auto f:{std::array<uint16_t,4>{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6},{0,4,6,2},{1,3,7,5}}){t.push_back({f[0],f[1],f[2]});t.push_back({f[0],f[2],f[3]});}
   return std::make_pair(v,t);};
  const float X=-203000;  // out at sea, where a float's rounding is 1/64
  const auto [av,at]=box({X-500,0,-500},{X,1000,500});const Solid a=place_solid(av,at,{0,0,0},{0,0,0},{1,1,1});
  for(float d:{0.f,.1f,-.1f,.2f,-.2f}){const auto [bv,bt]=box({X+d,0,-200},{X+400,800,200});assert(!solids_overlap(a,place_solid(bv,bt,{0,0,0},{0,0,0},{1,1,1})));}
  for(float d:{-1.f,-30.f}){const auto [bv,bt]=box({X+d,0,-200},{X+400,800,200});assert(solids_overlap(a,place_solid(bv,bt,{0,0,0},{0,0,0},{1,1,1})));}
  // the same block made around its own origin and placed there (float rounding of the placement), and one turned 30
  // degrees about a's corner edge: touching along it is not overlapping
  {const auto [bv,bt]=box({-.3f,0,-200.7f},{399.7f,800,199.3f});assert(!solids_overlap(a,place_solid(bv,bt,{X+.3f,0,.7f},{0,0,0},{1,1,1})));}
  {const auto [bv,bt]=box({0,0,0},{400,800,300});assert(!solids_overlap(a,place_solid(bv,bt,{X,0,500},{0,30,0},{1,1,1})));
   assert(solids_overlap(a,place_solid(bv,bt,{X-5,0,495},{0,30,0},{1,1,1})));}
 }
 { // issue #281: new ground whose join corners are exactly the island's own (a modder's extension snapped to the island's
  // vertices). The island, far out at sea: a plateau whose edge turns at B; the cliff under its edge A-B leans out (a
  // little overhang), the next cliff turns away from B. The extension's top joins A-B and reaches out over the cliff, its
  // sides going down to the sea floor. Its corner B touched the next cliff only there; that counted as a crossing the
  // flush rule found no point of, and placement was refused unless the model was moved off the island's corners.
  const float X=-0x1.5d0006p+17f,Z=-0x1.31c3a2p+17f,ang=0x1.e1c274p+0f,len=0x1.05feb8p+9f,turn=0x1.4206dep+0f,lean=0x1.86e674p-3f,h=0x1.dcb77ap+10f,slope=-0x1.a995fp-4f,out=0x1.4bdce8p+8f;
  auto rot=[&](float x,float z,float y){return Point{X+x*std::cos(ang)-z*std::sin(ang),y,Z+x*std::sin(ang)+z*std::cos(ang)};};
  const Point A=rot(0,0,h),B=rot(0,len,h+slope*len),Gc=rot(-400,len*.5f,h+slope*len*.5f),C=rot(-300*std::cos(turn),len+300*std::sin(turn),h+slope*len);
  Index isle;isle.coverage.push_back({"sea",11,true,true,true,{}});
  auto tri=[&](Point p,Point q,Point r){Bounds wb{{std::min({p.x,q.x,r.x}),std::min({p.y,q.y,r.y}),std::min({p.z,q.z,r.z})},{std::max({p.x,q.x,r.x}),std::max({p.y,q.y,r.y}),std::max({p.z,q.z,r.z})}};
   isle.bounds.push_back({"sea",11,"collision","t"+std::to_string(isle.bounds.size()),wb,true,{p,q,r}});};
  auto down=[&](Point p,float ox,float oz){const Point o=rot(ox*lean*h,oz*lean*h,0);return Point{o.x-X+p.x,0,o.z-Z+p.z};};
  const Point Ad=down(A,1,0),Bd=down(B,1,0),Cd=down(C,std::sin(turn),std::cos(turn));
  tri(A,Gc,B);tri(B,Gc,C);tri(A,B,Bd);tri(A,Bd,Ad);tri(B,C,Cd);tri(B,Cd,Bd);
  // the extension, made around a rounded origin as an editor import places it; `in`: moved toward the island, `lift` up
  auto extension=[&](float in,float lift){const Point Ao=rot(out,0,A.y),Bo=rot(out,len,B.y),m=rot(-in,0,0),u{m.x-X,0,m.z-Z};
   std::vector<Point> v{A,B,Bo,Ao,{A.x,0,A.z},{B.x,0,B.z},{Bo.x,0,Bo.z},{Ao.x,0,Ao.z}};for(unsigned k=0;k<8;++k){v[k].x+=u.x;v[k].z+=u.z;if(k<4)v[k].y+=lift;}
   const std::vector<std::array<uint16_t,3>> t{{0,3,2},{0,2,1},{4,5,6},{4,6,7},{3,7,6},{3,6,2},{1,2,6},{1,6,5},{0,4,7},{0,7,3}};
   const Point at{std::round(A.x),0,std::round(A.z)};for(auto&p:v){p.x-=at.x;p.z-=at.z;}
   const Solid s=place_solid(v,t,at,{0,0,0},{1,1,1});return check("sea",11,s.bounds,isle,kClearance,&s);};
  // exactly on the island's corners, and a little off them either way: joined
  for(float in:{0.f,.05f,-.05f,.2f,-.2f})assert(extension(in,0).empty());
  // really into the island: 50 into the plateau and 100 above it (a step standing in it), or its top 100 under the
  // plateau's edge (its sides cut the cliff where no floor of its own meets the island's)
  assert(extension(50,100).size()==1&&extension(50,100)[0].kind=="collision");
  assert(extension(0,-100).size()==1&&extension(0,-100)[0].kind=="collision");
 }
 { // characters whose names are not a capital, a small letter and a digit: Windfall's kids and Tott are villagers too
  for(const char*n:{"UkD","UkB2","Mk","Tt","P1a"}){LayoutEntry e;e.tag="ACT0";e.name=n;assert(building_extent(e).keep==Keep::Villager);}
  LayoutEntry e;e.tag="ACT0";e.name="Ttx";assert(building_extent(e).keep==Keep::Object);
 }
 { // a boat of the layout (ikada, d_a_obj_ikada: its path in the home angle's high byte) names its route a boat route
  Bytes b=layout();tag(b,40,"ikad");tag(b,44,"a_h\0");u32(b,48,0x00080031);u16(b,64,0x0001);xyz(b,88,{-100,0,0});xyz(b,104,{100,0,0});
  Index x;std::string why;assert(add_native_layout(x,"test",0,b,&why)&&x.routes.size()==1&&x.routes[0].user=="boat");x.coverage[0].collision=true;
  const auto c=check("test",0,{{-1,-2,-3},{1,2,3}},x,0);assert(c.size()==1&&c[0].kind=="boat route"&&c[0].message=="a boat route of the island");
  u16(b,64,0x0100);Index y;assert(add_native_layout(y,"test",0,b,&why)&&y.routes[0].user.empty());  // path 1: not this route
 }
}
