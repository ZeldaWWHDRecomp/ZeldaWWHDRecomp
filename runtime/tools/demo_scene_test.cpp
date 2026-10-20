// SPDX-License-Identifier: MPL-2.0
// Mod demo step: manifest parsing, checks and the compiled STB (read back with demo_stb).
#include "mods/demo_event_image.h"
#include "mods/demo_scene.h"
#include "mods/demo_stb.h"
#include "mods/demo_attach_math.h"
#include "mods/demo_start_gate.h"

#include <cmath>

#include <cassert>
#include <cstdio>
#include <string>

namespace D = demo_scene;
static bool has(const std::string& text, const std::string& line) { return text.find(line) != std::string::npos; }
static D::Demo parsed(const std::string& json) {
    D::Demo d;
    std::string why;
    if (!D::parse(mods::json::parse(json), d, why)) { std::fprintf(stderr, "parse: %s\n", why.c_str()); assert(false); }
    return d;
}
static std::string refused(const std::string& json) {
    D::Demo d;
    std::string why;
    assert(!D::parse(mods::json::parse(json), d, why));
    return why;
}

static const char* kBird = R"({"type":"demo","frames":240,"origin":"link",
 "camera":{"eye":{"keys":[{"frame":0,"value":{"x":0,"y":700,"z":-1200}}]},
           "target":{"curve":"linear","keys":[{"frame":0,"value":{"x":-2500,"y":1800,"z":3000}},{"frame":240,"value":{"x":2500,"y":900,"z":3000}}]},
           "fovy":{"keys":[{"frame":0,"value":55}]}},
 "actors":[{"id":"bird","actor":"Dk",
            "position":{"curve":"linear","keys":[{"frame":0,"value":{"x":-2500,"y":1800,"z":3000}},{"frame":240,"value":{"x":2500,"y":900,"z":3000}}]},
            "rotation":{"keys":[{"frame":0,"value":{"x":0,"y":90,"z":0}}]},
            "clips":[{"frame":0,"archive":"Dk","file":"fly1.bck"}]},
           {"id":"gull","model":{"archive":"Kamome","file":"ka.bdl"},
            "position":{"curve":"smooth","keys":[{"frame":0,"value":{"x":1500,"y":600,"z":1500}},{"frame":120,"value":{"x":0,"y":650,"z":1500}},{"frame":240,"value":{"x":-1500,"y":700,"z":1500}}]},
            "clips":[{"frame":0,"archive":"Kamome","file":"ka_fly1.bck","speed":1.5}]},
           {"id":"hero","actor":"Link","modes":[{"frame":0,"mode":2},{"frame":100,"mode":512,"data":"3a03008f002a003439030000"}]}],
 "sounds":[{"frame":30,"id":22820,"actor":"bird","volume":0.8}],
 "particles":[{"frame":50,"id":33059,"actor":"bird","joint":"j_dk_ashi_r3"}],
 "messages":[{"frame":60,"text":"Help!"},{"frame":150,"text":"Big brother!"}]})";

