// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_turn_camera_image.h"
#include <cassert>
#include <limits>
#include <string>
#include <vector>
#include <cstdint>
namespace T=story_native_turn_camera_image;
uint32_t word(const auto&b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
void put(auto&b,size_t p,uint32_t x){for(unsigned i=0;i<4;++i)b[p+i]=uint8_t(x>>(24-i*8));}
int main(){
 T::Program p;p.camera.start_eye={0,100,200};p.camera.start_center={0,30,0};p.camera.end_eye={100,100,200};p.camera.end_center={100,30,0};p.actor_a_target={100,0,100};p.actor_b_target={-100,0,100};std::vector<uint8_t>b;std::string why;assert(T::make(p,b,why));T::Program decoded;assert(T::validate(b,decoded,why));assert(decoded.actor_a_target.x==100&&decoded.actor_b_target.x==-100&&decoded.wait_timer==30);
 // Independent consumer walk: staff initial cut -> TURN properties -> WAIT.
 for(unsigned n=0;n<2;++n){auto cut=word(b,0xF0+n*80+0x30);assert(cut==7+n);size_t at=word(b,16)+cut*80;assert(word(b,at+0x3C)==n);auto prop=word(b,at+0x38);size_t a=word(b,24)+prop*64;assert(word(b,a+0x24)==1&&word(b,a+0x2C)==3);auto timer=word(b,a+0x30);size_t t=0x500+timer*64;assert(word(b,t+0x24)==3&&word(b,0xE00+word(b,t+0x28)*4)==1);size_t speed=0x500+word(b,t+0x30)*64;assert(word(b,0xE00+word(b,speed+0x28)*4)==1024&&word(b,speed+0x30)==0xFFFFFFFF);}
 assert(word(b,0x230+2*80+0x3C)==6);assert(word(b,0x230+5*80+0x28)==6);assert(word(b,0x230+4*80+0x28)==0&&word(b,0x230+4*80+0x2C)==1);assert(word(b,0xE14)==300);
 std::vector<uint8_t> old;assert(story_native_camera_image::make(p.camera,old,why));assert(!T::validate(old,decoded,why));story_native_camera_image::Program oldp;assert(!story_native_camera_image::validate(b,oldp,why));
 for(size_t at:{size_t(0xF0+0x30),size_t(0x230+7*80),size_t(0x500+28*64+0x2C),size_t(0x500+29*64+0x28),size_t(0xE00+12*4),size_t(0x230+5*80+0x28),size_t(0xDFD),size_t(0xE3D)}){auto bad=b;bad[at]^=1;assert(!T::validate(bad,decoded,why));}
 auto bad=b;put(bad,0x500+30*64+0x30,28);assert(!T::validate(bad,decoded,why));bad=b;bad.pop_back();assert(!T::validate(bad,decoded,why));
 auto invalid=p;invalid.actor_a_target.x=std::numeric_limits<float>::infinity();assert(!T::make(invalid,old,why));invalid=p;invalid.timekeeper_timer=10;assert(!T::make(invalid,old,why));invalid=p;invalid.wait_timer=9001;assert(!T::make(invalid,old,why));
}
