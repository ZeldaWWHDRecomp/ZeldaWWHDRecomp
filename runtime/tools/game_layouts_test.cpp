// game_layouts (runtime/src/mods/game_layouts.h): reading layout textures and panes back. The archives are
// written by the port's own layout writer (lyt_build), so no game file is needed.
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "game_layouts.h"
#include "lyt_build.h"

using Bytes = game_layouts::Bytes;

void log_msg(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vfprintf(stderr, fmt, a);
    va_end(a);
    fputc('\n', stderr);
}

static Bytes pattern(uint32_t w, uint32_t h) {
    Bytes px(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint8_t* p = &px[(size_t(y) * w + x) * 4];
            p[0] = uint8_t(x * 7), p[1] = uint8_t(y * 5), p[2] = uint8_t((x ^ y) * 3), p[3] = uint8_t(255 - x - y);
        }
    return px;
}

static void exact_rgba8() {
    for (const char* tile : {"linear", "1d", "tiled"})
        for (uint32_t swizzle : {0u, 3u, 7u}) {
            if (swizzle && std::string(tile) != "tiled") continue;
            const uint32_t w = 37, h = 21;
            const Bytes px = pattern(w, h);
            const game_layouts::Image img = game_layouts::decode_bflim(lyt_build::texture(px, w, h, "rgba8", tile, swizzle));
            assert(img.width == w && img.height == h && img.rgba == px);
        }
}

// BC is lossy: flat 4 x 4 blocks come back exactly; the channel meaning is checked on them
static void bc_flat_blocks() {
    const uint32_t w = 16, h = 8;
    Bytes px(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint8_t* p = &px[(size_t(y) * w + x) * 4];
            const uint32_t block = (x / 4) + 4 * (y / 4);
            p[0] = p[1] = p[2] = uint8_t(block * 30), p[3] = uint8_t(255 - block * 20);
        }
    // BC3: grey levels at 8-bit; 565 rounds them, so allow a small difference
    {
        const game_layouts::Image img = game_layouts::decode_bflim(lyt_build::texture(px, w, h, "bc3", "tiled", 2));
        for (size_t i = 0; i < px.size(); i++) assert(std::abs(int(img.rgba[i]) - int(px[i])) <= 4);
    }
    // BC5 luminance + alpha (the game's "^t" textures): drawn as grey with alpha
    {
        const game_layouts::Image img = game_layouts::decode_bflim(lyt_build::texture(px, w, h, "bc5la", "tiled", 5));
        for (size_t i = 0; i < px.size(); i += 4) {
            assert(img.rgba[i] == img.rgba[i + 1] && img.rgba[i] == img.rgba[i + 2]);
            assert(std::abs(int(img.rgba[i]) - int(px[i])) <= 1 && std::abs(int(img.rgba[i + 3]) - int(px[i + 3])) <= 1);
        }
    }
    // BC1: opaque colour blocks
    {
        Bytes opaque = px;
        for (size_t i = 3; i < opaque.size(); i += 4) opaque[i] = 255;
        const game_layouts::Image img = game_layouts::decode_bflim(lyt_build::texture(opaque, w, h, "bc1", "1d", 0));
        for (size_t i = 0; i < opaque.size(); i++) assert(std::abs(int(img.rgba[i]) - int(opaque[i])) <= 4);
    }
}

