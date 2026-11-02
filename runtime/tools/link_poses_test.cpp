// SPDX-License-Identifier: MPL-2.0
// Link pose icons (runtime/src/mods/link_poses.h): the pose table, the camera, the rasteriser, the
// outline. With the player's game files (WWHD_GAME_DIR=<game> and WWHD_LINK_POSES_OUT=<dir>) it also
// renders every pose into <dir> as PNG for a look (the files stay on that machine; CI has no game files).
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>

#include <zlib.h>

#include "mods/context_buttons.h"
#include "mods/link_poses.h"

void log_msg(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vfprintf(stderr, fmt, a);
    va_end(a);
    fputc('\n', stderr);
}

using namespace link_poses;
namespace pc = mods::playctx;

static int failures = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        if (!(c)) {                                                    \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
            failures++;                                                \
        }                                                              \
    } while (0)

static bool write_png(const std::string& path, int w, int h, const Bytes& rgba) {
    auto be = [](Bytes& b, uint32_t v) { for (int s = 24; s >= 0; s -= 8) b.push_back(uint8_t(v >> s)); };
    auto chunk = [&](Bytes& out, const char* kind, const Bytes& body) {
        be(out, uint32_t(body.size()));
        Bytes kb(kind, kind + 4);
        kb.insert(kb.end(), body.begin(), body.end());
        out.insert(out.end(), kb.begin(), kb.end());
        be(out, uint32_t(crc32(0, kb.data(), uInt(kb.size()))));
    };
    Bytes raw;
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba.begin() + size_t(y) * w * 4, rgba.begin() + size_t(y + 1) * w * 4);
    }
    uLongf n = compressBound(uLong(raw.size()));
    Bytes z(n);
    if (compress(z.data(), &n, raw.data(), uLong(raw.size())) != Z_OK) return false;
    z.resize(n);
    Bytes out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    Bytes ihdr;
    be(ihdr, uint32_t(w)), be(ihdr, uint32_t(h));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(out, "IHDR", ihdr), chunk(out, "IDAT", z), chunk(out, "IEND", {});
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    return bool(f);
}