int main() {
    const D::Demo d = parsed(kBird);
    assert(d.frames == 240 && d.origin_link && d.actors.size() == 3 && d.messages.size() == 2);
    // the game's ids of the referenced files (from its resource id tables)
    const D::Resolver ids = [](const D::Resource& r) -> int32_t {
        if (r.archive == "Dk" && r.file == "fly1.bck") return 0x11;
        if (r.archive == "Kamome" && r.file == "ka.bdl") return 0x17;
        if (r.archive == "Kamome" && r.file == "ka_fly1.bck") return 0xD;
        return -1;
    };
    D::Compiled c;
    std::string why;
    assert(D::compile(d, ids, c, why));
    assert(c.participants == (std::vector<std::string>{"Dk", "Link"}));
    assert(c.virtual_ids.size() == 3 && c.virtual_ids[0].second == 0x11 && c.virtual_ids[1].second == 0x17 && c.virtual_ids[2].second == 0xD);
    demo_stb::File f;
    assert(demo_stb::parse(c.stb, f, why));
    const std::string t = demo_stb::dump(f);
    std::fputs(t.c_str(), stdout);
    assert(has(t, "== JCMR \"camera\"\n  f0 EYE_XYZ = 0,700,-1200\n  f0 TGT_XYZ = curves 0,1,2\n  f0 FOVY = 55\n  f240 TGT_XYZ = 2500,900,3000\n  f240 end\n"));
    assert(has(t, "fn #0 type 5 p18:0,8 p22:00000001 p1:n=2/2[0,-2500,8,2500]"));  // 240 frames = 8 s
    assert(has(t, "== JACT \"Dk\"\n  f0 TXYZ = curves 3,4,5\n  f0 RXYZ = 0,90,0\n  f0 ANIMATION = u32 65536\n  f0 ANIM_MODE = int 1\n  f0 ANIM_FRAME = rate 30\n  f240 TXYZ = 2500,900,3000\n  f240 end\n"));
    assert(has(t, "== JACT \"d_act0\"\n  f0 SHAPE = u32 65537\n  f0 TXYZ = curves 6,7,8\n  f0 ANIMATION = u32 65538\n  f0 ANIM_MODE = int 1\n  f0 ANIM_FRAME = rate 45\n"));
    assert(has(t, "fn #6 type 6 p18:0,8 p1:n=3/4[0,1500,-375,-375,4,0,-375,-375,8,-1500,-375,-375]"));  // smooth: Catmull-Rom slopes per second
    assert(has(t, "== JACT \"Link\"\n  f0 ANIMATION = u32 2\n  f100 ANIMATION = u32 512\n  f100 ANIM_MODE = int 1\n  f100 ANIM_FRAME = rate 30\n  f100 reserved 129 00000004000000013a03008f002a003439030000\n  f240 end\n"));
    assert(has(t, "== JSND \"se0\"\n  f0 XYZ = curves 9,10,11\n  f30 SOUND = u32 22820\n  f30 VOLUME = 0.8\n  f30 PAN = 0.5\n  f240 XYZ = 2500,900,3000\n"));
    assert(has(t, "== JPTC \"efx0\"\n  f50 PARENT = name Dk\n  f50 PARENT_NODE = name j_dk_ashi_r3\n  f50 PARENT_ENABLE = int 1\n  f50 PARTICLE = u32 33059\n"));
    assert(has(t, "== JMSG \"message\"\n  f60 MESSAGE = u32 889\n  f150 MESSAGE = u32 891\n  f240 end\n"));
    assert(has(t, "== CTRL \"control\"\n  f61 seq 4 1\n  f151 seq 4 1\n  f151 end\n"));  // waits one frame after each message

    // a file the game doesn't have stops the build with a reason
    D::Compiled missing;
    assert(!D::compile(d, [](const D::Resource&) { return int32_t(-1); }, missing, why) && has(why, "the game has no fly1.bck in archive Dk"));

    // refusals with reasons
    assert(has(refused(R"({"type":"demo","frames":0})"), "frames"));
    assert(has(refused(R"({"type":"demo","frames":30,"bogus":1})"), "unknown demo field"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","model":{"archive":"Demo04","file":"ka.bdl"}}]})"), "invalid archive name"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"d_act1"}]})"), "invalid stage actor name"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk","position":{"keys":[{"frame":20,"value":{"x":0,"y":0,"z":0}},{"frame":10,"value":{"x":0,"y":0,"z":0}}]}}]})"), "key frames must increase"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Link","clips":[{"frame":0,"archive":"Link","file":"a.bck"}]}]})"), "Link plays demo modes"));
    assert(has(refused(R"({"type":"demo","frames":30,"camera":{"eye":{"keys":[{"frame":0,"value":{"x":0,"y":0,"z":0}}]}}})"), "eye and target together"));
    assert(has(refused(R"({"type":"demo","frames":30,"messages":[{"frame":30,"text":"late"}]})"), "frame before the end"));
    assert(has(refused(R"({"type":"demo","frames":30,"sounds":[{"frame":0,"id":5,"actor":"nobody"}],"actors":[{"id":"a","actor":"Dk"}]})"), "unknown actor nobody"));

    // phase 2: game actors created when missing, mod-placed characters, Link's modes by name and clip
    const D::Demo cast = parsed(R"({"type":"demo","frames":90,"origin":"link",
      "actors":[{"id":"bird","actor":"Dk","params":"0x00000000","position":{"keys":[{"frame":0,"value":{"x":0,"y":1500,"z":2000}}]}},
                {"id":"baker","character":"baker","position":{"curve":"linear","keys":[{"frame":0,"value":{"x":0,"y":0,"z":300}},{"frame":90,"value":{"x":200,"y":0,"z":300}}]},
                 "clips":[{"frame":0,"clip":"walk"},{"frame":60,"clip":"idle","loop":true}]},
                {"id":"hero","actor":"Link","modes":[{"frame":0,"mode":"walk"},{"frame":45,"clip":"udeage_onetime.bck"}]}]})");
    assert(cast.actors[0].spawn && cast.actors[1].kind == D::Actor::Kind::Character && cast.actors[1].named_clips.size() == 2);
    assert(cast.actors[2].modes[0].mode == 2 && cast.actors[2].modes[1].mode == 512 && cast.actors[2].modes[1].clip == "udeage_onetime.bck");
    D::Compiled cc;
    assert(D::compile(cast, [](const D::Resource& r) -> int32_t { return r.archive == "@Link" && r.file == "udeage_onetime.bck" ? 0x8f : -1; }, cc, why));
    assert(cc.cast.size() == 1 && cc.cast[0].name == "Dk" && cc.cast[0].placed && cc.cast[0].position.y == 1500);
    assert(cc.characters.size() == 1 && cc.characters[0].name == "MA00" && cc.characters[0].character == "baker" && cc.characters[0].clips.size() == 2);
    demo_stb::File cf;
    assert(demo_stb::parse(cc.stb, cf, why));
    const std::string ct = demo_stb::dump(cf);
    assert(has(ct, "== JACT \"MA00\"\n  f0 TXYZ = curves 0,1,2\n"));
    assert(has(ct, "  f45 ANIMATION = u32 512\n  f45 ANIM_MODE = int 1\n  f45 ANIM_FRAME = rate 30\n  f45 reserved 129 000000040000000032008f39020000\n"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Link","modes":[{"frame":0,"mode":"fly"}]}]})"), "unknown Link mode"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","character":"x","spawn":false}]})"), "spawn and params are for game actors"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk","model":{"archive":"Dk","file":"dk.bdl"}}]})"), "one of actor, model or character"));

    // the event image that plays it: one PACKAGE staff, PLAY cut naming the STB file
    demo_event_image::Spec spec;
    spec.origin = demo_event_image::Origin{668, 95, -197304, 0};
    std::vector<uint8_t> image;
    assert(demo_event_image::make(spec, image, why));
    demo_event_image::Spec back;
    assert(demo_event_image::validate(image, back, why) && back.file == "moddemo.stb" && back.origin && back.origin->z == -197304.f);
    assert(std::string(reinterpret_cast<const char*>(image.data() + 0xF0)) == "PACKAGE" && std::string(reinterpret_cast<const char*>(image.data() + 0x140)) == "PLAY");
    assert(image[0xF0 + 0x2C + 3] == 11);  // staff type PACKAGE
    image[0x140 + 0x3C] = 0;              // any other byte is refused
    assert(!demo_event_image::validate(image, back, why));
    spec.file = "../x";
    assert(!demo_event_image::make(spec, image, why));
    spec = {};
    assert(demo_event_image::make(spec, image, why) && demo_event_image::validate(image, back, why) && !back.origin);

    // phase 3: joint attach and detach
    const char* kCarry = R"({"type":"demo","frames":240,"origin":"link",
      "actors":[{"id":"bird","actor":"Dk","position":{"keys":[{"frame":0,"value":{"x":0,"y":800,"z":1000}}]}},
                {"id":"baker","character":"baker"},
                {"id":"hero","actor":"Link","modes":[{"frame":0,"mode":"wait"},{"frame":100,"clip":"udeage_onetime.bck"}]}],
      "attach":[{"frame":10,"actor":"baker","to":"bird","joint":"j_dk_ashi_r3","offset":{"x":0,"y":-120,"z":0},"rotation":{"x":0,"y":180,"z":0},"upright":true},
                {"frame":120,"actor":"hero","to":"bird","joint":"j_dk_ago"}],
      "detach":[{"frame":90,"actor":"baker","fall":true}]})";
    const D::Demo carry = parsed(kCarry);
    assert(carry.attach.size() == 2 && carry.attach[0].release == 90 && carry.attach[0].fall && !carry.attach[1].fall && carry.attach[1].release == D::kNoRelease);
    assert(carry.attach[0].offset.y == -120 && carry.attach[0].upright && !carry.attach[1].upright && carry.attach[0].rotation.y == 180 && carry.attach[1].joint == "j_dk_ago");
    // Link carried in a mode that isn't one of his clips
    std::string early = kCarry;
    early.replace(early.find("\"frame\":120,\"actor\":\"hero\""), 12, "\"frame\":50,");
    assert(has(refused(early), "carried Link plays one of his demo clips"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk"}],"attach":[{"frame":0,"actor":"a","to":"a","joint":"j"}]})"), "carry itself"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk"}],"attach":[{"frame":0,"actor":"a","to":"nobody","joint":"j"}]})"), "must be demo actors"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk"},{"id":"m","model":{"archive":"Dk","file":"dk.bdl"}}],"attach":[{"frame":0,"actor":"m","to":"a","joint":"j"}]})"), "can't carry or be carried"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk"},{"id":"b","actor":"Kb"}],"detach":[{"frame":5,"actor":"b"}]})"), "detach needs an attach"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk"},{"id":"b","actor":"Kb"}],"attach":[{"frame":5,"actor":"b","to":"a","joint":"j"}],"detach":[{"frame":5,"actor":"b"}]})"), "without an attach before it"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk"},{"id":"b","actor":"Kb"},{"id":"c","actor":"Bk"}],"attach":[{"frame":0,"actor":"b","to":"a","joint":"j"},{"frame":5,"actor":"b","to":"c","joint":"j"}]})"), "carried twice"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk"},{"id":"b","actor":"Kb"},{"id":"c","actor":"Bk"}],"attach":[{"frame":0,"actor":"b","to":"a","joint":"j"},{"frame":5,"actor":"c","to":"b","joint":"j"}]})"), "carries while it is carried"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk"},{"id":"b","actor":"Kb"}],"attach":[{"frame":0,"actor":"b","to":"a","joint":"j"}],"detach":[{"frame":5,"actor":"b","fall":1}]})"), "fall must be true or false"));
    // detached first, then carried by another: fine
    const D::Demo pass = parsed(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk"},{"id":"b","actor":"Kb"},{"id":"c","actor":"Bk"}],
      "attach":[{"frame":0,"actor":"b","to":"a","joint":"j"},{"frame":10,"actor":"b","to":"c","joint":"j"}],"detach":[{"frame":10,"actor":"b"}]})");
    assert(pass.attach[0].release == 10 && pass.attach[1].release == D::kNoRelease);
    // the pose: identity joint at (100,200,300) scaled 2 -> offset scaled, rotation kept; ZXY angles round-trip
    {
        demo_attach_math::M34 j = {2, 0, 0, 100, 0, 2, 0, 200, 0, 0, 2, 300};
        float p[3]; int16_t r[3];
        assert(demo_attach_math::pose(j, {0, -60, 10}, {0, 90, 0}, p, r));
        assert(p[0] == 100 && p[1] == 80 && p[2] == 320 && r[0] == 0 && r[1] == 16384 && r[2] == 0);
        float m[9];
        demo_attach_math::zxy({20, -70, 35}, m);
        demo_attach_math::M34 k = {m[0], m[1], m[2], 0, m[3], m[4], m[5], 0, m[6], m[7], m[8], 0};
        assert(demo_attach_math::pose(k, {}, {}, p, r));
        auto near = [](int16_t a, float degrees) { return std::abs(int(a) - int(std::lround(degrees * 32768.f / 180.f))) <= 2; };
        assert(near(r[0], 20) && near(r[1], -70) && near(r[2], 35));
        // joint turned 90 degrees about Y, offset along its X: lands on world -Z
        demo_attach_math::zxy({0, 90, 0}, m);
        demo_attach_math::M34 y = {m[0], m[1], m[2], 0, m[3], m[4], m[5], 0, m[6], m[7], m[8], 0};
        assert(demo_attach_math::pose(y, {50, 0, 0}, {0, 90, 0}, p, r) && std::fabs(p[2] + 50) < 1e-3f && std::fabs(p[0]) < 1e-3f && r[1] == -32768);
        // upright keeps the heading only: a joint rolled 90 degrees and turned 30 gives yaw 30
        demo_attach_math::zxy({0, 30, 90}, m);
        demo_attach_math::M34 rolled = {m[0], m[1], m[2], 0, m[3], m[4], m[5], 0, m[6], m[7], m[8], 0};
        assert(demo_attach_math::pose(rolled, {}, {}, p, r, true) && r[0] == 0 && r[2] == 0 && near(r[1], 30));
        assert(demo_attach_math::pose(rolled, {}, {}, p, r, false) && near(r[2], 90));
        demo_attach_math::M34 broken = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        assert(!demo_attach_math::pose(broken, {}, {}, p, r));
    }

    // phase 5: time and weather through the event's LIGHT staff
    const D::Demo night = parsed(R"({"type":"demo","frames":200,"camera":{"eye":{"keys":[{"frame":0,"value":{"x":0,"y":0,"z":0}}]},"target":{"keys":[{"frame":0,"value":{"x":0,"y":0,"z":1}}]}},
      "time":[{"frame":0,"hour":22.5},{"frame":120,"hour":3}],"weather":[{"frame":30,"rain":1},{"frame":120,"rain":0.25},{"frame":180,"rain":0}],"keep_weather":true})");
    assert(night.time.size() == 2 && night.weather.size() == 3 && night.keep_weather && !night.keep_time);
    const auto points = D::light_points(night);
    assert(points.size() == 4 && points[0].frame == 0 && points[0].hour == 22.5f && !points[0].rain);
    assert(points[1].frame == 30 && points[1].rain == 1.f && !points[1].hour);
    assert(points[2].frame == 120 && points[2].hour == 3.f && points[2].rain == 0.25f && points[3].frame == 180 && points[3].rain == 0.f);
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk"}],"time":[{"frame":0,"hour":24}]})"), "hour 0..24"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk"}],"weather":[{"frame":0,"rain":2}]})"), "rain 0..1"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk"}],"weather":[{"frame":5,"rain":1},{"frame":5,"rain":0}]})"), "frames must increase"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk"}],"keep_time":true})"), "keep_time needs a time change"));
    assert(has(refused(R"({"type":"demo","frames":30,"actors":[{"id":"a","actor":"Dk"}],"time":[{"frame":0,"hour":1},{"frame":1,"hour":2},{"frame":2,"hour":3},{"frame":3,"hour":4},{"frame":4,"hour":5}],
      "weather":[{"frame":5,"rain":1},{"frame":6,"rain":1},{"frame":7,"rain":1},{"frame":8,"rain":0}]})"), "at most at 8 frames"));
    // the image: PACKAGE and LIGHT staff, WAIT then CHANGE cuts with the game's Hour / Weather properties
    demo_event_image::Spec lit;
    lit.origin = demo_event_image::Origin{1, 2, 3, 0};
    for (uint32_t i = 0; i < points.size(); ++i) lit.light.push_back({D::kLightStartFlag0 + i, D::kLightEndFlag0 + i, points[i].hour, points[i].rain && *points[i].rain > 0});
    lit.light_wait_flag = D::kLightWaitFlag;
    std::vector<uint8_t> lit_image;
    assert(demo_event_image::make(lit, lit_image, why));
    demo_event_image::Spec lit_back;
    assert(demo_event_image::validate(lit_image, lit_back, why) && lit_back.light == lit.light && lit_back.origin && lit_back.origin->y == 2.f && lit_back.light_wait_flag == 20);
    auto word = [&](size_t at) { return uint32_t(lit_image[at]) << 24 | uint32_t(lit_image[at + 1]) << 16 | uint32_t(lit_image[at + 2]) << 8 | lit_image[at + 3]; };
    assert(word(0x40 + 0x7C) == 2 && word(0x40 + 0x2C) == 0 && word(0x40 + 0x30) == 1);  // the event's two staffs
    assert(std::string(reinterpret_cast<const char*>(lit_image.data() + 0x140)) == "LIGHT" && word(0x140 + 0x2C) == 9 && word(0x140 + 0x30) == 1);
    const uint32_t cut_at = word(2 * 8), prop_at = word(3 * 8);
    assert(word(2 * 8 + 4) == 6 && cut_at == 0x190);
    assert(std::string(reinterpret_cast<const char*>(lit_image.data() + cut_at + 0x50)) == "WAIT" && word(cut_at + 0x50 + 0x34) == 20 && word(cut_at + 0x50 + 0x3C) == 2);
    const uint32_t change2 = cut_at + 4 * 0x50;  // frame 120: hour 3 and rain
    assert(std::string(reinterpret_cast<const char*>(lit_image.data() + change2)) == "CHANGE" && word(change2 + 0x28) == 42 && word(change2 + 0x2C) == 0xFFFFFFFFu && word(change2 + 0x34) == 23);
    const uint32_t hour = prop_at + word(change2 + 0x38) * 0x40;
    assert(std::string(reinterpret_cast<const char*>(lit_image.data() + hour)) == "Hour" && word(hour + 0x24) == 0);
    const uint32_t weather = prop_at + word(hour + 0x30) * 0x40;
    assert(std::string(reinterpret_cast<const char*>(lit_image.data() + weather)) == "Weather" && word(weather + 0x24) == 3 && word(word(5 * 8) + word(weather + 0x28) * 4) == 0);
    assert(word(cut_at + 5 * 0x50 + 0x38) == 0xFFFFFFFFu && word(cut_at + 5 * 0x50 + 0x3C) == 0xFFFFFFFFu);  // rain 0: no property, last cut
    lit_image[change2 + 0x2B] = 0;  // a changed flag is refused
    assert(!demo_event_image::validate(lit_image, lit_back, why));
    demo_event_image::Spec clash = lit;
    clash.light[1].start_flag = 5;  // the event's finish flag
    assert(!demo_event_image::make(clash, lit_image, why) && has(why, "distinct"));
    clash = lit;
    clash.light[0].hour = 24.f;
    assert(!demo_event_image::make(clash, lit_image, why));
    // messages across demos: the window keeps the last code it took, so the next demo continues the
    // alternation (a demo after one that ended on 889 starts on 891)
    {
        assert(D::first_message_code(0) == 0 && D::first_message_code(889) == 1 && D::first_message_code(891) == 0 && D::first_message_code(1234) == 0);
        D::Compiled shifted;
        assert(D::compile(d, ids, shifted, why, D::first_message_code(889)));
        demo_stb::File sf;
        assert(demo_stb::parse(shifted.stb, sf, why));
        const std::string st = demo_stb::dump(sf);
        assert(has(st, "== JMSG \"message\"\n  f60 MESSAGE = u32 891\n  f150 MESSAGE = u32 889\n"));
        // two demos in a row with one message each never repeat a code
        uint32_t last = 0;
        for (int demo = 0; demo < 4; ++demo) {
            const uint32_t code = D::demo_message_code(0, D::first_message_code(last));
            assert(code != last);
            last = code;
        }
    }
    // a demo step right after another waits for the previous demo's cleanup, bounded
    {
        namespace G = demo_start_gate;
        G::Gate g;
        assert(g.step(false, false) == G::Step::GiveUp);  // nothing waiting
        g.arm();
        for (int i = 0; i < 10; ++i) assert(g.step(true, true) == G::Step::Wait);  // cleanup takes 10 ticks
        assert(g.step(false, false) == G::Step::Start && !g.armed && g.ticks == 10);
        g.arm();
        for (uint32_t i = 0; i < G::kLimitTicks; ++i) assert(g.step(true, true) == G::Step::Wait);
        assert(g.step(true, true) == G::Step::GiveUp && !g.armed);  // never cleared: gives up
        g.arm();
        assert(g.step(false, true) == G::Step::GiveUp);  // another demo took over meanwhile
    }
    std::puts("demo_scene: ok");
    return 0;
}
