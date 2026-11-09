// SPDX-License-Identifier: MPL-2.0
// Link's colour masks (runtime/src/mods/link_masks.h) on synthetic textures made here (no game data): the
// classification (toon shading kept in one part, an enclosed mark, anti-aliased edges), block_order against
// lyt_build's tiling, endpoint labels, tint (colours, BC1 colour modes), exchange and the cache format.
// With WWHD_GAME_DIR=<game> it also makes the masks of the player's own Link and prints their sizes and the
// time taken (the masks stay in memory; CI has no game files).
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>

#include "mods/link_masks.h"
#include "mods/lyt_build.h"

void log_msg(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vfprintf(stderr, fmt, a);
    va_end(a);
    fputc('\n', stderr);
}

using namespace link_masks;

static int failures = 0;
#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
            failures++;                                                           \
        }                                                                         \
    } while (0)

static uint16_t rgb565(uint32_t rgb) {
    return uint16_t(((rgb >> 19) & 31) << 11 | ((rgb >> 10) & 63) << 5 | ((rgb >> 3) & 31));
}
static void px(Bytes& img, uint32_t w, uint32_t x, uint32_t y, uint32_t rgb) {
    uint8_t* p = &img[(size_t(y) * w + x) * 4];
    p[0] = uint8_t(rgb >> 16), p[1] = uint8_t(rgb >> 8), p[2] = uint8_t(rgb), p[3] = 255;
}
static uint32_t mix(uint32_t a, uint32_t b, double t) {
    uint32_t o = 0;
    for (int s = 0; s < 24; s += 8) o |= uint32_t(std::lround((a >> s & 255) * (1 - t) + (b >> s & 255) * t)) << s;
    return o;
}
static const std::vector<PaletteEntry> kHero = {{Part::kHair, 0xFFEF10}, {Part::kSkin, 0xF7DB9C}, {Part::kTunic, 0x5AB24A},
                                                {Part::kUndershirt, 0xADE342}, {Part::kPants, 0xFFFFFF}, {Part::kBoots, 0x944908}};

// A 96 x 48 toon atlas: tunic green (left) with a soft dark stroke, skin (top right) with a pink mark inside, hair
// yellow (bottom right) with a soft darker swirl and a bright highlight inside it, boots brown strip at the bottom,
// a 1-texel anti-aliased seam between tunic and skin.
static Bytes atlas(uint32_t& w, uint32_t& h, std::vector<uint8_t>& truth) {
    w = 96, h = 48;
    Bytes img(size_t(w) * h * 4);
    truth.assign(size_t(w) * h, 0);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t c;
            Part p;
            if (y >= 40) c = 0x944908, p = Part::kBoots;
            else if (x < 48) {
                // the tunic, a dark stroke painted in with a soft edge (distance from the line y = x / 2 + 6)
                const double d = std::fabs(double(y) - (x / 2.0 + 6));
                c = mix(0x5AB24A, 0x219E39, std::clamp(1.0 - (d - 1) / 4, 0.0, 1.0)), p = Part::kTunic;
            } else if (y < 20) {
                c = 0xF7DB9C, p = Part::kSkin;
                if (x >= 60 && x < 76 && y >= 6 && y < 14) c = 0xF7976B;  // the mark (hard edge)
            } else {
                // hair: a soft ring of darker yellow, a bright highlight inside the ring
                const double r = std::hypot(double(x) - 72, double(y) - 30);
                c = mix(0xFFEF10, 0xB79F00, std::clamp(1.0 - std::fabs(r - 6) / 3, 0.0, 1.0)), p = Part::kHair;
                if (r < 2.5) c = 0xFFF773;
            }
            if (x == 48 && y < 40) c = mix(0x5AB24A, 0xF7DB9C, 0.5), p = y < 20 ? Part::kSkin : Part::kHair;  // the seam
            px(img, w, x, y, c);
            truth[size_t(y) * w + x] = uint8_t(p);
        }
    return img;
}

static void test_classify() {
    uint32_t w, h;
    std::vector<uint8_t> truth;
    const Bytes img = atlas(w, h, truth);
    const std::vector<uint8_t> got = classify(img.data(), w, h, kHero);
    size_t agree = 0, seam = 0;
    for (size_t i = 0; i < got.size(); i++) {
        if (i % w == 48) { seam++; continue; }  // (the seam's texels may go either way)
        agree += got[i] == truth[i];
    }
    CHECK(agree == got.size() - seam);
    CHECK(got[size_t(10) * w + 66] == uint8_t(Part::kSkin));   // the enclosed mark
    CHECK(got[size_t(30) * w + 72] == uint8_t(Part::kHair));   // the highlight
    CHECK(got[size_t(30) * w + 78] == uint8_t(Part::kHair));   // the dark ring
    CHECK(got[size_t(16) * w + 20] == uint8_t(Part::kTunic));  // the dark stroke
    for (uint8_t p : got) CHECK(p != uint8_t(Part::kNone));
    // a texture of one colour: all of it the nearest part
    Bytes one(16 * 16 * 4);
    for (uint32_t i = 0; i < 256; i++) px(one, 16, i % 16, i / 16, 0x63696B);
    const auto g1 = classify(one.data(), 16, 16, {{Part::kBoots, 0x63696B}, {Part::kHair, 0xFFEF10}});
    CHECK(std::count(g1.begin(), g1.end(), uint8_t(Part::kBoots)) == 256);
    CHECK(classify(one.data(), 16, 16, {}).size() == 256);
}