static Bytes read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main() {
    // ---- the pose table: one pose per action code, every pose names an animation and a sane camera
    std::set<uint8_t> seen;
    for (const PoseSpec& p : kPoses) {
        CHECK(seen.insert(p.action).second);  // no action twice
        CHECK(p.anim && *p.anim);
        CHECK(p.frame >= 0);
        CHECK(p.pitch >= 0 && p.pitch <= 45);
        CHECK(pose_for_action(p.action) == &p);
    }
    // the actions the plan names: the sword on the sword button, the jump on Jump...
    CHECK(pose_for_action(pc::kActSword) && pose_for_action(pc::kActSword)->gear != Gear::kNone);
    CHECK(pose_for_action(pc::kActJump) && !strcmp(pose_for_action(pc::kActJump)->anim, "jmpst"));
    CHECK(pose_for_action(pc::kActCrouch) != nullptr);
    CHECK(pose_for_action(pc::kActLift) != nullptr);
    CHECK(pose_for_action(pc::kActSpeak) != nullptr);
    CHECK(pose_for_action(pc::kActDefend) && pose_for_action(pc::kActDefend)->gear == Gear::kSwordShield);
    // words that are no pose of Link keep their word: Choose, Next, Return, Cancel, Put away, Take photo, Bid
    for (uint8_t a : {pc::kActChoose, pc::kActNext, pc::kActReturn, pc::kActCancel, pc::kActPutAway, pc::kActTakePhoto, pc::kActBid,
                      pc::kActNone, pc::kActHidden})
        CHECK(pose_for_action(a) == nullptr);
    CHECK(file_for(0x35) == "pose_35.png");
    CHECK(file_for(0x0A) == "pose_0A.png");

    // ---- the camera: yaw 0 looks at Link's front (+z towards the viewer); 90 shows his left side (+x)
    {
        const V3 f = camera(0, 0).point({0, 0, 1});
        CHECK(std::fabs(f.z - 1) < 1e-5f);
        const V3 side = camera(90, 0).point({1, 0, 0});
        CHECK(std::fabs(side.z - 1) < 1e-5f);
        const V3 up = camera(0, 30).point({0, 1, 0});  // looking down from above: the top of his head shows
        CHECK(up.z > 0 && up.y > 0.8f);
        // rigid inverse
        M34 m = camera(37, 12);
        m.m[0][3] = 3, m.m[1][3] = -2, m.m[2][3] = 5;
        const V3 p = (m.rigid_inverse() * m).point({1, 2, 3});
        CHECK(std::fabs(p.x - 1) < 1e-4f && std::fabs(p.y - 2) < 1e-4f && std::fabs(p.z - 3) < 1e-4f);
    }

    // ---- the rasteriser: the nearer triangle wins, alpha below one half is cut out, light gives two tones
    {
        Canvas c(16, 16);
        const ScreenVertex a{0, 0, 1, 0, 0, 1}, b{16, 0, 1, 0, 0, 1}, d{0, 16, 1, 0, 0, 1};
        draw_triangle(c, a, b, d, nullptr, {100, 100, 100});
        CHECK(c.rgba[(1 * 16 + 1) * 4 + 3] == 255);
        CHECK(c.rgba[(15 * 16 + 15) * 4 + 3] == 0);  // the other half stays empty
        // a farther triangle over it changes nothing; a nearer one paints
        const ScreenVertex fa{0, 0, 0, 0, 0, 1}, fb{16, 0, 0, 0, 0, 1}, fd{0, 16, 0, 0, 0, 1};
        draw_triangle(c, fa, fb, fd, nullptr, {10, 10, 10});
        CHECK(c.rgba[(1 * 16 + 1) * 4] == 100);
        const ScreenVertex na{0, 0, 2, 0, 0, -1}, nb{16, 0, 2, 0, 0, -1}, nd{0, 16, 2, 0, 0, -1};
        draw_triangle(c, na, nb, nd, nullptr, {100, 100, 100});
        CHECK(c.rgba[(1 * 16 + 1) * 4] == uint8_t(100 * toon(-1) + 0.5f));  // in shade
        CHECK(toon(1) == 1.0f && toon(-1) < 0.7f && toon(-1) > 0.6f);
        // a texture: alpha cut-out
        Texture t;
        t.w = 2, t.h = 1, t.rgba = {255, 0, 0, 255, 0, 255, 0, 0};
        Canvas c2(8, 8);
        const ScreenVertex ta{0, 0, 1, 0, 0.5f, 1}, tb{8, 0, 1, 1, 0.5f, 1}, td{0, 8, 1, 0, 0.5f, 1};
        draw_triangle(c2, ta, tb, td, &t);
        CHECK(c2.rgba[(0 * 8 + 1) * 4] == 255 && c2.rgba[(0 * 8 + 1) * 4 + 3] == 255);  // left texel: red
        CHECK(c2.rgba[(0 * 8 + 6) * 4 + 3] == 0);                                       // right texel: cut out
    }
    // ---- downsample and outline
    {
        Canvas c(6, 6);
        for (int y = 0; y < 3; y++)
            for (int x = 0; x < 3; x++) {
                uint8_t* p = &c.rgba[(size_t(y) * 6 + x) * 4];
                p[0] = 200, p[3] = 255;
            }
        Bytes px = downsample(c, 3);
        CHECK(px.size() == 2 * 2 * 4);
        CHECK(px[0] == 200 && px[3] == 255);    // fully covered
        CHECK(px[4 * 3 + 3] == 0);              // empty
        Bytes img(9 * 9 * 4, 0);
        uint8_t* mid = &img[(4 * 9 + 4) * 4];
        mid[0] = mid[1] = mid[2] = 250, mid[3] = 255;
        outline(img, 9, 9, 2);
        CHECK(img[(4 * 9 + 4) * 4] == 250);           // the picture stays on top
        CHECK(img[(4 * 9 + 5) * 4 + 3] == 255);       // 1 px away: outline
        CHECK(img[(4 * 9 + 5) * 4] == 24);
        CHECK(img[(4 * 9 + 6) * 4 + 3] > 100 && img[(4 * 9 + 6) * 4 + 3] < 160);  // 2 px: its soft rim
        CHECK(img[(4 * 9 + 8) * 4 + 3] == 0);         // 4 px away: nothing
        CHECK(img[(0 * 9 + 0) * 4 + 3] == 0);
    }
    // ---- bad input is refused, not crashed on
    {
        std::string why;
        CHECK(load(Bytes(16, 0), why) == nullptr && !why.empty());
        Bytes a, b;
        CHECK(!read_pack(Bytes(64, 0), a, b, why));
    }

    // ---- with the player's game files: render every pose (a look, and that every animation reads)
    const char* game = getenv("WWHD_GAME_DIR");
    const char* out = getenv("WWHD_LINK_POSES_OUT");
    if (game && *game) {
        Bytes link, anim;
        std::string why;
        const Bytes pack = read_file(std::string(game) + "/content/Common/Pack/permanent_3d.pack");
        CHECK(read_pack(pack, link, anim, why));
        const auto model = load(link, why);
        if (!model) fprintf(stderr, "load: %s\n", why.c_str());
        CHECK(model != nullptr);
        for (const PoseSpec& p : kPoses) {
            if (!model) break;
            Bytes rgba;
            const bool ok = render(*model, anim, p, rgba, why);
            if (!ok) fprintf(stderr, "pose %02X (%s): %s\n", p.action, p.anim, why.c_str());
            CHECK(ok);
            if (!ok) continue;
            size_t covered = 0;
            for (size_t i = 3; i < rgba.size(); i += 4) covered += rgba[i] > 128;
            CHECK(covered > size_t(kIconSize * kIconSize / 12));  // Link fills a good part of it
            if (out && *out) write_png(std::string(out) + "/" + file_for(p.action), kIconSize, kIconSize, rgba);
        }
        fprintf(stderr, "rendered %zu poses from %s\n", sizeof kPoses / sizeof kPoses[0], game);
        // trying a pose: WWHD_LINK_POSE_TRY="anim:frame:yaw:pitch:gear,..." (gear 0 none, 1 sword, 2 sword and shield)
        // writes try_<n>.png into WWHD_LINK_POSES_OUT
        if (const char* t = getenv("WWHD_LINK_POSE_TRY"); t && model && out && *out) {
            std::string all = t;
            int n = 0;
            for (size_t at = 0; at < all.size();) {
                const size_t end = std::min(all.find(',', at), all.size());
                const std::string one = all.substr(at, end - at);
                at = end + 1;
                char name[64] = {};
                float fr = 0, yaw = 0, pitch = 0;
                int gear = 0;
                if (sscanf(one.c_str(), "%63[^:]:%f:%f:%f:%d", name, &fr, &yaw, &pitch, &gear) < 4) continue;
                const PoseSpec p{0, name, fr, yaw, pitch, Gear(gear)};
                Bytes rgba;
                if (render(*model, anim, p, rgba, why))
                    write_png(std::string(out) + "/try_" + std::to_string(n++) + ".png", kIconSize, kIconSize, rgba);
                else
                    fprintf(stderr, "try %s: %s\n", one.c_str(), why.c_str());
            }
        }
    }
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("link_poses: ok\n");
    return 0;
}
