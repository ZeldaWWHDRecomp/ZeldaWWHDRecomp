// Drawing converted processes at the interpolated world's phase (t60_draw.h). Decomp build only.
#include "t60/t60_draw.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>

#include "guest_addr.h"
#include "interp_camera.h"
#include "interp_joints.h"
#include "mods/mods.h"
#include "runtime.h"
#include "t60/t60.h"
#include "t60/t60_draw_hist.h"
#include "t60/t60_draw_glide.h"
#include "t60/t60_world.h"

// the decompiled rate gate's table (f_pc/f_pc_executor.cpp, game build only; the runtime-only decomp build has no game
// units: nothing is converted there)
extern "C" __attribute__((weak)) int wwhd_t60_proc_converted(int) { return 0; }

namespace interp {
void blend_world_matrix(const float* a, const float* b, float* out, float t);
double t60_world_time();  // interp.cpp (decomp build)
}

namespace t60::draw {
namespace {
constexpr uint32_t kProfName = 0xE, kProcId = 0x4;
constexpr float kCut = 400.0f;  // a joint that moved further between two kept steps: drawn at the later one (teleport)

const bool g_switch = [] {
    const char* e = getenv("WWHD_T60_DRAW_ALIGN");
    return !e || atoi(e) != 0;
}();
// Whole-form actors: their picture steps stay out of its draw history (WWHD_T60_WHOLE_BLEND=0: stamped as steps)
const bool g_whole_blend = [] {
    const char* e = getenv("WWHD_T60_WHOLE_BLEND");
    return !e || atoi(e) != 0;
}();
const bool g_whole_keep = [] {
    const char* e = getenv("WWHD_T60_WHOLE_KEEP");
    return !e || atoi(e) != 0;
}();
FILE* g_log = getenv("WWHD_T60_DRAWLOG") ? fopen(getenv("WWHD_T60_DRAWLOG"), "w") : nullptr;
// debug: WWHD_T60_DRAWLOG_JOINT=j adds joint j's drawn translation (and its offset from the root) to the model lines
const int g_log_joint = getenv("WWHD_T60_DRAWLOG_JOINT") ? atoi(getenv("WWHD_T60_DRAWLOG_JOINT")) : -1;

uint64_t g_pass = 0;
uint32_t g_drawing = 0;

struct Exec { double time; uint64_t pass; uint32_t id; };
std::unordered_map<uint32_t, Exec> g_exec;  // converted processes: the time of their newest step
// Whole-form actors whose execute on this pass's step was a picture step (decomp d/t60_whole.h)
struct Picture { uint64_t pass; uint32_t id; };
std::unordered_map<uint32_t, Picture> g_picture;

// Link carried by a whole-form carrier (decomp d/actor/d_a_player_carry_t60.h through wwhd_t60_link_carry): the carries a whole-form carrier gave
// Link (riding: MoveBgCrrPos; pushing / pulling: setPushPullKeepData), each stamped with the step time it happened at. The
// carrier moves once a 30 Hz frame and is drawn between its frame ends; Link, drawn at his steps, would show the carry as
// a 30 Hz staircase. His models are drawn with the frame's carry spread over the frame instead (carry_at). Draw only (no
// logic). WWHD_T60_LINK_CARRY=0: off (drawn as before).
const bool g_carry_on = [] {
    const char* e = getenv("WWHD_T60_LINK_CARRY");
    return !e || atoi(e) != 0;
}();
struct CarryEv { double time; float d[3]; int da; };
struct CarryHist { uint32_t id = 0; uint64_t pass = 0; std::deque<CarryEv> ev; };
std::unordered_map<uint32_t, CarryHist> g_carry;

// (2026-11-24; decomp d/t60_basehold.h through wwhd_t60_draw_basehold /
// _basefollow): an actor whose skeleton calc runs on a held base matrix (the process-order read rule base-frame form: the Moblin,
// d_a_mo2_exec.cpp mo2_t60_base_frame: every step's calc on frame f - 1's end) leaves its skeleton at the held placement,
// and the base matrix the step sets after its move is not in the drawn joints: the body was drawn at 30 Hz, one frame
// behind (stair sweep majE15: 3.0 units every other pass). The step's models of that actor are kept in their draw history
// moved rigidly from the held base to the step's base (C = B_step * inv(B_held), B_step = the published model's base
// matrix at the draw); a follower placed from the holder's hand words (the Moblin's lantern) takes the holder's C of the
// same pass. Draw only (host copies; guest words untouched). WWHD_T60_BASEHOLD_DRAW_NEGCTRL=1: as before.
const bool g_basehold_on = [] {
    const char* e = getenv("WWHD_T60_BASEHOLD_DRAW_NEGCTRL");
    return !(e && e[0] == '1');
}();
struct BaseHold { uint64_t pass = ~0ull; uint32_t model = 0; float held[12]; };
std::unordered_map<uint32_t, BaseHold> g_basehold;  // by actor
struct BaseFollow { uint64_t pass = ~0ull; uint32_t holder_id = 0; };
std::unordered_map<uint32_t, BaseFollow> g_basefollow;  // by follower actor
// the 3x4 affine inverse (rotation-scale part by its adjugate)
bool inv34(const float* m, float* o) {
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const float det = a * A + b * B + c * C;
    if (!(std::fabs(det) > 1e-12f)) return false;
    const float k = 1.0f / det;
    const float r[9] = {A * k, -(b * i - c * h) * k, (b * f - c * e) * k, B * k, (a * i - c * g) * k, -(a * f - c * d) * k,
                        C * k, -(a * h - b * g) * k, (a * e - b * d) * k};
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) o[4 * row + col] = r[3 * row + col];
        o[4 * row + 3] = -(r[3 * row] * m[3] + r[3 * row + 1] * m[7] + r[3 * row + 2] * m[11]);
    }
    return true;
}
void mul34(const float* x, const float* y, float* o) {  // o = x * y (3x4 affine)
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 4; col++) {
            float v = x[4 * row] * y[col] + x[4 * row + 1] * y[4 + col] + x[4 * row + 2] * y[8 + col];
            if (col == 3) v += x[4 * row + 3];
            o[4 * row + col] = v;
        }
    }
}
// this pass's correction of actor `proc` (directly, or as a follower of its holder); false: none
bool basehold_c(uint32_t proc, float* C, uint64_t pass) {
    if (!g_basehold_on) return false;
    auto it = g_basehold.find(proc);
    if (it == g_basehold.end() || it->second.pass != pass) {
        auto f = g_basefollow.find(proc);
        if (f == g_basefollow.end() || f->second.pass != pass) return false;
        it = g_basehold.end();
        for (auto h = g_basehold.begin(); h != g_basehold.end(); ++h)
            if (h->second.pass == pass && ld32(h->first + kProcId) == f->second.holder_id) { it = h; break; }
        if (it == g_basehold.end()) return false;
    }
    using interp::joints::word_f;
    const uint32_t* bw = (const uint32_t*)ppc_ptr(it->second.model + 0xC8);  // (J3DModel mBaseTransformMtx)
    float B[12], Hi[12];
    for (int k = 0; k < 12; k++) B[k] = word_f(bw[k]);
    if (memcmp(B, it->second.held, sizeof B) == 0) return false;
    if (!inv34(it->second.held, Hi)) return false;
    mul34(B, Hi, C);
    return true;
}
void basehold_apply(const float* C, std::vector<uint32_t>& w) {
    using interp::joints::f_word;
    using interp::joints::word_f;
    for (size_t j = 0; j + 12 <= w.size(); j += 12) {
        float m[12], o[12];
        for (int k = 0; k < 12; k++) m[k] = word_f(w[j + k]);
        mul34(C, m, o);
        for (int k = 0; k < 12; k++) w[j + k] = f_word(o[k]);
    }
}
struct Carry { float d[3] = {0, 0, 0}; float da = 0; bool any = false; };
// The correction at step time t (in 30 Hz frames; frame F's steps lie in (F - 1, F], its tick30 step at F): what the
// interpolated world shows of frame F's carry at t, minus what Link already got by t, i.e. frac(t) * C_F - c(t) (C_F the
// frame's whole carry, c(t) its part on steps <= t). Zero at a frame end and while frame F is not complete yet in Link's
// history (newest < F: drawn as before).
Carry carry_at(const CarryHist& h, double t, double newest) {
    Carry c;
    constexpr double kEps = 1e-6;
    const double F = std::ceil(t - kEps);
    if (F - t < kEps || newest < F - kEps) return c;
    const double frac = t - (F - 1.0);
    float tot[3] = {0, 0, 0}, got[3] = {0, 0, 0};
    double tota = 0, gota = 0;
    for (const CarryEv& e : h.ev) {
        if (e.time <= F - 1.0 + kEps || e.time > F + kEps) continue;
        for (int k = 0; k < 3; k++) tot[k] += e.d[k];
        tota += e.da;
        if (e.time <= t + kEps) {
            for (int k = 0; k < 3; k++) got[k] += e.d[k];
            gota += e.da;
        }
    }
    for (int k = 0; k < 3; k++) c.d[k] = (float)(frac * tot[k] - got[k]);
    c.da = (float)(frac * tota - gota);
    c.any = c.d[0] != 0.0f || c.d[1] != 0.0f || c.d[2] != 0.0f || c.da != 0.0f;
    return c;
}
// the correction on drawn joint matrices (12 big-endian words each, 3x4 row major): turned by da (s16 angle units) about
// the Y axis through the root joint, then moved by d
void apply_carry(const Carry& c, uint32_t* out, uint32_t n) {
    using interp::joints::f_word;
    using interp::joints::word_f;
    const float px = word_f(out[3]), pz = word_f(out[11]);
    const float a = c.da * (float)(M_PI / 32768.0), cs = std::cos(a), sn = std::sin(a);
    for (uint32_t j = 0; j < n; j++) {
        uint32_t* m = out + 12 * j;
        if (c.da != 0.0f) {
            for (int col = 0; col < 3; col++) {  // R_y * M for the 3x3 part
                const float x = word_f(m[col]), z = word_f(m[8 + col]);
                m[col] = f_word(cs * x + sn * z);
                m[8 + col] = f_word(-sn * x + cs * z);
            }
            const float x = word_f(m[3]) - px, z = word_f(m[11]) - pz;
            m[3] = f_word(cs * x + sn * z + px);
            m[11] = f_word(-sn * x + cs * z + pz);
        }
        m[3] = f_word(word_f(m[3]) + c.d[0]);
        m[7] = f_word(word_f(m[7]) + c.d[1]);
        m[11] = f_word(word_f(m[11]) + c.d[2]);
    }
}

