// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_camera_image.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include <cstdint>
namespace N=story_native_camera_image;
uint32_t be(const std::vector<uint8_t>&b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
void put(std::vector<uint8_t>&b,size_t p,uint32_t n){for(unsigned i=0;i<4;++i)b[p+i]=uint8_t(n>>(24-8*i));}
size_t parameter(const std::vector<uint8_t>&b,unsigned cut,const char*name,uint32_t type){
 const size_t cuts=be(b,16),props=be(b,24);uint32_t index=be(b,cuts+cut*80+0x38);unsigned visits=0;
 while(index!=0xFFFFFFFFu){assert(index<28&&++visits<=28);size_t p=props+index*64;if(!std::strcmp(reinterpret_cast<const char*>(b.data()+p),name)){assert(be(b,p+0x24)==type&&be(b,p+0x2C)>0);uint32_t offset=be(b,p+0x28);return be(b,(type<3?4:type==3?5:6)*8)+(type==4?offset:offset*4);}index=be(b,p+0x30);}
 assert(false);return 0;
}
int main(){
 N::Program p;p.hold_timer=12;p.move_timer=48;p.start_eye={400,1350,-201250};p.start_center={870,1110,-201500};p.end_eye={350,1400,-201200};p.end_center={850,1110,-201480};p.fovy=55;
 std::vector<uint8_t>b,again;std::string why;N::Program decoded;assert(N::make(p,b,why)&&b.size()==0xC20);assert(N::validate(b,decoded,why)&&decoded.hold_timer==12&&decoded.move_timer==48&&decoded.end_eye.x==350&&decoded.fovy==55);assert(N::make(decoded,again,why)&&again==b);
 // Independent native getMySubstance-style property chain/pool projection.
 assert(be(b,0x230+2*80+0x3C)==6&&be(b,0x230+5*80+0x28)==6);assert(!std::strcmp(reinterpret_cast<const char*>(b.data()+0x230+2*80),"FIXEDFRM"));assert(!std::strcmp(reinterpret_cast<const char*>(b.data()+0x230+6*80),"UNITRANS"));
 assert(be(b,parameter(b,2,"Timer",3))==12&&be(b,parameter(b,6,"Timer",3))==48);assert(be(b,parameter(b,0,"Timer",3))==60&&be(b,parameter(b,3,"Timer",3))==60);
 assert(parameter(b,2,"Eye",1)==0xB60&&parameter(b,6,"StartEye",1)==0xB80);assert(be(b,parameter(b,6,"Cushion",0))==0x3F800000);
 assert(!std::strcmp(reinterpret_cast<const char*>(b.data()+parameter(b,2,"RelUseMask",4)),"--"));assert(!std::strcmp(reinterpret_cast<const char*>(b.data()+parameter(b,6,"RelUseMask",4)),"----"));assert(!std::strcmp(reinterpret_cast<const char*>(b.data()+parameter(b,6,"Trim",4)),"CINESCO"));
 assert(be(b,parameter(b,6,"BSpCurve",3))==0&&be(b,parameter(b,6,"TransType",3))==0);assert(be(b,parameter(b,2,"BGCheck",3))==0);
 // Wrong native action, property type/count/index/chain, barrier reuse,
 // mutable live state and reserved data are rejected as pristine inputs.
 for(size_t at:{size_t(0),size_t(7),size_t(0xE4),size_t(0xF0+0x38),size_t(0x230+2*80),size_t(0x230+5*80+0x2B),size_t(0x230+6*80+0x37),size_t(0x460+2*64+0x24),size_t(0x460+2*64+0x2C),size_t(0x460+2*64+0x28),size_t(0x460+2*64+0x30),size_t(0xB80),size_t(0xBC4),size_t(0xBF0),size_t(0xC10)}){auto corrupt=b;corrupt[at]^=0x40;assert(!N::validate(corrupt,decoded,why)&&!why.empty());}
 auto cyclic=b;put(cyclic,0x460+2*64+0x30,2);assert(!N::validate(cyclic,decoded,why));
 auto wrong_type=b;put(wrong_type,0x460+2*64+0x24,0);assert(!N::validate(wrong_type,decoded,why));
 for(size_t length:{size_t(0),size_t(0x38),size_t(0xB60),size_t(0xC1F)}){auto short_image=b;short_image.resize(length);assert(!N::validate(short_image,decoded,why));}auto long_image=b;long_image.push_back(0);assert(!N::validate(long_image,decoded,why));
 for(unsigned n=0;n<8;++n){auto bad=p;switch(n){case 0:bad.hold_timer=0;break;case 1:bad.move_timer=9001;break;case 2:bad.hold_timer=5000;bad.move_timer=5000;break;case 3:bad.fovy=std::numeric_limits<float>::quiet_NaN();break;case 4:bad.start_eye.x=350001;break;case 5:bad.end_eye=bad.end_center;break;case 6:bad.start_eye={-1,0,0};bad.start_center={0,0,0};bad.end_eye={1,0,0};bad.end_center={0,0,0};break;case 7:bad.fovy=4;break;}auto out=b;assert(!N::make(bad,out,why)&&out.empty());}
 auto changed=p;changed.end_eye.x-=1;changed.hold_timer+=1;assert(N::make(changed,again,why)&&again!=b&&N::validate(again,decoded,why));
 // Existing structural WAIT image is a different grammar/extent, never adopted.
 assert(!N::validate(std::vector<uint8_t>(0x520),decoded,why));
}
