// SPDX-License-Identifier: MPL-2.0
// JStudio STB writer/parser contract. With WWHD_TEST_RETAIL_STB_DIR set to a folder of STB files taken
// from the player's own game (e.g. extracted Demo04/stolensister.stb), every retail file must parse and
// write back byte for byte; without it that part reports SKIP (the repository ships no game data).
#include "mods/demo_stb.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace S = demo_stb;
static bool has(const std::string& text, const std::string& line) { return text.find(line) != std::string::npos; }

static void builder_round_trip() {
    S::Builder b;
    const uint32_t x = b.list({{0, -2500}, {10, 2500}}, S::Interpolation::Linear);
    const uint32_t y = b.list({{0, 1800}, {5, 1000}, {10, 900}}, S::Interpolation::BSpline);
    const uint32_t z = b.constant(3000);
    const uint32_t r = b.hermite({{0, 0, 0, 0}, {6, 360, 60, 60}});
    const size_t dk = b.object(S::kActor, "Dk");
    b.curves(dk, 0, S::actor::TXYZ, {x, y, z});
    b.immediate(dk, 0, S::actor::RXYZ, {0, 90, 0});
    b.value(dk, 0, S::actor::Animation, 0x10011);
    b.immediate_word(dk, 0, S::actor::AnimationMode, 1);
    b.time(dk, 0, S::actor::AnimationFrame, 30);
    b.curves(dk, 40, S::actor::RY, {r});
    b.immediate(dk, 40, S::actor::SXYZ, {2, 2, 2});
    b.data(dk, 40, 1, S::Bytes{0x3a, 0x03, 0x00, 0x8f, 0x00, 0x2a, 0x00, 0x34, 0x39, 0x03, 0x00, 0x00});
    b.end(dk, 240);
    const size_t cam = b.object(S::kCamera, "camera");
    b.immediate(cam, 0, S::camera::Fovy, {55});
    b.immediate(cam, 0, S::camera::Roll, {0});
    b.immediate(cam, 0, S::camera::Eye, {0, 700, -1200});
    b.curves(cam, 0, S::camera::Target, {x, y, z});
    b.immediate(cam, 0, S::camera::Near, {10});
    b.immediate(cam, 0, S::camera::Far, {100000});
    b.end(cam, 240);
    const size_t snd = b.object(S::kSound, "se");
    b.value(snd, 30, S::sound::Sound, 22820);
    b.curves(snd, 30, S::sound::XYZ, {x, y, z});
    b.immediate(snd, 30, S::sound::Volume, {0.8f});
    b.immediate(snd, 30, S::sound::Pan, {0.5f});
    const size_t ptc = b.object(S::kParticle, "efx1");
    b.name(ptc, 50, S::particle::Parent, "Dk");
    b.name(ptc, 50, S::particle::ParentNode, "j_dk_ashi_r3");
    b.immediate_word(ptc, 50, S::particle::ParentEnable, 1);
    b.value(ptc, 50, S::particle::Particle, 0x8123);
    const size_t msg = b.object(S::kMessage, "message");
    b.value(msg, 60, S::message::Message, 889);
    b.suspend(60);
    b.suspend(120);
    const S::Bytes bytes = b.finish();

    S::File f;
    std::string why;
    assert(S::parse(bytes, f, why));
    assert(S::write(f) == bytes);  // writer output is a fixed point of parse/write
    const std::string d = S::dump(f);
    std::fputs(d.c_str(), stdout);
    assert(f.functions.size() == 4 && f.objects.size() == 6);
    assert(has(d, "fn #0 type 5 p18:0,10 p22:00000001 p1:n=2/2[0,-2500,10,2500]"));
    assert(has(d, "fn #1 type 5 p18:0,10 p22:00000003 p1:n=3/2[0,1800,5,1000,10,900]"));
    assert(has(d, "fn #2 type 2 p1:3000"));
    assert(has(d, "fn #3 type 6 p18:0,6 p1:n=2/4[0,0,0,0,6,360,60,60]"));
    assert(has(d, "== JACT \"Dk\"\n  f0 TXYZ = curves 0,1,2\n  f0 RXYZ = 0,90,0\n  f0 ANIMATION = u32 65553\n  f0 ANIM_MODE = int 1\n  f0 ANIM_FRAME = rate 30\n"));
    assert(has(d, "  f40 RY = curves 3\n  f40 SXYZ = 2,2,2\n  f40 reserved 129 00000004000000013a03008f002a003439030000\n  f240 end\n"));
    assert(has(d, "== JCMR \"camera\"\n  f0 FOVY = 55\n  f0 ROLL = 0\n  f0 EYE_XYZ = 0,700,-1200\n  f0 TGT_XYZ = curves 0,1,2\n  f0 NEAR = 10\n  f0 FAR = 100000\n  f240 end\n"));
    assert(has(d, "== JSND \"se\"\n  f30 SOUND = u32 22820\n  f30 XYZ = curves 0,1,2\n  f30 VOLUME = 0.8\n  f30 PAN = 0.5\n  f30 end\n"));
    assert(has(d, "== JPTC \"efx1\"\n  f50 PARENT = name Dk\n  f50 PARENT_NODE = name j_dk_ashi_r3\n  f50 PARENT_ENABLE = int 1\n  f50 PARTICLE = u32 33059\n"));
    assert(has(d, "== JMSG \"message\"\n  f60 MESSAGE = u32 889\n"));
    assert(has(d, "== CTRL \"control\"\n  f60 seq 4 1\n  f120 seq 4 1\n  f120 end\n"));
    // header and layout facts the game's parser checks
    assert(std::memcmp(bytes.data(), "STB\0\xfe\xff\x00\x03", 8) == 0);
    assert(uint32_t(bytes[8]) << 24 == (bytes.size() & 0xFF000000u) && bytes.size() % 4 == 0);
    assert(std::memcmp(bytes.data() + 0x24, "JFVB", 4) == 0 && std::memcmp(bytes.data() + 0x28, "FVB\0", 4) == 0);
}