struct ModelHist {
    uint32_t owner = 0, owner_id = 0, mtx = 0, n = 0;
    uint64_t pass = 0;
    std::deque<std::pair<double, std::vector<uint32_t>>> steps;  // joint matrices (guest words) per step
};
std::unordered_map<uint32_t, ModelHist> g_models;  // by joint block

struct CamStep { interp::camera::CamState s; interp::camera::Shake k; };
struct CamHist {
    uint32_t id = 0;
    uint64_t pass = 0;
    std::deque<std::pair<double, CamStep>> steps;
    // Camera draw alignment (rot_newest): the newest step seen, and its manual-yaw weight (1 in an episode, 0.5 on the first
    // step after it, 0 then)
    double rot_stamp = -1.0;
    float rot_w = 0.0f;
    // held event-camera steps, part 2: the newest step's camera as executed (kept in `steps` or not; rot_newest reads it)
    double newest_time = -1.0;
    interp::camera::CamState newest{};
};
std::unordered_map<uint32_t, CamHist> g_cams;

// Per scene: did an unconverted process draw a model since the scene started (the play scene process changed)?
uint32_t g_scene_id = 0;
bool g_scene_unconverted_drawn = true;
// The world systems dScnPly_Draw runs on the tick30 step (grass, trees, flowers, magma, particles, the sea's and the
// rooms' background models): converted with the world systems (t60_world.h: the decompilation's mask). Until all are, every
// scene has an interpolated world.
bool world_systems_converted() { return t60::world::all_converted(); }
constexpr int16_t kPlayScene = 0x0007;  // fpcNm_PLAY_SCENE_e

void sweep() {
    if (g_pass % 256) return;
    for (auto it = g_models.begin(); it != g_models.end();) it = it->second.pass + 64 < g_pass ? g_models.erase(it) : std::next(it);
    for (auto it = g_cams.begin(); it != g_cams.end();) it = it->second.pass + 64 < g_pass ? g_cams.erase(it) : std::next(it);
    for (auto it = g_exec.begin(); it != g_exec.end();) it = it->second.pass + 64 < g_pass ? g_exec.erase(it) : std::next(it);
    for (auto it = g_picture.begin(); it != g_picture.end();) it = it->second.pass + 64 < g_pass ? g_picture.erase(it) : std::next(it);
    for (auto it = g_carry.begin(); it != g_carry.end();) it = it->second.pass + 64 < g_pass ? g_carry.erase(it) : std::next(it);
}
// Camera draw alignment: WWHD_T60_CAM_ROT_NEWEST=1 draws the aligned
// camera (at the world time W, as above) with the eye's direction around the centre taken from the camera's NEWEST step
// while a manual-yaw episode runs: the right stick the game read on that step (wwhd_t60_pad) is off-centre and no event
// or message runs (player-controlled modes). Centre, distance, fovy, up, bank and shake stay at W, so Link and the world
// stay one moment; only the orbit angle the stick drives shows a step earlier (latency test walkcam 58.3 ms vs interp 41.7 ms).
// The episode's end slerps back over one step (weight 0.5, then 0); a cut between W and the newest step draws the newest
// step whole. Draw only: the camera's logic words are put back after its draw (interp.cpp t60_camera), so cam.txt and
// compare60 (logic camera at execute, game_hooks.cpp trace_camera) are unchanged. Default 1 (
// after the latency test on a camera scenario: walkcam and campan both beat interp with rot on, latency 41.7 / 62.5 ms vs rot off 58.3 / 75.0);
// WWHD_T60_CAM_ROT_NEWEST=0 draws the blended orbit angle as before.
const bool g_rot_newest = [] {
    const char* e = getenv("WWHD_T60_CAM_ROT_NEWEST");
    return !e || atoi(e) != 0;
}();
constexpr float kRotStick = 0.1f;  // right-stick magnitude that counts as manual input (the raw -1..1 value)
const uint32_t kPlayInfo = GD(0x1046F0B0);  // g_dComIfG_gameInfo.play (mods/hud_gate.cpp)
constexpr uint32_t kEventRun = 0x5292, kMesgStatus = 0x5BB2, kScopeMesgStatus = 0x5BB3;
bool rot_manual_now() {
    const float rx = wwhd_t60_pad.stick[2], ry = wwhd_t60_pad.stick[3];
    if (rx * rx + ry * ry < kRotStick * kRotStick) return false;
    return ld8(kPlayInfo + kEventRun) == 0 && ld8(kPlayInfo + kMesgStatus) == 0 && ld8(kPlayInfo + kScopeMesgStatus) == 0;
}
// out (the aligned camera) with its eye direction from `newest` by weight w (slerp of the unit eye - centre vectors)
void rot_from_newest(interp::camera::CamState& out, const interp::camera::CamState& newest, float w) {
    float da[3], dn[3], la = 0.0f, ln = 0.0f;
    for (int i = 0; i < 3; i++) {
        da[i] = out.eye[i] - out.center[i], dn[i] = newest.eye[i] - newest.center[i];
        la += da[i] * da[i], ln += dn[i] * dn[i];
    }
    la = std::sqrt(la), ln = std::sqrt(ln);
    if (la < 1e-3f || ln < 1e-3f) return;
    float ua[3], un[3], dot = 0.0f;
    for (int i = 0; i < 3; i++) ua[i] = da[i] / la, un[i] = dn[i] / ln, dot += ua[i] * un[i];
    dot = std::fmax(-1.0f, std::fmin(1.0f, dot));
    const float th = std::acos(dot);
    float u[3];
    if (th < 1e-5f) {
        for (int i = 0; i < 3; i++) u[i] = un[i];
    } else {
        const float sa = std::sin((1.0f - w) * th) / std::sin(th), sb = std::sin(w * th) / std::sin(th);
        for (int i = 0; i < 3; i++) u[i] = sa * ua[i] + sb * un[i];
    }
    for (int i = 0; i < 3; i++) out.eye[i] = out.center[i] + la * u[i];
}

