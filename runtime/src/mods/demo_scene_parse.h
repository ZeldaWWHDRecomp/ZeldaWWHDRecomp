// SPDX-License-Identifier: MPL-2.0
// demo_scene::parse (included by demo_scene.h): header-only, so every target that reads mod manifests
// (story_world_manifest.h) checks demo steps without linking the STB writer.
#pragma once
#include <algorithm>
#include <cmath>
#include <set>

namespace demo_scene {
namespace parse_detail {
using mods::json::Value;
inline bool fail(std::string& why, const std::string& text) { why = text; return false; }
inline bool keys_only(const Value& v, std::initializer_list<const char*> allowed) {
    if (v.type != Value::Object) return false;
    for (const auto& [key, value] : v.object) {
        bool found = false;
        for (const char* a : allowed) found |= key == a;
        if (!found) return false;
    }
    return true;
}
inline bool integer(const Value& v, double lo, double hi, uint32_t& out) {
    if (v.type != Value::Number || !std::isfinite(v.number) || v.number < lo || v.number > hi || std::floor(v.number) != v.number) return false;
    out = uint32_t(v.number);
    return true;
}
inline bool real(const Value& v, double lo, double hi, float& out) {
    if (v.type != Value::Number || !std::isfinite(v.number) || v.number < lo || v.number > hi) return false;
    out = float(v.number);
    return true;
}
inline bool vec(const Value& v, Vec& out) {
    if (!keys_only(v, {"x", "y", "z"})) return false;
    return real(v.get("x"), -1e6, 1e6, out.x) && real(v.get("y"), -1e6, 1e6, out.y) && real(v.get("z"), -1e6, 1e6, out.z);
}
inline bool plain(const std::string& s, size_t max, bool dots = false) {
    if (s.empty() || s.size() > max) return false;
    for (char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || (dots && c == '.'))) return false;
    return true;
}
inline bool hexbytes(const Value& v, std::vector<uint8_t>& out, size_t max) {
    if (v.type != Value::String || v.text.size() % 2 || v.text.size() / 2 > max) return false;
    out.clear();
    for (size_t i = 0; i < v.text.size(); i += 2) {
        auto nibble = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
        const int a = nibble(v.text[i]), b = nibble(v.text[i + 1]);
        if (a < 0 || b < 0) return false;
        out.push_back(uint8_t(a << 4 | b));
    }
    return true;
}
// Any of the game's archives can lend a file, its demo archives (Demo04, Demo07..) included: the
// demo itself always plays from the port's virtual archive, so the game's checks of the running
// demo's archive name (Demo04, Demo08, Demo29, Demo37, Demo45, Demo46) never see these names.
inline bool resource(const Value& v, const char* ext, Resource& out, std::string& why) {
    if (!keys_only(v, {"archive", "file"}) && !keys_only(v, {"frame", "archive", "file", "speed", "loop"})) return fail(why, "resource needs archive and file");
    out.archive = v.get("archive").string();
    out.file = v.get("file").string();
    if (!plain(out.archive, 11)) return fail(why, "invalid archive name \"" + out.archive + "\"");
    if (!plain(out.file, 31, true) || out.file.size() < 5 || out.file.compare(out.file.size() - 4, 4, ext) != 0) return fail(why, std::string("resource file must be a ") + ext + " name");
    return true;
}
inline bool track(const Value& v, bool scalar, uint32_t frames, Track& out, std::string& why, const char* what) {
    out = {};
    if (v.type == Value::Null) return true;
    if (!keys_only(v, {"curve", "keys"})) return fail(why, std::string(what) + ": track needs curve and keys");
    const std::string curve = v.get("curve").string("linear");
    if (curve == "linear") out.curve = Curve::Linear;
    else if (curve == "smooth") out.curve = Curve::Smooth;
    else if (curve == "bspline") out.curve = Curve::BSpline;
    else if (curve == "step") out.curve = Curve::Step;
    else return fail(why, std::string(what) + ": curve must be linear, smooth, bspline or step");
    const auto& keys = v.get("keys");
    if (keys.type != Value::Array || keys.array.empty() || keys.array.size() > kMaxKeys) return fail(why, std::string(what) + ": 1..64 keys");
    for (const auto& k : keys.array) {
        Key key;
        if (!keys_only(k, {"frame", "value"}) || !integer(k.get("frame"), 0, frames, key.frame)) return fail(why, std::string(what) + ": key needs a frame inside the demo");
        if (scalar ? !real(k.get("value"), -1e6, 1e6, key.value.x) : !vec(k.get("value"), key.value)) return fail(why, std::string(what) + ": key needs a value");
        if (!out.keys.empty() && key.frame <= out.keys.back().frame) return fail(why, std::string(what) + ": key frames must increase");
        out.keys.push_back(key);
    }
    return true;
}
template <class T> inline bool frame_order(const std::vector<T>& v) { return std::is_sorted(v.begin(), v.end(), [](const T& a, const T& b) { return a.frame < b.frame; }); }
// "attach": [{"frame","actor","to","joint","offset","rotation"}], "detach": [{"frame","actor"}]
inline bool attachments(const Value& st, Demo& d, std::string& why) {
    const auto& attach = st.get("attach");
    const auto& detach = st.get("detach");
    if (attach.type == Value::Null) return detach.type == Value::Null || fail(why, "detach needs an attach");
    if (attach.type != Value::Array || attach.array.empty() || attach.array.size() > kMaxAttach) return fail(why, "demo attach must be a list of up to 8");
    if (detach.type != Value::Null && (detach.type != Value::Array || detach.array.size() > kMaxAttach)) return fail(why, "demo detach must be a list of up to 8");
    auto actor = [&](const std::string& id) -> const Actor* { for (const auto& a : d.actors) if (a.id == id) return &a; return nullptr; };
    for (const auto& v : attach.array) {
        Attach a;
        if (!keys_only(v, {"frame", "actor", "to", "joint", "offset", "rotation", "upright", "blend"}) || !integer(v.get("frame"), 0, d.frames, a.frame)) return fail(why, "attach needs a frame inside the demo");
        a.actor = v.get("actor").string(); a.to = v.get("to").string(); a.joint = v.get("joint").string();
        const Actor* carried = actor(a.actor);
        const Actor* carrier = actor(a.to);
        if (!carried || !carrier) return fail(why, "attach: actor and to must be demo actors");
        if (carried == carrier) return fail(why, "attach: an actor can't carry itself");
        if (carried->kind == Actor::Kind::Model || carrier->kind == Actor::Kind::Model) return fail(why, "attach: models from the game's archives can't carry or be carried");
        if (!plain(a.joint, 31)) return fail(why, "attach needs the carrier's joint name");
        if (v.object.contains("offset") && !vec(v.get("offset"), a.offset)) return fail(why, "attach offset must be {x,y,z}");
        if (v.object.contains("rotation") && !vec(v.get("rotation"), a.rotation)) return fail(why, "attach rotation must be {x,y,z} degrees");
        if (v.object.contains("upright")) { if (v.get("upright").type != Value::Bool) return fail(why, "attach upright must be true or false"); a.upright = v.get("upright").boolean; }
        if (v.object.contains("blend") && !integer(v.get("blend"), 0, 60, a.blend)) return fail(why, "attach blend must be 0..60 frames");
        d.attach.push_back(a);
    }
    if (!frame_order(d.attach)) return fail(why, "attach entries must be in frame order");
    for (const auto& v : detach.type == Value::Array ? detach.array : std::vector<Value>{}) {
        uint32_t frame = 0;
        if (!keys_only(v, {"frame", "actor", "fall"}) || !integer(v.get("frame"), 0, d.frames, frame)) return fail(why, "detach needs a frame inside the demo");
        if (v.object.contains("fall") && v.get("fall").type != Value::Bool) return fail(why, "detach fall must be true or false");
        const std::string id = v.get("actor").string();
        Attach* open = nullptr;  // the actor's last attach before this frame
        for (auto& a : d.attach) if (a.actor == id && a.frame < frame) open = &a;
        if (!open || open->release != kNoRelease) return fail(why, "detach of " + id + " at frame " + std::to_string(frame) + " without an attach before it");
        open->release = frame;
        open->fall = v.get("fall").boolean;
    }
    // one carrier per actor at a time; no chains (a carried actor doesn't carry); Link only in his demo clips
    for (size_t i = 0; i < d.attach.size(); ++i) {
        const auto& a = d.attach[i];
        for (size_t k = 0; k < d.attach.size(); ++k) {
            const auto& b = d.attach[k];
            if (k == i || !(a.frame < b.release && b.frame < a.release)) continue;
            if (a.actor == b.actor) return fail(why, "attach: " + a.actor + " is carried twice at once (detach it first)");
            if (a.actor == b.to) return fail(why, "attach: " + a.actor + " carries while it is carried");
        }
        if (actor(a.actor)->kind == Actor::Kind::Link) {
            const auto& modes = actor(a.actor)->modes;
            const Mode* current = nullptr;
            bool ok = true;
            for (const auto& m : modes) {
                if (m.frame <= a.frame) current = &m;
                else if (m.frame < a.release && m.mode < 512) ok = false;
            }
            if (!current || current->mode < 512 || !ok) return fail(why, "a carried Link plays one of his demo clips (mode \"clip\") while he is carried");
        }
    }
    return true;
}
// "time": [{"frame","hour"}], "weather": [{"frame","rain"}], "keep_time", "keep_weather"
inline bool light(const Value& st, Demo& d, std::string& why) {
    const auto& time = st.get("time");
    const auto& weather = st.get("weather");
    if (time.type != Value::Null) {
        if (time.type != Value::Array || time.array.empty() || time.array.size() > kMaxLightPoints) return fail(why, "demo time must be a list of up to 8");
        for (const auto& v : time.array) {
            TimeKey k;
            if (!keys_only(v, {"frame", "hour"}) || !integer(v.get("frame"), 0, d.frames, k.frame) || !real(v.get("hour"), 0, 23.99, k.hour)) return fail(why, "time needs a frame inside the demo and an hour 0..24");
            if (!d.time.empty() && k.frame <= d.time.back().frame) return fail(why, "time frames must increase");
            d.time.push_back(k);
        }
    }
    if (weather.type != Value::Null) {
        if (weather.type != Value::Array || weather.array.empty() || weather.array.size() > kMaxLightPoints) return fail(why, "demo weather must be a list of up to 8");
        for (const auto& v : weather.array) {
            WeatherKey k;
            if (!keys_only(v, {"frame", "rain"}) || !integer(v.get("frame"), 0, d.frames, k.frame) || !real(v.get("rain"), 0, 1, k.rain)) return fail(why, "weather needs a frame inside the demo and rain 0..1");
            if (!d.weather.empty() && k.frame <= d.weather.back().frame) return fail(why, "weather frames must increase");
            d.weather.push_back(k);
        }
    }
    for (const char* key : {"keep_time", "keep_weather"}) {
        if (!st.object.contains(key)) continue;
        if (st.get(key).type != Value::Bool) return fail(why, std::string(key) + " must be true or false");
        (std::string(key) == "keep_time" ? d.keep_time : d.keep_weather) = st.get(key).boolean;
    }
    if (d.keep_time && d.time.empty()) return fail(why, "keep_time needs a time change");
    if (d.keep_weather && d.weather.empty()) return fail(why, "keep_weather needs a weather change");
    if (light_points(d).size() > kMaxLightPoints) return fail(why, "a demo changes time and weather at most at 8 frames");
    return true;
}
}  // namespace parse_detail