static void malformed() {
    S::File f;
    std::string why;
    assert(!S::parse(S::Bytes{}, f, why));
    S::Builder b;
    const size_t o = b.object(S::kActor, "Link");
    b.immediate(o, 0, S::actor::TXYZ, {1, 2, 3});
    S::Bytes bytes = b.finish();
    S::Bytes cut(bytes.begin(), bytes.end() - 4);
    assert(!S::parse(cut, f, why));  // size field no longer matches
    S::Bytes bad = bytes;
    bad[0x20 + 3] = 0xFF;  // block size beyond the file
    assert(!S::parse(bad, f, why));
}

static int retail() {
    const char* dir = std::getenv("WWHD_TEST_RETAIL_STB_DIR");
    if (!dir || !*dir) { std::puts("retail STB round trip: SKIP (WWHD_TEST_RETAIL_STB_DIR not set)"); return 0; }
    int count = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (e.path().extension() != ".stb") continue;
        std::ifstream in(e.path(), std::ios::binary);
        const S::Bytes data{std::istreambuf_iterator<char>(in), {}};
        S::File f;
        std::string why;
        if (!S::parse(data, f, why)) { std::fprintf(stderr, "%s: %s\n", e.path().c_str(), why.c_str()); assert(false); }
        assert(S::write(f) == data);
        S::File again;
        assert(S::parse(S::write(f), again, why) && again == f);
        if (e.path().filename() == "stolensister.stb") {
            const std::string d = S::dump(f);
            // facts verified by hand with the research dumper (demo-research/stolensister.txt)
            assert(f.functions.size() == 83 && f.objects.size() == 14);
            assert(has(d, "== JACT \"Dk\"\n  f0 TXYZ = -196695,32610,301848\n  f0 RXYZ = 0,90,0\n  f0 ANIMATION = u32 65629\n"));
            assert(has(d, "  f725 TXYZ = curves 7,8,9\n  f725 RXYZ = 0,180,0\n  f725 ANIMATION = u32 65591\n"));
            assert(has(d, "== JACT \"Ls1\"") && has(d, "  f1380 TXYZ = curves 10,11,12\n  f1380 SXYZ = 0,0,0\n"));
            assert(has(d, "== CTRL \"control\"\n  f97 seq 4 1\n"));
            assert(has(d, "== JSND \"se\"\n  f1380 XYZ = curves 10,11,12\n  f1380 SOUND = u32 22820\n"));
            std::puts("stolensister.stb: retail facts match");
        }
        ++count;
    }
    std::printf("retail STB round trip: %d files byte-identical\n", count);
    assert(count > 0);
    return count;
}

int main() {
    builder_round_trip();
    malformed();
    retail();
    std::puts("demo_stb: ok");
    return 0;
}
