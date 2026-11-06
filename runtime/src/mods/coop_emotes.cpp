// SPDX-License-Identifier: MPL-2.0
// Online co-op: emotes, the port's part (coop_emotes.h). Link's skeleton and the emotes' clips are read once from the
// player's own 3D pack (link_poses::game_files) on a thread of their own, the first time a session plays one; each clip
// becomes its joints per frame in Link's model space. Until then (or if the files can't be read) emotes are not shown
// on puppets; the bubble over one's own Link shows all the same.
#include "coop_emotes.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "coop_names.h"
#include "coop_net.h"
#include "imgui.h"
#include "link_poses.h"
#include "mods.h"
#include "runtime.h"
#include "story_resident_walk_clip.h"
#include "../aspect.h"
#include "../interp.h"
#include "../overlay/extensions.h"

namespace mods::coop::emotes {
namespace {
namespace wc = story_resident_walk_clip;

// the clips as joints: [emote][frame][bone], Link's model space
struct Clips {
    std::vector<std::vector<std::vector<Mx>>> frames;  // by index in kEmotes (empty: that clip is missing)
    size_t bones = 0;
};
std::atomic<int> g_load{0};  // 0 not asked, 1 loading, 2 ready, 3 failed
Clips g_clips;               // (written by the loader before g_load is 2)
std::vector<std::vector<Mx>> g_gaits[kGaits];  // (C13) the cheap puppets' clips, frame by frame

void load() {
    wc::Bytes link, anim;
    std::string why;
    std::vector<wc::Bone> skel;
    Clips c;
    if (link_poses::game_files(link, anim, why) && wc::skeleton(link, "cl", skel, why)) {
        c.bones = skel.size();
        for (const Emote& e : kEmotes) {
            std::vector<std::vector<Mx>> out;
            wc::Clip clip;
            std::string no;
            if (wc::clip(anim, e.clip, clip, no) && clip.frames > 1) {
                std::vector<int> track(skel.size(), -1);
                for (size_t i = 0; i < skel.size(); i++)
                    for (size_t j = 0; j < clip.bones.size(); j++)
                        if (clip.bones[j].name == skel[i].name) track[i] = int(j);
                for (unsigned f = 0; f < clip.frames; f++) {
                    const wc::World w = wc::fk(skel, wc::sample(skel, clip, track, f));
                    std::vector<Mx> joints(skel.size());
                    for (size_t b = 0; b < skel.size(); b++)
                        for (int r = 0; r < 3; r++) {
                            for (int k = 0; k < 3; k++) joints[b].m[4 * r + k] = float(w.rot[b].m[r][k]);
                            joints[b].m[4 * r + 3] = float(w.pos[b][r]);
                        }
                    out.push_back(std::move(joints));
                }
            } else {
                mods::trace("coop: emote %s: its clip %s is not read (%s)", e.name, e.clip, no.c_str());
            }
            c.frames.push_back(std::move(out));
        }
        for (int gi = 0; gi < kGaits; gi++) {
            wc::Clip clip;
            std::string no;
            if (!wc::clip(anim, kGaitClips[gi], clip, no) || clip.frames <= 1) { mods::trace("coop: clip %s not read (%s)", kGaitClips[gi], no.c_str()); continue; }
            std::vector<int> track(skel.size(), -1);
            for (size_t i = 0; i < skel.size(); i++)
                for (size_t j = 0; j < clip.bones.size(); j++)
                    if (clip.bones[j].name == skel[i].name) track[i] = int(j);
            for (unsigned f = 0; f < clip.frames; f++) {
                const wc::World w = wc::fk(skel, wc::sample(skel, clip, track, f));
                std::vector<Mx> joints(skel.size());
                for (size_t b = 0; b < skel.size(); b++)
                    for (int r = 0; r < 3; r++) {
                        for (int k = 0; k < 3; k++) joints[b].m[4 * r + k] = float(w.rot[b].m[r][k]);
                        joints[b].m[4 * r + 3] = float(w.pos[b][r]);
                    }
                g_gaits[gi].push_back(std::move(joints));
            }
        }
        g_clips = std::move(c);
        mods::trace("coop: emotes: Link's skeleton (%zu bones) and %d clips read", g_clips.bones, kCount);
        g_load = 2;
    } else {
        mods::trace("coop: emotes: Link's files are not read (%s): emotes show on no puppet", why.c_str());
        g_load = 3;
    }
}
bool ready() {
    int want = 0;
    if (g_load.compare_exchange_strong(want, 1)) std::thread(load).detach();
    return g_load == 2;
}
int index_of(uint8_t id) {
    for (int i = 0; i < kCount; i++)
        if (kEmotes[i].id == id) return i;
    return -1;
}

std::mutex g_mu;
struct Playing {
    int emote = -1;  // index in kEmotes
    uint32_t since = 0;
    bool placed = false;  // `at` known (its first pose with it)
    float at[3] = {};
    bool traced = false;
} g_playing[kPuppets];
std::atomic<int> g_ask{0};      // this player's emote asked for (an id; 0 none)
std::atomic<int> g_bubble{-1};  // the bubble's emote (index), and where it shows (layout units), while it plays
std::atomic<uint32_t> g_bubble_until{0};
std::atomic<float> g_bubble_x{0}, g_bubble_y{0};
std::atomic<bool> g_bubble_on{false};

float seconds_of(int emote) {
    const unsigned frames = g_load == 2 && !g_clips.frames[size_t(emote)].empty() ? unsigned(g_clips.frames[size_t(emote)].size()) : 31;
    return length(kEmotes[emote], frames);
}

// the drawn camera as a projection to layout units (touch_talk.cpp's, guest_live.h's View)
struct View {
    float eye[3], f[3], r[3], u[3], k = 0;
    bool ok = false;
};
View camera() {
    View v;
    const uint32_t cam = interp::last_camera();
    if (cam < 0x10000000u || cam >= 0x50000000u) return v;
    float center[3], up[3];
    for (int e = 0; e < 3; e++) v.eye[e] = float(ldf32(cam + 0xDC + 4 * e)), center[e] = float(ldf32(cam + 0xE8 + 4 * e)), up[e] = float(ldf32(cam + 0xF4 + 4 * e));
    const float fovy = float(ldf32(cam + 0xD4));
    const float k = aspect::layout_scale(fovy);
    float n = 0;
    for (int e = 0; e < 3; e++) v.f[e] = center[e] - v.eye[e], n += v.f[e] * v.f[e];
    if (!(n > 0.25f) || !(k > 0) || !std::isfinite(k)) return v;
    for (float& x : v.f) x /= std::sqrt(n);
    v.r[0] = v.f[1] * up[2] - v.f[2] * up[1], v.r[1] = v.f[2] * up[0] - v.f[0] * up[2], v.r[2] = v.f[0] * up[1] - v.f[1] * up[0];
    n = v.r[0] * v.r[0] + v.r[1] * v.r[1] + v.r[2] * v.r[2];
    if (!(n > 1e-6f)) return v;
    for (float& x : v.r) x /= std::sqrt(n);
    v.u[0] = v.r[1] * v.f[2] - v.r[2] * v.f[1], v.u[1] = v.r[2] * v.f[0] - v.r[0] * v.f[2], v.u[2] = v.r[0] * v.f[1] - v.r[1] * v.f[0];
    v.k = k, v.ok = true;
    return v;
}
bool project(const View& v, const float p[3], float& x, float& y) {
    if (!v.ok) return false;
    float d[3], z = 0, a = 0, b = 0;
    for (int e = 0; e < 3; e++) d[e] = p[e] - v.eye[e];
    for (int e = 0; e < 3; e++) z += d[e] * v.f[e], a += d[e] * v.r[e], b += d[e] * v.u[e];
    if (!(z > 1)) return false;
    x = a / z * v.k, y = b / z * v.k;
    return std::isfinite(x) && std::isfinite(y);
}

}  // namespace
bool to_layout(const float p[3], float& x, float& y) { return project(camera(), p, x, y); }
namespace {

// test aid: WWHD_COOP_TEST_EMOTE=<s>:<id>[,...]: this player's emotes at those seconds after the session's first step
struct Planned { uint32_t at_ms; uint8_t id; };
std::vector<Planned> planned() {
    std::vector<Planned> out;
    const char* e = getenv("WWHD_COOP_TEST_EMOTE");
    for (const char* p = e; p && *p;) {
        char* end = nullptr;
        const double s = strtod(p, &end);
        if (end == p || *end != ':') break;
        const long id = strtol(end + 1, &end, 10);
        out.push_back({uint32_t(s * 1000), uint8_t(id)});
        p = *end == ',' ? end + 1 : end;
        if (!*end) break;
    }
    return out;
}

}  // namespace

void play(int slot, uint8_t id) {
    const int e = index_of(id);
    if (slot < 0 || slot >= kPuppets || e < 0) return;
    ready();
    std::lock_guard<std::mutex> l(g_mu);
    Playing& p = g_playing[slot];
    const uint32_t now = net::now_ms();
    if (p.emote >= 0 && now - p.since < kMinGapMs) return;  // (one a second at most)
    p = Playing{e, now};
    mods::trace("coop: puppet %d: emote %s", slot, kEmotes[e].name);
}

void stop(int slot) {
    if (slot < 0 || slot >= kPuppets) return;
    std::lock_guard<std::mutex> l(g_mu);
    g_playing[slot] = Playing{};
}

void pose(int slot, Pose& p, const float pos[3], int body_k) {
    if (slot < 0 || slot >= kPuppets) return;
    std::lock_guard<std::mutex> l(g_mu);
    Playing& u = g_playing[slot];
    if (u.emote < 0) return;
    const float t = (net::now_ms() - u.since) / 1000.f, len = seconds_of(u.emote);
    if (!u.placed) memcpy(u.at, pos, sizeof u.at), u.placed = true;
    const float dx = pos[0] - u.at[0], dz = pos[2] - u.at[2];
    // over, moved away, or a character in Link's place (Tetra, Medli, Zelda: their own models)
    if (t >= len || dx * dx + dz * dz > kMoveAway * kMoveAway || (p.flags >> 4 & 3)) {
        mods::trace("coop: puppet %d: emote %s over (%s)", slot, kEmotes[u.emote].name, t >= len ? "played" : (p.flags >> 4 & 3) ? "a character" : "moved");
        u = Playing{};
        return;
    }
    if (g_load != 2 || g_clips.frames[size_t(u.emote)].empty()) return;
    const auto& frames = g_clips.frames[size_t(u.emote)];
    const float f = frame_at(kEmotes[u.emote], unsigned(frames.size()), t);
    const size_t f0 = std::min(size_t(f), frames.size() - 1), f1 = std::min(f0 + 1, frames.size() - 1);
    const float ft = f - float(f0);
    std::vector<Mx> joints(frames[f0].size());
    for (size_t b = 0; b < joints.size(); b++) joints[b] = blend(frames[f0][b], frames[f1][b], ft);
    const float w = weight(t, len);
    const bool ok = apply(p, body_k, joints, w);
    if (!u.traced && w > 0) {  // (its first pose: the body's root and last joint as the player sent them and as the clip has them)
        u.traced = true;
        const Model* body = nullptr;
        for (const Model& m : p.models) if (m.k == body_k) body = &m;
        mods::trace("coop: puppet %d: emote %s on its pose: %s (body %d: %zu joints, the clip %zu)", slot, kEmotes[u.emote].name,
                    ok ? "put on" : "not put on", body_k, body ? body->j.size() : 0, joints.size());
    }
}

bool cheap_pose(Gait g, float t, int body_k, Pose& p) {
    if (!ready() || g >= kGaits || body_k < 0 || g_gaits[g].size() < 2) return false;
    const auto& frames = g_gaits[g];
    const float len = float(frames.size() - 1) / kFps;
    const float f = std::fmod(std::max(t, 0.f), len) * kFps;
    const size_t f0 = std::min(size_t(f), frames.size() - 1), f1 = std::min(f0 + 1, frames.size() - 1);
    Model m;
    m.k = (uint8_t)body_k;
    m.j.resize(frames[f0].size());
    for (size_t b = 0; b < m.j.size(); b++)
        if (!to_joint(blend(frames[f0][b], frames[f1][b], f - float(f0)), m.j[b])) return false;
    p.models.clear();
    p.models.push_back(std::move(m));
    return true;
}

void mine(uint8_t id) {
    if (index_of(id) >= 0) g_ask = id;
}

void step(uint32_t link) {
    static const std::vector<Planned> plan = planned();
    static uint32_t first = 0;
    const uint32_t now = net::now_ms();
    if (!first) first = now ? now : 1;
    static size_t next = 0;
    if (next < plan.size() && now - first >= plan[next].at_ms) g_ask = plan[next++].id;
    static uint32_t last_sent = 0;
    if (const int id = g_ask.exchange(0); id && net::role() != net::Role::kOff) {
        if (last_sent && now - last_sent < kMinGapMs) {
            mods::trace("coop: emote %d not sent: one a second at most", id);
        } else {
            last_sent = now;
            ready();
            const int e = index_of(uint8_t(id));
            net::send(-1, net::kEmote, std::string(1, char(id)));
            g_bubble = e, g_bubble_until = now + uint32_t(seconds_of(e) * 1000) + 300;
            mods::trace("coop: emote %s sent", kEmotes[e].name);
        }
    }
    // the bubble over his head (layout units), while it shows
    bool on = false;
    if (g_bubble >= 0 && now < g_bubble_until && link >= 0x10000000u && link < 0x50000000u) {
        const uint32_t at = link + names::ac::kPos;
        const float head[3] = {float(ldf32(at)), float(ldf32(at + 4)) + 140, float(ldf32(at + 8))};
        float x, y;
        if (project(camera(), head, x, y)) g_bubble_x = x, g_bubble_y = y, on = true;
    } else if (g_bubble >= 0 && now >= g_bubble_until) {
        g_bubble = -1;
    }
    g_bubble_on = on;
}

void register_overlay() {
    overlay::ext::add_frame_layer({[](std::string&) { return g_bubble_on.load() && g_bubble.load() >= 0; },
                                   [](float w, float h) {
                                       const int e = g_bubble.load();
                                       if (e < 0 || e >= kCount) return false;
                                       // layout units: the picture 720 high around its centre, +y up
                                       const float a = aspect::game() > 0 ? aspect::game() : 16.f / 9.f;
                                       const float s = std::min(h / 720.f, w / (720.f * a));
                                       const float cx = w / 2 + g_bubble_x.load() * s, cy = h / 2 - g_bubble_y.load() * s;
                                       ImDrawList* d = ImGui::GetForegroundDrawList();
                                       const char* text = kEmotes[e].name;
                                       const ImVec2 ts = ImGui::CalcTextSize(text);
                                       const float px = 8, py = 4;
                                       const ImVec2 lo(cx - ts.x / 2 - px, cy - ts.y - 2 * py - 10), hi(cx + ts.x / 2 + px, cy - 10);
                                       d->AddRectFilled(lo, hi, IM_COL32(250, 246, 232, 235), 8);
                                       d->AddRect(lo, hi, IM_COL32(60, 48, 32, 255), 8.0f, 2.0f);
                                       d->AddTriangleFilled(ImVec2(cx - 6, hi.y - 1), ImVec2(cx + 6, hi.y - 1), ImVec2(cx, cy), IM_COL32(250, 246, 232, 235));
                                       d->AddText(ImVec2(lo.x + px, lo.y + py), IM_COL32(40, 32, 20, 255), text);
                                       return false;
                                   }});
}

}  // namespace mods::coop::emotes
