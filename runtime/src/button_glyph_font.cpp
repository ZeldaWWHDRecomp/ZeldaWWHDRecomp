#include "button_glyph_font.h"
#include "button_glyph_art.h"
#include "runtime.h"
#include "gx2_texture_regs.h"
#include "gfx/vulkan/bc_reference.h"  // backend-independent CPU BC channel decoder

#include <algorithm>
#include <cmath>
#include <cstring>

namespace button_glyphs {
namespace {

struct Glyph {
    int width, height, left, advance;
    std::vector<uint8_t> alpha;
    float at(int x, int y) const {
        return x < 0 || y < 0 || x >= width || y >= height ? 0 : alpha[y*width+x];
    }
    float sample(float x, float y) const {
        const int ix = (int)std::floor(x), iy = (int)std::floor(y);
        const float fx = x-ix, fy = y-iy;
        return (at(ix,iy)*(1-fx)+at(ix+1,iy)*fx)*(1-fy)
             + (at(ix,iy+1)*(1-fx)+at(ix+1,iy+1)*fx)*fy;
    }
};

bool read_glyph(uint32_t record, Glyph& glyph) {
    const uint32_t sheet = ld32(record + 0x14);
    if (!sheet) return false;
    const auto& texture = *(const GX2::GX2Texture*)mem::ptr(sheet + 0x10);
    const auto& surface = texture.surface;
    const uint32_t width = surface.width, height = surface.height, depth = surface.depth;
    const uint32_t logical_w = ld16(record + 8), logical_h = ld16(record + 10);
    if (!width || !height || width > 4096 || height > 4096 || !depth || !surface.imagePtr) return false;
    if (!logical_w || !logical_h || logical_w % width || logical_h % height) return false;
    const uint32_t rx = logical_w/width, ry = logical_h/height;
    if (!rx || !ry || rx*ry > 4) return false;
    const uint32_t slice = ld8(record + 0x13) + (uint32_t)texture.viewFirstSlice;
    if (slice >= depth) return false;
    glyph = {(int)ld8(record + 5), (int)ld8(record + 7), (int)(int8_t)ld8(record + 4), (int)ld8(record + 6), {}};
    const uint32_t x0 = ld16(record + 12), cell_y = ld16(record + 14);
    if (!glyph.width || !glyph.height || x0+glyph.width > logical_w || cell_y+glyph.height > logical_h) return false;
    // The game's CharWriter converts cell Y to 1-(Y+height)/sheetHeight for the top vertex.
    const uint32_t y0 = logical_h-cell_y-glyph.height;
    const uint32_t format = (uint32_t)surface.format.value() & 0x3F;
    const bool compressed = format == 0x34;
    const uint32_t bytes = compressed ? 8 : format == 1 || format == 2 ? 1 : format == 5 || format == 7 || format == 11 ? 2 : format == 26 ? 4 : 0;
    if (!bytes) return false;
    LatteAddrLib::AddrSurfaceInfo_OUT geometry{};
    LatteAddrLib::GX2CalculateSurfaceInfo(surface.format, width, height, depth, surface.dim,
                                        surface.tileMode, surface.aa, 0, &geometry);
    const auto tile = geometry.hwTileMode;
    const uint32_t pitch = surface.pitch ? (uint32_t)surface.pitch : geometry.pitch;
    const uint32_t swizzle = surface.swizzle;
    LatteAddrLib::CachedSurfaceAddrInfo cached{};
    if (Latte::TM_IsMacroTiled(tile))
        LatteAddrLib::SetupCachedSurfaceAddrInfo(&cached, slice, 0, bytes*8, pitch, geometry.height,
                                               geometry.depth, 1, tile, false, (swizzle>>8)&1, (swizzle>>9)&3);
    glyph.alpha.resize(glyph.width*glyph.height);
    const uint32_t select = texture.compSel;
    for (int y = 0; y < glyph.height; y++) for (int x = 0; x < glyph.width; x++) {
        const uint32_t lx = x0+x, ly = y0+y, px = lx/rx, py = ly/ry;
        const uint32_t element_x = compressed ? px/4 : px, element_y = compressed ? py/4 : py;
        uint32_t offset;
        if (tile == Latte::E_HWTILEMODE::TM_LINEAR_GENERAL || tile == Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED)
            offset = LatteAddrLib::ComputeSurfaceAddrFromCoordLinear(element_x, element_y, slice, 0, bytes*8, pitch, geometry.height, geometry.depth);
        else if (!Latte::TM_IsMacroTiled(tile))
            offset = LatteAddrLib::ComputeSurfaceAddrFromCoordMicroTiled(element_x, element_y, slice, bytes*8, pitch, geometry.height, tile, false);
        else offset = LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiledCached(element_x, element_y, &cached);
        if (uint64_t(offset)+bytes > (uint32_t)surface.imageSize) return false;
        const uint8_t* p = mem::ptr(surface.imagePtr) + offset;
        std::array<uint8_t,4> channels{0,0,0,255};
        uint16_t packed = 0;
        if (bytes == 2) std::memcpy(&packed,p,2);
        if (compressed) channels[0] = (uint8_t)gfxvk::bc::channel(p,(py%4)*4+px%4,false);
        else if (format == 1) channels[0] = p[0];
        else if (format == 2) { channels[0] = (p[0]>>4)*17; channels[1] = (p[0]&15)*17; }
        else if (format == 5) channels[0] = packed>>8;
        else if (format == 7) { channels[0] = p[0]; channels[1] = p[1]; }
        else if (format == 11) for (int i = 0; i < 4; i++) channels[i] = ((packed>>(i*4))&15)*17;
        else std::copy(p,p+4,channels.begin());
        const uint32_t component = rx*ry > 1 ? (ly%ry)*rx+(lx%rx) : 3;
        uint32_t mapped = (select >> (24-8*component)) & 255;
        // Single-channel alpha fonts expose coverage in R; packed alpha sheets expose
        // multiple logical pixels through the sampler's component mapping.
        if (rx*ry == 1 && format == 1) mapped = 0;
        if (rx*ry == 1 && format == 7 && mapped > 1) mapped = 1;
        // NintendoWare's glyph sheet is bottom-up. Normalise the extracted glyph to a
        // top-down mask before composing our colour-font captions.
        glyph.alpha[(glyph.height-1-y)*glyph.width+x] = mapped < 4 ? channels[mapped] : mapped == 5 ? 255 : 0;
    }
    return true;
}

}  // namespace

std::vector<uint8_t> button_atlas(const std::array<uint32_t,kAtlasGlyphs>& records) {
    std::array<Glyph,kAtlasGlyphs> glyphs;  // L R 1 2 B T A 3
    for (int i = 0; i < kAtlasGlyphs; i++) if (!read_glyph(records[i],glyphs[i])) return {};
    std::vector<uint8_t> atlas(kAtlasWidth*kAtlasHeight*4);
    auto put = [&](int row, int column, int x, int y, uint32_t rgba) {
        const uint32_t offset = ((row*64+y)*kAtlasWidth+column*64+x)*4;
        atlas[offset] = rgba>>24; atlas[offset+1] = rgba>>16; atlas[offset+2] = rgba>>8; atlas[offset+3] = rgba;
    };
    constexpr int kCaptions = 11;  // rows 0-7 the shoulders, then A1 A2 A3 at item_name_row()
    for (int caption = 0; caption < kCaptions; caption++) {
        const int row = caption < 8 ? caption : item_name_row(caption - 8);
        const int shoulder = caption & 3;
        const bool xbox = caption >= 4 && caption < 8;
        // L/R, then 1/2 (numbered) or B/T (Xbox: bumper, trigger); the item names: A, then 1/2/3
        const int digit[3] = {2, 3, 7};
        const Glyph& letter = caption >= 8 ? glyphs[6] : glyphs[shoulder&1];
        const Glyph& suffix = caption >= 8 ? glyphs[digit[caption - 8]] : glyphs[(xbox ? 4 : 2) + (shoulder < 2 ? 0 : 1)];
        const int suffix_x = letter.advance;
        int left = 10000, top = 10000, right = -10000, bottom = -10000;
        auto bounds = [&](const Glyph& glyph, int origin) {
            for (int y = 0; y < glyph.height; y++) for (int x = 0; x < glyph.width; x++) if (glyph.at(x,y) > 0) {
                left = std::min(left, origin+glyph.left+x); right = std::max(right, origin+glyph.left+x+1);
                top = std::min(top,y); bottom = std::max(bottom,y+1);
            }
        };
        bounds(letter,0); bounds(suffix,suffix_x);
        if (right <= left || bottom <= top) return {};
        for (int column = 0; column < 2; column++) {
            const bool frame = column == 0;
            const float scale_x = 54.0f/(right-left), scale_y = (frame ? 34.0f : 48.0f)/(bottom-top);
            const float origin_x = 5, origin_y = frame ? 15 : 8;
            for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
                const float sx = left+(x-origin_x+0.5f)/scale_x-0.5f;
                const float sy = top+(y-origin_y+0.5f)/scale_y-0.5f;
                const uint32_t coverage = std::clamp<int>(std::lround(std::max(letter.sample(sx-letter.left,sy),
                                                  suffix.sample(sx-suffix_x-suffix.left,sy))),0,255);
                uint32_t rgba = 0xFFFFFF00 | coverage;
                if (frame) {
                    const uint32_t background = shoulder_background(x,y);
                    const uint32_t shade = background>>24;
                    const uint32_t ink = (36*coverage + shade*(255-coverage)+127)/255;
                    rgba = (ink<<24)|(ink<<16)|(ink<<8)|std::max(background&255,coverage);
                }
                put(row, column, x, y, rgba);
            }
        }
    }
    // the PlayStation shapes: our own geometry, no font involved
    for (int face = 0; face < 4; face++) for (bool colour : {true, false}) for (int column = 0; column < 2; column++)
        for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++)
            put(shape_row(face, colour), column, x, y, face_shape(face, colour, column == 0, x, y));
    return atlas;
}

}  // namespace button_glyphs