// Held event-camera steps (2026-11-23; seen in play: a stutter when a talk starts). An event camera
// (the talk camera included, d_camera_run_t60.h: the event engine's whole form) runs its engine on the tick30 step only;
// on the frame's earlier steps the converted camera executes but its view holds. Those held steps were kept in the
// camera's draw history like steps of their own, so at N 2 the picture showed the frame's whole move on one pass and
// stood still on the other (30 Hz stairs: the talk camera's swing into place, a talk route: eye 7.15 / 0 / 184 / 0 per
// pass). A held step (a step before the tick30 step whose camera is bit-equal to the newest kept step) is not kept: the
// camera is drawn between its frame ends like the interpolated world (as at N 1). And the history's blend takes frame
// interpolation's relative cut rule (interp_camera.h cam_snap: a step far larger than the camera's previous kept step,
// scaled to the same time) besides the absolute one, so the talk camera's cut is a cut, not a two-pass smear (EVENTS60=0
// frames at N 1 had it too). Draw only. NC: WWHD_T60_CAM_HELD_NEGCTRL=1 (both off, as before).
// Part 2 (seen in play after part 1: the start is fixed, the swing back after the talk still stutters). The follow
// / lock-on / ride / hung cameras run their frames whole on the tick30 step (d_camera_follow_whole_t60.h); an earlier
// step's camera is a picture only (the previous frame's change extrapolated, the eye half-way to the per-step run:
// wwhd_t60_cam_follow_picture). While the camera eases (the return from the talk camera, a settle after a warp) the
// per-step eye hardly moves, so that picture showed ~25 % of the frame's move and the tick30 pass the rest (Windfall
// talk end: eye per pass 3.9 / 13.5 / 4.4 / 11.2 / 3.9 / 9.0; a warp settle 0.26 of 140 units per frame for
// 35 frames). Drawn at the world time W one step behind its newest step, the camera's next frame end is known when W
// is an earlier step's time: every step before the tick30 step is now left out of the draw history and the camera is
// drawn between its exact frame ends (interpolated, not extrapolated). rot_newest still turns the orbit toward the
// newest step as executed (`newest`, picture steps included), so a right-stick orbit keeps its lead. NC of part 2
// alone: WWHD_T60_CAM_FRAMEEND_NEGCTRL=1 (only the held steps are left out, the part 1 form).
// Part 3 (seen in play after part 2: turning the camera is shaky): while the player turns the camera (the right stick
// as rot_newest counts it, or mods::camera_turned_by_player: the stick's manual camera, the mouse or a swipe in the last
// steps) the earlier steps are kept as before part 2 (the picture the camera draw tuning judged: its blends, rot_newest),
// so a turned camera is drawn exactly as before part 2; part 2's frame-end form applies while nobody turns it (the talk
// camera's swing back, the settles). Part of part 2's NC.
const bool g_cam_frameend_nc = [] {
    const char* e = getenv("WWHD_T60_CAM_FRAMEEND_NEGCTRL");
    return e && atoi(e) != 0;
}();
const bool g_cam_held_nc = [] {
    const char* e = getenv("WWHD_T60_CAM_HELD_NEGCTRL");
    return e && atoi(e) != 0;
}();
bool same_cam(const interp::camera::CamState& a, const interp::camera::CamState& b) {
    for (int i = 0; i < 3; i++)
        if (a.eye[i] != b.eye[i] || a.center[i] != b.center[i] || a.up[i] != b.up[i]) return false;
    return a.fovy == b.fovy && a.bank == b.bank;
}

// Link's cap and hair between the frame ends (2026-11-23; seen in play: the cap swayed unevenly while Link
// moves). The cap is a 30 Hz spring (decomp daPy_lk_c::setHatAngle: HATA / HATB / HATC angles chased per frame, read by
// jointBeforeCB) that runs on the frame's tick30 step only; on the frame's earlier steps its angles hold, so the cap
// stays rigid on the moving head and the frame's whole sway shows on one pass (a walking route at N 2: the cap's rotation
// against the head changed on 1 pass of 2, 665 of 710 judged passes still). Link is drawn one step behind his newest step: on an
// earlier step's pass the frame's next end is kept already. There the three cap joints are drawn with their rotation
// against their parent blended between the two frame ends (as frame interpolation draws them at N 1); the head and
// everything else as drawn. Only while every kept step inside the frame holds the cap against the head as at the frame's
// start (the spring's hold; an animated cap, e.g. the original hat animation, is drawn as it is). setHatAngle also sets
// the hair (momi) words mLMomiAngleY / mRMomiAngleY / mMomiAngleZ (the wind's lean), which jointBeforeCB concatenates
// after the LMOMI / RMOMI joints' animated rotation (q = anim x Euler(0, y, 0) x Euler(0, 0, z)); the animation runs per
// step, the lean holds. Their lean is drawn blended between the frame ends' words likewise (the words kept with each
// step: the joint's drawn rotation x lean(held)^-1 x lean(blend)); the head's front angles only feed HATA. Draw only
// (no logic). NC: WWHD_T60_HAT_FRAMEEND_NEGCTRL=1 (drawn as before).
const bool g_hat_nc = [] {
    const char* e = getenv("WWHD_T60_HAT_FRAMEEND_NEGCTRL");
    return e && atoi(e) != 0;
}();
constexpr uint32_t kPlayerPtr = 0x5B34;  // play + 0x5B34: the player actor (interp.cpp passlog)
constexpr uint32_t kCLModel = 0x448;     // daPy_lk_c::mpCLModel
constexpr uint32_t kHead = 15, kHatA = 26, kHatB = 27, kHatC = 28;  // CL_JNT_HEAD_JNT_e, CL_JNT_HATA..C_JNT_e
constexpr uint32_t kHatJ[3] = {kHatA, kHatB, kHatC}, kHatP[3] = {kHead, kHatA, kHatB};  // (HATA under the head's frame)
constexpr uint32_t kLMomi = 16, kRMomi = 17;            // CL_JNT_LMOMI_JNT_e / CL_JNT_RMOMI_JNT_e
constexpr uint32_t kMomiWords = 0x6966;                  // mLMomiAngleY, mRMomiAngleY, mMomiAngleZ (be s16 each)
// the words kept after a Link CL model step's matrices: (LMomiY << 16 | RMomiY), MomiZ (host order)
void momi_words(uint32_t proc, std::vector<uint32_t>& v) {
    v.push_back((uint32_t)ld16(proc + kMomiWords) << 16 | ld16(proc + kMomiWords + 2));
    v.push_back(ld16(proc + kMomiWords + 4));
}
// the lean rotation Ry(y) * Rz(z) (s16 angles; J3D's Euler / quaternion convention, row major 3x3)
void lean(float y, float z, float* R) {
    const float k = (float)(M_PI / 32768.0), cy = std::cos(y * k), sy = std::sin(y * k), cz = std::cos(z * k), sz = std::sin(z * k);
    const float Ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy}, Rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) R[3 * r + c] = Ry[3 * r] * Rz[c] + Ry[3 * r + 1] * Rz[3 + c] + Ry[3 * r + 2] * Rz[6 + c];
}
void mtx_f(const uint32_t* w, float* m) {
    for (int k = 0; k < 12; k++) m[k] = interp::joints::word_f(w[k]);
}
// inv(P) * J for 3x4 affine matrices (row major); false: P is singular
bool local_of(const float* P, const float* J, float* L) {
    const float a = P[0], b = P[1], c = P[2], d = P[4], e = P[5], f = P[6], g = P[8], h = P[9], i = P[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < 1e-12f) return false;
    const float r = 1.0f / det;
    const float I[9] = {(e * i - f * h) * r, (c * h - b * i) * r, (b * f - c * e) * r,
                        (f * g - d * i) * r, (a * i - c * g) * r, (c * d - a * f) * r,
                        (d * h - e * g) * r, (b * g - a * h) * r, (a * e - b * d) * r};
    const float t[3] = {J[3] - P[3], J[7] - P[7], J[11] - P[11]};
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++)
            L[4 * row + col] = I[3 * row] * J[col] + I[3 * row + 1] * J[4 + col] + I[3 * row + 2] * J[8 + col];
        L[4 * row + 3] = I[3 * row] * t[0] + I[3 * row + 1] * t[1] + I[3 * row + 2] * t[2];
    }
    return true;
}
// P * L (3x4 affine)
void compose(const float* P, const float* L, float* O) {
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 4; col++)
            O[4 * row + col] = P[4 * row] * L[col] + P[4 * row + 1] * L[4 + col] + P[4 * row + 2] * L[8 + col];
        O[4 * row + 3] += P[4 * row + 3];
    }
}
// the cap's rotation against its parent equal in two steps (the 3x3 parts of the locals, scale-free to ~1e-3)
bool same_local_rot(const float* A, const float* B) {
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++)
            if (std::fabs(A[4 * row + col] - B[4 * row + col]) > 1e-3f) return false;
    return true;
}
// the hair joints' lean between the frame ends i1 / i2 (the words kept after the matrices), while every kept step between
// them holds the words of i1
template <class Steps>
void momi_frameend(const Steps& steps, int i1, int i2, float frac, uint32_t n, uint32_t* out) {
    const size_t at = 12 * (size_t)n;
    for (int i = i1; i <= i2; i++)
        if (steps[i].second.size() != at + 2) return;
    const uint32_t* w1 = steps[i1].second.data() + at;
    const uint32_t* w2 = steps[i2].second.data() + at;
    for (int i = i1 + 1; i < i2; i++)
        if (std::memcmp(steps[i].second.data() + at, w1, 8) != 0) return;  // (not held: drawn as it is)
    if (std::memcmp(w1, w2, 8) == 0) return;  // (no change over the frame)
    const float z1 = (int16_t)w1[1], z2 = (int16_t)w2[1];
    const float y1[2] = {(float)(int16_t)(w1[0] >> 16), (float)(int16_t)(w1[0] & 0xFFFF)};
    const float y2[2] = {(float)(int16_t)(w2[0] >> 16), (float)(int16_t)(w2[0] & 0xFFFF)};
    const uint32_t jn[2] = {kLMomi, kRMomi};
    for (int k = 0; k < 2; k++) {
        float A[9], B[9], C[9], M[12];
        lean(y1[k], z1, A);
        lean(y1[k] + frac * (y2[k] - y1[k]), z1 + frac * (z2 - z1), B);
        for (int r = 0; r < 3; r++)  // C = A^T * B
            for (int c = 0; c < 3; c++) C[3 * r + c] = A[r] * B[c] + A[3 + r] * B[3 + c] + A[6 + r] * B[6 + c];
        mtx_f(out + 12 * jn[k], M);
        for (int r = 0; r < 3; r++) {
            const float m0 = M[4 * r], m1 = M[4 * r + 1], m2 = M[4 * r + 2];
            for (int c = 0; c < 3; c++) out[12 * jn[k] + 4 * r + c] = interp::joints::f_word(m0 * C[c] + m1 * C[3 + c] + m2 * C[6 + c]);
        }
    }
}
template <class Steps>
void hat_frameend(const Steps& steps, double td, uint32_t* out, uint32_t n) {
    if (n <= kHatC) return;
    const double F1 = std::floor(td + hist::kEps), F2 = F1 + 1.0, frac = td - F1;
    if (frac < hist::kEps || frac > 1.0 - hist::kEps) return;  // (a frame end: drawn as it is)
    int i1 = -1, i2 = -1;
    for (int i = 0; i < (int)steps.size(); i++) {
        if (std::fabs(steps[i].first - F1) < hist::kEps) i1 = i;
        if (std::fabs(steps[i].first - F2) < hist::kEps) i2 = i;
    }
    if (i1 < 0 || i2 < 0) return;  // (a frame end not kept: drawn as it is)
    momi_frameend(steps, i1, i2, (float)frac, n, out);
    float l1[3][12], l2[3][12], P[12], J[12], L[12];
    for (int k = 0; k < 3; k++) {
        mtx_f(steps[i1].second.data() + 12 * kHatP[k], P), mtx_f(steps[i1].second.data() + 12 * kHatJ[k], J);
        if (!local_of(P, J, l1[k])) return;
        mtx_f(steps[i2].second.data() + 12 * kHatP[k], P), mtx_f(steps[i2].second.data() + 12 * kHatJ[k], J);
        if (!local_of(P, J, l2[k])) return;
    }
    for (int i = i1 + 1; i < i2; i++)  // (the earlier steps: the spring's hold, else drawn as it is)
        for (int k = 0; k < 3; k++) {
            mtx_f(steps[i].second.data() + 12 * kHatP[k], P), mtx_f(steps[i].second.data() + 12 * kHatJ[k], J);
            if (!local_of(P, J, L) || !same_local_rot(L, l1[k])) return;
        }
    float O[12];
    for (int k = 0; k < 3; k++) {
        interp::blend_world_matrix(l1[k], l2[k], L, (float)frac);
        mtx_f(out + 12 * kHatP[k], P);
        compose(P, L, O);
        for (int w = 0; w < 12; w++) out[12 * kHatJ[k] + w] = interp::joints::f_word(O[w]);
    }
}