static void bc4_modes() {
    // 8-value mode (a0 > a1) and 6-value mode with the 0 / 255 entries
    const uint8_t eight[8] = {200, 10, 0x88, 0xC6, 0xFA, 0x00, 0x00, 0x00};
    const Bytes out8 = game_layouts::decode_bc(4, Bytes(eight, eight + 8), 4, 4);
    assert(out8[0] == 200 && out8[3] == 255);  // index 0: a0
    const uint8_t six[8] = {10, 200, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // every index 7: 255
    const Bytes out6 = game_layouts::decode_bc(4, Bytes(six, six + 8), 4, 4);
    for (size_t i = 0; i < out6.size(); i += 4) assert(out6[i] == 255);
}

static void bad_input() {
    bool threw = false;
    try { game_layouts::decode_bflim(Bytes(16, 0)); } catch (const std::exception&) { threw = true; }
    assert(threw);
    std::string l, i, m;
    assert(game_layouts::split_spec("Map_00/MapShip_00^t", l, i, m) && l == "Map_00" && i == "MapShip_00^t" && m.empty());
    assert(game_layouts::split_spec("BtnMapBalloon_00/r2c_b^t#P_DummyMapBig_00", l, i, m) && m == "P_DummyMapBig_00");
    assert(!game_layouts::split_spec("../x/y", l, i, m));
    assert(!game_layouts::split_spec("a/b/c", l, i, m));
    assert(!game_layouts::split_spec("Map_00/..", l, i, m));
    assert(!game_layouts::split_spec("Map_00/", l, i, m));
    assert(!game_layouts::split_spec("Map 00/x", l, i, m));
    assert(!game_layouts::split_spec("Map_00\\x/y", l, i, m));
}

static void layout_panes_and_material() {
    const std::string json = R"({"name": "T_00", "size": [1280, 720], "color_space": "linear",
      "textures": {"Tex_00": {"fill": {"color": [255, 255, 255, 255]}, "width": 8, "height": 8}},
      "materials": {"M_00": {"textures": [{"texture": "Tex_00"}], "black": [40, 30, 5, 0], "white": [184, 132, 49, 255]}},
      "panes": [
        {"type": "null", "name": "N_All_00", "size": [800, 800], "scale": [0.5, 0.5], "children": [
          {"type": "null", "name": "N_1F_00", "size": [30, 40], "translate": [100, -40], "visible": false, "children": [
            {"type": "picture", "name": "P_Room_00", "size": [78, 98], "translate": [-308, -295], "material": "M_00"},
            {"type": "picture", "name": "P_Room_01", "size": [40, 20], "translate": [10, 10], "origin": 5, "material": "M_00"}
          ]}
        ]}
      ]})";
    const auto files = lyt_build::build_files(json, ".");
    const game_layouts::Layout lay = game_layouts::parse_bflyt(files.at("blyt/T_00.bflyt"));
    // Authored native window materials can have trailing padding spaces.
    Bytes padded = files.at("blyt/T_00.bflyt");
    const auto name = std::search(padded.begin(), padded.end(), "M_00", "M_00" + 4);
    assert(name != padded.end());
    *(name + 4) = ' ';
    assert(game_layouts::parse_bflyt(padded).materials.count("M_00") == 1);
    const game_layouts::Pane* room = lay.find("P_Room_00");
    assert(room && room->kind == "pic1" && room->texture == "Tex_00^l" && room->material == "M_00");
    assert(std::fabs(room->x - (0.5f * (100 - 308))) < 1e-3f && std::fabs(room->y - (0.5f * (-40 - 295))) < 1e-3f);
    assert(std::fabs(room->width - 39) < 1e-3f && std::fabs(room->height - 49) < 1e-3f);
    assert(!room->visible);  // its parent is hidden
    const game_layouts::Pane* left = lay.find("P_Room_01");  // origin 5: left (1) and top (1 << 2)
    assert(left && std::fabs(left->x - (0.5f * 110 + 10)) < 1e-3f && std::fabs(left->y - (0.5f * -30 - 5)) < 1e-3f);
    assert(lay.find("N_All_00")->visible && !lay.find("Missing"));
    const auto& mat = lay.materials.at("M_00");
    assert(mat.black.r == 40 && mat.white.b == 49 && mat.white.a == 255 && mat.textures.size() == 1);
    game_layouts::Image img{1, 1, {255, 0, 128, 255}};
    game_layouts::apply_material(img, mat);
    // linear material colours, shown as sRGB: 184 -> 221; 30 -> 96; 5 + (49 - 5) * 128 / 255 -> 92
    assert(std::abs(int(img.rgba[0]) - 221) <= 1 && std::abs(int(img.rgba[1]) - 96) <= 1 && std::abs(int(img.rgba[2]) - 92) <= 1);
    assert(img.rgba[3] == 255 && img.srgb);
}

