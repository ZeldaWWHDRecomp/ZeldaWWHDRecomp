// SPDX-License-Identifier: MPL-2.0
#include "demo_scene.h"

#include "demo_stb.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <map>
#include <set>

namespace demo_scene {
namespace {
namespace S = demo_stb;
bool fail(std::string& why, const std::string& text) { why = text; return false; }
float seconds(uint32_t frames) { return float(frames) / 30.f; }
float component(const Vec& v, unsigned i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }
// One curve function for component i of a track (from its first key), or none for a single key.
uint32_t curve(S::Builder& b, const Track& t, unsigned i) {
    const uint32_t f0 = t.keys.front().frame;
    if (t.curve == Curve::Smooth) {
        std::vector<S::HermiteKey> keys;
        for (size_t k = 0; k < t.keys.size(); ++k) {
            const size_t a = k ? k - 1 : k, c = k + 1 < t.keys.size() ? k + 1 : k;
            const float dt = seconds(t.keys[c].frame - t.keys[a].frame);
            const float slope = dt > 0 ? (component(t.keys[c].value, i) - component(t.keys[a].value, i)) / dt : 0;
            keys.push_back({seconds(t.keys[k].frame - f0), component(t.keys[k].value, i), slope, slope});
        }
        return b.hermite(keys);
    }
    std::vector<std::pair<float, float>> keys;
    for (const auto& k : t.keys) keys.push_back({seconds(k.frame - f0), component(k.value, i)});
    return b.list(keys, t.curve == Curve::BSpline ? S::Interpolation::BSpline : S::Interpolation::Linear);
}
// Writes a vector track (3 components, code3 = the XYZ paragraph code) or a scalar one (code1).
void emit(S::Builder& b, size_t o, const Track& t, uint32_t code3, uint32_t code1, bool scalar) {
    if (t.keys.empty()) return;
    auto at = [&](const Key& k) {
        if (scalar) b.immediate(o, k.frame, code1, {k.value.x});
        else b.immediate(o, k.frame, code3, {k.value.x, k.value.y, k.value.z});
    };
    if (t.keys.size() == 1 || t.curve == Curve::Step) { for (const auto& k : t.keys) at(k); return; }
    if (scalar) b.curves(o, t.keys.front().frame, code1, {curve(b, t, 0)});
    else { const uint32_t x = curve(b, t, 0), y = curve(b, t, 1), z = curve(b, t, 2); b.curves(o, t.keys.front().frame, code3, {x, y, z}); }
    at(t.keys.back());  // hold the last value after the curve
}
}  // namespace

bool compile(const Demo& d, const Resolver& resolve, Compiled& out, std::string& why, uint32_t first_code) {
    out = {};
    S::Builder b;
    std::vector<Resource> virtual_table;
    auto virtual_id = [&](const Resource& r, uint32_t& id) {
        auto it = std::find(virtual_table.begin(), virtual_table.end(), r);
        if (it == virtual_table.end()) {
            if (virtual_table.size() >= 48) return fail(why, "demo uses more than 48 archive files");
            const int32_t real = resolve ? resolve(r) : -1;
            if (real < 0) return fail(why, "the game has no " + r.file + " in archive " + r.archive);
            virtual_table.push_back(r);
            out.virtual_ids.push_back({r, uint32_t(real)});
            it = virtual_table.end() - 1;
        }
        id = 0x10000u | uint32_t(it - virtual_table.begin());
        return true;
    };
    if (!d.eye.keys.empty() || !d.fovy.keys.empty() || !d.roll.keys.empty()) {
        const size_t cam = b.object(S::kCamera, "camera");
        emit(b, cam, d.eye, S::camera::Eye, 0, false);
        emit(b, cam, d.target, S::camera::Target, 0, false);
        emit(b, cam, d.fovy, 0, S::camera::Fovy, true);
        emit(b, cam, d.roll, 0, S::camera::Roll, true);
        b.end(cam, d.frames);
    }
    std::map<std::string, std::pair<size_t, const Actor*>> objects;  // demo id -> builder object
    unsigned models = 0, characters = 0;
    for (const auto& a : d.actors) {
        std::string name = a.actor;
        if (a.kind == Actor::Kind::Model) name = "d_act" + std::to_string(models++);
        else if (a.kind == Actor::Kind::Character) {
            char stand_in[8];
            std::snprintf(stand_in, sizeof stand_in, "MA%02u", characters++);
            name = stand_in;
            out.characters.push_back({name, a.character, a.named_clips});
        } else {
            out.participants.push_back(a.actor);
            if (a.kind == Actor::Kind::Game) {
                Cast cast{a.actor, a.spawn, a.params, !a.position.keys.empty(), {}};
                if (cast.placed) cast.position = a.position.keys.front().value;
                out.cast.push_back(cast);
            }
        }
        const size_t o = b.object(S::kActor, name);
        objects[a.id] = {o, &a};
        if (a.kind == Actor::Kind::Model) {
            uint32_t shape;
            if (!virtual_id(a.model, shape)) return false;
            b.value(o, 0, S::actor::Shape, shape);
        }
        emit(b, o, a.position, S::actor::TXYZ, 0, false);
        emit(b, o, a.rotation, S::actor::RXYZ, 0, false);
        emit(b, o, a.scale, S::actor::SXYZ, 0, false);
        for (const auto& c : a.clips) {
            uint32_t id;
            if (!virtual_id(c.resource, id)) return false;
            b.value(o, c.frame, S::actor::Animation, id);
            b.immediate_word(o, c.frame, S::actor::AnimationMode, 1);
            b.time(o, c.frame, S::actor::AnimationFrame, 30.f * c.speed);
        }
        for (const auto& m : a.modes) {
            b.value(o, m.frame, S::actor::Animation, m.mode);
            if (!m.clip.empty()) {
                // one of Link's demo animations: data id 0 = {u16 animation id, u8 hands}
                const int32_t id = resolve ? resolve({"@Link", m.clip}) : -1;
                if (id < 0) return fail(why, "Link has no demo animation " + m.clip);
                b.immediate_word(o, m.frame, S::actor::AnimationMode, 1);
                b.time(o, m.frame, S::actor::AnimationFrame, 30.f);
                b.data(o, m.frame, 0, S::Bytes{0x32, uint8_t(id >> 8), uint8_t(id), 0x39, 0x02, 0x00, 0x00});
            } else if (!m.data.empty()) {
                b.immediate_word(o, m.frame, S::actor::AnimationMode, 1);
                b.time(o, m.frame, S::actor::AnimationFrame, 30.f);
                b.data(o, m.frame, 1, m.data);
            }
        }
        for (const auto& e : a.data) b.data(o, e.frame, e.id, e.bytes);
        b.end(o, d.frames);
    }
    unsigned n = 0;
    for (const auto& s : d.sounds) {
        const size_t o = b.object(S::kSound, "se" + std::to_string(n++));
        if (!s.actor.empty()) {
            const Actor* a = objects[s.actor].second;
            Track t = a->position;
            // the sound follows the actor's own position track from the sound's frame on
            if (!t.keys.empty()) emit(b, o, t, S::sound::XYZ, 0, false);
        } else if (s.position) b.immediate(o, s.frame, S::sound::XYZ, {s.position->x, s.position->y, s.position->z});
        b.value(o, s.frame, S::sound::Sound, s.id);
        b.immediate(o, s.frame, S::sound::Volume, {s.volume});
        b.immediate(o, s.frame, S::sound::Pan, {s.pan});
        b.end(o, d.frames);
    }
    n = 0;
    for (const auto& p : d.particles) {
        const size_t o = b.object(S::kParticle, "efx" + std::to_string(n++));
        if (!p.actor.empty()) {
            const Actor* a = objects[p.actor].second;
            const std::string target = a->kind == Actor::Kind::Model ? "d_act" + std::to_string(std::count_if(d.actors.begin(), d.actors.begin() + (a - d.actors.data()), [](const Actor& x) { return x.kind == Actor::Kind::Model; })) : a->actor;
            b.name(o, p.frame, S::particle::Parent, target);
            if (!p.joint.empty()) b.name(o, p.frame, S::particle::ParentNode, p.joint);
            b.immediate_word(o, p.frame, S::particle::ParentEnable, 1);
        } else b.immediate(o, p.frame, S::particle::TXYZ, {p.position->x, p.position->y, p.position->z});
        b.value(o, p.frame, S::particle::Particle, p.id);
        b.end(o, d.frames);
    }
    if (!d.messages.empty()) {
        const size_t o = b.object(S::kMessage, "message");
        for (const auto& m : d.messages) { b.value(o, m.frame, S::message::Message, demo_message_code(out.messages.size(), first_code)); b.suspend(m.frame + 1); out.messages.push_back(m); }
        b.end(o, d.frames);
    }
    out.stb = b.finish();
    if (out.stb.size() > kMaxStbBytes) return fail(why, "demo data exceeds 48 KiB");
    return true;
}
}  // namespace demo_scene
