// SPDX-License-Identifier: MPL-2.0
// A mod scene's "demo" step: keyframe tracks for the camera, game actors (by stage name, Link
// included), models from the player's game archives, sounds, particles and messages, compiled into
// a JStudio STB (demo_stb.h) that the game's own demo system plays. The manifest is parsed and
// checked at load; the STB is built when the step starts, once the game's resource ids are known.
//
// {"type":"demo","frames":240,"origin":"link"|{"x","y","z"},"angle":0,
//  "camera":{"eye":TRACK,"target":TRACK,"fovy":TRACK,"roll":TRACK},
//  "actors":[{"id":"bird","actor":"Dk"|"Link"|"model":{"archive":"Kamome","file":"ka.bdl"},
//             "position":TRACK,"rotation":TRACK,"scale":TRACK,
//             "clips":[{"frame":0,"archive":"Dk","file":"fly1.bck","speed":1}],
//             "modes":[{"frame":0,"mode":2,"data":"hex"}],          (Link: demo modes)
//             "data":[{"frame":0,"id":1,"bytes":"hex"}]}],           (raw actor data paragraphs)
//  "sounds":[{"frame":30,"id":22820,"actor":"bird"|"position":{..},"volume":1,"pan":0.5}],
//  "particles":[{"frame":50,"id":33059,"actor":"bird","joint":"j_dk_ashi_r3"|"position":{..}}],
//  "messages":[{"frame":60,"text":"..."}]}
// TRACK = {"curve":"linear"|"smooth"|"bspline"|"step","keys":[{"frame":0,"value":{x,y,z} or number}]}
// Frames are the game's 30 Hz demo frames.
#pragma once
#include "mod_json.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace demo_scene {
inline constexpr uint32_t kMaxFrames = 9000, kMaxActors = 24, kMaxKeys = 64, kMaxEvents = 64, kMaxStbBytes = 0xC000;
struct Vec { float x = 0, y = 0, z = 0; bool operator==(const Vec&) const = default; };
enum class Curve { Linear, Smooth, BSpline, Step };
struct Key { uint32_t frame = 0; Vec value; bool operator==(const Key&) const = default; };
struct Track { Curve curve = Curve::Linear; std::vector<Key> keys; bool operator==(const Track&) const = default; };  // scalars use value.x
struct Resource { std::string archive, file; bool operator==(const Resource&) const = default; };
struct Clip { uint32_t frame = 0; Resource resource; float speed = 1; bool operator==(const Clip&) const = default; };
struct Mode { uint32_t frame = 0, mode = 0; std::vector<uint8_t> data; std::string clip; bool operator==(const Mode&) const = default; };  // clip: Link's own demo animation (.bck)
struct NamedClip { uint32_t frame = 0; std::string clip; bool loop = true; bool operator==(const NamedClip&) const = default; };  // a mod character's own clip
struct Data { uint32_t frame = 0, id = 0; std::vector<uint8_t> bytes; bool operator==(const Data&) const = default; };
struct Actor {
    enum class Kind { Game, Link, Model, Character } kind = Kind::Game;
    std::string id, actor;  // demo-local id; the game's stage actor name (Game, Link)
    Resource model;         // Model
    std::string character;  // Character: an actor this mod placed (resident villager, mod actor, enemy) by its id
    bool spawn = true;      // Game: created for the demo when the room doesn't have one (and removed after)
    uint32_t params = 0;    // Game: the spawned actor's parameters
    std::vector<NamedClip> named_clips;  // Character: the mod actor's own clips
    Track position, rotation, scale;
    std::vector<Clip> clips;
    std::vector<Mode> modes;
    std::vector<Data> data;
    bool operator==(const Actor&) const = default;
};
struct Sound { uint32_t frame = 0, id = 0; std::string actor; std::optional<Vec> position; float volume = 1, pan = 0.5f; bool operator==(const Sound&) const = default; };
struct Particle { uint32_t frame = 0, id = 0; std::string actor, joint; std::optional<Vec> position; bool operator==(const Particle&) const = default; };
struct Message { uint32_t frame = 0; std::string text; bool operator==(const Message&) const = default; };
// One demo actor carried on a joint of another from `frame` until `release` (a detach's frame, or
// kNoRelease: to the end). Its pose is the carrier joint's matrix times offset/rotation (degrees, ZXY).
inline constexpr uint32_t kNoRelease = 0xFFFFFFFFu, kMaxAttach = 8;
struct Attach {
    uint32_t frame = 0, release = kNoRelease;
    std::string actor, to, joint;
    Vec offset, rotation;
    bool upright = false;
    bool fall = false;     // detach: the released actor falls to the ground (else it stays where it was let go)  // keep only the joint's heading (the carried actor stays upright, as in the game's Demo07)
    bool operator==(const Attach&) const = default;
};
// Time and weather at a frame: hour 0..24, rain 0..1 (0 no rain, 1 the game's full rain).
struct TimeKey { uint32_t frame = 0; float hour = 0; bool operator==(const TimeKey&) const = default; };
struct WeatherKey { uint32_t frame = 0; float rain = 0; bool operator==(const WeatherKey&) const = default; };
struct Demo {
    uint32_t frames = 0;
    bool origin_link = false;
    std::optional<Vec> origin;
    float angle = 0;
    Track eye, target, fovy, roll;
    std::vector<Actor> actors;
    std::vector<Sound> sounds;
    std::vector<Particle> particles;
    std::vector<Message> messages;
    std::vector<Attach> attach;
    std::vector<TimeKey> time;
    std::vector<WeatherKey> weather;
    bool keep_time = false, keep_weather = false;  // otherwise both come back when the demo ends
    bool operator==(const Demo&) const = default;
};
// The demo's time and weather changes merged per frame, in frame order: each is one CHANGE cut of the
// LIGHT staff in the demo's event (demo_event_image.h), started by the port with flag
// kLightStartFlag0 + index at its frame; the cut sets kLightEndFlag0 + index when it ends.
inline constexpr uint32_t kMaxLightPoints = 8, kLightWaitFlag = 20, kLightEndFlag0 = 21, kLightStartFlag0 = 40;
struct LightPoint { uint32_t frame = 0; std::optional<float> hour, rain; };
inline std::vector<LightPoint> light_points(const Demo& d) {
    std::vector<LightPoint> out;
    auto at = [&](uint32_t frame) -> LightPoint& {
        for (auto& p : out) if (p.frame == frame) return p;
        auto it = out.begin();
        while (it != out.end() && it->frame < frame) ++it;
        return *out.insert(it, LightPoint{frame, {}, {}});
    };
    for (const auto& t : d.time) at(t.frame).hour = t.hour;
    for (const auto& w : d.weather) at(w.frame).rain = w.rain;
    return out;
}
inline bool parse(const mods::json::Value& step, Demo& out, std::string& why);  // demo_scene_parse.h

