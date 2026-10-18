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
 i.routes.push_back({"test",0,"cross",{{-100,10000,0},{100,10000,0}},0,false});assert(check("test",0,local,i,0).front().kind=="route");i.routes[0].points={{-100,0,50},{100,0,50}};assert(check("test",0,local,i,0).empty());i.routes[0].radius=50;assert(check("test",0,local,i,0).size()==1);
 Index ground;ground.coverage.push_back({"test",0,true,true,true,{}});ground.bounds.push_back({"test",0,"collision","floor",{{-100,0,-100},{100,0,100}}});
 assert(check("test",0,{{-1,0,-1},{1,2,1}},ground).empty()); // Floor contact is intentional.
 assert(!check("test",0,{{-1,-.05f,-1},{1,2,1}},ground).empty()); // Penetration remains invalid.
 ground.bounds[0].bounds.maximum.y=10;assert(!check("test",0,{{-1,0,-1},{1,2,1}},ground).empty()); // Sloping walls are protected.
 Index parsed;std::string why;assert(add_native_collision(parsed,"test",0,mesh(),{100,20,0},90,&why));assert(parsed.bounds.size()==1);assert(near(parsed.bounds[0].bounds.minimum.x,90));auto bad=mesh();u16(bad,0x34+36,3);size_t count=parsed.bounds.size();assert(!add_native_collision(parsed,"test",0,bad,{},0,&why));assert(parsed.bounds.size()==count);
 assert(add_native_layout(parsed,"test",0,layout(),{},0,&why));assert(parsed.routes.size()==1&&parsed.routes[0].points.size()==2);assert(near(parsed.routes[0].points[0].x,-100));assert(check("test",0,local,parsed,0).size()==1);
 bad=layout();u32(bad,80,1);assert(!add_native_layout(parsed,"test",0,bad,{},0,&why));assert(parsed.routes.size()==1);
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
 write_fixture("test_Stage.szs",sarc({{"stage.dzs",stage}}));write_fixture("test_Room0.szs",sarc({{"Room0.bfres",bfres({{"room.dzb",mesh()},{"room.dzr",layout()}})}}));auto loaded=load_native_room(temporary.string(),"test",0);bool complete=false;for(auto&c:loaded.coverage)if(c.room==0)complete=c.collision&&c.routes&&c.events;assert(complete);assert(loaded.bounds.size()==2);assert(near(loaded.bounds.back().bounds.minimum.x,20100));assert(check("test",0,{{-5000,0,-5000},{-4999,1,-4999}},loaded,0).empty());
 write_fixture("test_Room0.szs",{0,1,2});loaded=load_native_room(temporary.string(),"test",0);assert(check("test",0,local,loaded,0).front().kind=="coverage");assert(check("../test",0,local,load_native_room(temporary.string(),"../test",0)).front().kind=="coverage");std::filesystem::remove_all(temporary);
}