// (x y z: the drawn root joint's translation, or the drawn eye)
void log_draw(char kind, uint32_t owner, double w, double ta, double tb, double newest, float f, float x, float y, float z,
              const uint32_t* joints = nullptr, uint32_t n = 0) {
    if (!g_log) return;
    fprintf(g_log, "%llu %c %08X %.4f %.4f %.4f %.4f %.3f %.3f %.3f %.3f", (unsigned long long)g_pass, kind, owner, w, ta, tb,
            newest, f, x, y, z);
    if (joints && g_log_joint >= 0 && (uint32_t)g_log_joint < n) {  // (joint j: translation, and its offset from the root)
        const uint32_t* m = joints + 12 * g_log_joint;
        const float jx = interp::joints::word_f(m[3]), jy = interp::joints::word_f(m[7]), jz = interp::joints::word_f(m[11]);
        fprintf(g_log, " j%d %.3f %.3f %.3f d %.3f %.3f %.3f", g_log_joint, jx, jy, jz, jx - x, jy - y, jz - z);
    }
    fputc('\n', g_log);
}
}  // namespace

bool switch_on() { return g_switch; }
bool all_visible_converted() { return world_systems_converted() && !g_scene_unconverted_drawn; }
bool align_on() { return g_switch && selected_n() > 1 && !all_visible_converted(); }
bool converted(uint32_t proc) { return proc && wwhd_t60_proc_converted((int16_t)ld16(proc + kProfName)) != 0; }

void pass_start(uint64_t pass) {
    g_pass = pass;
    sweep();
    if (g_log && pass % 600 == 0) fflush(g_log);
}

void note_execute(uint32_t proc) {
    if (selected_n() <= 1 || !converted(proc)) return;
    // test aid WWHD_T60_WHOLE_TEST=profile[,profile]: a whole-form actor's earlier-step execute counts as a picture step
    // without the decomp's report (a binary whose decomp predates b641341e6; only for profiles whose earlier steps are
    // all picture steps, e.g. an actor never held). Never set without the variable
    static int test[8] = {0};
    static const bool test_on = [] {
        const char* e = getenv("WWHD_T60_WHOLE_TEST");
        return e && t60_parse_convert_list(e, test, 7) != 0;
    }();
    if (test_on && sub_step()) {
        const int prof = (int16_t)ld16(proc + kProfName);
        for (int i = 0; test[i]; i++)
            if (test[i] == prof) note_picture(proc);
    }
    g_exec[proc] = {step_time(), g_pass, ld32(proc + kProcId)};
}
void note_picture(uint32_t proc) {
    if (!proc || selected_n() <= 1) return;
    g_picture[proc] = {g_pass, ld32(proc + kProcId)};
}
void note_carry(uint32_t proc, float dx, float dy, float dz, int dangle) {
    if (!proc || selected_n() <= 1 || !g_carry_on) return;
    CarryHist& h = g_carry[proc];
    const uint32_t id = ld32(proc + kProcId);
    if (h.id != id) h = CarryHist{}, h.id = id;
    h.pass = g_pass;
    h.ev.push_back({step_time(), {dx, dy, dz}, dangle});
    while (h.ev.size() > 64) h.ev.pop_front();
}
bool picture_step(uint32_t proc) {
    auto it = g_picture.find(proc);
    return it != g_picture.end() && it->second.pass == g_pass && it->second.id == ld32(proc + kProcId);
}
bool stepped_this_pass(uint32_t proc) {
    if (!sub_step()) return false;
    // (a whole-form actor's picture step is no step of its own: its draw on that pass is an in-between draw,
    // kept and put back like an unconverted actor's (interp.cpp actor_keep: the actor's memory and the models its draw
    // calcs), so its tick30 execute sees what the one draw of the 30 Hz frame left. WWHD_T60_WHOLE_KEEP=0: as before)
    if (g_whole_keep && picture_step(proc)) return false;
    auto it = g_exec.find(proc);
    return it != g_exec.end() && it->second.pass == g_pass && it->second.id == ld32(proc + kProcId);
}

void words_owner_draw(uint32_t proc);  // (draw words, below)
uint32_t draw_begin(uint32_t proc) {
    const uint32_t previous = g_drawing;
    g_drawing = proc;
    words_owner_draw(proc);
    if (proc && (int16_t)ld16(proc + kProfName) == kPlayScene && ld32(proc + kProcId) != g_scene_id) {
        g_scene_id = ld32(proc + kProcId);  // a new scene: nothing unconverted drawn yet
        g_scene_unconverted_drawn = false;
    }
    return previous;
}
void draw_end(uint32_t previous) { g_drawing = previous; }
uint32_t drawing() { return g_drawing; }

