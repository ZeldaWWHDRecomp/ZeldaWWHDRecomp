// SPDX-License-Identifier: MPL-2.0
// The GameCube player model converter (runtime/src/gcmodel, from GreenNaugahyde/ZeldaWWHDRecompAndroid) on synthetic
// files made here (gcmodel_synth.h): containers, textures, tiling, a J3D model converted onto a small HD BFRES, and
// damaged inputs (they fail with an error, never a crash).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.h"
#include "gcmodel/gcm_image.h"
#include "gcmodel/gcm_io.h"
#include "gcmodel/gcm_j3d.h"
#include "gcmodel/gcmodel.h"
#include "gcmodel_synth.h"

using namespace gcmodel;
namespace s = gcmsynth;

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

static bool near(double a, double b, double eps = 1e-3) { return std::fabs(a - b) < eps; }

static size_t surface_size(uint32_t fmt, int w, int h, uint32_t tm) {
    LatteAddrLib::AddrSurfaceInfo_OUT info{};
    LatteAddrLib::GX2CalculateSurfaceInfo((Latte::E_GX2SURFFMT)fmt, w, h, 1, Latte::E_DIM::DIM_2D, (Latte::E_GX2TILEMODE)tm, 0, 0, &info);
    return info.surfSize;
}

static Image solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    Image im(w, h);
    for (int i = 0; i < w * h; i++) im.px[i * 4] = r, im.px[i * 4 + 1] = g, im.px[i * 4 + 2] = b, im.px[i * 4 + 3] = a;
    return im;
}

static void containers() {
    std::mt19937 rng(1);
    Bytes data(70000);
    for (size_t i = 0; i < data.size(); i++) data[i] = (uint8_t)(i % 97 < 50 ? i % 7 : rng());
    const Bytes y = yaz0_encode(data, 0x80);
    CHECK(y.size() < data.size());
    CHECK(be32(&y[8]) == 0x80);
    CHECK(yaz0_decode(y) == data);
    const Bytes sarc = s::sarc({{"a.bin", Bytes(10, 1)}, {"Link.szs", Bytes(300, 2)}, {"z", Bytes(5, 3)}});
    auto files = sarc_read(sarc);
    CHECK(files.size() == 3 && files["Link.szs"] == Bytes(300, 2) && files["z"] == Bytes(5, 3));
    const Bytes repl = sarc_replace(sarc, {{"Link.szs", Bytes(1000, 9)}});
    files = sarc_read(repl);
    CHECK(files["Link.szs"] == Bytes(1000, 9) && files["a.bin"] == Bytes(10, 1) && files["z"] == Bytes(5, 3));
    CHECK(link_szs(repl) == Bytes(1000, 9));
    std::string err;
    CHECK(assemble(sarc, Bytes(7, 4), err).size() > 0 && err.empty());
    const Bytes arc = s::rarc({{"cl.bdl", Bytes(64, 5)}, {"x.bti", Bytes(33, 6)}});
    auto a = rarc_read(arc);
    CHECK(a.size() == 2 && a["cl.bdl"] == Bytes(64, 5) && a["x.bti"] == Bytes(33, 6));
    CHECK(rarc_read(yaz0_encode(arc, 0)) == a);
    // damaged headers: errors
    bool threw = false;
    try {
        Bytes bad = y;
        put32(&bad[4], 0xFFFFFFF0);
        yaz0_decode(bad);
    } catch (const Error&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        Bytes bad = sarc;
        put16(&bad[0x14 + 6], 0x7FFF);
        sarc_read(bad);
    } catch (const Error&) {
        threw = true;
    }
    CHECK(threw);
}

