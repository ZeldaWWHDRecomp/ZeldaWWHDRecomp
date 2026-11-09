// SPDX-License-Identifier: MPL-2.0
// Online co-op: emotes, the port's part (coop_emotes.h). Link's skeleton and the emotes' clips are read once from the
// player's own 3D pack (link_poses::game_files) on a thread of their own, as a session starts; each clip becomes its
// joints per frame in Link's model space. Until they are read (or if the files can't be read) no emote starts here,
// and none shows on a puppet.
#include "coop_emotes.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "coop_names.h"
#include "coop_net.h"
#include "guest_addr.h"
#include "hud_visibility.h"
#include "imgui.h"
#include "link_poses.h"
#include "mods.h"
#include "runtime.h"
#include "story_resident_walk_clip.h"
#include "../aspect.h"
#include "../interp.h"
#include "../overlay/extensions.h"
#include "../overlay/overlay.h"
#include "../savestate.h"
#include "../screenshot.h"

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

// this player's own emote (game thread): which (index in kEmotes, -1 none), the logic steps since it began, and the
// step it was ended at (-1: not ended)
struct Mine {
    int emote = -1;
    uint32_t steps = 0;
    int ended = -1;
} g_mine;
std::atomic<int> g_ask{0};            // this player's emote asked for (an id; 0 none)
std::atomic<bool> g_ask_menu{false};  // asked from a menu: tried once the menu is closed
std::atomic<int> g_bubble{-1};  // the bubble's emote (index), and where it shows (layout units), while it plays
std::atomic<float> g_bubble_x{0}, g_bubble_y{0};
std::atomic<bool> g_bubble_on{false};

float seconds_of(int emote) {
    const unsigned frames = g_load == 2 && !g_clips.frames[size_t(emote)].empty() ? unsigned(g_clips.frames[size_t(emote)].size()) : 31;
    return length(kEmotes[emote], frames);
}
// emote `e`'s clip at `t` seconds (its joints, Link's model space); false while the clips aren't read
bool clip_at(int e, float t, std::vector<Mx>& joints) {
    if (g_load != 2 || e < 0 || e >= kCount || g_clips.frames[size_t(e)].empty()) return false;
    const auto& frames = g_clips.frames[size_t(e)];
    const float f = frame_at(kEmotes[e], unsigned(frames.size()), t);
    const size_t f0 = std::min(size_t(f), frames.size() - 1), f1 = std::min(f0 + 1, frames.size() - 1);
    const float ft = f - float(f0);
    joints.resize(frames[f0].size());
    for (size_t b = 0; b < joints.size(); b++) joints[b] = blend(frames[f0][b], frames[f1][b], ft);
    return true;
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

// test aid: WWHD_COOP_TEST_EMOTE=<s>:<id>[m][,...]: this player's emotes at those seconds after the session's first step
// (as a key asks for one; with m as the Online page in a menu does: tried once the menu is closed)
struct Planned { uint32_t at_ms; uint8_t id; bool menu; };
std::vector<Planned> planned() {
    std::vector<Planned> out;
    const char* e = getenv("WWHD_COOP_TEST_EMOTE");
    for (const char* p = e; p && *p;) {
        char* end = nullptr;
        const double s = strtod(p, &end);
        if (end == p || *end != ':') break;
        const long id = strtol(end + 1, &end, 10);
        const bool menu = *end == 'm';
        if (menu) end++;
        out.push_back({uint32_t(s * 1000), uint8_t(id), menu});
        p = *end == ',' ? end + 1 : end;
        if (!*end) break;
    }
    return out;
}
// test aid: WWHD_COOP_TEST_EMOTE_SHOTS=<s>[,<s>...]: a screenshot (WWHD_SCREENSHOT_DIR) as an emote reaches each of those
// seconds: this game's own Link's, and a puppet's as its pose shows it (the same moment on both screens)
void shots(float was, float now, const char* whose) {
    static const std::vector<float> at = [] {
        std::vector<float> v;
        for (const char* p = getenv("WWHD_COOP_TEST_EMOTE_SHOTS"); p && *p;) {
            char* end = nullptr;
            v.push_back(strtof(p, &end));
            if (end == p || *end != ',') break;
            p = end + 1;
        }
        return v;
    }();
    for (float s : at)
        if (was < s && now >= s) {
            screenshot::request();
            mods::trace("coop: test: screenshot of %s at %.2f s of its emote", whose, now);
        }
}

}  // namespace

void play(int from, uint8_t id) {
    if (const Emote* e = find(id)) mods::trace("coop: player %d: emote %s (its pose carries it)", from, e->name);
}

