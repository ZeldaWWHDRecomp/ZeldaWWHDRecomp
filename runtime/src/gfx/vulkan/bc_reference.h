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
// A 4-channel alpha/BC4 palette: the 8 values a 3-bit selector picks from.
inline std::array<int,8> channel_palette(const uint8_t* p, bool sign) {
    int a = sign ? std::max(-127, int(int8_t(p[0]))) : p[0];
    int b = sign ? std::max(-127, int(int8_t(p[1]))) : p[1];
    std::array<int,8> v{a, b};
    for (int i = 2; i < 8; ++i)
        v[i] = a > b ? ((8-i)*a + (i-1)*b)/7 : i < 6 ? ((6-i)*a + (i-1)*b)/5 : i == 6 ? (sign ? -127 : 0) : (sign ? 127 : 255);
    return v;
}
// One whole block, row by row (16 RGBA pixels): the palettes once per block. The production CPU
// decoder (devices without BC textures whose driver takes the compute decoder's rule, bc_decoder_choice.h);
// pixel() stays the per-pixel reference the tests compare it with.
inline void decode_block(const uint8_t* block, unsigned type, bool sign, uint8_t out[64]) {
    for (unsigned n = 0; n < 16; ++n) { out[4*n] = out[4*n+1] = out[4*n+2] = 0; out[4*n+3] = uint8_t(sign ? 127 : 255); }
    if (type >= 4) {
        for (unsigned c = 0; c < (type == 5 ? 2u : 1u); ++c) {
            const uint8_t* p = block + 8*c;
            const auto v = channel_palette(p, sign);
            uint64_t bits = 0; for (unsigned n = 0; n < 6; ++n) bits |= uint64_t(p[2+n]) << (8*n);
            for (unsigned n = 0; n < 16; ++n) out[4*n+c] = uint8_t(v[(bits >> (3*n)) & 7]);
        }
        return;
    }
    const uint8_t* color = block + (type == 1 ? 0 : 8);
    const auto c0 = word(color,0), c1 = word(color,2);
    std::array<std::array<uint8_t,4>,4> pal{};
    const int a[3]{int((c0>>11)<<3|(c0>>11)>>2), int((c0>>5&63)<<2|(c0>>5&63)>>4), int((c0&31)<<3|(c0&31)>>2)};
    const int b[3]{int((c1>>11)<<3|(c1>>11)>>2), int((c1>>5&63)<<2|(c1>>5&63)>>4), int((c1&31)<<3|(c1&31)>>2)};
    const bool four = c0 > c1 || type != 1;
    for (unsigned n = 0; n < 3; ++n) {
        pal[0][n] = uint8_t(a[n]); pal[1][n] = uint8_t(b[n]);
        pal[2][n] = uint8_t(four ? (2*a[n]+b[n])/3 : (a[n]+b[n])/2);
        pal[3][n] = uint8_t(four ? (a[n]+2*b[n])/3 : 0);
    }
    pal[0][3] = pal[1][3] = pal[2][3] = 255; pal[3][3] = four ? 255 : 0;
    std::array<int,8> alpha{};
    uint64_t abits = 0;
    if (type == 3) { alpha = channel_palette(block, false); for (unsigned n = 0; n < 6; ++n) abits |= uint64_t(block[2+n]) << (8*n); }
    for (unsigned n = 0; n < 16; ++n) {
        const auto& c = pal[(color[4+n/4] >> (2*(n%4))) & 3];
        std::copy(c.begin(), c.end(), out + 4*n);
        if (type == 2) out[4*n+3] = uint8_t(((block[n/2] >> (4*(n%2))) & 15)*17);
        if (type == 3) out[4*n+3] = uint8_t(alpha[(abits >> (3*n)) & 7]);
    }
}
inline std::vector<uint8_t> decode(const std::vector<uint8_t>& blocks, unsigned w, unsigned h, unsigned slices, unsigned mode) {
    unsigned type=mode&255; bool sign=mode&256;
    if (type<1 || type>5 || !w || !h || !slices) throw std::invalid_argument("Invalid BC decode geometry");
    size_t bx=(size_t(w)+3)/4,by=(size_t(h)+3)/4;
    if (blocks.size()!=bx*by*slices*block_bytes(type)) throw std::invalid_argument("Invalid BC data length");
    std::vector<uint8_t> result(size_t(w)*h*slices*4);
    uint8_t texels[64];
    for (size_t z=0;z<slices;++z) for (size_t y=0;y<by;++y) for (size_t x=0;x<bx;++x) {
        decode_block(blocks.data()+((z*by+y)*bx+x)*block_bytes(type),type,sign,texels);
        for (size_t row=0;row<4&&y*4+row<h;++row) {
            const size_t columns=std::min<size_t>(4,w-x*4);
            std::copy(texels+row*16,texels+row*16+columns*4,result.begin()+((z*h+y*4+row)*w+x*4)*4);
        }
    }
    return result;
}
}
