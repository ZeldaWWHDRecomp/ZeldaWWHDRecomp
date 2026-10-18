// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_image.h"
#include <array>
#include <cstring>
#include <stdexcept>
namespace story_native_event_image { namespace {
constexpr uint32_t none=0xffffffffu;
using Image=std::vector<uint8_t>;
uint32_t get(const Image& b,size_t p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
void put(Image& b,size_t p,uint32_t v){for(unsigned i=0;i<4;++i)b[p+i]=uint8_t(v>>(24-8*i));}
void name(Image& b,size_t p,const char* s){std::memcpy(b.data()+p,s,std::strlen(s));}
constexpr std::array<uint32_t,7> offsets={0x40,0xf0,0x230,0x410,0,0x510,0};
constexpr std::array<uint32_t,7> counts={1,4,6,4,0,4,0};
constexpr std::array<uint32_t,7> strides={0xb0,0x50,0x50,0x40,4,4,1};
constexpr std::array<std::array<uint32_t,3>,6> starts={{{none,none,none},{none,none,none},{none,none,none},{none,none,none},{0,1,3},{2,4,none}}};
Image build(uint32_t frames){
 Image b(0x520);
 for(unsigned i=0;i<7;++i){put(b,i*8,offsets[i]);put(b,i*8+4,counts[i]);}
 name(b,0x40,"SDK_PARALLEL");
 for(unsigned i=0;i<20;++i)put(b,0x40+0x2c+i*4,i<4?i:none);
 put(b,0x40+0x7c,4);put(b,0x40+0x88,5);put(b,0x40+0x8c,none);put(b,0x40+0x90,none);
 const char* names[]={"SDK_ACTOR_A","SDK_ACTOR_B","CAMERA","TIMEKEEPER"};
 const uint32_t kinds[]={0,0,2,4};
 for(unsigned i=0;i<4;++i){size_t p=0xf0+i*0x50;name(b,p,names[i]);put(b,p+0x24,i);put(b,p+0x2c,kinds[i]);put(b,p+0x30,i);put(b,p+0x38,none);put(b,p+0x3c,none);}
 for(unsigned i=0;i<6;++i){size_t p=0x230+i*0x50;name(b,p,i==3?"COUNTDOWN":"WAIT");put(b,p+0x20,i);for(unsigned j=0;j<3;++j)put(b,p+0x28+j*4,starts[i][j]);put(b,p+0x34,i);put(b,p+0x38,i<4?i:none);put(b,p+0x3c,i==3?4:i==4?5:none);}
 for(unsigned i=0;i<4;++i){size_t p=0x410+i*0x40;name(b,p,"Timer");put(b,p+0x20,i);put(b,p+0x24,3);put(b,p+0x28,i);put(b,p+0x2c,1);put(b,p+0x30,none);put(b,0x510+i*4,frames);}
 return b;
}
}
std::vector<uint8_t> make_wait_fixture(uint32_t frames){if(!frames||frames>32767)throw std::invalid_argument("native WAIT duration must be 1..32767");return build(frames);}
bool validate_wait_fixture(const Image& b,std::string& why){
 auto reject=[&](const char* s){why=s;return false;};
 if(b.size()!=0x520)return reject("wrong structural fixture extent");
 size_t end=0x38;
 for(unsigned i=0;i<7;++i){uint32_t off=get(b,i*8),n=get(b,i*8+4);if(off!=offsets[i]||n!=counts[i])return reject("unsupported section layout/count");if(!n){if(off)return reject("noncanonical empty section");continue;}if(off%16||off<end||n>(b.size()-off)/strides[i])return reject("invalid or overlapping section extent");end=off+size_t(n)*strides[i];}
 uint32_t frames=get(b,0x510);if(!frames||frames>32767)return reject("invalid timer");
 // This API intentionally accepts only the audited immutable fixture grammar.
 // Canonical comparison checks every name, reserved byte, index, parameter chain,
 // pool extent/type/count, completion flag and native barrier dependency. It is
 // not a validator for arbitrary retail event files or mutated live instances.
 if(b!=build(frames))return reject("noncanonical WAIT fixture field or reference");
 why.clear();return true;
}
}