static void test_reassign() {
    // 8 x 8 hair, a skin column at x = 5 splitting it; `under` reaches the left area, `keep` holds a 2 x 2 patch in it:
    // the left area but the patch becomes the buckle, the patch and the right area stay hair
    std::vector<uint8_t> t(64, uint8_t(Part::kHair)), under(64, 0), keep(64, 0);
    for (int y = 0; y < 8; y++) t[size_t(y) * 8 + 5] = uint8_t(Part::kSkin);
    under[size_t(7) * 8 + 0] = 1;
    keep[size_t(3) * 8 + 2] = keep[size_t(3) * 8 + 3] = keep[size_t(4) * 8 + 2] = keep[size_t(4) * 8 + 3] = 1;
    reassign(t, 8, 8, Part::kHair, Part::kBuckle, under, keep);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            const uint8_t got = t[size_t(y) * 8 + size_t(x)];
            if (x == 5) CHECK(got == uint8_t(Part::kSkin));
            else if (x > 5 || keep[size_t(y) * 8 + size_t(x)]) CHECK(got == uint8_t(Part::kHair));
            else CHECK(got == uint8_t(Part::kBuckle));
        }
}

static void test_block_order() {
    // lyt_build writes a tiled BC1 surface; block_order must say where each raw block lands when untiled
    uint32_t w = 128, h = 64;
    Bytes img(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) px(img, w, x, y, (x / 4 * 7 % 256) << 16 | (y / 4 * 13 % 256) << 8 | 0x40);
    for (uint32_t swz : {0u, 2u, 5u}) {
        const Bytes flim = lyt_build::texture(img, w, h, "bc1", "tiled", swz);
        const size_t f = flim.size() - 0x28;
        const uint32_t size = uint32_t(flim[f + 0x24]) << 24 | uint32_t(flim[f + 0x25]) << 16 | uint32_t(flim[f + 0x26]) << 8 | flim[f + 0x27];
        const Bytes raw(flim.begin(), flim.begin() + size);
        const Bytes lin = lyt_build::untile(raw, w / 4, h / 4, 64, 4, swz);
        CHECK(!lin.empty());
        const std::vector<uint32_t> order = block_order(w / 4, h / 4, 64, 4, swz, size / 8);
        size_t real = 0, same = 0;
        for (size_t i = 0; i < order.size(); i++) {
            if (order[i] == ~0u) continue;
            real++;
            same += !memcmp(&raw[i * 8], &lin[size_t(order[i]) * 8], 8);
        }
        CHECK(real == size_t(w / 4) * (h / 4));
        CHECK(same == real);
    }
    const auto lin_order = block_order(4, 2, 64, 1, 0, 8);  // linear: the identity
    for (uint32_t i = 0; i < 8; i++) CHECK(lin_order[i] == i);
}

// one BC1 block: endpoints, 16 2-bit indices
static void block(uint8_t* b, uint16_t c0, uint16_t c1, uint32_t idx) {
    b[0] = uint8_t(c0), b[1] = uint8_t(c0 >> 8), b[2] = uint8_t(c1), b[3] = uint8_t(c1 >> 8);
    for (int i = 0; i < 4; i++) b[4 + i] = uint8_t(idx >> (8 * i));
}