static void textures() {
    // BC1/BC3/BC4/BC5 of flat colours come back as they went in
    const Image red = solid(16, 16, 255, 0, 0);
    Image back = bc_decode(0x31, 16, 16, bc_encode(0x31, red));
    CHECK(back.px[0] == 255 && back.px[1] == 0 && back.px[2] == 0);
    const Image half = solid(8, 8, 10, 200, 30, 128);
    back = bc_decode(0x33, 8, 8, bc_encode(0x33, half));
    CHECK(std::abs(back.px[3] - 128) <= 2 && std::abs(back.px[1] - 200) <= 4);
    back = bc_decode(0x34, 8, 8, bc_encode(0x34, solid(8, 8, 77, 0, 0)));
    CHECK(std::abs(back.px[0] - 77) <= 1);
    back = bc_decode(0x35, 8, 8, bc_encode(0x35, solid(8, 8, 40, 220, 0)));
    CHECK(std::abs(back.px[0] - 40) <= 1 && std::abs(back.px[1] - 220) <= 1);
    // resizing keeps a flat picture flat
    const Image big = upscale_sharp(solid(8, 8, 12, 34, 56), 32, 32);
    CHECK(big.w == 32 && big.px[(5 * 32 + 7) * 4 + 1] == 34);
    CHECK(resize_lanczos(red, 4, 4).px[0] == 255 && resize_box(red, 2, 2).px[0] == 255);
    // GX2 tiling: tile then untile level 0 gives the blocks back (linear, 1D and 2D tiled, with a swizzle)
    for (uint32_t tm : {1u, 2u, 4u})
        for (uint32_t sw : {0u, 0x100u, 0x600u}) {
            Image grad(64, 64);
            for (int i = 0; i < 64 * 64; i++) grad.px[i * 4] = (uint8_t)i, grad.px[i * 4 + 1] = (uint8_t)(i >> 6), grad.px[i * 4 + 3] = 255;
            const Bytes lin = bc_encode(0x31, grad);
            Bytes img, mip;
            const size_t sz = surface_size(0x31, 64, 64, tm);
            gx2_tile(0x31, 64, 64, 1, tm, sw, lin, img, mip, sz, 0);
            CHECK(img.size() == sz);
            CHECK(gx2_untile_level0(0x31, 64, 64, tm, sw, img.data(), img.size()) == lin);
        }
    // see-through texels take the nearest visible colour
    Image cut = solid(8, 8, 0, 255, 0);
    for (int x = 4; x < 8; x++)
        for (int y = 0; y < 8; y++) cut.at(x, y)[3] = 0, cut.at(x, y)[1] = 0;
    const Image filled = bleed(cut, {}, nullptr);
    CHECK(filled.at(7, 3)[1] == 255 && filled.at(7, 3)[3] == 255);
    // UV coverage of a triangle
    const auto cov = raster_uv({{{{0, 0}, {1, 0}, {0, 1}}}}, 8, 8);
    CHECK(cov.size() == 64 && cov[0] && !cov[63]);
}

// The test model: HD Link "cl" with two bones (root, arm 20 up), a skinned body shape (2 weights) and a rigid hat on
// the arm; the GameCube model has the same joints with the arm 10 up, three draw matrices (root, arm, 1/4 root +
// 3/4 arm), a red 8x8 texture for the body
struct Scene {
    Bytes pack, arc;
};

static s::J3DModel gc_model() {
    s::J3DModel m;
    m.joints = {{"root", -1, {0, 0, 0}}, {"arm", 0, {0, 10, 0}}};
    m.pos = {{1, 0, 0}, {0, 10, 0}, {0, 1, 0}, {2, 0, 0}, {0, 0, 1}, {3, 3, 0}};
    m.uv = {{0, 0}, {1, 0}, {0, 1}, {0.5, 0.5}};
    m.draw = {{{0, 1.0}}, {{1, 1.0}}, {{0, 0.25}, {1, 0.75}}};
    m.weighted = {false, false, true};
    // body (material 0): a triangle on the arm (rigid), one with the weighted vertex, one on the root
    s::J3DShape body;
    body.material = 0;
    body.tris = {{{{0, 0, 1}, {3, 1, 1}, {4, 2, 1}}}, {{{1, 3, 2}, {3, 1, 1}, {4, 2, 1}}}, {{{0, 0, 0}, {3, 1, 0}, {5, 2, 0}}}};
    s::J3DShape hat;
    hat.material = 1;
    hat.tris = {{{{2, 0, 1}, {0, 1, 1}, {4, 2, 1}}}};
    m.shapes = {body, hat};
    m.materials = {"body", "hat"};
    m.materialTexture = {0, -1};
    s::J3DTexture t;
    t.name = "testtex";
    t.rgba.assign(8 * 8 * 4, 0);
    for (int i = 0; i < 64; i++) t.rgba[i * 4] = 255, t.rgba[i * 4 + 3] = 255;
    m.textures = {t};
    return m;
}

static Bytes hd_bfres() {
    s::HDModel cl;
    cl.name = "cl";
    cl.bones = {{"root", -1, 0, -1, {0, 0, 0}}, {"arm", 0, 1, 0, {0, 20, 0}}};
    s::HDShape body;
    body.name = "body__skin";
    body.material = 0, body.bone = 0, body.skin = 2, body.vertices = 4, body.indexFormat = 9;
    body.attrs = {{"_p0", 0x811}, {"_n0", 0x811}, {"_u0", 0x80D}, {"_i0", 0x10C}, {"_w0", 0x80D}};
    s::HDShape hat;
    hat.name = "hat";
    hat.material = 1, hat.bone = 1, hat.skin = 0, hat.vertices = 3, hat.indexFormat = 0;
    hat.attrs = {{"_p0", 0x811}, {"_u0", 0x80D}};
    cl.shapes = {body, hat};
    cl.materials = {{"body", {"testtex"}}, {"hat", {}}};
    s::HDTexture t;
    t.name = "testtex";
    const Bytes lin = bc_encode(0x31, solid(16, 16, 255, 255, 255));
    Bytes mip;
    gx2_tile(0x31, 16, 16, 1, 4, 0, lin, t.image, mip, surface_size(0x31, 16, 16, 4), 0);
    return s::bfres({cl}, {t});
}

