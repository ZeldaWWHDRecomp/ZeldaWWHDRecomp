#pragma once
namespace gfxvk::bc {
// One invocation decodes one 4x4 block (x, y: block column and row, z: slice) and writes its pixels
// that lie inside the image. Written for older mobile shader compilers: each block word is loaded once
// into a local with a fixed index, every selector and endpoint is cut out of a known word with a
// constant or bounded shift (never a dynamically chosen vector component or byte), there is no module-
// scope mutable state, no 64-bit or 8/16-bit types, no bitfieldExtract, no integer division by a
// non-constant and no signed division (signed BC4/5 divide magnitudes). Channel palettes are built once
// per block and the helpers called in the pixel loop have a single exit (see channel_palette). The
// earlier per-pixel decoder
// picked bytes out of the block with chained selects on module-scope variables; Adreno X1's Windows
// driver had already miscompiled a per-byte variant of it.
inline constexpr const char* shader = R"glsl(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=0,std430) readonly buffer Input { uint src[]; };
layout(set=0,binding=1,std430) writeonly buffer Output { uint dst[]; };
layout(push_constant) uniform Params { uint width; uint height; uint slices; uint mode; } p;
// 3-bit selector `pixel` of a BC3/BC4/BC5 channel block: 48 index bits from bit 16 of lo:hi. One exit,
// and every shift amount stays in 0..31 on both sides of each select.
uint selector3(uint lo, uint hi, uint pixel) {
    uint bit = 16u + pixel * 3u;
    bool high = bit >= 32u;
    uint fromLo = lo >> min(bit, 31u);
    uint straddle = (bit > 29u && !high) ? hi << ((32u - bit) & 31u) : 0u;
    uint fromHi = hi >> ((bit - 32u) & 31u);
    return (high ? fromHi : (fromLo | straddle)) & 7u;
}
int div7(int v) { return v < 0 ? -int(uint(-v) / 7u) : int(uint(v) / 7u); }
int div5(int v) { return v < 0 ? -int(uint(-v) / 5u) : int(uint(v) / 5u); }
int raw_endpoint(uint byte, bool sign) { int v = int(byte); return (sign && v >= 128) ? v - 256 : v; }
// The 8 values of a channel block (endpoints in the low 16 bits of lo; unsigned 0..255, signed
// -127..127), built once per block before the pixel loop, the CPU decoder's channel_palette(). The
// palette mode is decided once from the raw endpoints (signed -127 > -128 is the 8-value mode).
// Qualcomm's v762 compiler (Adreno 750) miscompiled the previous per-pixel decoder, a function with
// an early return per case called in the pixel loop: from the second pixel on, selector 6 of the
// 6-value mode came out as the 8-value interpolant (2a + 5b) / 7 instead of 0.
struct Palette { int v0, v1, v2, v3, v4, v5, v6, v7; };
Palette channel_palette(uint lo, bool sign) {
    int ra = raw_endpoint(lo & 255u, sign), rb = raw_endpoint((lo >> 8u) & 255u, sign);
    bool eight = ra > rb;
    int a = max(ra, -127), b = max(rb, -127);
    Palette p;
    p.v0 = a;
    p.v1 = b;
    p.v2 = eight ? div7(6 * a + b) : div5(4 * a + b);
    p.v3 = eight ? div7(5 * a + 2 * b) : div5(3 * a + 2 * b);
    p.v4 = eight ? div7(4 * a + 3 * b) : div5(2 * a + 3 * b);
    p.v5 = eight ? div7(3 * a + 4 * b) : div5(a + 4 * b);
    p.v6 = eight ? div7(2 * a + 5 * b) : (sign ? -127 : 0);
    p.v7 = eight ? div7(a + 6 * b) : (sign ? 127 : 255);
    return p;
}
// palette entry i as a byte (signed values in two's complement), by a chain of selects
uint pick(Palette p, uint i) {
    int v = p.v0;
    v = i == 1u ? p.v1 : v;
    v = i == 2u ? p.v2 : v;
    v = i == 3u ? p.v3 : v;
    v = i == 4u ? p.v4 : v;
    v = i == 5u ? p.v5 : v;
    v = i == 6u ? p.v6 : v;
    v = i == 7u ? p.v7 : v;
    return uint(v) & 255u;
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
    // channel palettes: BC4/BC5 red and BC3 alpha from w0:w1, BC5 green from w2:w3
    Palette first = channel_palette(w0, sign), second = channel_palette(w2, sign);
    for (uint pixel = 0u; pixel < 16u; ++pixel) {
        uint x = bx * 4u + (pixel & 3u), y = by * 4u + (pixel >> 2u);
        if (x >= p.width || y >= p.height) continue;
        uvec4 c = uvec4(0u, 0u, 0u, sign ? 127u : 255u);
        if (type >= 4u) {
            c.r = pick(first, selector3(w0, w1, pixel));
            if (type == 5u) c.g = pick(second, selector3(w2, w3, pixel));
        } else {
            uint i = (selectors >> (pixel * 2u)) & 3u;
            if (i == 0u) c.rgb = a;
            else if (i == 1u) c.rgb = b;
            else if (four) c.rgb = i == 2u ? (2u * a + b) / 3u : (a + 2u * b) / 3u;
            else if (i == 2u) c.rgb = (a + b) / 2u;
            else c = uvec4(0u);
            if (type == 2u) c.a = ((pixel < 8u ? (w0 >> (pixel * 4u)) : (w1 >> ((pixel - 8u) * 4u))) & 15u) * 17u;
            if (type == 3u) c.a = pick(first, selector3(w0, w1, pixel));
        }
        dst[(z * p.height + y) * p.width + x] = c.r | (c.g << 8u) | (c.b << 16u) | (c.a << 24u);
    }
}
)glsl";
}