static void test_endpoints_and_tint() {
    // 8 x 4 texels, 2 blocks, linear: block 0 tunic (left 2 columns index 0: green) + pants (index 1: white);
    // block 1 all hair. Mips: a green block, a zero (padding) block.
    const uint16_t green = rgb565(0x5AB24A), white = rgb565(0xFFFFFF), yellow = rgb565(0xFFEF10);
    Bytes image(16), mips(16);
    uint32_t idx0 = 0;
    for (int t = 0; t < 16; t++) idx0 |= uint32_t(t % 4 >= 2) << (2 * t);  // columns 2-3 index 1
    // 4-colour mode needs c0 > c1: white (0xFFFF) > green
    block(&image[0], white, green, idx0 ^ 0x55555555u);  // columns 0-1 index 1 (green), 2-3 index 0 (white)
    block(&image[8], yellow, yellow, 0);
    block(&mips[0], green, rgb565(0x219E39), 0xAAAAAAAAu);
    std::vector<uint8_t> level0(8 * 4);
    for (uint32_t y = 0; y < 4; y++)
        for (uint32_t x = 0; x < 8; x++) level0[y * 8 + x] = uint8_t(x >= 4 ? Part::kHair : x < 2 ? Part::kTunic : Part::kPants);
    const std::vector<uint32_t> order = {0, 1};
    const auto ep = label_endpoints(1, image.data(), 16, mips.data(), 16, order, 8, 4, level0);
    CHECK(ep.size() == 8);
    CHECK(ep[0] == uint8_t(Part::kPants) && ep[1] == uint8_t(Part::kTunic));
    CHECK(ep[2] == uint8_t(Part::kHair) && ep[3] == uint8_t(Part::kHair));
    CHECK(ep[4] == uint8_t(Part::kTunic) && ep[5] == uint8_t(Part::kTunic));  // the nearest level-0 colour
    CHECK(ep[6] == uint8_t(Part::kNone) && ep[7] == uint8_t(Part::kNone));    // padding

    Mask m;
    m.texture = "test", m.width = 8, m.height = 4, m.format = 0x431, m.image_size = 16, m.mip_size = 16;
    m.texels = level0, m.endpoints = ep;
    m.base_rgb[size_t(Part::kTunic)] = 0x5AB24A, m.base_rgb[size_t(Part::kHair)] = 0xFFEF10, m.base_rgb[size_t(Part::kPants)] = 0xFFFFFF;
    Bytes img2 = image, mip2 = mips;
    tint(m, Part::kTunic, 0xE52B36, img2.data(), mip2.data());  // red
    const Bytes before = decode(1, image.data(), 8, 4), after = decode(1, img2.data(), 8, 4);
    // the tunic texels are now red, the pants and the hair as they were, nothing transparent
    for (uint32_t y = 0; y < 4; y++)
        for (uint32_t x = 0; x < 8; x++) {
            const uint8_t* a = &after[(y * 8 + x) * 4];
            const uint8_t* b = &before[(y * 8 + x) * 4];
            CHECK(a[3] == 255);
            if (level0[y * 8 + x] == uint8_t(Part::kTunic)) CHECK(a[0] > 180 && a[1] < 80 && a[2] < 90);
            else CHECK(!memcmp(a, b, 4));
        }
    CHECK(!memcmp(&img2[8], &image[8], 8));  // the hair block untouched
    CHECK(memcmp(&mip2[0], &mips[0], 4) != 0);  // the mip's tunic endpoints moved
    CHECK(!memcmp(&mip2[8], &mips[8], 8));
    // a tint whose colour sorts above the other endpoint: c0 > c1 kept by swapping endpoints and indices
    Bytes img3(8);
    block(img3.data(), rgb565(0x5AB24A), rgb565(0x219E39), 0x1B1B1B1Bu);  // green > dark green: 4-colour mode
    Mask m3 = m;
    m3.image_size = 8, m3.mip_size = 0, m3.endpoints = {uint8_t(Part::kNone), uint8_t(Part::kTunic)};
    const Bytes d0 = decode(1, img3.data(), 4, 1);
    tint(m3, Part::kTunic, 0xFFFFFF, img3.data(), nullptr);  // dark green -> near white, above c0
    const uint16_t c0 = uint16_t(img3[0] | img3[1] << 8), c1 = uint16_t(img3[2] | img3[3] << 8);
    CHECK(c0 > c1);
    const Bytes d1 = decode(1, img3.data(), 4, 1);
    for (int t = 0; t < 4; t++) CHECK(d1[size_t(t) * 4 + 3] == 255);
    CHECK(!memcmp(&d1[12], &d0[12], 4));  // texel 3 (index 0: the untouched green) still green
}

static void test_exchange() {
    // the base colour itself becomes the wanted colour (within RGB565 steps); a darker shade stays darker
    for (uint32_t want : {0xE52B36u, 0x698DB9u, 0x9A4F9Fu, 0x202020u, 0xFFFFFFu}) {
        int r, g, b;
        const uint16_t c = exchange(rgb565(0x5AB24A), 0x5AB24A, want);
        r = (c >> 11) * 255 / 31, g = (c >> 5 & 63) * 255 / 63, b = (c & 31) * 255 / 31;
        CHECK(std::abs(r - int(want >> 16)) <= 18 && std::abs(g - int(want >> 8 & 255)) <= 18 && std::abs(b - int(want & 255)) <= 18);
        const uint16_t dark = exchange(rgb565(0x219E39), 0x5AB24A, want);
        const int lum_c = (c >> 11) * 2 + (c >> 5 & 63) + (c & 31) * 2, lum_d = (dark >> 11) * 2 + (dark >> 5 & 63) + (dark & 31) * 2;
        CHECK(lum_d <= lum_c);
    }
    // a grey base (white pants) takes the wanted hue
    const uint16_t c = exchange(rgb565(0xF0F0F0), 0xFFFFFF, 0x3050E0);
    CHECK((c & 31) > (c >> 11));
}