static Scene scene() {
    Scene sc;
    const Bytes link = s::sarc({{"Link.bfres", hd_bfres()}});
    sc.pack = s::sarc({{"Other.szs", Bytes(40, 7)}, {"Link.szs", yaz0_encode(link, 0x2000)}});
    sc.arc = yaz0_encode(s::rarc({{"cl.bdl", s::j3d(gc_model())}}), 0);
    return sc;
}

struct Out {
    Bytes f;
    uint32_t u32(size_t o) const { return be32(&f[o]); }
    uint16_t u16(size_t o) const { return be16(&f[o]); }
    float f32(size_t o) const { return bef32(&f[o]); }
    size_t rel(size_t o) const { return (size_t)((int64_t)o + (int32_t)u32(o)); }
    size_t entry(size_t g, int i) const { return rel(g + 8 + 16 * (i + 1) + 12); }
};

static void j3d_reader() {
    const Model m(s::j3d(gc_model()));
    CHECK(m.joints.size() == 2 && m.joints[1].name == "arm" && m.joints[1].parent == 0);
    CHECK(near(m.joints[1].world[1][3], 10));
    CHECK(m.shapes.size() == 2 && m.shapes[0].tris.size() == 3 && m.shapes[1].material == 1);
    CHECK(m.materialNames.size() == 2 && m.materialNames[0] == "body");
    CHECK(m.textures.count("testtex") && m.textures.at("testtex").rgba[0] == 255);
    // rigid vertex on the arm: (1,0,0) in the arm's space = (1,10,0); weighted one stays as stored
    const Vec3 p = m.position(m.shapes[0].tris[0][0]);
    CHECK(near(p[0], 1) && near(p[1], 10));
    const Vec3 q = m.position(m.shapes[0].tris[1][0]);
    CHECK(near(q[1], 10));
}