namespace {
// Link's leg IK between the frame ends (2026-11-23, after the per-step leg angle split): Link's leg IK on stairs / slopes
// drawn between the frame ends. footBgCheck (decomp d_a_player_main_03.cpp) eases the foot records' leg angles once a
// frame from the frame start; jointCB1 turns the thigh, knee and ankle joints by them after the model calc: LLEGA 32 =
// anim x Rz(mLegAAngle), LLEGB 33 = 32 x T Q(33) x Rz(mLegBJointAngle), LFOOT 34 = 33 x T Q(34) x Rz(mFootAngle),
// LTOE 35 = 34 x T Q(35) (left: foot record 1; right 37..40: record 0). On the frame's earlier steps the eases start
// from the frame start toward the step's own in-between ground target (the per-step split takes k / N of that), and at the stair
// edge that target is not the frame end's (a walking route's stair edge: right legA 609 -> 1164 -> 305). Link is drawn one step behind
// his newest step, so on an earlier step's pass the frame's next end is kept: the three IK angles are drawn at the frame
// start + frac x (the frame end - the frame start) (the words kept with each step, angle space, the short way): each
// joint's matrix turned by Rz(blend - drawn) about its own z after its parent's change, the toe following. Frame-end
// passes, or a frame end not kept: drawn as they are. Draw only (no logic), Link's CL model only.
// NC: WWHD_T60_LEGPIC_NEGCTRL=1 (drawn as before). Debug: WWHD_T60_LEGPIC_LOG=path (one line per earlier pass).
const bool g_legpic_nc = [] {
    const char* e = getenv("WWHD_T60_LEGPIC_NEGCTRL");
    return e && atoi(e) != 0;
}();
FILE* const g_legpic_log = getenv("WWHD_T60_LEGPIC_LOG") ? fopen(getenv("WWHD_T60_LEGPIC_LOG"), "w") : nullptr;
constexpr uint32_t kLegPlayer = 0x5B34;   // play + 0x5B34: the player actor
constexpr uint32_t kLegCLModel = 0x448;   // daPy_lk_c::mpCLModel
constexpr uint32_t kFootData = 0x73F0, kFootStride = 0x118;  // daPy_lk_c::mFootData[2]
constexpr uint32_t kFootAngle = 0x2, kLegAAngle = 0x8, kLegBJointAngle = 0xA;
constexpr uint32_t kLegJ[2][4] = {{37, 38, 39, 40}, {32, 33, 34, 35}};  // foot record 0: right, 1: left
struct LegWords { int16_t a[2][3]; };  // per foot record: legA, legBJoint, foot
struct LegHist { uint32_t proc = 0, id = 0; std::deque<std::pair<double, LegWords>> steps; };
LegHist g_leg;
LegWords leg_words(uint32_t proc) {
    LegWords v;
    for (int i = 0; i < 2; i++) {
        const uint32_t r = proc + kFootData + i * kFootStride;
        v.a[i][0] = (int16_t)ld16(r + kLegAAngle), v.a[i][1] = (int16_t)ld16(r + kLegBJointAngle), v.a[i][2] = (int16_t)ld16(r + kFootAngle);
    }
    return v;
}
const LegWords* leg_at(double t) {
    for (const auto& s : g_leg.steps)
        if (std::fabs(s.first - t) < hist::kEps) return &s.second;
    return nullptr;
}
float ang_d(int16_t a, int16_t b) { return (float)(int16_t)(uint16_t)((uint16_t)b - (uint16_t)a); }
// M x Rz(d) on the 3x3 part (s16 angle units; mDoMtx_ZrotS's [[c, -s], [s, c]])
void turn_z(float* M, float d) {
    const float k = (float)(M_PI / 32768.0), c = std::cos(d * k), s = std::sin(d * k);
    for (int r = 0; r < 3; r++) {
        const float m0 = M[4 * r], m1 = M[4 * r + 1];
        M[4 * r] = m0 * c + m1 * s;
        M[4 * r + 1] = -m0 * s + m1 * c;
    }
}
void leg_mtx(const uint32_t* w, float* m) {
    for (int k = 0; k < 12; k++) m[k] = interp::joints::word_f(w[k]);
}
// inv(P) x J (3x4 affine, row major); false: P singular
bool leg_local(const float* P, const float* J, float* L) {
    const float a = P[0], b = P[1], c = P[2], d = P[4], e = P[5], f = P[6], g = P[8], h = P[9], i = P[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < 1e-12f) return false;
    const float r = 1.0f / det;
    const float I[9] = {(e * i - f * h) * r, (c * h - b * i) * r, (b * f - c * e) * r, (f * g - d * i) * r, (a * i - c * g) * r,
                        (c * d - a * f) * r, (d * h - e * g) * r, (b * g - a * h) * r, (a * e - b * d) * r};
    const float t[3] = {J[3] - P[3], J[7] - P[7], J[11] - P[11]};
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++)
            L[4 * row + col] = I[3 * row] * J[col] + I[3 * row + 1] * J[4 + col] + I[3 * row + 2] * J[8 + col];
        L[4 * row + 3] = I[3 * row] * t[0] + I[3 * row + 1] * t[1] + I[3 * row + 2] * t[2];
    }
    return true;
}
void leg_compose(const float* P, const float* L, float* O) {
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 4; col++)
            O[4 * row + col] = P[4 * row] * L[col] + P[4 * row + 1] * L[4 + col] + P[4 * row + 2] * L[8 + col];
        O[4 * row + 3] += P[4 * row + 3];
    }
}
// The Iron Boots on the blended feet: the Iron Boots models (daPy_lk_c::mpHbootsModels[2] at +0x442C: right, left) take copies of
// the CL leg joints in execute (setBootsModel 024038A0, heavy boots on: boots joint 1 = knee CL 38 / 33, 2 = toe 40 / 35,
// 3 = foot 39 / 34). They are drawn from their own kept steps, i.e. with the step's own IK: on an earlier pass the boots
// would stand off the blended foot. The CL draw of the pass leaves each leg joint's change (drawn after x drawn before
// ^-1, world space) here; a boots model drawn on the same pass at the same time takes it on its copied joints. NC:
// WWHD_T60_LEGPIC_BOOTS_NEGCTRL=1 (boots as before). Debug: the LEGPIC log's B lines (boots foot vs CL foot per pass).
const bool g_legboots_nc = [] {
    const char* e = getenv("WWHD_T60_LEGPIC_BOOTS_NEGCTRL");
    return e && atoi(e) != 0;
}();
constexpr uint32_t kHboots = 0x442C;
constexpr uint32_t kBootsCL[2][4] = {{0, 38, 40, 39}, {0, 33, 35, 34}};  // boots joint -> CL joint (boots 0: right)
struct LegCache {
    uint64_t pass = ~0ull;
    double td = -1;
    bool changed = false;
    float delta[41][12];   // per CL joint: drawn after x drawn before^-1 (only the leg joints set)
    uint32_t before[41][12], after[41][12];  // the CL leg joints' drawn words before / after the blend
    float foot[2][3];      // the drawn CL foot translations ([0] right 39, [1] left 34: the boots side)
};
LegCache g_legcache;
void leg_mul(const float* A, const float* B, float* O) {  // A x B (3x4 affine)
    leg_compose(A, B, O);
}
bool leg_inv(const float* M, float* I) {  // M^-1 (3x4 affine)
    const float id[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    return leg_local(M, id, I);
}
void leg_cache_note(double td, const uint32_t* before, const uint32_t* after, bool changed) {
    g_legcache.pass = g_pass, g_legcache.td = td, g_legcache.changed = changed;
    for (int i = 0; i < 2; i++) {
        const uint32_t* j = kLegJ[i];
        float A[12];
        leg_mtx(after + 12 * j[2], A);
        g_legcache.foot[i][0] = A[3], g_legcache.foot[i][1] = A[7], g_legcache.foot[i][2] = A[11];
        if (!changed) continue;
        for (int k = 0; k < 4; k++) {
            float B[12], Bi[12];
            memcpy(g_legcache.before[j[k]], before + 12 * j[k], 48), memcpy(g_legcache.after[j[k]], after + 12 * j[k], 48);
            leg_mtx(before + 12 * j[k], B), leg_mtx(after + 12 * j[k], A);
            if (!leg_inv(B, Bi)) { g_legcache.changed = false; return; }
            leg_mul(A, Bi, g_legcache.delta[j[k]]);
        }
    }
}
// a boots model of Link drawn: side 0 right, 1 left
void leg_boots(int side, double td, uint32_t* out, uint32_t n) {
    if (n < 4) return;
    const bool same = g_legcache.pass == g_pass && std::fabs(g_legcache.td - td) < hist::kEps;
    if (same && g_legcache.changed && !g_legboots_nc)
        for (int k = 1; k < 4; k++) {
            const uint32_t c = kBootsCL[side][k];
            if (memcmp(out + 12 * k, g_legcache.before[c], 48) == 0) {  // (the copy as drawn: the CL joint as drawn now)
                memcpy(out + 12 * k, g_legcache.after[c], 48);
                continue;
            }
            float B[12], O[12];
            leg_mtx(out + 12 * k, B);
            leg_mul(g_legcache.delta[kBootsCL[side][k]], B, O);
            for (int w = 0; w < 12; w++) out[12 * k + w] = interp::joints::f_word(O[w]);
        }
    if (g_legpic_log) {
        float F[12];
        leg_mtx(out + 36, F);
        if (same)
            fprintf(g_legpic_log, "B %.4f side %d changed %d boots-foot %.4f %.4f %.4f\n", td, side, g_legcache.changed ? 1 : 0,
                    F[3] - g_legcache.foot[side][0], F[7] - g_legcache.foot[side][1], F[11] - g_legcache.foot[side][2]);
        else
            fprintf(g_legpic_log, "B %.4f side %d nocache (CL pass %llu td %.4f)\n", td, side, (unsigned long long)g_legcache.pass, g_legcache.td);
    }
}

// td: the drawn time; da / db, f: the kept steps drawn (out = their blend at f)
void leg_frameend(double td, double ta, double tb, float f, uint32_t* out, uint32_t n) {
    if (n <= 40) return;
    leg_cache_note(td, out, out, false);  // (the drawn CL feet; no change unless set below)
    const double F1 = std::floor(td + hist::kEps), F2 = F1 + 1.0, frac = td - F1;
    if (frac < hist::kEps || frac > 1.0 - hist::kEps) return;  // (a frame end: drawn as it is)
    const LegWords *w1 = leg_at(F1), *w2 = leg_at(F2), *wa = leg_at(ta), *wb = leg_at(tb);
    if (!w1 || !w2 || !wa || !wb) {
        if (g_legpic_log) fprintf(g_legpic_log, "%.4f miss F1 %d F2 %d a %d b %d\n", td, !!w1, !!w2, !!wa, !!wb);
        return;  // (a frame end not kept: drawn as it is)
    }
    float d[2][3];
    bool any = false;
    for (int i = 0; i < 2; i++)
        for (int k = 0; k < 3; k++) {
            const float drawn = (float)wa->a[i][k] + f * ang_d(wa->a[i][k], wb->a[i][k]);
            const float want = (float)w1->a[i][k] + (float)frac * ang_d(w1->a[i][k], w2->a[i][k]);
            d[i][k] = std::remainder(want - drawn, 65536.0f);
            any = any || std::fabs(d[i][k]) > 0.5f;
        }
    if (g_legpic_log)
        fprintf(g_legpic_log, "%.4f frac %.3f f %.3f R %d %d %d | %d %d %d | %d %d %d L %d %d %d | %d %d %d | %d %d %d\n", td, frac, f,
                w1->a[0][0], w1->a[0][1], w1->a[0][2], wa->a[0][0], wa->a[0][1], wa->a[0][2], w2->a[0][0], w2->a[0][1], w2->a[0][2],
                w1->a[1][0], w1->a[1][1], w1->a[1][2], wa->a[1][0], wa->a[1][1], wa->a[1][2], w2->a[1][0], w2->a[1][1], w2->a[1][2]);
    if (!any) return;
    const std::vector<uint32_t> before(out, out + 12 * n);
    for (int i = 0; i < 2; i++) {
        const uint32_t* j = kLegJ[i];
        float W[4][12], L[4][12], N[4][12];
        for (int k = 0; k < 4; k++) leg_mtx(out + 12 * j[k], W[k]);
        bool ok = true;
        for (int k = 1; k < 4; k++) ok = ok && leg_local(W[k - 1], W[k], L[k]);
        if (!ok) continue;
        memcpy(N[0], W[0], sizeof N[0]);
        turn_z(N[0], d[i][0]);
        for (int k = 1; k < 4; k++) {
            if (k < 3) turn_z(L[k], d[i][k]);
            leg_compose(N[k - 1], L[k], N[k]);
        }
        for (int k = 0; k < 4; k++)
            for (int w = 0; w < 12; w++) out[12 * j[k] + w] = interp::joints::f_word(N[k][w]);
    }
    leg_cache_note(td, before.data(), out, true);
}
}  // namespace

