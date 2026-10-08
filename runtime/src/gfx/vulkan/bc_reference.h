#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace gfxvk::bc {
inline unsigned block_bytes(unsigned type) { return type == 1 || type == 4 ? 8 : 16; }
inline uint32_t word(const uint8_t* p, unsigned n) { return uint32_t(p[n]) | uint32_t(p[n+1]) << 8; }
inline int channel(const uint8_t* p, unsigned pixel, bool sign) {
    int a = sign ? std::max(-127, int(int8_t(p[0]))) : p[0];
    int b = sign ? std::max(-127, int(int8_t(p[1]))) : p[1];
    uint64_t bits = 0; for (unsigned n = 0; n < 6; ++n) bits |= uint64_t(p[2+n]) << (8*n);
    unsigned i = (bits >> (3*pixel)) & 7;
    if (i == 0) return a;
    if (i == 1) return b;
    if (a > b) return ((8-int(i))*a + (int(i)-1)*b)/7;
    if (i < 6) return ((6-int(i))*a + (int(i)-1)*b)/5;
    return i == 6 ? (sign ? -127 : 0) : (sign ? 127 : 255);
}
inline std::array<uint8_t,4> pixel(const uint8_t* block, unsigned type, bool sign, unsigned pixel) {
    std::array<uint8_t,4> out{0,0,0,uint8_t(sign ? 127 : 255)};
    if (type >= 4) {
        out[0] = uint8_t(channel(block,pixel,sign));
        if (type == 5) out[1] = uint8_t(channel(block+8,pixel,sign));
        return out;
    }
    const uint8_t* color = block + (type == 1 ? 0 : 8);
    auto c0 = word(color,0), c1 = word(color,2);
    std::array<int,3> a{int(c0 >> 11),int(c0 >> 5 & 63),int(c0 & 31)};
    std::array<int,3> b{int(c1 >> 11),int(c1 >> 5 & 63),int(c1 & 31)};
    for (int n=0;n<3;++n) {
        a[n] = n == 1 ? (a[n]<<2)|(a[n]>>4) : (a[n]<<3)|(a[n]>>2);
        b[n] = n == 1 ? (b[n]<<2)|(b[n]>>4) : (b[n]<<3)|(b[n]>>2);
    }
    unsigned i = (color[4+pixel/4] >> (2*(pixel%4))) & 3;
    for (unsigned n=0;n<3;++n)
        out[n] = i == 0 ? a[n] : i == 1 ? b[n] : c0 > c1 || type != 1 ?
            (i == 2 ? (2*a[n]+b[n])/3 : (a[n]+2*b[n])/3) : i == 2 ? (a[n]+b[n])/2 : 0;
    if (type == 1 && c0 <= c1 && i == 3) out[3] = 0;
    if (type == 2) out[3] = ((block[pixel/2] >> (4*(pixel%2))) & 15)*17;
    if (type == 3) out[3] = uint8_t(channel(block,pixel,false));
    return out;
}
inline std::vector<uint8_t> decode(const std::vector<uint8_t>& blocks, unsigned w, unsigned h, unsigned slices, unsigned mode) {
    unsigned type=mode&255; bool sign=mode&256;
    if (type<1 || type>5 || !w || !h || !slices) throw std::invalid_argument("Invalid BC decode geometry");
    size_t bx=(size_t(w)+3)/4,by=(size_t(h)+3)/4;
    if (blocks.size()!=bx*by*slices*block_bytes(type)) throw std::invalid_argument("Invalid BC data length");
    std::vector<uint8_t> result(size_t(w)*h*slices*4);
    for (unsigned z=0;z<slices;++z) for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
        auto p=pixel(blocks.data()+((size_t(z)*by+y/4)*bx+x/4)*block_bytes(type),type,sign,(y%4)*4+x%4);
        std::copy(p.begin(),p.end(),result.begin()+((size_t(z)*h+y)*w+x)*4);
    }
    return result;
}
}