static void convert_scene() {
    const Scene sc = scene();
    Bytes szs;
    std::vector<int> steps;
    const Result r = convert(sc.arc, sc.pack, szs, [&](int pc, const std::string&) { steps.push_back(pc); });
    if (!r.error.empty()) fprintf(stderr, "convert: %s\n", r.error.c_str());
    CHECK(r.error.empty());
    CHECK(!steps.empty() && steps.back() == 100);
    CHECK(r.report.find("model cl") != std::string::npos && r.report.find("texture testtex") != std::string::npos);
    CHECK(szs.size() > 16 && !memcmp(szs.data(), "Yaz0", 4) && be32(&szs[8]) == 0x2000);
    auto files = sarc_read(yaz0_decode(szs));
    CHECK(files.count("Link.bfres"));
    const Bytes orig = hd_bfres();
    Out o{files["Link.bfres"]};
    CHECK(o.f.size() > orig.size());           // new buffers appended
    CHECK(o.u32(0xC) == o.f.size());
    CHECK(!memcmp(o.f.data(), orig.data(), 0xC));  // the header's start as it was (then the file size)
    const size_t fm = o.entry(o.rel(0x20), 0);
    const size_t vbl = o.rel(fm + 16), shapes = o.rel(fm + 20);
    // body: 2 weights; the GameCube vertices (unique per index set): 0/3/4 on the arm, 1 weighted, 0/3/5 on the root
    {
        const size_t so = o.entry(shapes, 0), vb = vbl;
        const uint32_t n = o.u32(vb + 8);
        CHECK(n == 7);
        const size_t bl = o.rel(vb + 0x18), data = o.rel(bl + 0x14);
        const int stride = o.u16(bl + 0xC);
        CHECK(data >= orig.size() && stride == 12 + 12 + 8 + 8 + 8);
        CHECK(o.u32(bl + 4) == n * stride);
        // first vertex: GameCube (1,10,0) on the arm (10 up) -> HD's arm (20 up): (1,20,0), index arm's smooth 1, weight 1
        CHECK(near(o.f32(data), 1) && near(o.f32(data + 4), 20) && near(o.f32(data + 8), 0));
        CHECK(o.u32(data + 32) == 1 && near(o.f32(data + 40), 1) && near(o.f32(data + 44), 0));
        // its UV (0,0)
        CHECK(near(o.f32(data + 24), 0) && near(o.f32(data + 28), 0));
        // the weighted vertex (fourth unique: tri 2's first): 1/4 at (0,10,0) + 3/4 at (0,20,0) = (0,17.5,0), arm first
        const size_t w = data + 3 * stride;
        CHECK(near(o.f32(w + 4), 17.5));
        CHECK(o.u32(w + 32) == 1 && o.u32(w + 36) == 0 && near(o.f32(w + 40), 0.75) && near(o.f32(w + 44), 0.25));
        // indices: 3 triangles, u32, the winding turned (0,2,1)
        const size_t mesh = o.rel(so + 0x24), ib = o.rel(mesh + 0x14), id = o.rel(ib + 0x14);
        CHECK(o.u32(mesh + 8) == 9 && o.u32(ib + 4) == 36);
        CHECK(o.u32(id) == 0 && o.u32(id + 4) == 2 && o.u32(id + 8) == 1);
        // skin bones: root and arm
        CHECK(o.u16(so + 0x14) == 2);
        // a bounding radius
        CHECK(o.f32(so + 0x1C) > 1);
    }
    // hat: rigid on the arm: GameCube (0,1,0) in the arm's space -> HD model (0,21,0) -> the arm's space again (0,1,0)
    {
        const size_t so = o.entry(shapes, 1), vb = vbl + 0x20;
        CHECK(o.u32(vb + 8) == 3);
        const size_t bl = o.rel(vb + 0x18), data = o.rel(bl + 0x14);
        CHECK(near(o.f32(data), 0) && near(o.f32(data + 4), 1) && near(o.f32(data + 8), 0));
        const size_t mesh = o.rel(so + 0x24), ib = o.rel(mesh + 0x14);
        CHECK(o.u32(mesh + 8) == 3 && o.u32(ib + 4) == 6);  // u16 indices
    }
    // texture: same header, red where it was white, written in place
    {
        const size_t t = o.entry(o.rel(0x24), 0), ot = t;
        CHECK(o.rel(t + 0xB0) == (size_t)((int64_t)ot + 0xB0 + (int32_t)be32(&orig[ot + 0xB0])));
        const size_t img = o.rel(t + 0xB0);
        const Bytes lin = gx2_untile_level0(0x31, 16, 16, 4, 0, &o.f[img], o.u32(t + 0x24));
        const Image px = bc_decode(0x31, 16, 16, lin);
        CHECK(px.px[0] > 240 && px.px[1] < 16 && px.px[2] < 16);
        CHECK(!memcmp(&o.f[t], &orig[t], 0xB0));
    }
    // the pack around it
    std::string err;
    const Bytes pack = assemble(sc.pack, szs, err);
    CHECK(err.empty() && link_szs(pack) == szs && sarc_read(pack).at("Other.szs") == Bytes(40, 7));
}

static void refusals() {
    const Scene sc = scene();
    Bytes out;
    // not a player model
    Result r = convert(s::rarc({{"other.bdl", s::j3d(gc_model())}}), sc.pack, out);
    CHECK(r.error.find("no cl.bdl") != std::string::npos);
    r = convert(Bytes(100, 0), sc.pack, out);
    CHECK(!r.error.empty());
    r = convert(sc.arc, s::sarc({{"x", Bytes(3, 1)}}), out);
    CHECK(r.error.find("without Link.szs") != std::string::npos);
    // damaged models: refused ("skipped cl": nothing converted) or converted, never a crash or a read past the data (ASan builds check the latter)
    const Bytes bdl = s::j3d(gc_model());
    std::mt19937 rng(7);
    int errors = 0, done = 0;
    const char* fuzz = getenv("GCMODEL_TEST_FUZZ");  // (more rounds for a sanitizer build)
    const int rounds = fuzz ? atoi(fuzz) : 600;
    for (int it = 0; it < rounds; it++) {
        Bytes b = bdl;
        const int flips = 1 + (int)(rng() % 8);
        for (int k = 0; k < flips; k++) b[rng() % b.size()] = (uint8_t)rng();
        if (it % 5 == 0) b.resize(rng() % b.size());
        r = convert(s::rarc({{"cl.bdl", b}}), sc.pack, out);
        (r.error.empty() && r.report.find("skipped cl") == std::string::npos ? done : errors)++;
    }
    fprintf(stderr, "damaged models: %d refused, %d converted\n", errors, done);
    CHECK(errors > 0);
}

int main() {
    containers();
    textures();
    j3d_reader();
    convert_scene();
    refusals();
    if (g_fail) {
        fprintf(stderr, "gcmodel_test: %d failed\n", g_fail);
        return 1;
    }
    printf("gcmodel_test: ok\n");
    return 0;
}