bool model(uint32_t model, uint32_t jnt, uint32_t mtx, uint32_t n, double w, uint32_t* out) {
    const uint32_t proc = g_drawing;
    if (!converted(proc)) {  // (the per-scene check: an unconverted model is visible)
        g_scene_unconverted_drawn = true;
        return false;
    }
    if (!align_on()) return false;
    auto e = g_exec.find(proc);
    const uint32_t id = ld32(proc + kProcId);
    if (e == g_exec.end() || e->second.id != id) return false;  // (not executed yet: drawn as it is)
    const uint32_t words = 12 * n;
    const uint32_t* cur = (const uint32_t*)ppc_ptr(mtx);
    ModelHist& h = g_models[jnt];
    if (h.owner != proc || h.owner_id != id || h.mtx != mtx || h.n != n) {
        h = ModelHist{};
        h.owner = proc, h.owner_id = id, h.mtx = mtx, h.n = n;
    }
    h.pass = g_pass;
    // (a whole-form actor's picture step is no step of its own: not kept; drawn between its frame ends)
    const bool picture = g_whole_blend && picture_step(proc);
    const bool link_gl = !glide::g_nc && proc == ld32(kPlayInfo + 0x5B34);  // (the glide: Link's models)
    if (link_gl && !picture) glide::record(proc, id, e->second.time);
    const bool link_legs = !g_legpic_nc && n > 40 && proc == ld32(kPlayInfo + kLegPlayer) &&
                           model == ld32(proc + kLegCLModel);  // (the leg IK: Link's CL model)
    if (link_legs && !picture) {  // (the leg words kept with the step, as its matrices)
        if (g_leg.proc != proc || g_leg.id != id) g_leg = LegHist{}, g_leg.proc = proc, g_leg.id = id;
        const double t = e->second.time;
        if (!g_leg.steps.empty() && std::fabs(g_leg.steps.back().first - t) < hist::kEps) g_leg.steps.back().second = leg_words(proc);
        else if (g_leg.steps.empty() || g_leg.steps.back().first < t) g_leg.steps.emplace_back(t, leg_words(proc));
        while (g_leg.steps.size() > 32) g_leg.steps.pop_front();
    }
    const bool link_cl = !g_hat_nc && n > kHatC && proc == ld32(kPlayInfo + kPlayerPtr) &&
                         model == ld32(proc + kCLModel);  // (the cap and hair: Link's CL model)
    if (!hist::record(h.steps, e->second.time, picture, [&] {
            std::vector<uint32_t> v(cur, cur + words);
            float C[12];
            if (basehold_c(proc, C, g_pass)) basehold_apply(C, v);  // (the step's base placement)
            if (link_cl) momi_words(proc, v);
            return v;
        }))
        return false;  // (nothing kept yet: drawn as it is)
    const hist::Pick p = hist::pick(h.steps, w);
    const auto& sa = h.steps[p.a];
    const auto& sb = h.steps[p.b];
    float f = p.f;
    if (f == 0.0f) {
        memcpy(out, sa.second.data(), 4 * words);
    } else {
        const auto r = interp::joints::blend(sa.second.data(), sb.second.data(), nullptr, n, kCut, f, out, interp::blend_world_matrix);
        if (r.cut) {  // (a teleport between the two steps: the later one)
            memcpy(out, sb.second.data(), 4 * words);
            f = 1.0f;
        }
    }
    if (link_cl)  // (Link's cap and hair between the frame ends)
        hat_frameend(h.steps, sa.first + f * (sb.first - sa.first), out, n);
    if (link_legs)  // (Link's leg IK between the frame ends)
        leg_frameend(sa.first + f * (sb.first - sa.first), sa.first, sb.first, f, out, n);
    else if (!g_legpic_nc && proc == ld32(kPlayInfo + kLegPlayer) &&
             (model == ld32(proc + kHboots) || model == ld32(proc + kHboots + 4)))  // (the Iron Boots on the blended feet)
        leg_boots(model == ld32(proc + kHboots) ? 0 : 1, sa.first + f * (sb.first - sa.first), out, n);
    if (link_gl) {  // (the Deku Leaf glide's lean / sway / flutter, the gust)
        glide::frameend(proc, model, sa.first, sb.first, f, out, n);
        glide::gust(proc, model, h.steps, sa.first + f * (sb.first - sa.first), out, n);
    }
    if (g_carry_on) {  // (Link's carry by a whole-form carrier spread over the frame)
        auto c = g_carry.find(proc);
        if (c != g_carry.end() && c->second.id == id && !c->second.ev.empty()) {
            const double newest = h.steps.back().first;
            Carry k = carry_at(c->second, sa.first, newest);
            if (f != 0.0f) {
                const Carry kb = carry_at(c->second, sb.first, newest);
                for (int i = 0; i < 3; i++) k.d[i] += f * (kb.d[i] - k.d[i]);
                k.da += f * (kb.da - k.da);
                k.any = k.any || kb.any;
            }
            if (k.any) apply_carry(k, out, n);
        }
    }
    log_draw(picture ? 'w' : 'm', proc, w, sa.first, sb.first, h.steps.back().first, f, interp::joints::word_f(out[3]),
             interp::joints::word_f(out[7]), interp::joints::word_f(out[11]), out, n);
    return true;
}

