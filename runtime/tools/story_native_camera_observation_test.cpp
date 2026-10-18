// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_camera_observation.h"
#include <cassert>
#include <stdexcept>
#include <algorithm>
#include <string>
#include <vector>
#include <cstdint>
#include <span>
namespace O=story_native_camera_observation;
void put(auto&b,size_t p,uint32_t x){for(unsigned i=0;i<4;++i)b[p+i]=uint8_t(x>>(24-i*8));}
int main(){
 story_native_camera_image::Program p;p.start_eye={0,10,20};p.end_eye={10,10,20};p.start_center={0,0,0};p.end_center={10,0,0};
 std::vector<uint8_t> bytes;std::string why;assert(story_native_camera_image::make(p,bytes,why));
 story_native_event_lease::Identity id{0x10010000,0x60010000,0xC20,1,2,3,4};O::Capture c;unsigned reads=0;
 assert(O::collect(id,[&](uint32_t at,std::span<uint8_t> out){++reads;assert(at==id.image&&out.size()==0xC20);std::copy(bytes.begin(),bytes.end(),out.begin());return true;},c));assert(reads==1);c.exclusive=true;
 assert(O::validate(O::Phase::Pristine,id,p,c).valid);
 auto running=c;put(running.image,0xE4,2);for(unsigned s=0;s<4;++s){size_t at=0xF0+s*80;put(running.image,at+0x38,s);put(running.image,at+0x3C,s==2?4:0);running.image[at+0x46]=2;}
 auto r=O::validate(O::Phase::Running,id,p,running);assert(r.valid&&r.camera_cut==2&&r.camera_action==4);
 auto moved=running;put(moved.image,0xF0+2*80+0x38,6);put(moved.image,0xF0+2*80+0x3C,5);put(moved.image,0xF0+3*80+0x38,5);put(moved.image,0xF0+3*80+0x3C,1);assert(O::validate(O::Phase::Running,id,p,moved).valid);
 // Exact immutable grammar: names, pools, chains, masks and barrier flags.
 for(size_t at:{size_t(0x230),size_t(0x460+0x30),size_t(0xB60),size_t(0xC00),size_t(0x230+5*80+0x28)}){auto bad=running;bad.image[at]^=1;assert(!O::validate(O::Phase::Running,id,p,bad).valid);}
 for(unsigned k=0;k<9;++k){auto bad=running;size_t at=0xF0+2*80;switch(k){case 0:put(bad.image,at+0x38,3);break;case 1:put(bad.image,at+0x3C,5);break;case 2:bad.image[at+0x40]=1;break;case 3:bad.image[at+0x42]=0xFF;break;case 4:bad.image[at+0x44]=1;break;case 5:bad.image[at+0x45]=2;break;case 6:bad.image[at+0x46]=3;break;case 7:bad.image[at+0x47]=2;break;case 8:put(bad.image,0xE4,3);break;}assert(!O::validate(O::Phase::Running,id,p,bad).valid);}
 auto stale=running;stale.identity.generation++;assert(!O::validate(O::Phase::Running,id,p,stale).valid);stale=running;stale.foreign=true;assert(!O::validate(O::Phase::Running,id,p,stale).valid);stale=running;stale.camera_depth=1;assert(!O::validate(O::Phase::Running,id,p,stale).valid);
 assert(!O::validate(O::Phase::Published,id,p,running).valid);assert(!O::validate(O::Phase::Running,id,p,c).valid);
 O::Capture out;assert(!O::collect(id,[](auto,auto){return false;},out)&&out.identity.image==0);assert(!O::collect(id,[](auto,auto)->bool{throw std::runtime_error("read");},out));auto overflow=id;overflow.image=0xFFFFFFF0;assert(!O::collect(overflow,[](auto,auto){assert(false);return true;},out));
}
