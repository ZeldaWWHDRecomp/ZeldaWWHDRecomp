// SPDX-License-Identifier: MPL-2.0
// Link's colour masks: which texels and which BC colour endpoints of Link's textures are his tunic, his hair,
// his skin... computed on the player's machine from the player's own game files (Link.bfres in
// content/Common/Pack/permanent_3d.pack). No mask, texture or anything cut from them ships with the port, is
// committed or downloaded; the result is cached in the user folder (host::config_dir()/link_masks), keyed by
// the pack file and the algorithm version, and every mask carries the hash of the texture bytes it was made
// from, so a changed texture (another game build, a content mod) never gets a stale mask.
//
// Used by the online co-op tunics (coop_pose.cpp: each player's Link in his own colour) and meant for the
// "Link's colours" mod (a colour wheel for tunic, hair...): both recolour a copy of the texture's own BC
// blocks in memory with tint(), which moves only the endpoints of the chosen part.
//
// The textures (identical in the USA and EU games):
//   linktexS3TC  BC1 640x384, the hero's clothes (hair, skin, tunic with the hat, undershirt, pants, boots, buckle)
//   linktexbci4  BC3 640x384, the island clothes (the game swaps it in for linktexS3TC; same UV layout)
//   katsuraS3TC  BC1 32x32,   the hair piece worn with the island clothes (all hair)
//
// How a mask is made (classify(), from the texture alone):
//  1. level 0 decoded to sRGB colours, each texel in CIELAB;
//  2. regions: 4-neighbour texels joined while their colour differs by less than Params::join (Delta E), so
//     painted toon shading stays one region and the hard edges between parts split them;
//  3. small regions (anti-aliased edges, specks) merged into the most similar neighbour;
//  4. each region named by the nearest base colour of the texture's part palette (Lab, lightness weighed
//     less: shading changes lightness, parts differ in hue and chroma);
//  5. a region almost enclosed by a much larger region of another part with a near hue takes that part
//     (the ear's pink mark in the skin, the boots' dark strokes, the buckle swirl's highlights);
//  6. the hero's belt buckle has his hair's yellow: compute() tells it apart by the shape whose UVs draw it
//     (reassign()).
// Endpoints: on level 0 each endpoint takes the part of the texels that use it (BC index weights; the block's
// place found by untiling an index surface); on the mip levels (no texel places needed) the part of the
// nearest level-0 endpoint colour.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace link_masks {

using Bytes = std::vector<uint8_t>;

// A part of Link's look. kTunic: the hero's tunic and hat, or the island shirt; kBoots: also the island shoes and
// their armbands (the same grey, next to them in the texture); kEmblem, kSoles: the island clothes'; kBuckle: the
// hero's belt buckle (his hair's yellow). His belt is not in the texture (vertex colours).
enum class Part : uint8_t { kNone, kHair, kSkin, kTunic, kUndershirt, kPants, kBoots, kEmblem, kSoles, kBuckle, kCount };
const char* part_name(Part p);
inline constexpr int kParts = int(Part::kCount);
inline constexpr uint32_t kAlgorithm = 1;  // bump when the classification changes (old caches are recomputed)

struct Mask {
    std::string texture;                  // e.g. "linktexS3TC"
    uint32_t width = 0, height = 0;
    uint32_t format = 0;                  // GX2 surface format (0x31 BC1, 0x33 BC3, | 0x400 sRGB)
    uint32_t image_size = 0, mip_size = 0;  // bytes of level 0 and of the mip chain, as in the file and in memory
    uint64_t hash = 0;                    // hash_bytes() of the level-0 bytes then the mip bytes
    std::vector<uint8_t> texels;          // level 0, row-major width x height, a Part per texel
    std::vector<uint8_t> endpoints;       // 2 per BC block in the raw (tiled) order, image then mips: Part of colour 0, colour 1
    uint32_t base_rgb[kParts] = {};       // each part's own colour (its commonest texel colour), 0xRRGGBB; 0: not in the texture
    uint32_t count(Part p) const;         // texels of a part
};

// ---- pure parts (unit-tested with synthetic textures, link_masks_test) ----
uint64_t hash_bytes(const uint8_t* a, size_t na, const uint8_t* b = nullptr, size_t nb = 0);

struct PaletteEntry { Part part; uint32_t rgb; };
struct Params {
    float join = 6.0f;        // Delta E joining neighbour texels into one region
    float lightness = 0.4f;   // weight of L* against a* b* when naming a region
    uint32_t small = 30;      // regions below this many texels are merged into a neighbour
    float enclosed = 0.9f;    // share of a region's border (on other parts) on one part to take that part...
    float hue = 26.0f;        // ... when its hue is this close (degrees) and the encloser is 4x as large
    float mip_reach = 20.0f;  // a mip endpoint takes the part of the nearest level-0 endpoint colour within this (weighted Delta E)
};
// Parts of an RGBA image (row-major, sRGB values as decoded).
std::vector<uint8_t> classify(const uint8_t* rgba, uint32_t w, uint32_t h, const std::vector<PaletteEntry>& palette,
                              const Params& params = {});

