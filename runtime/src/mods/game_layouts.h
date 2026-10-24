// The game's own layout art for guest mods: textures and pane geometry of the layout archives in the
// installed 2D language pack (Common/Pack/permanent_2d_<language>.pack), read on the player's machine at
// run time. Nothing of the game ships with the port or a mod; a mod names a layout, a texture or a pane.
//
// Used by the guest HUD (wwhd_hud_texture with WWHD_HUD_GAME, mod-sdk-v2.md "The game's layout art") and the
// structured settings query "layout-pane:<Layout>/<pane>". The pack is the one the game itself opened (the
// language source and content mods applied, before the port adds mod item icons).
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace game_layouts {

using Bytes = std::vector<uint8_t>;

struct Image { uint32_t width = 0, height = 0; Bytes rgba; bool srgb = false; };  // srgb: an sRGB format (RGB decoded on sampling)
struct Colour { uint8_t r = 0, g = 0, b = 0, a = 0; };
struct Material { Colour black, white{255, 255, 255, 255}; std::vector<std::string> textures; Colour shadow_white{255, 255, 255, 255}; };
struct TextStyle {
    float width = 0, height = 0;
    float shadow_x = 0, shadow_y = 0;
    bool shadow = false;
    Colour ink{0, 0, 0, 255}, shade{255, 255, 255, 100}; // linear RGB, straight alpha
};
struct Pane {
    TextStyle text;  // populated for txt1 panes
    std::string name, kind, texture, material;  // kind: pan1, pic1, txt1, wnd1, prt1, bnd1
    float x = 0, y = 0;                           // centre in root-pane coordinates (+y up, as in the layout)
    float width = 0, height = 0;                  // size with the accumulated scale
    float scale_x = 1, scale_y = 1, rotation = 0; // accumulated scale; the pane's own Z rotation (degrees)
    bool visible = true;                          // the pane and all its parents visible
    uint8_t alpha = 255;
    std::array<Colour, 4> colours{{{255,255,255,255}, {255,255,255,255}, {255,255,255,255}, {255,255,255,255}}}; // TL, TR, BL, BR, linear
};
struct Layout {
    std::map<std::string, Material> materials;
    std::vector<Pane> panes;  // in file order
    const Pane* find(const std::string& name) const;
};

// ---- pure parts (unit-tested with archives written by lyt_build) ----
// A .bflim as RGBA, drawn as the game draws its channels: BC4 alpha-only as white with alpha, BC4 luminance
// as opaque grey, BC5 as luminance (red) + alpha (green). Throws std::runtime_error with a short reason.
Image decode_bflim(const Bytes& flim);
// BC1 / BC2 / BC3 / BC4 / BC5 blocks (row-major, untiled) to RGBA; BC4 to (v, v, v, 255), BC5 to (r, g, 0, 255).
Bytes decode_bc(int kind, const Bytes& blocks, uint32_t w, uint32_t h);
// A .bflyt's materials and panes (positions accumulated through the pane tree).
Layout parse_bflyt(const Bytes& flyt);
// What ui2d draws for a texel with a material's black and white colours: black + (white - black) * texel,
// in linear light (material colours are linear values; an sRGB texel is decoded first), stored as sRGB like
// the HUD's PNG images. Alpha is interpolated as is.
void apply_material(Image& image, const Material& material);
// "<Layout>/<texture>[#<material>]" or "<Layout>/<pane>": layout and item names; false for anything else
// (path separators, "..", over 64 bytes, characters other than A-Z a-z 0-9 _ ^ + - .).
bool split_spec(const std::string& spec, std::string& layout, std::string& item, std::string& material);

// Lanczos-3 enlargement by 2-4 (premultiplied alpha, clamped edges) with an unsharp mask on the colour and a
// steeper alpha ramp, for drawing small soft game art large and crisp; the image as it is for another
// factor or above 2048 x 2048.
Image upscale(const Image& image, uint32_t factor);
// As split_spec, also taking a trailing "@2".."@4" (an enlargement factor; 1 without it).
bool split_spec(const std::string& spec, std::string& layout, std::string& item, std::string& material, uint32_t& factor);

// ---- the port ----
// The game opened a file (fs.cpp): remember the host path of its 2D language pack.
void note_open(const std::string& guest_path, const std::string& host_path);
// "<Layout>/<texture>[#<material>][@<2-4>]" as RGBA (at most 2048 x 2048; "@N" enlarges it N times with
// upscale()). False with a short reason.
bool texture(const std::string& spec, Image& out, std::string& error);
// "<Layout>/<texture>": whether the texture exists and its size, without decoding it.
bool texture_size(const std::string& spec, uint32_t& width, uint32_t& height);
// "<Layout>/<pane>". False when the pack, the layout or the pane is missing.
bool pane(const std::string& spec, Pane& out);

}  // namespace game_layouts
