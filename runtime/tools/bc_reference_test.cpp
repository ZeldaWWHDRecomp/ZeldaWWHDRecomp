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
    // BC2: explicit 4-bit alpha, pixel n has alpha n*17; the colour half is the BC1 block above
    uint8_t bc2[16]={0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe,0,0xf8,0xe0,7,0xe4,0xe4,0xe4,0xe4};
    for(unsigned n=0;n<16;++n)assert(pixel(bc2,2,false,n)[3]==n*17);
    assert((pixel(bc2,2,false,0)==std::array<uint8_t,4>{255,0,0,0}));
    // BC3 alpha / BC4 red, 8-value mode (a0 > a1): selectors 0, 1, 2 on pixels 0, 1, 2
    uint8_t bc4[8]={255,0,0x88,0,0,0,0,0};
    assert((pixel(bc4,4,false,0)==std::array<uint8_t,4>{255,0,0,255}));
    assert(pixel(bc4,4,false,1)[0]==0&&pixel(bc4,4,false,2)[0]==218&&pixel(bc4,4,false,3)[0]==255);
    // 6-value mode (a0 <= a1): selector 7 on pixel 5 spans the index bytes 3 and 4 (bits 15..17) -> 255,
    // selector 6 on pixel 6 -> 0
    uint8_t six[8]={0,255,0,0x80,3|(6<<2),0,0,0};
    assert(channel(six,5,false)==255&&channel(six,6,false)==0&&channel(six,4,false)==0);
    uint8_t bc3[16]={255,0,0x88,0,0,0,0,0,0,0xf8,0xe0,7,0xe4,0xe4,0xe4,0xe4};
    assert((pixel(bc3,3,false,2)==std::array<uint8_t,4>{170,85,0,218}));
    // BC5: red from the first half, green from the second
    uint8_t bc5[16]={255,0,0x88,0,0,0,0,0,0,255,0,0x80,3|(6<<2),0,0,0};
    assert((pixel(bc5,5,false,5)==std::array<uint8_t,4>{255,255,0,255}));
    assert((pixel(bc5,5,false,2)==std::array<uint8_t,4>{218,0,0,255}));
    // the block decoder (production CPU path) equals the per-pixel reference on random blocks of every
    // format, and decode() places each block's pixels (tails included) where the per-pixel lookup does
    uint32_t random=12345;
    for(unsigned type=1;type<=5;++type)for(unsigned sign=0;sign<=(type>=4?1u:0u);++sign)for(unsigned round=0;round<2000;++round) {
        uint8_t block[16],out[64];
        for(auto& b:block){random=random*1664525+1013904223;b=uint8_t(random>>24);}
        decode_block(block,type,sign,out);
        for(unsigned n=0;n<16;++n){auto p=pixel(block,type,sign,n);assert(std::memcmp(out+4*n,p.data(),4)==0);}
    }
    for(unsigned type=1;type<=5;++type) {
        const unsigned w=9,h=6,slices=2,bx=3,by=2;
        std::vector<uint8_t> blocks(bx*by*slices*block_bytes(type));
        for(auto& b:blocks){random=random*1664525+1013904223;b=uint8_t(random>>24);}
        auto decoded=decode(blocks,w,h,slices,type);
        for(unsigned z=0;z<slices;++z)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            auto p=pixel(blocks.data()+((z*by+y/4)*bx+x/4)*block_bytes(type),type,false,(y%4)*4+x%4);
            assert(std::memcmp(decoded.data()+((z*h+y)*w+x)*4,p.data(),4)==0);
        }
    }
    bool rejected=false;try{decode({},4,4,1,1);}catch(...){rejected=true;}assert(rejected);
}
