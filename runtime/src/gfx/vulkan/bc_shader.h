#pragma once
namespace gfxvk::bc {
// One invocation decodes one 4x4 block (x, y: block column and row, z: slice) and writes its pixels
// that lie inside the image. Written for older mobile shader compilers: each block word is loaded once
// into a local with a fixed index, every selector and endpoint is cut out of a known word with a
// constant or bounded shift (never a dynamically chosen vector component or byte), there is no module-
// scope mutable state, no 64-bit or 8/16-bit types, no bitfieldExtract, no integer division by a
// non-constant and no signed division (signed BC4/5 divide magnitudes). The previous per-pixel decoder
// picked bytes out of the block with chained selects on module-scope variables; Adreno X1's Windows
// driver had already miscompiled a per-byte variant of it.
inline constexpr const char* shader = R"glsl(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0,std430) readonly buffer Input { uint src[]; };
layout(set=0,binding=1,std430) writeonly buffer Output { uint dst[]; };
layout(push_constant) uniform Params { uint width; uint height; uint slices; uint mode; } p;
// 3-bit selector `pixel` of a BC3/BC4/BC5 channel block: 48 index bits from bit 16 of lo:hi
uint selector3(uint lo, uint hi, uint pixel) {
    uint bit = 16u + pixel * 3u;
    if (bit >= 32u) return (hi >> (bit - 32u)) & 7u;
    uint v = lo >> bit;
    if (bit > 29u) v |= hi << (32u - bit);
    return v & 7u;
}
int signed_endpoint(uint b) { int v = int(b); if (v >= 128) v -= 256; return max(v, -127); }
int divide(int v, uint d) { return v < 0 ? -int(uint(-v) / d) : int(uint(v) / d); }
// channel value (unsigned: 0..255, signed: -127..127) for selector i of endpoints a, b
int channel(int a, int b, uint i, bool sign) {
    if (i == 0u) return a;
    if (i == 1u) return b;
    int s = int(i);
    if (a > b) return divide((8 - s) * a + (s - 1) * b, 7u);
    if (i < 6u) return divide((6 - s) * a + (s - 1) * b, 5u);
    if (i == 6u) return sign ? -127 : 0;
    return sign ? 127 : 255;
}
int channel_pixel(uint lo, uint hi, uint pixel, bool sign) {
    int a = sign ? signed_endpoint(lo & 255u) : int(lo & 255u);
    int b = sign ? signed_endpoint((lo >> 8u) & 255u) : int((lo >> 8u) & 255u);
    return channel(a, b, selector3(lo, hi, pixel), sign);
}
uvec3 rgb565(uint c) {
    uint r = (c >> 11u) & 31u, g = (c >> 5u) & 63u, b = c & 31u;
    return uvec3((r << 3u) | (r >> 2u), (g << 2u) | (g >> 4u), (b << 3u) | (b >> 2u));
}
void main() {
    uint bx = gl_GlobalInvocationID.x, by = gl_GlobalInvocationID.y, z = gl_GlobalInvocationID.z;
    uint blocksX = (p.width + 3u) / 4u, blocksY = (p.height + 3u) / 4u;
    if (bx >= blocksX || by >= blocksY || z >= p.slices) return;
    uint type = p.mode & 255u;
    bool sign = (p.mode & 256u) != 0u;
    bool wide = !(type == 1u || type == 4u);
    uint index = (z * blocksY + by) * blocksX + bx;
    uint base = wide ? index * 4u : index * 2u;
    uint w0 = src[base], w1 = src[base + 1u], w2 = 0u, w3 = 0u;
    if (wide) { w2 = src[base + 2u]; w3 = src[base + 3u]; }
    // BC1: colour in w0 (endpoints) / w1 (selectors); BC2/BC3: alpha in w0:w1, colour in w2 / w3
    uint ends = type == 1u ? w0 : w2, selectors = type == 1u ? w1 : w3;
    uint c0 = ends & 65535u, c1 = ends >> 16u;
    uvec3 a = rgb565(c0), b = rgb565(c1);
    bool four = c0 > c1 || type != 1u;
    for (uint pixel = 0u; pixel < 16u; ++pixel) {
        uint x = bx * 4u + (pixel & 3u), y = by * 4u + (pixel >> 2u);
        if (x >= p.width || y >= p.height) continue;
        uvec4 c = uvec4(0u, 0u, 0u, sign ? 127u : 255u);
        if (type >= 4u) {
            c.r = uint(channel_pixel(w0, w1, pixel, sign)) & 255u;
            if (type == 5u) c.g = uint(channel_pixel(w2, w3, pixel, sign)) & 255u;
        } else {
            uint i = (selectors >> (pixel * 2u)) & 3u;
            if (i == 0u) c.rgb = a;
            else if (i == 1u) c.rgb = b;
            else if (four) c.rgb = i == 2u ? (2u * a + b) / 3u : (a + 2u * b) / 3u;
            else if (i == 2u) c.rgb = (a + b) / 2u;
            else c = uvec4(0u);
            if (type == 2u) c.a = ((pixel < 8u ? (w0 >> (pixel * 4u)) : (w1 >> ((pixel - 8u) * 4u))) & 15u) * 17u;
            if (type == 3u) c.a = uint(channel_pixel(w0, w1, pixel, false));
        }
        dst[(z * p.height + y) * p.width + x] = c.r | (c.g << 8u) | (c.b << 16u) | (c.a << 24u);
    }
}
)glsl";
}