bool camera(uint32_t cam, const interp::camera::CamState& cur, const interp::camera::Shake& shake, double w,
            interp::camera::CamState& out) {
    if (!align_on() || !converted(cam)) return false;
    auto e = g_exec.find(cam);
    const uint32_t id = ld32(cam + kProcId);
    if (e == g_exec.end() || e->second.id != id) return false;
    CamHist& h = g_cams[cam];
    if (h.id != id) h = CamHist{}, h.id = id;
    h.pass = g_pass;
    const double time = e->second.time;
    // (held event-camera steps: a held step before the tick30 step is not kept; part 2: no step before the tick30 step is)
    const bool earlier = !g_cam_held_nc && std::fabs(time - std::round(time)) > hist::kEps && !h.steps.empty() &&
                         h.steps.back().first < time - hist::kEps;
    const bool turning = earlier && !g_cam_frameend_nc && (rot_manual_now() || mods::camera_turned_by_player());
    const bool held = earlier && ((!g_cam_frameend_nc && !turning) || (!shake.on && !h.steps.back().second.k.on &&
                                                                       same_cam(h.steps.back().second.s, cur)));
    if (!held) hist::stamp(h.steps, time, CamStep{cur, shake});
    if (time >= h.newest_time - hist::kEps) h.newest_time = time, h.newest = cur;
    const hist::Pick p = hist::pick(h.steps, w);
    const CamStep& sa = h.steps[p.a].second;
    const CamStep& sb = h.steps[p.b].second;
    float f = p.f;
    if (f == 0.0f) {
        out = sa.s;
    } else {
        float last = 1e30f;  // (only the absolute cuts: a jump of eye or centre, or of the field of view)
        if (!g_cam_held_nc && p.a > 0) {  // (held event-camera steps: and the relative rule, on the kept step before, per time)
            const auto& s0 = h.steps[p.a - 1];
            const double dt0 = h.steps[p.a].first - s0.first, dt1 = h.steps[p.b].first - h.steps[p.a].first;
            if (dt0 > hist::kEps) {
                const float m0 = std::max(interp::camera::dist(s0.second.s.eye, sa.s.eye), interp::camera::dist(s0.second.s.center, sa.s.center));
                last = (float)(m0 * dt1 / dt0);
            }
        }
        if (interp::camera::cam_snap(sa.s, sb.s, last)) out = sb.s, f = 1.0f;  // (a cut: the later step)
        else out = interp::camera::blend_shaken(sa.s, sa.k, sb.s, sb.k, f, false);
    }
    if (g_rot_newest) {  // camera draw alignment: the newest step's orbit direction in a manual-yaw episode (above)
        const bool own = !g_cam_held_nc && !g_cam_frameend_nc;  // (part 2: the newest step as executed)
        const double tn = own ? h.newest_time : h.steps.back().first;
        if (tn != h.rot_stamp) {  // once per new step: the episode weight of this newest step
            h.rot_stamp = tn;
            h.rot_w = rot_manual_now() ? 1.0f : (h.rot_w >= 1.0f ? 0.5f : 0.0f);
        }
        const interp::camera::CamState& sn = own ? h.newest : h.steps.back().second.s;
        if (h.rot_w > 0.0f && (own ? tn > h.steps[p.a].first + hist::kEps || f != 0.0f : &sn != &sa.s)) {
            float last = 1e30f;
            if (interp::camera::cam_snap(out, sn, last)) out = sn;  // (a cut between W and the newest step: newest whole)
            else rot_from_newest(out, sn, h.rot_w);
        }
    }
    log_draw('c', cam, w, h.steps[p.a].first, h.steps[p.b].first, h.steps.back().first, f, out.eye[0], out.eye[1], out.eye[2]);
    return true;
}
}  // namespace t60::draw

