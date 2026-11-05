#pragma once
/* SPDX-License-Identifier: MPL-2.0
 * From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (v0.9.9, MPL-2.0), runtime/src/gcmodel/, taken over
 * as it is; only the glue to this port changed. */
// Pictures for gcmodel.h: RGBA images, resizing (Lanczos, box), the "sharp" upscale for cel-shaded textures,
// BC1/BC3/BC4/BC5 encoding and decoding, filling see-through texels, and GX2 tiling through the runtime's own
// address library (LatteAddrLib). Ports of the fork's tools/gcmodels bc.py, upscale.py, gx2tile.cpp and parts of convert.py.
#include <cstdint>
#include <vector>

#include "gcm_io.h"

namespace gcmodel {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // RGBA rows
    Image() = default;
    Image(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0) {}
    uint8_t* at(int x, int y) { return &px[((size_t)y * w + x) * 4]; }
    const uint8_t* at(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
};

Image resize_lanczos(const Image& src, int w, int h);
Image resize_box(const Image& src, int w, int h);
// a smooth resize, then each channel's edges steepened (flat areas and soft shading stay)
Image upscale_sharp(const Image& src, int w, int h);

// BC blocks (GX2 formats: low 6 bits 0x31 BC1, 0x33 BC3, 0x34 BC4, 0x35 BC5); BC5 = red and green, BC4 = red
Bytes bc_encode(uint32_t fmt, const Image& img);
Image bc_decode(uint32_t fmt, int w, int h, const Bytes& data);

// GX2 surfaces: levels as rows of blocks one after the other <-> a BFRES texture's image and mip data
void gx2_tile(uint32_t fmt, int w, int h, int mips, uint32_t tileMode, uint32_t swizzle, const Bytes& linear, Bytes& image,
              Bytes& mipData, size_t imageSize, size_t mipSize);
// a level-0 surface's size in bytes (GX2CalculateSurfaceInfo)
size_t gx2_surface_size(uint32_t fmt, int w, int h, uint32_t tileMode);
Bytes gx2_untile_level0(uint32_t fmt, int w, int h, uint32_t tileMode, uint32_t swizzle, const uint8_t* image, size_t imageSize);

// see-through texels get the colour of the nearest visible one and become visible; inside each region (the
// texels a material covers) first; `under` (same size): its visible texels first (the hero texture under the
// casual one)
Image bleed(const Image& src, const std::vector<std::vector<uint8_t>>& regions, const Image* under);
// the texels a triangle list covers in a w x h texture (UVs wrap)
std::vector<uint8_t> raster_uv(const std::vector<std::array<std::array<double, 2>, 3>>& tris, int w, int h);

}  // namespace gcmodel