inline bool parse(const mods::json::Value& st, Demo& d, std::string& why) {
    using namespace parse_detail;
    using mods::json::Value;
    d = {};
    if (!keys_only(st, {"type", "frames", "origin", "angle", "camera", "actors", "sounds", "particles", "messages", "attach", "detach", "time", "weather", "keep_time", "keep_weather"}) || st.get("type").string() != "demo")
        return fail(why, "unknown demo field");
    if (!integer(st.get("frames"), 1, kMaxFrames, d.frames)) return fail(why, "demo needs frames 1..9000");
    const auto& origin = st.get("origin");
    if (origin.type == Value::String) {
        if (origin.text != "link") return fail(why, "demo origin must be \"link\" or a position");
        d.origin_link = true;
    } else if (origin.type != Value::Null) {
        Vec o;
        if (!vec(origin, o)) return fail(why, "invalid demo origin");
        d.origin = o;
    }
    if (st.object.contains("angle") && !real(st.get("angle"), -360, 360, d.angle)) return fail(why, "demo angle must be -360..360");
    const auto& cam = st.get("camera");
    if (cam.type != Value::Null) {
        if (!keys_only(cam, {"eye", "target", "fovy", "roll"})) return fail(why, "unknown demo camera field");
        if (!track(cam.get("eye"), false, d.frames, d.eye, why, "camera eye") || !track(cam.get("target"), false, d.frames, d.target, why, "camera target") ||
            !track(cam.get("fovy"), true, d.frames, d.fovy, why, "camera fovy") || !track(cam.get("roll"), true, d.frames, d.roll, why, "camera roll"))
            return false;
        if (d.eye.keys.empty() != d.target.keys.empty()) return fail(why, "demo camera needs eye and target together");
        for (const auto& k : d.fovy.keys) if (k.value.x < 5 || k.value.x > 120) return fail(why, "camera fovy must be 5..120");
    }
    const auto& actors = st.get("actors");
    if (actors.type != Value::Null) {
        if (actors.type != Value::Array || actors.array.size() > kMaxActors) return fail(why, "demo actors must be a list of up to 24");
        std::set<std::string> ids, names;
        for (const auto& a : actors.array) {
            Actor x;
            if (!keys_only(a, {"id", "actor", "model", "character", "spawn", "params", "position", "rotation", "scale", "clips", "modes", "data"})) return fail(why, "unknown demo actor field");
            x.id = a.get("id").string();
            if (!plain(x.id, 31) || !ids.insert(x.id).second) return fail(why, "demo actor needs a unique id");
            if (int(a.object.contains("actor")) + int(a.object.contains("model")) + int(a.object.contains("character")) != 1) return fail(why, "demo actor " + x.id + ": one of actor, model or character");
            if (a.object.contains("character")) {
                x.kind = Actor::Kind::Character;
                x.character = a.get("character").string();
                if (!plain(x.character, 63)) return fail(why, "demo actor " + x.id + ": invalid character id");
            } else if (a.object.contains("model")) {
                x.kind = Actor::Kind::Model;
                if (!resource(a.get("model"), ".bdl", x.model, why)) return fail(why, "demo actor " + x.id + ": " + why);
            } else {
                x.actor = a.get("actor").string();
                if (!plain(x.actor, 7) || x.actor.rfind("d_act", 0) == 0) return fail(why, "demo actor " + x.id + ": invalid stage actor name");
                if (!names.insert(x.actor).second) return fail(why, "demo actor " + x.actor + " listed twice");
                x.kind = x.actor == "Link" ? Actor::Kind::Link : Actor::Kind::Game;
            }
            if (a.object.contains("spawn") || a.object.contains("params")) {
                if (x.kind != Actor::Kind::Game) return fail(why, "demo actor " + x.id + ": spawn and params are for game actors");
                if (a.object.contains("spawn")) {
                    const auto& v = a.get("spawn");
                    if (v.type == Value::String && v.text == "always") x.own = true;
                    else if (v.type != Value::Bool) return fail(why, "spawn must be true, false or \"always\"");
                    else x.spawn = v.boolean;
                }
                if (a.object.contains("params")) {
                    const auto& v = a.get("params");
                    if (v.type == Value::String && v.text.size() > 2 && v.text.size() <= 10 && v.text.compare(0, 2, "0x") == 0 && v.text.find_first_not_of("0123456789abcdefABCDEF", 2) == std::string::npos)
                        x.params = uint32_t(std::stoul(v.text.substr(2), nullptr, 16));
                    else if (!integer(v, 0, 4294967295.0, x.params)) return fail(why, "params must be a number or \"0x\" hex");
                }
            }
            if (!track(a.get("position"), false, d.frames, x.position, why, "actor position") || !track(a.get("rotation"), false, d.frames, x.rotation, why, "actor rotation") ||
                !track(a.get("scale"), false, d.frames, x.scale, why, "actor scale"))
                return fail(why, "demo actor " + x.id + ": " + why);
            const auto& clips = a.get("clips");
            if (clips.type != Value::Null) {
                if (x.kind == Actor::Kind::Link) return fail(why, "Link plays demo modes, not clips");
                if (clips.type != Value::Array || clips.array.size() > kMaxEvents) return fail(why, "demo actor " + x.id + ": clips must be a list");
                if (x.kind == Actor::Kind::Character) {
                    for (const auto& c : clips.array) {
                        NamedClip clip;
                        if (!keys_only(c, {"frame", "clip", "loop"}) || !integer(c.get("frame"), 0, d.frames, clip.frame) || !plain(c.get("clip").string(), 31)) return fail(why, "demo actor " + x.id + ": a character clip needs frame and its clip name");
                        clip.clip = c.get("clip").string();
                        if (c.object.contains("loop")) { if (c.get("loop").type != Value::Bool) return fail(why, "clip loop must be true or false"); clip.loop = c.get("loop").boolean; }
                        x.named_clips.push_back(clip);
                    }
                    if (!frame_order(x.named_clips)) return fail(why, "demo clips must be in frame order");
                }
                for (const auto& c : x.kind == Actor::Kind::Character ? std::vector<Value>{} : clips.array) {
                    Clip clip;
                    if (!keys_only(c, {"frame", "archive", "file", "speed", "loop"}) || !integer(c.get("frame"), 0, d.frames, clip.frame)) return fail(why, "demo actor " + x.id + ": clip needs a frame");
                    if (!resource(c, ".bck", clip.resource, why)) return fail(why, "demo actor " + x.id + ": " + why);
                    if (c.object.contains("speed") && !real(c.get("speed"), 0, 8, clip.speed)) return fail(why, "clip speed must be 0..8");
                    if (c.object.contains("loop")) { if (c.get("loop").type != Value::Bool) return fail(why, "clip loop must be true or false"); clip.loop = c.get("loop").boolean; }
                    x.clips.push_back(clip);
                }
                if (!frame_order(x.clips)) return fail(why, "demo clips must be in frame order");
            }
            const auto& modes = a.get("modes");
            if (modes.type != Value::Null) {
                if (x.kind != Actor::Kind::Link) return fail(why, "only Link has demo modes");
                if (modes.type != Value::Array || modes.array.size() > kMaxEvents) return fail(why, "Link modes must be a list");
                for (const auto& m : modes.array) {
                    Mode mode;
                    if (!keys_only(m, {"frame", "mode", "data", "clip", "btp", "btk", "loop", "blend"}) || !integer(m.get("frame"), 0, d.frames, mode.frame)) return fail(why, "Link mode needs a frame");
                    if (m.get("mode").type == Value::String) { mode.mode = link_mode(m.get("mode").text); if (!mode.mode) return fail(why, "unknown Link mode \"" + m.get("mode").text + "\""); }
                    else if (m.object.contains("mode")) { if (!integer(m.get("mode"), 1, 0xFFFF, mode.mode)) return fail(why, "Link mode must be a name or 1..65535"); }
                    else if (m.object.contains("clip")) mode.mode = 512;
                    else return fail(why, "Link mode needs mode or clip");
                    if (m.object.contains("data") && !hexbytes(m.get("data"), mode.data, 64)) return fail(why, "Link mode data must be hex bytes");
                    if (m.object.contains("clip")) {
                        mode.clip = m.get("clip").string();
                        if (mode.mode != 512 || !mode.data.empty() || !plain(mode.clip, 31, true) || mode.clip.size() < 5 || mode.clip.compare(mode.clip.size() - 4, 4, ".bck") != 0)
                            return fail(why, "a Link clip is a .bck of his demo animations, with mode \"clip\" and no data");
                    }
                    // a clip's face (btp + btk), loop and blend
                    const bool face = m.object.contains("btp") || m.object.contains("btk");
                    if ((face || m.object.contains("loop") || m.object.contains("blend")) && mode.clip.empty()) return fail(why, "btp, btk, loop and blend go with a Link clip");
                    if (face) {
                        mode.btp = m.get("btp").string(); mode.btk = m.get("btk").string();
                        auto named = [](const std::string& f, const char* ext) { return plain(f, 31, true) && f.size() >= 5 && f.compare(f.size() - 4, 4, ext) == 0; };
                        if (!named(mode.btp, ".btp") || !named(mode.btk, ".btk")) return fail(why, "a Link clip's face needs both btp (a .btp) and btk (a .btk) of his demo animations");
                    }
                    if (m.object.contains("loop")) { if (m.get("loop").type != Value::Bool) return fail(why, "Link clip loop must be true or false"); mode.loop = m.get("loop").boolean; }
                    if (m.object.contains("blend")) { uint32_t b = 0; if (!integer(m.get("blend"), 0, 60, b)) return fail(why, "Link clip blend must be 0..60 frames"); mode.blend = int32_t(b); }
                    x.modes.push_back(mode);
                }
                if (!frame_order(x.modes)) return fail(why, "Link modes must be in frame order");
            }
            const auto& data = a.get("data");
            if (data.type != Value::Null) {
                if (data.type != Value::Array || data.array.size() > kMaxEvents) return fail(why, "demo actor data must be a list");
                for (const auto& e : data.array) {
                    Data item;
                    if (!keys_only(e, {"frame", "id", "bytes"}) || !integer(e.get("frame"), 0, d.frames, item.frame) || !integer(e.get("id"), 0, 255, item.id) || !hexbytes(e.get("bytes"), item.bytes, 64))
                        return fail(why, "demo actor data needs frame, id and hex bytes");
                    x.data.push_back(item);
                }
            }
            d.actors.push_back(std::move(x));
        }
    }
    auto find_actor = [&](const std::string& id) { return std::any_of(d.actors.begin(), d.actors.end(), [&](const Actor& a) { return a.id == id; }); };
    const auto& sounds = st.get("sounds");
    if (sounds.type != Value::Null) {
        if (sounds.type != Value::Array || sounds.array.size() > kMaxEvents) return fail(why, "demo sounds must be a list");
        for (const auto& s : sounds.array) {
            Sound x;
            if (!keys_only(s, {"frame", "id", "actor", "position", "volume", "pan"}) || !integer(s.get("frame"), 0, d.frames, x.frame) || !integer(s.get("id"), 1, 0xFFFFFFFE, x.id))
                return fail(why, "demo sound needs frame and id");
            if (s.object.contains("actor") && s.object.contains("position")) return fail(why, "demo sound: actor or position");
            if (s.object.contains("actor")) { x.actor = s.get("actor").string(); if (!find_actor(x.actor)) return fail(why, "demo sound: unknown actor " + x.actor); }
            if (s.object.contains("position")) { Vec p; if (!vec(s.get("position"), p)) return fail(why, "demo sound: invalid position"); x.position = p; }
            if (s.object.contains("volume") && !real(s.get("volume"), 0, 1, x.volume)) return fail(why, "demo sound volume must be 0..1");
            if (s.object.contains("pan") && !real(s.get("pan"), 0, 1, x.pan)) return fail(why, "demo sound pan must be 0..1");
            d.sounds.push_back(x);
        }
    }
    const auto& particles = st.get("particles");
    if (particles.type != Value::Null) {
        if (particles.type != Value::Array || particles.array.size() > kMaxEvents) return fail(why, "demo particles must be a list");
        for (const auto& p : particles.array) {
            Particle x;
            if (!keys_only(p, {"frame", "id", "actor", "joint", "position"}) || !integer(p.get("frame"), 0, d.frames, x.frame) || !integer(p.get("id"), 0, 0xFFFF, x.id))
                return fail(why, "demo particle needs frame and id");
            if (p.object.contains("actor") == p.object.contains("position")) return fail(why, "demo particle: actor or position, exclusively");
            if (p.object.contains("actor")) {
                x.actor = p.get("actor").string();
                if (!find_actor(x.actor)) return fail(why, "demo particle: unknown actor " + x.actor);
                if (p.object.contains("joint")) { x.joint = p.get("joint").string(); if (!plain(x.joint, 31)) return fail(why, "demo particle: invalid joint name"); }
            } else {
                if (p.object.contains("joint")) return fail(why, "demo particle: a joint needs an actor");
                Vec v;
                if (!vec(p.get("position"), v)) return fail(why, "demo particle: invalid position");
                x.position = v;
            }
            d.particles.push_back(x);
        }
    }
    const auto& messages = st.get("messages");
    if (messages.type != Value::Null) {
        if (messages.type != Value::Array || messages.array.size() > 16) return fail(why, "demo messages must be a list of up to 16");
        for (const auto& m : messages.array) {
            Message x;
            if (!keys_only(m, {"frame", "text"}) || !integer(m.get("frame"), 0, d.frames >= 2 ? d.frames - 2 : 0, x.frame) || d.frames < 2 || m.get("text").type != Value::String || m.get("text").text.empty() || m.get("text").text.size() > 400)
                return fail(why, "demo message needs a frame before the end and text");
            x.text = m.get("text").text;
            if (!d.messages.empty() && x.frame <= d.messages.back().frame) return fail(why, "demo message frames must increase");
            d.messages.push_back(x);
        }
    }
    if (!parse_detail::attachments(st, d, why) || !parse_detail::light(st, d, why)) return false;
    if (d.actors.empty() && d.eye.keys.empty()) return fail(why, "demo needs a camera or actors");
    return true;
}

}  // namespace demo_scene