// Each 4-connected area of part `from` outside `keep` (w x h, nonzero: kept) that `under` (w x h) reaches anywhere
// becomes part `to` (compute(): the hero's buckle, under its own shape's UVs, outside the other shapes').
void reassign(std::vector<uint8_t>& texels, uint32_t w, uint32_t h, Part from, Part to, const std::vector<uint8_t>& under,
              const std::vector<uint8_t>& keep);

// Raw BC blocks: kind 1 (BC1, 8 bytes) or 3 (BC3, 16 bytes; its colour half at +8). `order[i]` is the
// row-major block index of raw level-0 block i (block_order()). `level0` decoded texels' parts as above.
// Returns 2 parts per raw block over image + mips.
std::vector<uint8_t> label_endpoints(int kind, const uint8_t* image, uint32_t image_size, const uint8_t* mips, uint32_t mip_size,
                                     const std::vector<uint32_t>& order, uint32_t w, uint32_t h, const std::vector<uint8_t>& level0,
                                     const Params& params = {});
// Row-major block index of each raw block of a GX2 surface of raw_blocks elements (bw x bh blocks of bpp bits,
// tile mode, swizzle 0-7); ~0u for the surface's padding.
std::vector<uint32_t> block_order(uint32_t bw, uint32_t bh, uint32_t bpp, int tile_mode, uint32_t swizzle, uint32_t raw_blocks);
// BC1/BC3 row-major blocks to RGBA (colour only for BC3, alpha 255).
Bytes decode(int kind, const uint8_t* blocks, uint32_t w, uint32_t h);

// Recolour the endpoints of `part` in a copy of the texture's raw bytes (image_size bytes at image, mip_size at
// mips, laid out as the mask's). Each endpoint keeps its shading relative to the part's own colour: the hue
// turned by (want - base), saturation and value scaled towards the wanted colour's. BC1 blocks keep their
// colour mode (endpoints swapped with their indices when the order would flip). Pure, thread-agnostic.
void tint(const Mask& m, Part part, uint32_t want_rgb, uint8_t* image, uint8_t* mips);
// One RGB565 colour as tint() moves it (base, want: 0xRRGGBB).
uint16_t exchange(uint16_t cur, uint32_t base_rgb, uint32_t want_rgb);

// The masks of Link's textures from Link.bfres's bytes (the 3D pack's Link.szs > Link.bfres). Throws
// std::runtime_error on a file it can't read; a texture it doesn't know is left out.
std::vector<Mask> compute(const Bytes& link_bfres);
// The masks of the 3D pack's bytes (a SARC holding Link.szs), or of Link.szs's (Yaz0).
std::vector<Mask> compute_from_pack(const Bytes& pack3d);
std::vector<Mask> compute_from_szs(const Bytes& link_szs);
// One member of a SARC file, read without loading the rest (the 3D pack is 24 MB; Link.szs about 4 MB of it).
// Throws std::runtime_error.
Bytes sarc_member(const std::string& path, const std::string& name);
// Cache file contents (zlib-packed) and back; false on a file of another algorithm version or a bad one.
Bytes serialize(const std::vector<Mask>& masks);
bool deserialize(const Bytes& data, std::vector<Mask>& out);

// ---- the port (link_masks_port.cpp) ----
// The game opened a file (hle/fs.cpp): remember the host path of its 3D pack.
void note_open(const std::string& guest_path, const std::string& host_path);
// Start making the masks (read from the cache, or computed and cached) on a worker thread; does nothing once
// started or before the game opened its 3D pack. get() and find() call it.
void prepare();
// A texture's mask, nullptr until ready (or if the texture is not Link's).
const Mask* get(const std::string& texture);
// The mask of a texture by its bytes (e.g. guest memory): sizes first, then hash_bytes(); nullptr until ready
// or if no mask was made from these bytes. The last match is remembered by address and sizes, and checked
// with a hash of its first 256 bytes, so a repeat call costs next to nothing.
const Mask* find(const uint8_t* image, uint32_t image_size, const uint8_t* mips, uint32_t mip_size);
// Wait until prepare()'s work is done (tests, tools); true when masks are ready.
bool wait_ready(int timeout_ms);
// Once per frame on the game thread (interp.cpp): the test aid WWHD_LINK_COLOURS (link_masks_port.cpp);
// nothing without it.
void frame();

}  // namespace link_masks