void pose(Pose& p, int body_k) {
    if (!p.emote) return;
    ready();
    std::vector<Mx> joints;
    // (a character in Link's place, Tetra, Medli, Zelda: their own models, no emote; the emoting game refuses it too)
    if (!p.emote_w || (p.flags >> 4 & 3) || !clip_at(index_of(p.emote), p.emote_s, joints)) return;
    const bool on = apply(p, body_k, joints, p.emote_w / 255.f);
    static float was = 1e9f;  // (test aids: its time as the last pose showed it; what it was put on)
    shots(was, p.emote_s, "a puppet");
    if (p.emote_s < was)  // (a new one: its first pose)
        mods::trace("coop: a puppet's emote %s at %.2f s, weight %.2f: %s", find(p.emote)->name, p.emote_s, p.emote_w / 255.f, on ? "put on" : "not put on");
    was = p.emote_s;
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

void mine(uint8_t id, bool from_menu) {
    if (index_of(id) < 0) return;
    g_ask_menu = from_menu;
    g_ask = id;
}

namespace {
// Link as this logic step leaves him (coop_emotes.h Link)
Link link_now(uint32_t link) {
    using namespace names;
    Link s;
    s.proc = (int)ld32(link + py::mCurProc);
    s.mode = ld32(link + py::mModeFlg);
    s.damage = (int16_t)ld16(link + py::mDamageWaitTimer);
    s.equip = (uint16_t)ld16(link + py::mEquipItem);
    s.stick = float(ldf32(link + py::mStickDistance));
    s.trigger = (uint8_t)ld8(link + py::mItemTrigger);
    const GameUi ui = mods::game_ui_now();
    s.event = ui.event, s.message = ui.message, s.menu = ui.menu;
    s.overlay = overlay::is_open();
    s.first_person = mods::first_person();
    s.next_stage = ld8(GD(0x1046F0B0) + inf::kNextStageEnabled) != 0;
    s.character = own_char();
    s.clips = g_load == 2;
    return s;
}
}  // namespace

void step(uint32_t link) {
    static const std::vector<Planned> plan = planned();
    static uint32_t first = 0;
    const uint32_t now = net::now_ms();
    if (!first) first = now ? now : 1;
    static size_t next = 0;
    if (next < plan.size() && now - first >= plan[next].at_ms) g_ask_menu = plan[next].menu, g_ask = plan[next].id, next++;
    const bool on = net::role() != net::Role::kOff && link >= 0x10000000u && link < 0x50000000u;
    if (!on) {  // (no session, or no Link: nothing plays, nothing is asked)
        if (g_mine.emote >= 0) mods::trace("coop: emote %s over (the session or Link is gone)", kEmotes[g_mine.emote].name);
        g_mine = Mine{}, g_bubble = -1, g_bubble_on = false;
        g_ask = 0;
        return;
    }
    ready();  // (the clips read as a session starts, so the first emote can play at once)
    const Link s = link_now(link);
    // the one playing: a step on, ended by the stick, a button or anything that takes him out of standing
    if (g_mine.emote >= 0) {
        Mine& m = g_mine;
        m.steps++;
        const float t = m.steps / kFps, len = seconds_of(m.emote);
        shots((m.steps - 1) / kFps, t, "this game's Link");
        if (m.ended < 0)
            if (const char* why = ends(s)) {
                m.ended = int(m.steps);
                mods::trace("coop: emote %s ended at %.2f s of %.2f (%s)", kEmotes[m.emote].name, t, len, why);
            }
        if (over(t, len, m.ended < 0 ? -1.f : m.ended / kFps)) {
            if (m.ended < 0) mods::trace("coop: emote %s played (%.2f s)", kEmotes[m.emote].name, len);
            m = Mine{}, g_bubble = -1;
        }
    }
    // one asked for: one from a menu waits for it to close (at most a minute)
    static uint32_t asked_at = 0;
    if (const int id = g_ask.load(); id && !(g_ask_menu && (s.menu || s.overlay) && now - (asked_at ? asked_at : (asked_at = now)) < 60000)) {
        g_ask = 0, asked_at = 0;
        const int e = index_of(uint8_t(id));
        static uint32_t last_sent = 0;
        if (last_sent && now - last_sent < kMinGapMs) {
            mods::trace("coop: emote %s not played: one a second at most", kEmotes[e].name);
        } else if (const char* why = refused(s)) {
            ss::notice(std::string("Can't emote now: ") + why);
            mods::trace("coop: emote %s refused: %s (procedure %02X, mode %08X, equip %04X)", kEmotes[e].name, why, s.proc, s.mode, s.equip);
        } else {
            last_sent = now;
            g_mine = Mine{e, 0, -1};
            net::send(-1, net::kEmote, std::string(1, char(id)));
            g_bubble = e;
            mods::trace("coop: emote %s started and sent (%.2f s)", kEmotes[e].name, seconds_of(e));
        }
    }
    // the bubble over his head (layout units), while it plays
    bool shown = false;
    if (g_bubble >= 0 && g_mine.emote >= 0) {
        const uint32_t at = link + names::ac::kPos;
        const float head[3] = {float(ldf32(at)), float(ldf32(at + 4)) + 140, float(ldf32(at + 8))};
        float x, y;
        if (project(camera(), head, x, y)) g_bubble_x = x, g_bubble_y = y, shown = true;
    }
    g_bubble_on = shown;
}

bool own_frame(std::vector<Mx>& joints, float& w) {
    const Mine& m = g_mine;
    if (m.emote < 0) return false;
    const float t = m.steps / kFps;
    w = weight(t, seconds_of(m.emote), m.ended < 0 ? -1.f : m.ended / kFps);
    // (the same joints for each of his models in a draw: worked out once a step)
    static int at_emote = -1;
    static uint32_t at_steps = 0;
    static std::vector<Mx> at_joints;
    if (at_emote != m.emote || at_steps != m.steps || at_joints.empty()) {
        at_joints.clear();
        if (!clip_at(m.emote, t, at_joints)) return false;
        at_emote = m.emote, at_steps = m.steps;
    }
    joints = at_joints;
    return w > 0;
}

void own_state(uint8_t& id, float& seconds, uint8_t& w) {
    const Mine& m = g_mine;
    id = 0, seconds = 0, w = 0;
    if (m.emote < 0) return;
    const float t = m.steps / kFps;
    id = kEmotes[m.emote].id, seconds = t;
    w = (uint8_t)std::lround(std::clamp(weight(t, seconds_of(m.emote), m.ended < 0 ? -1.f : m.ended / kFps), 0.f, 1.f) * 255);
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