static void text_style() {
    const auto files = lyt_build::build_files(R"({"name":"Text", "size":[800,600], "panes":[
      {"type":"null", "name":"Parent", "size":[400,300], "scale":[0.5,0.75], "children":[
        {"type":"text", "name":"Label", "size":[200,80], "text":"Example", "font_size":[40,60],
         "text_material":{"white":[20,30,40,255]},
         "shadow":{"offset":[2,-4],"white":[100,200,50,128],"colors":[[255,128,255,200],[255,128,255,200]]}},
        {"type":"text", "name":"Plain", "size":[200,80], "text":"Plain"}
      ]}
    ]})", ".");
    const auto layout = game_layouts::parse_bflyt(files.at("blyt/Text.bflyt"));
    const auto& t = layout.find("Label")->text;
    assert(t.width == 20 && t.height == 45 && t.shadow);
    assert(t.shadow_x == 1 && t.shadow_y == 3); // layout +Y is up; screen +Y is down
    assert(t.ink.r == 20 && t.ink.g == 30 && t.ink.b == 40 && t.ink.a == 255);
    assert(t.shade.r == 100 && t.shade.g == 100 && t.shade.b == 50 && t.shade.a == 100);
    assert(!layout.find("Plain")->text.shadow);
}

static void enlarging() {
    // a constant colour stays constant (the sharpening leaves flat areas alone), the size grows; a transparent
    // texel's colour does not bleed
    game_layouts::Image flat{4, 3, Bytes(4 * 3 * 4, 0), true};
    for (size_t i = 0; i < flat.rgba.size(); i += 4) flat.rgba[i] = 200, flat.rgba[i + 1] = 100, flat.rgba[i + 2] = 50, flat.rgba[i + 3] = 255;
    const game_layouts::Image big = game_layouts::upscale(flat, 3);
    assert(big.width == 12 && big.height == 9 && big.srgb);
    for (size_t i = 0; i < big.rgba.size(); i += 4)
        assert(big.rgba[i] == 200 && big.rgba[i + 1] == 100 && big.rgba[i + 2] == 50 && big.rgba[i + 3] == 255);
    game_layouts::Image half = flat;
    for (size_t i = 0; i < half.rgba.size(); i += 4)
        if ((i / 4) % 4 >= 2) half.rgba[i] = 0, half.rgba[i + 1] = 255, half.rgba[i + 2] = 0, half.rgba[i + 3] = 0;
    const game_layouts::Image edge = game_layouts::upscale(half, 2);
    for (size_t i = 0; i < edge.rgba.size(); i += 4)
        if (edge.rgba[i + 3] > 8) assert(edge.rgba[i + 1] < 130);  // no green from the invisible half
    assert(game_layouts::upscale(flat, 1).width == 4 && game_layouts::upscale(flat, 5).width == 4);
    std::string l, it, m;
    uint32_t f = 0;
    assert(game_layouts::split_spec("BtnMapBalloon_00/r2c_b^t#P_DummyMapBig_00@3", l, it, m, f) && f == 3 && m == "P_DummyMapBig_00");
    assert(game_layouts::split_spec("Map_00/MapShip_00^t", l, it, m, f) && f == 1);
    assert(!game_layouts::split_spec("Map_00/MapShip_00^t@9", l, it, m, f));
}

int main() {
    text_style();
    enlarging();
    exact_rgba8();
    bc_flat_blocks();
    bc4_modes();
    bad_input();
    layout_panes_and_material();
    std::puts("game_layouts: ok");
    return 0;
}
