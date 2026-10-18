// SPDX-License-Identifier: MPL-2.0
#include "story_native_turn_camera_image.h"
#include <cmath>
#include <cstring>
namespace story_native_turn_camera_image {namespace {
uint32_t get(const std::vector<uint8_t>&b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
void put(std::vector<uint8_t>&b,size_t p,uint32_t v){for(unsigned i=0;i<4;++i)b[p+i]=uint8_t(v>>(24-i*8));}
void text(std::vector<uint8_t>&b,size_t p,const char*s){std::memcpy(b.data()+p,s,std::strlen(s));}
void fp(std::vector<uint8_t>&b,size_t p,float f){uint32_t v;std::memcpy(&v,&f,4);put(b,p,v);}
float readf(const std::vector<uint8_t>&b,size_t p){auto v=get(b,p);float f;std::memcpy(&f,&v,4);return f;}
bool point(story_native_camera_image::Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::fabs(p.x)<=350000&&std::fabs(p.y)<=350000&&std::fabs(p.z)<=350000;}
}
bool make(const Program&p,std::vector<uint8_t>&out,std::string&why){
 out.clear();why.clear();std::vector<uint8_t> base;
 if(!story_native_camera_image::make(p.camera,base,why))return false;
 if(!point(p.actor_a_target)||!point(p.actor_b_target)||!p.wait_timer||p.wait_timer>9000||p.timekeeper_timer>9000||p.timekeeper_timer<p.camera.hold_timer+p.camera.move_timer||p.timekeeper_timer<p.wait_timer){why="Invalid TURN target/wait/timekeeper bounds";return false;}
 std::vector<uint8_t>b(kImageBytes);const uint32_t offsets[]={0x40,0xF0,0x230,0x500,0xD80,0xE00,0xE40},counts[]={1,4,9,34,31,15,32};
 for(unsigned i=0;i<7;++i){put(b,i*8,offsets[i]);put(b,i*8+4,counts[i]);}
 auto copy=[&](size_t dst,size_t src,size_t n){std::memcpy(b.data()+dst,base.data()+src,n);};
 copy(0x40,0x40,0xB0);copy(0xF0,0xF0,0x140);copy(0x230,0x230,7*80);copy(0x500,0x460,28*64);copy(0xD80,0xB60,25*4);copy(0xE00,0xBD0,11*4);copy(0xE40,0xC00,32);
 // Names identify this separate program; no live C20 validator may accept it.
 std::memset(b.data()+0x40,0,32);text(b,0x40,"SDK_TURN_CAMERA");
 put(b,0xF0+0x30,7);put(b,0xF0+80+0x30,8);
 put(b,0xE00,p.wait_timer);put(b,0xE04,p.wait_timer);put(b,0xE14,p.timekeeper_timer);
 for(unsigned actor=0;actor<2;++actor){const unsigned cut=7+actor,first=28+actor*3;size_t at=0x230+cut*80;text(b,at,"TURN_TO_POS");put(b,at+0x20,cut);for(unsigned j=0;j<3;++j)put(b,at+0x28+j*4,0xFFFFFFFF);put(b,at+0x34,cut);put(b,at+0x38,first);put(b,at+0x3C,actor);
  const char*names[]={"Pos","Timer","TurnSpeed"};for(unsigned j=0;j<3;++j){size_t prop=0x500+(first+j)*64;text(b,prop,names[j]);put(b,prop+0x20,first+j);put(b,prop+0x24,j?3:1);put(b,prop+0x28,j?11+actor*2+j-1:25+actor*3);put(b,prop+0x2C,j?1:3);put(b,prop+0x30,j==2?0xFFFFFFFF:first+j+1);}
  const auto target=actor?p.actor_b_target:p.actor_a_target;fp(b,0xD80+(25+actor*3)*4,target.x);fp(b,0xD80+(26+actor*3)*4,target.y);fp(b,0xD80+(27+actor*3)*4,target.z);put(b,0xE00+(11+actor*2)*4,1);put(b,0xE00+(12+actor*2)*4,1024);
 }
 out=std::move(b);return true;
}
bool validate(const std::vector<uint8_t>&b,Program&out,std::string&why){
 out={};if(b.size()!=kImageBytes){why="Wrong TURN camera extent";return false;}
 // Decode bounded fixed locations, then reconstruct every immutable byte.
 Program p;auto&c=p.camera;c.hold_timer=get(b,0xE08);c.move_timer=get(b,0xE18);c.fovy=readf(b,0xD98);
 auto xyz=[&](unsigned i){return story_native_camera_image::Point{readf(b,0xD80+i*4),readf(b,0xD80+(i+1)*4),readf(b,0xD80+(i+2)*4)};};
 c.start_eye=xyz(0);c.start_center=xyz(3);c.end_eye=xyz(14);c.end_center=xyz(17);p.actor_a_target=xyz(25);p.actor_b_target=xyz(28);p.wait_timer=get(b,0xE00);p.timekeeper_timer=get(b,0xE14);
 std::vector<uint8_t> expected;if(!make(p,expected,why))return false;if(b!=expected){why="Noncanonical TURN camera grammar";return false;}out=p;why.clear();return true;
}
}