static void test_cache() {
    Mask m;
    m.texture = "linktexS3TC", m.width = 4, m.height = 4, m.format = 0x431, m.image_size = 8, m.mip_size = 8;
    m.hash = 0x1122334455667788ull, m.texels.assign(16, uint8_t(Part::kTunic)), m.endpoints = {3, 0, 1, 1};
    m.base_rgb[3] = 0x5AB24A;
    const Bytes data = serialize({m});
    std::vector<Mask> back;
    CHECK(deserialize(data, back) && back.size() == 1);
    CHECK(back[0].texture == m.texture && back[0].hash == m.hash && back[0].texels == m.texels && back[0].endpoints == m.endpoints);
    CHECK(back[0].base_rgb[3] == 0x5AB24A && back[0].count(Part::kTunic) == 16);
    Bytes bad = data;
    bad[4] ^= 1;  // another algorithm version
    CHECK(!deserialize(bad, back) && back.empty());
    bad = data;
    bad.resize(bad.size() - 3);  // cut short
    CHECK(!deserialize(bad, back));
    CHECK(!deserialize(Bytes{'W', 'W'}, back));
    // the hash covers both halves
    const uint8_t a[3] = {1, 2, 3}, b[2] = {4, 5}, ab[5] = {1, 2, 3, 4, 5};
    CHECK(hash_bytes(a, 3, b, 2) == hash_bytes(ab, 5));
    CHECK(hash_bytes(a, 3) != hash_bytes(ab, 5));
}

static void test_sarc_member() {
    // a SARC written by lyt_build (big-endian, as the game's packs): one member read straight from the file
    std::map<std::string, Bytes> files = {{"A.szs", Bytes(100, 1)}, {"Link.szs", Bytes{1, 2, 3, 4, 5}}, {"Z.bin", Bytes(3, 9)}};
    const Bytes sarc = lyt_build::sarc_of(files, 0x80);
    const std::string path = "link_masks_test.sarc";  // (in the test's working folder; removed below)
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(sarc.data()), std::streamsize(sarc.size()));
    CHECK(sarc_member(path, "Link.szs") == files["Link.szs"]);
    CHECK(sarc_member(path, "Z.bin") == files["Z.bin"]);
    bool threw = false;
    try {
        sarc_member(path, "Link");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
    std::remove(path.c_str());
}

static void test_game() {
    const char* dir = getenv("WWHD_GAME_DIR");
    if (!dir || !*dir) return;
    const std::string path = std::string(dir) + "/content/Common/Pack/permanent_3d.pack";
    const auto t0 = std::chrono::steady_clock::now();
    const auto masks = compute_from_szs(sarc_member(path, "Link.szs"));  // as the port reads it
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("the player's Link: %zu masks in %.0f ms\n", masks.size(), ms);
    CHECK(masks.size() == 4);  // the clothes, the island clothes, the hair piece, the irises
    for (const Mask& m : masks) {
        printf("  %s %ux%u:", m.texture.c_str(), m.width, m.height);
        for (int p = 1; p < kParts; p++)
            if (m.count(Part(p))) printf(" %s %u (%06X)", part_name(Part(p)), m.count(Part(p)), m.base_rgb[p]);
        printf("\n");
        CHECK(m.ftex > 0 && memcmp(m.file_head, "FRES", 4) == 0);
        if (m.texture == "hitomi") {  // the irises only (by colour), around a tenth of the eyes' picture
            CHECK(m.count(Part::kEyes) > 2000 && m.count(Part::kEyes) < m.texels.size() / 4 && m.base_rgb[int(Part::kEyes)]);
            CHECK(m.count(Part::kEyes) + m.count(Part::kNone) == m.texels.size());
            continue;
        }
        CHECK(m.count(Part::kHair) > 0 && m.count(Part::kEyes) == 0);
        if (m.texture == "linktexS3TC") CHECK(m.count(Part::kBuckle) > 10000 && m.count(Part::kHair) < 100);  // hair: a flat patch
        if (m.texture != "katsuraS3TC") CHECK(m.count(Part::kTunic) > m.texels.size() / 4);
    }
}

int main() {
    test_classify();
    test_reassign();
    test_block_order();
    test_endpoints_and_tint();
    test_exchange();
    test_cache();
    test_sarc_member();
    test_game();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("link_masks: all checks passed\n");
    return 0;
}
