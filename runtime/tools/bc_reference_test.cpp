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
    // Channel palettes against the BC4 rule written out independently (truncating integer division),
    // on the known-answer blocks: every selector in every pixel position, both palette modes, signed
    // and unsigned, as BC4 red, BC5 red and green and BC3 alpha.
    auto spec_channel=[](const uint8_t* p,unsigned px,bool sign){
        const int ra=sign?int(int8_t(p[0])):p[0],rb=sign?int(int8_t(p[1])):p[1];
        const int a=ra<-127?-127:ra,b=rb<-127?-127:rb;
        uint64_t bits=0;for(unsigned n=0;n<6;++n)bits|=uint64_t(p[2+n])<<(8*n);
        const int i=int((bits>>(3*px))&7);
        const int weights8[8][2]={{7,0},{0,7},{6,1},{5,2},{4,3},{3,4},{2,5},{1,6}};
        const int weights6[6][2]={{5,0},{0,5},{4,1},{3,2},{2,3},{1,4}};
        if(ra>rb)return (weights8[i][0]*a+weights8[i][1]*b)/7;  // the raw endpoints pick the mode
        if(i<6)return (weights6[i][0]*a+weights6[i][1]*b)/5;
        return i==6?(sign?-127:0):(sign?127:255);
    };
    unsigned sixOnLaterPixel=0;
    for(unsigned type=3;type<=5;++type)for(unsigned sign=0;sign<=(type>=4?1u:0u);++sign) {
        const auto blocks=known_answer_blocks(type,256);
        for(size_t n=0;n<256;++n) {
            const uint8_t* block=blocks.data()+n*block_bytes(type);
            uint8_t out[64];decode_block(block,type,sign,out);
            for(unsigned px=0;px<16;++px) {
                const auto ref=pixel(block,type,sign,px);
                assert(std::memcmp(out+4*px,ref.data(),4)==0);
                const unsigned channel0=type==3?3:0;
                assert(out[4*px+channel0]==uint8_t(spec_channel(block,px,sign)));
                if(type==5)assert(out[4*px+1]==uint8_t(spec_channel(block+8,px,sign)));
                // the Adreno 750 (Qualcomm v762) case: selector 6 of the 6-value mode after the first pixel
                const uint8_t* half=type==5?block+8:block;
                uint64_t bits=0;for(unsigned k=0;k<6;++k)bits|=uint64_t(half[2+k])<<(8*k);
                if(!sign&&px>0&&((bits>>(3*px))&7)==6&&half[0]<half[1]&&(2*half[0]+5*half[1])/7!=0)++sixOnLaterPixel;
            }
        }
    }
    assert(sixOnLaterPixel>=100);  // the GPU self-test blocks reach the miscompiled case many times
    // The blocks the Adreno 750 decoded wrongly, rebuilt synthetically: endpoints a0 <= a1 and selector 6
    // in every pixel is 0 everywhere (the miscompiled shader wrote (2*a0 + 5*a1) / 7 from pixel 1 on).
    {
        uint8_t bc5g[16]={1,0,0,0,0,0,0,0, 30,255,0,0,0,0,0,0};
        uint64_t six=0;for(unsigned px=0;px<16;++px)six|=uint64_t(6)<<(3*px);
        for(unsigned k=0;k<6;++k)bc5g[10+k]=uint8_t(six>>(8*k));
        uint8_t out[64];decode_block(bc5g,5,false,out);
        for(unsigned px=0;px<16;++px)assert(out[4*px]==1&&out[4*px+1]==0&&out[4*px+3]==255);
        uint8_t bc3a[16]={2,32,0,0,0,0,0,0, 0,0xf8,0xe0,7,0xe4,0xe4,0xe4,0xe4};
        for(unsigned k=0;k<6;++k)bc3a[2+k]=uint8_t(six>>(8*k));
        decode_block(bc3a,3,false,out);
        for(unsigned px=0;px<16;++px)assert(out[4*px+3]==0);
        const uint64_t seven=six|(six>>1)|(six>>2);  // every selector 7
        for(unsigned k=0;k<6;++k)bc3a[2+k]=uint8_t(seven>>(8*k));
        decode_block(bc3a,3,false,out);
        for(unsigned px=0;px<16;++px)assert(out[4*px+3]==255);
    }
    // Signed: the mode follows the raw endpoints. -127 > -128 is the 8-value mode, so selector 7 is an
    // interpolant of -1.0 and -1.0 (-127), not the 6-value mode's +1.0 (127).
    {
        uint8_t bc4s[8]={0x81,0x80,0xff,0xff,0xff,0xff,0xff,0xff};
        for(unsigned px=0;px<16;++px)assert(channel(bc4s,px,true)==-127&&pixel(bc4s,4,true,px)[0]==0x81);
        bc4s[0]=0x80;bc4s[1]=0x81;  // -128 < -127: 6-value mode, selector 7 is +1.0
        for(unsigned px=0;px<16;++px)assert(channel(bc4s,px,true)==127);
    }
    bool rejected=false;try{decode({},4,4,1,1);}catch(...){rejected=true;}assert(rejected);
}
