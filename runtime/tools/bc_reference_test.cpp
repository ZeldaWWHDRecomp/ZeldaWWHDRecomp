#include "gfx/vulkan/bc_reference.h"
#include <cassert>
#include <cstring>
int main() {
    using namespace gfxvk::bc;
    // BC1 red endpoint, green endpoint, then interpolants and transparent mode.
    uint8_t redgreen[8]={0,0xf8,0xe0,7,0xe4,0xe4,0xe4,0xe4};
    assert((pixel(redgreen,1,false,0)==std::array<uint8_t,4>{255,0,0,255}));
    assert((pixel(redgreen,1,false,1)==std::array<uint8_t,4>{0,255,0,255}));
    assert((pixel(redgreen,1,false,2)==std::array<uint8_t,4>{170,85,0,255}));
    assert((pixel(redgreen,1,false,3)==std::array<uint8_t,4>{85,170,0,255}));
    uint8_t transparent[8]={0,0,255,255,255,255,255,255};
    assert((pixel(transparent,1,false,0)==std::array<uint8_t,4>{0,0,0,0}));
    for(unsigned type=1;type<=5;++type) {
        std::vector<uint8_t> blocks(block_bytes(type)*8,0);
        auto decoded=decode(blocks,7,5,2,type);assert(decoded.size()==7*5*2*4);
        for(unsigned n=0;n<decoded.size()/4;++n)assert(decoded[4*n]==0);
        if(type>=4) {std::fill(blocks.begin(),blocks.end(),128);auto p=pixel(blocks.data(),type,true,0);assert(p[0]==129&&p[3]==127);}
    }
    uint8_t alpha[8]={0,255,6,0,0,0,0,0};assert(channel(alpha,0,false)==0);
    alpha[2]=7;assert(channel(alpha,0,false)==255);
    alpha[0]=128;alpha[1]=127;alpha[2]=6;assert(channel(alpha,0,true)==-127);
    alpha[2]=7;assert(channel(alpha,0,true)==127);
    bool rejected=false;try{decode({},4,4,1,1);}catch(...){rejected=true;}assert(rejected);
}