// The game's id of a file in one of its archives (the archive's resource id table), or -1.
using Resolver = std::function<int32_t(const Resource&)>;
// Link's demo modes by name (the game's daPy_demo_c modes); "clip" plays one of his demo animations.
inline constexpr std::pair<const char*, uint32_t> kLinkModes[] = {{"wait", 1}, {"walk", 2}, {"dash", 3}, {"turn", 5}, {"talk", 6}, {"holdup", 13}, {"keep", 14},
    {"look_around", 15}, {"salute", 19}, {"surprised", 24}, {"lookup", 26}, {"nod", 36}, {"present", 37}, {"smile", 50}, {"clip", 512}};
inline uint32_t link_mode(const std::string& name) {
    for (const auto& [n, v] : kLinkModes) if (name == n) return v;
    return 0;
}
inline constexpr const char* kLinkClipArchives[] = {"LkD00", "LkD01"};  // Link's demo animations (one is loaded at a time)
struct Cast { std::string name; bool spawn = true; uint32_t params = 0; bool placed = false; Vec position; };  // a game actor the demo names
struct CharacterRef { std::string name, character; std::vector<NamedClip> clips; };                       // "MA00".. -> a mod-placed actor
struct Compiled {
    std::vector<uint8_t> stb;
    std::vector<std::pair<Resource, uint32_t>> virtual_ids;  // virtual id n (0x10000|n in the STB) -> archive file and its id
    std::vector<std::string> participants;                   // stage actor names that must exist when the demo starts
    std::vector<Cast> cast;                                  // the same, with how to spawn them
    std::vector<CharacterRef> characters;                    // mod-placed actors, by their stand-in names
    std::vector<Message> messages;                           // in frame order; one suspend point each
};
// The demo message codes the JMSG track carries. The game's demo message window looks a code up in
// the message data's demo codes (not by message number) and takes a message only when the code
// differs from the last one, so messages alternate between two retail demo codes whose messages
// start without text codes (no size or colour carried over); the port resolves their message
// numbers at run time and gives them the mod's text.
inline constexpr uint32_t kDemoMessageCodes[2] = {889, 891};
inline constexpr uint32_t demo_message_code(size_t index, uint32_t first = 0) { return kDemoMessageCodes[(index + first) % 2]; }
// The alternation continues across demos: the window still holds the code it took last (a demo right
// after another one starts where that left off), so a demo's first message never repeats it.
inline constexpr uint32_t first_message_code(uint32_t window_last_code) { return window_last_code == kDemoMessageCodes[0] ? 1 : 0; }
// Each message: its demo message code at its frame (the port arms its text before the point); the
// demo waits one frame later until the player closes it (the game's own unsuspend).
bool compile(const Demo& demo, const Resolver& resolve, Compiled& out, std::string& why, uint32_t first_code = 0);  // first_code: first_message_code()
}  // namespace demo_scene
#include "demo_scene_parse.h"