// Draw words (2026-11-23): draws that are no J3DModel (the sail cloth's packet matrix and
// vertices, the boomerang's trail, Link's sword blur) read their converted owner's newest step at draw time, while its
// models are drawn at world time W (one step back at N 2): a constant 1/60 s lead. The decomp brackets such a draw with
// wwhd_t60_draw_words_begin / _end on a block of guest words; begin keeps the words per step of the owner (stamped with
// its newest step's time, as model() does), puts the words of the step at W in their place, and end puts the live words
// back (the draw's own writes outside the block stay). Draw only. WWHD_T60_DRAW_WORDS=0: off (drawn at the newest step).
namespace t60::draw {
namespace {
double w_now() { return interp::t60_world_time(); }
const bool g_words_on = [] {
    const char* e = getenv("WWHD_T60_DRAW_WORDS");
    return !e || atoi(e) != 0;
}();
const bool g_words_frameend = [] {  // (kind 3: the frame-end blend below; 0: as kind 1)
    const char* e = getenv("WWHD_T60_WORDS_FRAMEEND");
    return !e || atoi(e) != 0;
}();
struct WordsHist {
    uint32_t owner = 0, owner_id = 0, n = 0;
    int kind = 0;
    uint64_t pass = 0;
    std::deque<std::pair<double, std::vector<uint32_t>>> steps;
};
std::unordered_map<uint32_t, WordsHist> g_words;                         // by the block's address
std::vector<std::pair<uint32_t, std::vector<uint32_t>>> g_words_live;  // blocks swapped now (the live words)
// the world time at a converted owner's own draw on this pass: a packet drawn later on the pass (the draw lists: the
// boomerang trail, the sword blur) is drawn at the time its owner's models were (the painter's pass fraction can differ)
struct OwnerW { uint64_t pass; double w; };
std::unordered_map<uint32_t, OwnerW> g_owner_w;
}  // namespace
void words_owner_draw(uint32_t proc) {
    if (!g_words_on || !proc || !align_on() || !converted(proc)) return;
    if (g_pass % 256 == 0)
        for (auto it = g_owner_w.begin(); it != g_owner_w.end();) it = it->second.pass + 64 < g_pass ? g_owner_w.erase(it) : std::next(it);
    g_owner_w[proc] = {g_pass, w_now()};
}

// kind 0: words as a whole (a point list that shifts: the step nearer W), 1: floats (lerp), 2: 3x4 matrices; kind >> 8:
// the word the DRAWLOG point starts at (a matrix: its translation)
bool words_begin(uint32_t owner, uint32_t ea, uint32_t n, int kind_log) {
    const int kind = kind_log & 0x7F;
    const bool unit_vecs = (kind_log & 0x80) != 0;  // (kind 3: the block's last two thirds are unit vectors)
    const uint32_t lw = (uint32_t)kind_log >> 8;
    if (!owner) owner = g_drawing;  // (3D lines: the process whose draw runs)
    if (!g_words_on || !ea || !n || !align_on() || !converted(owner)) return false;
    auto e = g_exec.find(owner);
    const uint32_t id = ld32(owner + kProcId);
    if (e == g_exec.end() || e->second.id != id) return false;
    if (g_pass % 256 == 0)
        for (auto it = g_words.begin(); it != g_words.end();) it = it->second.pass + 64 < g_pass ? g_words.erase(it) : std::next(it);
    WordsHist& h = g_words[ea];
    // (another owner or block, or not drawn on the pass before: a new run of the draw, its old steps are no history)
    if (h.owner != owner || h.owner_id != id || h.n != n || h.kind != kind || h.pass + 1 < g_pass) {
        h = WordsHist{};
        h.owner = owner, h.owner_id = id, h.n = n, h.kind = kind;
    }
    h.pass = g_pass;
    uint32_t* cur = (uint32_t*)ppc_ptr(ea);
    // (a whole-form owner's picture step is no step of its own: not kept; drawn between its frame ends, as
    // its models)
    const bool picture = g_whole_blend && picture_step(owner);
    if (!hist::record(h.steps, e->second.time, picture, [&] { return std::vector<uint32_t>(cur, cur + n); })) return false;
    double w = w_now();
    if (owner != g_drawing) {  // (a packet drawn after its owner's draw: its owner's time)
        auto ow = g_owner_w.find(owner);
        if (ow != g_owner_w.end() && ow->second.pass == g_pass) w = ow->second.w;
    }
    const hist::Pick p = hist::pick(h.steps, w);
    const std::vector<uint32_t>& a = h.steps[p.a].second;
    const std::vector<uint32_t>& b = h.steps[p.b].second;
    std::vector<uint32_t> out(a);
    float f = p.f;
    if (f != 0.0f) {
        using interp::joints::f_word;
        using interp::joints::word_f;
        if (kind == 0) {
            if (f >= 0.5f) out = b;
        } else if (kind == 2) {
            for (uint32_t m = 0; m + 12 <= n; m += 12) {
                float fa[12], fb[12], fo[12];
                for (int k = 0; k < 12; k++) fa[k] = word_f(a[m + k]), fb[k] = word_f(b[m + k]);
                interp::blend_world_matrix(fa, fb, fo, f);
                for (int k = 0; k < 12; k++) out[m + k] = f_word(fo[k]);
            }
        } else {  // (kinds 1 and 3; kind 3's frame-end blend below)
            for (uint32_t k = 0; k < n; k++) out[k] = f_word(word_f(a[k]) + f * (word_f(b[k]) - word_f(a[k])));
        }
    }
    // kind 3: floats drawn between their frame ends (a 30 Hz value set once a frame, e.g. a converted actor's attention
    // position the Z-target arrow follows; the sail cloth, whose earlier steps show an extrapolated picture). When W lies
    // between two frame ends that are both kept (W one step back at N 2: the next end is kept on an earlier step's pass),
    // the block is drawn as the frame start + frac x (the frame end - the frame start); with kind bit 0x80 the first third
    // are positions and the last two thirds unit vectors (the sail: mPos, mNrm, mBackNrm), renormalized. A position jump
    // between the two ends beyond kCut: the later end. A frame end not kept: drawn as kind 1 (the step pick above).
    // Port switch WWHD_T60_WORDS_FRAMEEND=0: as kind 1.
    if (kind == 3 && g_words_frameend) {
        const double td = h.steps[p.a].first + f * (h.steps[p.b].first - h.steps[p.a].first);
        const double F1 = std::floor(td + hist::kEps), frac = td - F1;
        const bool vecs = unit_vecs && n % 9 == 0;
        if (frac > hist::kEps && frac < 1.0 - hist::kEps) {
            const std::vector<uint32_t>*e1 = nullptr, *e2 = nullptr;
            for (const auto& st : h.steps) {
                if (std::fabs(st.first - F1) < hist::kEps) e1 = &st.second;
                if (std::fabs(st.first - (F1 + 1.0)) < hist::kEps) e2 = &st.second;
            }
            if (e1 && e2) {
                using interp::joints::f_word;
                using interp::joints::word_f;
                const uint32_t np = vecs ? n / 3 : n;
                bool cut = false;
                for (uint32_t k = 0; k < np && !cut; k++) cut = !(std::fabs(word_f((*e2)[k]) - word_f((*e1)[k])) <= kCut);
                if (cut) {
                    out = *e2;
                } else {
                    const float fr = (float)frac;
                    for (uint32_t k = 0; k < n; k++) out[k] = f_word(word_f((*e1)[k]) + fr * (word_f((*e2)[k]) - word_f((*e1)[k])));
                    for (uint32_t k = np; vecs && k + 3 <= n; k += 3) {
                        const float x = word_f(out[k]), y = word_f(out[k + 1]), z = word_f(out[k + 2]);
                        const float l = std::sqrt(x * x + y * y + z * z);
                        if (l > 1e-6f) out[k] = f_word(x / l), out[k + 1] = f_word(y / l), out[k + 2] = f_word(z / l);
                    }
                }
            }
        }
    }
    // (log: kind 'p', the block's first point as drawn (a matrix: its translation) and as the newest step has it)
    const uint32_t t0 = kind == 2 ? 3 : lw, t1 = kind == 2 ? 7 : lw + 1, t2 = kind == 2 ? 11 : lw + 2;
    if (g_log && n > t2)
        fprintf(g_log, "%llu p %08X %.4f %.4f %.4f %.4f %.3f %.3f %.3f %.3f b %08X k%d new %.3f %.3f %.3f\n", (unsigned long long)g_pass,
                owner, w, h.steps[p.a].first, h.steps[p.b].first, h.steps.back().first, f, interp::joints::word_f(out[t0]),
                interp::joints::word_f(out[t1]), interp::joints::word_f(out[t2]), ea, kind, interp::joints::word_f(cur[t0]),
                interp::joints::word_f(cur[t1]), interp::joints::word_f(cur[t2]));
    if (memcmp(cur, out.data(), 4 * n) == 0) return false;  // (W is the newest step: drawn as it is)
    g_words_live.emplace_back(ea, std::vector<uint32_t>(cur, cur + n));
    memcpy(cur, out.data(), 4 * n);
    return true;
}
// A converted owner's 3D lines (the grappling hook's rope) are drawn at W through the words history; the frame interpolation's line
// blend (interp_fx.cpp line_packet: between the record pass's and this step's points, for every owner) is left out for
// it, or the history would keep its blended points (the rope's hand end was drawn up to 5.8 off Link's hand)
bool words_owns(uint32_t proc) { return g_words_on && proc && align_on() && converted(proc); }
// The Z-target arrow (2026-11-23): the Z-target arrow (dAttDraw_c, drawn by the play scene) is placed at its
// target's attention position (+0x390), which a converted target writes on every step: it sat on the newest step while the
// target is drawn at W, and the frame interpolation then blended the play scene's model between its record pass and this
// pass (a staircase: ~8-11 units on sub-step passes, ~2.8 on tick30 passes against the target's even 2.7). follow_begin
// puts the target's position of W in place (the words history) and has the models drawn until follow_end drawn as placed
// (no interpolation: the place is W already). Only for a converted target (an unconverted one is interpolated as before).
// WWHD_T60_ATTN_FOLLOW=0: off.
namespace {
const bool g_follow_on = [] {
    const char* e = getenv("WWHD_T60_ATTN_FOLLOW");
    return !e || atoi(e) != 0;
}();
int g_follow_exact = 0;
struct Follow { uint32_t ea, anim, anim_live; };
std::vector<Follow> g_follow_stack;  // the blocks follow_begin took (ea 0: not aligned)
// The arrow's animation frame (its McaMorf's frame, a f32) advances once a 30 Hz frame on the frame's FIRST step (the play
// scene runs first, the frame-level work on the frame's first step): its value on step F - 1/2 is frame F's. Kept per frame (stamp: the frame it belongs
// to, ceil of the step time) and drawn between frames at W; a loop wrap (the frame going back) is not blended.
struct AnimHist { uint64_t pass = 0; std::deque<std::pair<double, float>> steps; };
std::unordered_map<uint32_t, AnimHist> g_follow_anim;
}  // namespace
void follow_begin(uint32_t owner, uint32_t ea, uint32_t n, uint32_t anim) {
    const bool on = g_follow_on && words_owns(owner);
    Follow fo{0, 0, 0};
    if (on) {
        words_begin(owner, ea, n, 3 /* a 30 Hz value: its frame ends, blended at W */), g_follow_exact++;
        fo.ea = ea;
        if (anim) {
            AnimHist& h = g_follow_anim[anim];
            if (h.pass + 1 < g_pass) h = AnimHist{};
            h.pass = g_pass;
            const uint32_t live = ld32(anim);
            hist::stamp(h.steps, std::ceil(step_time() - hist::kEps), interp::joints::word_f(__builtin_bswap32(live)));
            const hist::Pick p = hist::pick(h.steps, w_now());
            const float a = h.steps[p.a].second, b = h.steps[p.b].second;
            const float v = p.f == 0.0f ? a : (b < a ? (p.f < 0.5f ? a : b) : a + p.f * (b - a));
            fo.anim = anim, fo.anim_live = live;
            st32(anim, __builtin_bswap32(interp::joints::f_word(v)));
        }
    }
    g_follow_stack.push_back(fo);
}
void follow_end() {
    if (g_follow_stack.empty()) return;
    const Follow fo = g_follow_stack.back();
    g_follow_stack.pop_back();
    if (fo.anim) st32(fo.anim, fo.anim_live);
    if (fo.ea) words_end(fo.ea), g_follow_exact--;
}
bool follow_exact() { return g_follow_exact > 0; }
void words_end(uint32_t ea) {
    for (auto it = g_words_live.rbegin(); it != g_words_live.rend(); ++it) {
        if (it->first != ea) continue;
        memcpy(ppc_ptr(ea), it->second.data(), 4 * it->second.size());
        g_words_live.erase(std::next(it).base());
        return;
    }
}
}  // namespace t60::draw
extern "C" __attribute__((visibility("default"))) void wwhd_t60_draw_words_begin(uint32_t owner, uint32_t ea, uint32_t n, int kind) {
    t60::draw::words_begin(owner, ea, n, kind);
}
extern "C" __attribute__((visibility("default"))) void wwhd_t60_draw_words_end(uint32_t ea) { t60::draw::words_end(ea); }
extern "C" __attribute__((visibility("default"))) void wwhd_t60_draw_follow_begin(uint32_t owner, uint32_t ea, uint32_t n, uint32_t anim) {
    t60::draw::follow_begin(owner, ea, n, anim);
}
extern "C" __attribute__((visibility("default"))) void wwhd_t60_draw_follow_end() { t60::draw::follow_end(); }

// decomp d/t60_whole.h (game build): this execute of `actor` (a whole-form actor, on a step before the tick30 step) was a
// picture step
// A carry a whole-form carrier gave Link on this pass's step (decomp d/actor/d_a_player_carry_t60.h)
extern "C" __attribute__((visibility("default"))) void wwhd_t60_link_carry(uint32_t actor, float dx, float dy, float dz, int dangle) {
    t60::draw::note_carry(actor, dx, dy, dz, dangle);
}
// decomp d/t60_basehold.h (above)
extern "C" __attribute__((visibility("default"))) void wwhd_t60_draw_basehold(uint32_t actor, uint32_t model, const float* held) {
    if (!actor || !model) return;
    t60::draw::BaseHold& h = t60::draw::g_basehold[actor];
    h.pass = t60::draw::g_pass, h.model = model;
    memcpy(h.held, held, sizeof h.held);
}
extern "C" __attribute__((visibility("default"))) void wwhd_t60_draw_basefollow(uint32_t actor, uint32_t holderId) {
    if (!actor) return;
    t60::draw::BaseFollow& f = t60::draw::g_basefollow[actor];
    f.pass = t60::draw::g_pass, f.holder_id = holderId;
}
extern "C" __attribute__((visibility("default"))) void wwhd_t60_whole_picture(uint32_t actor) {
    t60::draw::note_picture(actor);
}
