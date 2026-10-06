#include "mods/packages.h"
// Frame interpolation (60 fps output, game logic unchanged at 30 steps per second).
//
// With interpolation on, the main loop body runs every vsync (swap interval halved) but the game
// logic only on every other pass:
//   logic pass: logic advances N -> N+1; everything is drawn with the camera halfway (N, N+1)
//   hold pass:  no logic (no execute/create/delete, scene management, counters, audio);
//               everything is drawn again with the camera at N+1
// The painter at the start of each pass renders the previous pass's draw lists, so the screen shows
// halfway(N,N+1), N+1, halfway(N+1,N+2), N+2, ...
//
// Main loop functions in WWHD: see tools/recomp/hooks.txt and docs/decomp-notes.md.
// Camera layout (camera_draw, 024FFC40): near +0xCC, far +0xD0, fovy +0xD4, aspect +0xD8,
// eye +0xDC, center +0xE8, up +0xF4, bank (s16) +0x100.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "runtime.h"
#include "savestate.h"
#include "true60.h"


extern "C" {
void f_025F172C_orig(Cpu* c);  // main loop body
void f_0203593C_orig(Cpu* c);  // per-frame function (HD systems around the loop body)
void f_020315CC_orig(Cpu* c);  // audio frame callback (JAIZelBasic::gframeProcess)
void f_02032FE8_orig(Cpu* c);
void f_020357CC_orig(Cpu* c);
void f_025EE048_orig(Cpu* c);
void f_02756170_orig(Cpu* c);
void f_02617AF4_orig(Cpu* c);
void f_02614F74_orig(Cpu* c);
void f_0273841C_orig(Cpu* c);
void f_0270870C_orig(Cpu* c);
void f_0255E854_orig(Cpu* c);
void f_02738438_orig(Cpu* c);
void f_0278FEBC_orig(Cpu* c);
void f_027199C0_orig(Cpu* c);
void f_02618940_orig(Cpu* c);
void f_02728A74_orig(Cpu* c);
void f_02039834_orig(Cpu* c);

void f_025D42EC(Cpu* c);       // fapGm_Execute
void f_025DE788_orig(Cpu* c);  // fpcEx_Handler
void f_025DE024_orig(Cpu* c);  // fpcDt_Handler
void f_025E0EE4_orig(Cpu* c);  // fpcPi_Handler
void f_025DDCEC_orig(Cpu* c);  // fpcCt_Handler
void f_025D42C4_orig(Cpu* c);  // fapGm_After
void f_0200E6EC_orig(Cpu* c);  // cCt_Counter
void f_024FFC40_orig(Cpu* c);  // camera_draw
void f_025E1B44_orig(Cpu* c);  // mDoAud_getCameraInfo
void f_02027754_orig(Cpu* c);  // JAIZelBasic::setCameraPolygonPos
void f_0201EBA0_orig(Cpu* c);
void f_025E1988_orig(Cpu* c);
void f_025E19CC_orig(Cpu* c);
void f_025E1A04_orig(Cpu* c);
void f_025E1A40_orig(Cpu* c);
void f_025E1A7C_orig(Cpu* c);
void f_025E1AA4_orig(Cpu* c);
void f_027F55FC_orig(Cpu* c);  // J3DModel::viewCalc
void f_027F5018_orig(Cpu* c);  // J3DModel UBO update

}

namespace interp {

static std::atomic<bool> g_on{[] { const char* e = getenv("WWHD_INTERP"); return e && atoi(e) != 0 && !true60::enabled(); }()};
bool interp_on() { return g_on.load(std::memory_order_relaxed); }
// the 60 Hz pass structure below is used by both 60 fps modes: interpolation (30 Hz logic) and
// true 60 (true60.cpp: 60 Hz processes also execute on the in-between "hold" passes)
bool enabled() { return interp_on() || true60::enabled(); }
void set_enabled(bool v) {
    if (v) true60::set_enabled(false);
    g_on = v;
    LOG("[interp] frame interpolation %s", v ? "on (60 fps)" : "off");
}
// the 60 fps mode: 0 off, 1 frame interpolation, 2 true 60 (game logic at 60 steps per second)
int mode() { return true60::enabled() ? 2 : interp_on() ? 1 : 0; }
void set_mode(int m) {
    g_on = false;
    true60::set_enabled(false);
    if (m == 1) set_enabled(true);
    if (m == 2) true60::set_enabled(true);
}

// GX2SetSwapInterval: two paints per logic step need half the interval
uint32_t effective_swap_interval(uint32_t game) { return enabled() ? std::max<uint32_t>(1, game / 2) : game; }

namespace {
constexpr uint32_t kEye = 0xDC, kCenter = 0xE8, kUp = 0xF4, kFovy = 0xD4, kBank = 0x100;

struct CamState {
    float eye[3], center[3], up[3], fovy;
    int16_t bank;
};

CamState read_cam(uint32_t cam) {
    CamState s;
    for (int i = 0; i < 3; i++) {
        s.eye[i] = (float)ldf32(cam + kEye + 4 * i);
        s.center[i] = (float)ldf32(cam + kCenter + 4 * i);
        s.up[i] = (float)ldf32(cam + kUp + 4 * i);
    }
    s.fovy = (float)ldf32(cam + kFovy);
    s.bank = (int16_t)ld16(cam + kBank);
    return s;
}

void write_cam(uint32_t cam, const CamState& s) {
    for (int i = 0; i < 3; i++) {
        stf32(cam + kEye + 4 * i, s.eye[i]);
        stf32(cam + kCenter + 4 * i, s.center[i]);
        stf32(cam + kUp + 4 * i, s.up[i]);
    }
    stf32(cam + kFovy, s.fovy);
    st16(cam + kBank, (uint16_t)s.bank);
}

float dist(const float* a, const float* b) {
    float d = 0;
    for (int i = 0; i < 3; i++) d += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(d);
}

// halfway state; a cut (large jump) is not blended
// Halfway camera between the last exact step (a) and the new one (b).
// - Snaps (re-centring behind Link, doors, mode changes, cutscene cuts) are not blended: a step is a
//   snap when it is far larger than the camera's recent motion, or larger than an absolute limit.
// - Orbits around the look-at point blend the eye's direction and distance separately, so the
//   halfway eye stays on the arc instead of cutting the corner towards the target.
float g_last_step = 0;  // camera eye movement over the previous step

CamState blend(const CamState& a, const CamState& b) {
    static const float kCut = getenv("WWHD_INTERP_CUT") ? (float)atof(getenv("WWHD_INTERP_CUT")) : 800.0f;
    float step = std::max(dist(a.eye, b.eye), dist(a.center, b.center));
    float prev = g_last_step;
    g_last_step = step;
    bool snap = step > kCut || std::fabs(a.fovy - b.fovy) > 20.0f || (step > 60.0f && step > 4.0f * prev + 20.0f);
    if (snap) return b;
    CamState m;
    for (int i = 0; i < 3; i++) {
        m.center[i] = 0.5f * (a.center[i] + b.center[i]);
        m.up[i] = 0.5f * (a.up[i] + b.up[i]);
    }
    // eye = center + direction * distance, each blended on its own
    float da[3], db[3], la = 0, lb = 0;
    for (int i = 0; i < 3; i++) {
        da[i] = a.eye[i] - a.center[i];
        db[i] = b.eye[i] - b.center[i];
        la += da[i] * da[i];
        lb += db[i] * db[i];
    }
    la = std::sqrt(la);
    lb = std::sqrt(lb);
    if (la > 1e-3f && lb > 1e-3f) {
        float cosang = 0;
        for (int i = 0; i < 3; i++) cosang += (da[i] / la) * (db[i] / lb);
        if (cosang < 0.7071f) return b;  // view turned more than 45 degrees in one step: a snap
        // the camera's collision (trees, walls, bushes) pulls it in abruptly; a halfway distance could put
        // the in-between frame inside the obstacle, so abrupt distance changes are not blended
        if (std::fabs(la - lb) > 0.1f * std::max(la, lb)) return b;
        float dir[3], ld = 0;
        for (int i = 0; i < 3; i++) {
            dir[i] = da[i] / la + db[i] / lb;  // halfway direction (normalised below)
            ld += dir[i] * dir[i];
        }
        ld = std::sqrt(ld);
        float len = 0.5f * (la + lb);
        for (int i = 0; i < 3; i++) m.eye[i] = m.center[i] + (ld > 1e-3f ? dir[i] / ld : db[i] / lb) * len;
    } else {
        for (int i = 0; i < 3; i++) m.eye[i] = 0.5f * (a.eye[i] + b.eye[i]);
    }
    // up: normalised and made perpendicular to the halfway view direction, so the in-between frame
    // gets no extra roll (matters most when looking down from above, where small differences in up
    // turn into large twists)
    float fwd[3], lf = 0, lu = 0, d = 0;
    for (int i = 0; i < 3; i++) { fwd[i] = m.center[i] - m.eye[i]; lf += fwd[i] * fwd[i]; }
    lf = std::sqrt(lf);
    if (lf > 1e-3f) {
        for (int i = 0; i < 3; i++) { fwd[i] /= lf; d += m.up[i] * fwd[i]; }
        for (int i = 0; i < 3; i++) { m.up[i] -= d * fwd[i]; lu += m.up[i] * m.up[i]; }
        lu = std::sqrt(lu);
        if (lu > 0.1f) {
            for (int i = 0; i < 3; i++) m.up[i] /= lu;
        } else {
            for (int i = 0; i < 3; i++) m.up[i] = b.up[i];  // degenerate: keep the exact up vector
        }
    }
    m.fovy = 0.5f * (a.fovy + b.fovy);
    m.bank = (int16_t)(a.bank + (int16_t)(b.bank - a.bank) / 2);  // shortest way round
    return m;
}

uint64_t g_logic_steps = 0; // full logic steps (all passes without a 60 fps mode)
bool g_hold = false;        // hold pass: draw only, no logic
bool g_cam_blended = false; // camera_draw is drawing the blended (halfway) camera
bool g_logic_pass = false;  // logic pass with interpolation on: camera drawn halfway
bool g_hold_next = false;   // the next pass is a hold pass
bool g_hold_frame = false;  // inside the per-frame function on a hold pass
// paced interpolation: a logic pass that does not follow an in-between pass draws its step exactly.
// The previous states the halfway frames blend from (camera, model joints, effects) are recorded on
// the in-between pass; without one they are steps old (camera trailing behind Link, models jumping,
// a head blended from another step than its body).
bool g_exact_step = false;
// last camera state that was drawn normally, per camera process
struct Prev { uint32_t cam = 0; CamState s{}; bool valid = false; };
Prev g_prev[4];

Prev* prev_for(uint32_t cam) {
    for (auto& p : g_prev)
        if (p.cam == cam) return &p;
    for (auto& p : g_prev)
        if (!p.valid) { p.cam = cam; return &p; }
    g_prev[0] = Prev{cam};
    return &g_prev[0];
}
}  // namespace

}  // namespace interp

// camera_draw(camera_process_class*)
extern "C" void hook_024FFC40(Cpu* c) {
    using namespace interp;
    uint32_t cam = c->r[3];
    Prev* p = prev_for(cam);
    if (g_logic_pass && !true60::runs_60(cam)) {  // halfway between the step drawn last (N) and the new one (N+1)
        CamState cur = read_cam(cam);
        if (p->valid) write_cam(cam, blend(p->s, cur));
        g_cam_blended = p->valid;
        f_024FFC40_orig(c);
        g_cam_blended = false;
        write_cam(cam, cur);
        return;
    }
    p->s = read_cam(cam);  // exact step: remember it for the next halfway frame
    p->valid = true;
    if (g_hold && true60::enabled()) {  // true 60: the half pass's camera is a preview (true60.cpp)
        true60::camera_draw_preview(true);
        f_024FFC40_orig(c);
        true60::camera_draw_preview(false);
        return;
    }
    static const bool dbg = getenv("WWHD_T60_CAMDRAWLOG") != nullptr;  // debug: statics camera_draw writes
    if (dbg && g_hold) {
        static std::vector<uint32_t> before;
        static std::unordered_map<uint32_t, int> cnt;
        static int n = 0;
        const uint32_t lo = 0x10100000, hi = 0x10500000;
        before.assign((uint32_t*)ppc_ptr(lo), (uint32_t*)ppc_ptr(hi));
        f_024FFC40_orig(c);
        const uint32_t* cur = (const uint32_t*)ppc_ptr(lo);
        for (size_t i = 0; i < before.size(); i++)
            if (cur[i] != before[i]) cnt[lo + 4 * (uint32_t)i]++;
        if (++n % 200 == 0) {
            std::vector<std::pair<uint32_t, int>> v(cnt.begin(), cnt.end());
            std::sort(v.begin(), v.end());
            std::string o;
            uint32_t start = 0, last = 0; int c0 = 0;
            for (auto& [a, k] : v) {
                if (start && a == last + 4) { last = a; continue; }
                if (start) { char t[48]; snprintf(t, sizeof t, " %08X-%08X:%d", start, last + 3, c0); o += t; }
                start = last = a; c0 = k;
            }
            if (start) { char t[48]; snprintf(t, sizeof t, " %08X-%08X:%d", start, last + 3, c0); o += t; }
            LOG("[camdrawlog]%s", o.c_str());
        }
        return;
    }
    f_024FFC40_orig(c);
}

// Models. J3DModel::calc (027F4D5C, actor Execute or Draw) writes the world (joint) matrices:
// model +0x2C -> joint matrix block, block +0x10 -> world matrices (3x4, row-major), block +0x2C
// u16 count. In actor Draw, J3DModel::viewCalc (027F55FC, via mDoExt_modelUpdateDL /
// modelEntryDL) computes view-space draw matrices (027DE8A0) and queues the model for the
// "update_ubo" job thread, whose UBO update (027F5018) copies the world matrices into the uniform
// buffers the painter uses, concurrently with the rest of the main thread's draw.
// Hold pass: record each model's world matrices (step N+1, keyed by the joint matrix block).
// Logic pass: viewCalc and the UBO update run on blend(recorded step, current step); in between,
// and after each of them, the exact matrices are back in place, so game logic and attachments
// (swords, effects) keep reading the exact step.
namespace interp {
namespace {
constexpr uint32_t kMdlJoints = 0x2C, kJntMtx = 0x10, kJntNum = 0x2C;
constexpr int kMaxJoints = 1024;

struct ModelPrev {
    uint32_t mtx = 0;
    uint64_t pass = 0;  // hold pass it was recorded on
    std::vector<uint32_t> w;  // raw guest words, 12 per matrix
};
std::unordered_map<uint32_t, ModelPrev> g_models;
uint64_t g_hold_passes = 0;  // counts hold passes (record generation)
struct ModelStats { uint32_t blended = 0, fresh = 0, cut = 0; std::atomic<uint32_t> changed{0}; } g_mstats;
// halfway matrices waiting for the model's UBO update (update_ubo thread), per model
struct UboBlend { uint32_t mtx = 0; std::vector<uint32_t> exact, mid; };
std::mutex g_ubo_mu;
std::unordered_map<uint32_t, UboBlend> g_ubo;

float wf(uint32_t w) { return u32_as_f32(__builtin_bswap32(w)); }
uint32_t fw(float f) { return __builtin_bswap32(f32_as_u32(f)); }

// debug: WWHD_INTERP_MODEL_TRACE=n logs n draws of one model (the first with at least
// WWHD_INTERP_MODEL_TRACE_JOINTS joints, default 40): root joint translation as drawn
void trace_model(const char* what, uint32_t jnt, uint32_t n, const uint32_t* w) {
    static int left = getenv("WWHD_INTERP_MODEL_TRACE") ? atoi(getenv("WWHD_INTERP_MODEL_TRACE")) : 0;
    static const uint32_t min_joints = getenv("WWHD_INTERP_MODEL_TRACE_JOINTS") ? atoi(getenv("WWHD_INTERP_MODEL_TRACE_JOINTS")) : 40;
    static uint32_t which = 0;
    if (left <= 0 || n < min_joints || (which && which != jnt)) return;
    which = jnt;
    left--;
    LOG("[interp] model %08X (%u joints, mtx %08X) %-7s root %.2f %.2f %.2f", jnt, n, ld32(jnt + kJntMtx), what, wf(w[3]), wf(w[7]), wf(w[11]));
}

// rotation part of a 3x4 matrix (columns = scaled axes) to a unit quaternion; false if it is not a
// rotation times a positive scale per axis (shear, mirror, degenerate)
bool to_quat(const float* m, float* s, float* q) {
    float r[3][3];
    for (int j = 0; j < 3; j++) {
        s[j] = std::sqrt(m[j] * m[j] + m[4 + j] * m[4 + j] + m[8 + j] * m[8 + j]);
        if (s[j] < 1e-6f) return false;
        for (int i = 0; i < 3; i++) r[i][j] = m[4 * i + j] / s[j];
    }
    for (int a = 0; a < 3; a++)
        for (int b = a + 1; b < 3; b++)
            if (std::fabs(r[0][a] * r[0][b] + r[1][a] * r[1][b] + r[2][a] * r[2][b]) > 0.02f) return false;
    float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (det < 0.9f) return false;
    float tr = r[0][0] + r[1][1] + r[2][2];
    if (tr > 0) {
        float k = 0.5f / std::sqrt(tr + 1.0f);
        q[0] = 0.25f / k; q[1] = (r[2][1] - r[1][2]) * k; q[2] = (r[0][2] - r[2][0]) * k; q[3] = (r[1][0] - r[0][1]) * k;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        float k = 2.0f * std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]);
        q[0] = (r[2][1] - r[1][2]) / k; q[1] = 0.25f * k; q[2] = (r[0][1] + r[1][0]) / k; q[3] = (r[0][2] + r[2][0]) / k;
    } else if (r[1][1] > r[2][2]) {
        float k = 2.0f * std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]);
        q[0] = (r[0][2] - r[2][0]) / k; q[1] = (r[0][1] + r[1][0]) / k; q[2] = 0.25f * k; q[3] = (r[1][2] + r[2][1]) / k;
    } else {
        float k = 2.0f * std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]);
        q[0] = (r[1][0] - r[0][1]) / k; q[1] = (r[0][2] + r[2][0]) / k; q[2] = (r[1][2] + r[2][1]) / k; q[3] = 0.25f * k;
    }
    return true;
}

// halfway between two 3x4 matrices: translation and scale linear, rotation slerp (nlerp at 1/2 is
// exact); anything that is not rotation x scale is blended element-wise
void blend_mtx(const float* a, const float* b, float* out) {
    float sa[3], sb[3], qa[4], qb[4];
    for (int i = 0; i < 3; i++) out[4 * i + 3] = 0.5f * (a[4 * i + 3] + b[4 * i + 3]);
    if (!to_quat(a, sa, qa) || !to_quat(b, sb, qb)) {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) out[4 * i + j] = 0.5f * (a[4 * i + j] + b[4 * i + j]);
        return;
    }
    float d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    float sg = d < 0 ? -1.0f : 1.0f, q[4], n = 0;
    for (int k = 0; k < 4; k++) { q[k] = qa[k] + sg * qb[k]; n += q[k] * q[k]; }
    n = 1.0f / std::sqrt(n);
    float w = q[0] * n, x = q[1] * n, y = q[2] * n, z = q[3] * n;
    const float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                           {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                           {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
    for (int j = 0; j < 3; j++) {
        float s = 0.5f * (sa[j] + sb[j]);
        for (int i = 0; i < 3; i++) out[4 * i + j] = r[i][j] * s;
    }
}
}  // namespace
}  // namespace interp

// J3DModel::viewCalc(J3DModel*)
extern "C" void hook_027F55FC(Cpu* c) {
    using namespace interp;
    static const bool off = getenv("WWHD_INTERP_MODELS") && !atoi(getenv("WWHD_INTERP_MODELS"));  // debug
    if (!enabled() || off || (!g_hold && (true60::drawing_60() || g_exact_step))) {
        f_027F55FC_orig(c);
        return;
    }
    uint32_t model = c->r[3];
    uint32_t jnt = ld32(model + kMdlJoints);
    uint32_t mtx = jnt ? ld32(jnt + kJntMtx) : 0;
    uint32_t n = jnt ? ld16(jnt + kJntNum) : 0;
    if (!mtx || n == 0 || n > kMaxJoints) {
        f_027F55FC_orig(c);
        return;
    }
    const uint32_t words = 12 * n;
    const uint32_t* cur = (const uint32_t*)ppc_ptr(mtx);  // guest (big-endian) words
    if (g_hold) {  // exact step: remember it for the next halfway frame
        ModelPrev& p = g_models[jnt];
        p.mtx = mtx;
        p.pass = g_hold_passes;
        p.w.assign(cur, cur + words);
        trace_model("exact", jnt, n, cur);
        f_027F55FC_orig(c);
        return;
    }
    // logic pass: halfway between the step drawn last and the new one
    auto it = g_models.find(jnt);
    if (it == g_models.end() || it->second.mtx != mtx || it->second.w.size() != words ||
        it->second.pass + 1 < g_hold_passes) {  // new model, or not drawn on the last hold pass
        g_mstats.fresh++;
        trace_model("new", jnt, n, cur);
        f_027F55FC_orig(c);
        return;
    }
    const std::vector<uint32_t>& prev = it->second.w;
    if (memcmp(prev.data(), cur, 4 * words) == 0) {  // not moving
        trace_model("still", jnt, n, cur);
        f_027F55FC_orig(c);
        return;
    }
    static const float kCut = getenv("WWHD_INTERP_MODEL_CUT") ? (float)atof(getenv("WWHD_INTERP_MODEL_CUT")) : 400.0f;
    std::vector<uint32_t> saved(cur, cur + words);
    std::vector<uint32_t> mid(words);
    for (uint32_t m = 0; m < n; m++) {
        float a[12], b[12], o[12];
        for (int k = 0; k < 12; k++) { a[k] = wf(prev[12 * m + k]); b[k] = wf(saved[12 * m + k]); }
        float dx = a[3] - b[3], dy = a[7] - b[7], dz = a[11] - b[11];
        if (!(dx * dx + dy * dy + dz * dz <= kCut * kCut)) {  // teleport (or NaN): show the new step
            g_mstats.cut++;
            trace_model("cut", jnt, n, cur);
            f_027F55FC_orig(c);
            return;
        }
        blend_mtx(a, b, o);
        for (int k = 0; k < 12; k++) mid[12 * m + k] = fw(o[k]);
    }
    g_mstats.blended++;
    trace_model("halfway", jnt, n, mid.data());
    memcpy(ppc_ptr(mtx), mid.data(), 4 * words);
    f_027F55FC_orig(c);
    memcpy(ppc_ptr(mtx), saved.data(), 4 * words);
    std::lock_guard<std::mutex> lk(g_ubo_mu);
    g_ubo[model] = UboBlend{mtx, std::move(saved), std::move(mid)};
}

// J3DModel UBO update (027F5018), run by the "update_ubo" job thread while the main thread is
// still drawing: copies the world matrices into the model's uniform buffers. On logic passes it
// runs on the halfway matrices prepared by viewCalc, then the exact ones are put back.
extern "C" void hook_027F5018(Cpu* c) {
    using namespace interp;
    uint32_t model = c->r[3];
    UboBlend b;
    {
        std::lock_guard<std::mutex> lk(g_ubo_mu);
        auto it = g_ubo.find(model);
        if (!enabled()) g_ubo.clear();  // switched off since viewCalc
        if (!enabled() || it == g_ubo.end()) {
            f_027F5018_orig(c);
            return;
        }
        b = std::move(it->second);
        g_ubo.erase(it);
    }
    const size_t bytes = 4 * b.exact.size();
    if (memcmp(ppc_ptr(b.mtx), b.exact.data(), bytes) != 0) {  // changed since viewCalc
        g_mstats.changed++;
        f_027F5018_orig(c);
        return;
    }
    memcpy(ppc_ptr(b.mtx), b.mid.data(), bytes);
    f_027F5018_orig(c);
    memcpy(ppc_ptr(b.mtx), b.exact.data(), bytes);
}

// Per-frame function (0203593C): WWHD's own per-frame systems (HD menus, system UI, lighting setup)
// around the loop body. On hold passes the whole frame is held back and only redrawn, so these
// systems stay in step with the game logic at 30 steps per second.
namespace interp { void fx_pass_start(); }  // interp_fx.cpp: puts back the values drawn halfway
namespace interp {
void fx_ss_reset();
// a save state was loaded: nothing may blend across the jump
void ss_reset() {
    for (auto& p : g_prev) p = Prev{};
    g_last_step = 0;
    g_models.clear();
    {
        std::lock_guard<std::mutex> lk(g_ubo_mu);
        g_ubo.clear();
    }
    g_hold_passes += 8;  // step-stamped histories (models, effects) no longer match
    g_hold_next = false;
    fx_ss_reset();
    true60::ss_reset();
}
// Paced interpolation (WWHD_INTERP_PACED=1, the default on Android): the in-between pass is drawn
// only when it fits before the next logic step is due (33.3 ms after the last one, measured with
// the passes' recent durations); otherwise it is dropped and the next step waits for its time. The
// game then always advances 30 steps a second, and the picture gets 60 frames a second where the
// device draws them fast enough and fewer where it does not. Without pacing, every logic step is
// followed by an in-between pass, and a device that draws fewer than 60 frames a second runs the
// whole game slower than real time.
// The settings overlay switches it ("Keep game speed", saved with the graphics options); the
// variable sets it at start and wins over the saved value.
static std::atomic<bool> g_paced{[] {
    const char* e = getenv("WWHD_INTERP_PACED");
#ifdef __ANDROID__
    return !e || atoi(e) != 0;
#else
    return e && atoi(e) != 0;
#endif
}()};
static bool paced() { return g_paced.load(std::memory_order_relaxed); }
bool paced_interpolation() { return paced(); }
void set_paced_interpolation(bool on) {
    if (g_paced.exchange(on) != on) LOG("[interp] paced interpolation %s", on ? "on (keeps the game's speed)" : "off");
}
// share of in-between frames drawn over the last 60 decisions (performance overlay), -1 before any
static std::atomic<float> g_paced_share{-1};
float paced_drawn_share() { return paced() && interp_on() ? g_paced_share.load(std::memory_order_relaxed) : -1; }
// The decision is taken at the end of each logic pass, before the frame's controller read: that
// read repeats the previous sample when an in-between pass follows (repeat_input), so deciding later
// left every read a repeat while all in-between passes were dropped (the controller stopped).
using pace_clock = std::chrono::steady_clock;
static pace_clock::time_point g_last_logic{}, g_last_entry{};
static pace_clock::duration g_slept{}, g_pass_avg = std::chrono::milliseconds(16);
// in-between passes alone (they cost less than logic passes, and the decision is about them), and
// the margin they must leave, set from the game's speed (paced_pass_start): a device at its limit
// draws fewer of them, the game keeps 30 steps a second
static pace_clock::duration g_hold_avg = std::chrono::milliseconds(16), g_margin{};
static bool g_wait_step = false;  // no in-between pass: the next logic pass waits for its time
static bool g_pass_after_logic = false;  // this pass is a logic pass that follows a logic pass
static std::atomic<double> g_render_ms{0.0};  // render thread CPU ms per frame (0: not measured)
static std::atomic<uint64_t> g_render_readings{0};
static bool g_render_full = false;
static uint64_t g_paced_dropped = 0, g_paced_holds = 0, g_paced_late = 0;
constexpr auto kPacedStep = std::chrono::nanoseconds(33'333'333);
// start of every pass: the last pass's own duration, and the wait before a logic pass that follows
// another logic pass directly
static void paced_pass_start() {
    static bool previousHold = false;
    if (!paced() || !interp_on()) { g_exact_step = false; previousHold = false; return; }
    if (!g_hold_next) g_exact_step = !previousHold;  // this logic pass: blend only after a hold
    const bool endedHold = previousHold;
    previousHold = g_hold_next;
    g_pass_after_logic = !g_hold_next && !endedHold;
    const auto now = pace_clock::now();
    if (g_last_entry != pace_clock::time_point{}) {
        const auto pass = now - g_last_entry - g_slept;
        g_pass_avg = (g_pass_avg * 3 + pass) / 4;
        if (endedHold) g_hold_avg = (g_hold_avg * 3 + pass) / 4;
    }
    g_last_entry = now;
    g_slept = {};
    if (g_hold_next) return;
    // The margin follows the game's speed, counted over each second: below 29.5 steps with
    // in-between passes drawn, +4 ms (up to a whole step: none drawn, 30 exact steps as without
    // interpolation); at 29.7 or more, -2 ms. (The start of single steps varies by several ms on
    // the vsync grid, so timing them flagged steps late while the game kept 30 a second.)
    {
        using std::chrono::milliseconds;
        static pace_clock::time_point windowStart{};
        static int steps = 0, holds = 0;
        if (windowStart == pace_clock::time_point{}) windowStart = now;
        ++steps;
        holds += endedHold;
        const auto span = now - windowStart;
        if (span >= std::chrono::seconds(1)) {
            const double rate = steps / std::chrono::duration<double>(span).count();
            if (holds && rate < 29.5) {
                g_margin = std::min<pace_clock::duration>(g_margin + milliseconds(4), kPacedStep);
                g_paced_late++;
            } else if (rate >= 29.7 && !g_render_full) {
                g_margin = std::max<pace_clock::duration>(g_margin - milliseconds(2), {});
            }
            windowStart = now;
            steps = holds = 0;
        }
    }
    // A logic pass right after a logic pass: its swap waits the game's own interval (flip_gap), on
    // the vsync grid as without interpolation. (Sleeping until 33.3 ms after the last step instead
    // was off that grid: the swap then waited for the next vsync, and a device that dropped every
    // in-between pass ran 27 steps a second.)
    g_wait_step = false;
    g_last_logic = pace_clock::now();
}
// end of a logic pass: an in-between pass follows only if it fits before the next step is due
static void paced_after_logic() {
    if (!paced() || !interp_on()) { g_hold_next = true; g_wait_step = false; return; }
    const auto elapsed = pace_clock::now() - g_last_logic;
    // the render thread draws both frames of a step: at 14.5 ms or more per frame (its CPU time,
    // Android: perf_hint.cpp) two do not fit in 33.3 ms, whatever the game thread's passes take
    // (one reading a second; back below 13 ms for 3 readings in a row before they are tried again:
    // a single lighter second, such as a camera turn, brought them back into a scene too heavy)
    const double renderMs = g_render_ms.load(std::memory_order_relaxed);
    static uint64_t seenReading = 0;
    static int calm = 0;
    if (const uint64_t r = g_render_readings.load(std::memory_order_relaxed); r != seenReading) {
        seenReading = r;
        if (renderMs >= 14.5) { g_render_full = true; calm = 0; }
        else if (renderMs < 13.0) { if (++calm >= 3) g_render_full = false; }
        else calm = 0;
    }
    const bool fits = !g_render_full && elapsed + g_hold_avg + g_margin <= kPacedStep;
    g_hold_next = fits;
    g_wait_step = !fits;
    // without in-between passes their average is not measured: let it come down (0.5 ms a step)
    // so that one slow pass (a loading screen) does not keep them off for good
    if (!fits && g_hold_avg > std::chrono::milliseconds(4)) g_hold_avg -= std::chrono::microseconds(500);
    (fits ? g_paced_holds : g_paced_dropped)++;
    static unsigned recentHolds = 0, recentN = 0;
    recentHolds += fits;
    if (++recentN == 60) {
        g_paced_share.store(recentHolds / 60.0f, std::memory_order_relaxed);
        recentHolds = recentN = 0;
    }
    if (g_paced_dropped + g_paced_holds >= 300) {
        using ms = std::chrono::duration<double, std::milli>;
        LOG("[interp] paced: %.0f%% of in-between frames drawn (pass %.1f ms, in-between %.1f ms, margin %.1f ms, %llu slow seconds, render thread %.1f ms a frame)",
            100.0 * g_paced_holds / double(g_paced_dropped + g_paced_holds), ms(g_pass_avg).count(),
            ms(g_hold_avg).count(), ms(g_margin).count(), (unsigned long long)g_paced_late, renderMs);
        g_paced_dropped = g_paced_holds = g_paced_late = 0;
    }
}
void set_render_ms(double ms) {
    g_render_ms.store(ms, std::memory_order_relaxed);
    g_render_readings.fetch_add(1, std::memory_order_relaxed);
}
// vsyncs between the previous flip and the flip of a swap made now: the game's own interval for a
// logic pass after a logic pass (paced, its in-between pass dropped), half of it otherwise
uint32_t flip_gap(uint32_t game) {
    if (paced() && interp_on() && g_pass_after_logic) return game;
    return effective_swap_interval(game);
}
}  // namespace interp

namespace mods { void cheats_service(); }  // mods/cheats.cpp

extern "C" void hook_0203593C(Cpu* c) {
    using namespace interp;
    fx_pass_start();
    ss::service(c);  // save states: exact values are back in guest memory, all other threads idle
    mods::cheats_service();
    // test aid: WWHD_INTERP_AT_STEP=n switches interpolation on after n frames
    static uint64_t passes = 0;
    static const uint64_t at = getenv("WWHD_INTERP_AT_STEP") ? strtoull(getenv("WWHD_INTERP_AT_STEP"), nullptr, 10) : 0;
    if (at && ++passes == at) set_enabled(true);
    static uint64_t at60 = getenv("WWHD_TRUE60_AT_STEP") ? strtoull(getenv("WWHD_TRUE60_AT_STEP"), nullptr, 10) : 0;
    static uint64_t passes60 = 0;
    if (at60 && ++passes60 == at60) set_mode(2);
    paced_pass_start();
    true60::new_pass();
    true60::pass_begin(!enabled() || !g_hold_next);  // full pass: take back Link's half-pass preview
    if (!enabled() || !g_hold_next) g_logic_steps++;
    if (!enabled()) {
        g_hold_next = false;
        f_0203593C_orig(c);
        return;
    }
    if (g_hold_next) {
        // hold pass: the per-frame function runs, but its children only per kHoldRun and the loop
        // body draws without logic
        g_hold = true;
        g_hold_frame = true;
        g_hold_passes++;
        f_0203593C_orig(c);
        g_hold_frame = false;
        g_hold = false;
        g_hold_next = false;
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g_ubo_mu);
        g_ubo.clear();  // not updated last time (not drawn)
    }
    f_0203593C_orig(c);  // logic pass (the loop body hook marks it)
    paced_after_logic();  // g_hold_next: an in-between pass follows (always, unless paced)
    static uint64_t n = 0, t0 = timebase::now();
    if (++n % 300 == 0) {
        uint64_t t = timebase::now();
        LOG("[interp] %.1f logic steps/s; models per step: %.1f blended, %.1f new, %.1f cut, %.1f changed before UBO update",
            300.0 * timebase::kTicksPerSec / (double)(t - t0), g_mstats.blended / 300.0, g_mstats.fresh / 300.0, g_mstats.cut / 300.0,
            g_mstats.changed.exchange(0) / 300.0);
        t0 = t;
        g_mstats.blended = g_mstats.fresh = g_mstats.cut = 0;
        for (auto it = g_models.begin(); it != g_models.end();)  // models no longer drawn
            it = it->second.pass + 2 < g_hold_passes ? g_models.erase(it) : std::next(it);
    }
}

// main loop body (inside the per-frame function): logic pass -> camera drawn halfway;
// hold pass -> only fapGm_Execute, whose logic parts are skipped (hooks below)
extern "C" void hook_025F172C(Cpu* c) {
    using namespace interp;
    if (g_hold_frame) {
        f_025D42EC(c);
        return;
    }
    g_logic_pass = enabled() && !g_exact_step;  // (an exact step draws like interpolation off)
    f_025F172C_orig(c);
    g_logic_pass = false;

}

// children of the per-frame function on hold passes: run (bit set) or skip.
// debug: WWHD_HOLD_RUN=mask overrides the default
// Only calls made directly by the per-frame function (return address inside 0203593C..02035A78) are
// held back; the same functions are also called from elsewhere (e.g. 025EE048 as a getter inside
// drawing code), and those calls must always run.
static Cpu* g_hold_child_cpu = nullptr;
static bool called_from_frame_function() {
    uint32_t lr = g_hold_child_cpu ? g_hold_child_cpu->lr : 0;
    return lr > 0x0203593C && lr < 0x02035A78;
}
static bool hold_skip_child(int i) {
    // children 7-14 (after the loop body: frame setup, lighting, display) run; 1, 4, 5 (HD menus and
    // systems) are held. Child 0 always runs: it only reads a system state, and a non-zero result
    // makes the per-frame function skip the whole frame (a skipped call would leave its argument, a
    // pointer, in r3, so hold passes drew nothing and every step was shown twice).
    // Also run: 2-3 (025EE048/02756170: heap setup the drawing needs) and 6 (0273841C: clears the
    // light counts of the HD light manager at *101F8A20 (+0x10..+0x1C), which dKy_setLight (child 8)
    // fills via 0273862C and child 9 hands to the renderer; held, every point light was added a
    // second time on hold passes: candle light twice as bright on every other frame at 60 fps).
    static const uint32_t run = (getenv("WWHD_HOLD_RUN") ? (uint32_t)strtoul(getenv("WWHD_HOLD_RUN"), nullptr, 0) : 0x7FCC) | 1;
    return interp::g_hold_frame && !(run & (1u << i)) && called_from_frame_function();
}
extern "C" void hook_02032FE8(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(0)) f_02032FE8_orig(c); }
extern "C" void hook_020357CC(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(1)) f_020357CC_orig(c); }
extern "C" void hook_025EE048(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(2)) f_025EE048_orig(c); }
extern "C" void hook_02756170(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(3)) f_02756170_orig(c); }
extern "C" void hook_02617AF4(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(4)) f_02617AF4_orig(c); }
extern "C" void hook_02614F74(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(5)) f_02614F74_orig(c); }
extern "C" void hook_0273841C(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(6)) f_0273841C_orig(c); }
extern "C" void hook_0270870C(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(7)) f_0270870C_orig(c); }
namespace interp { void light_trace_add(const char* fmt, ...); }
// dKy_setLight (child 8, every pass) keeps state across calls: the candle flicker target (static,
// smoothed with cLib_addCalc2 toward a random value per call), the eflight target and the main
// light position (lightStatusPt->mPos, cLib_addCalc). On hold passes it would smooth a second time
// per logic step (light changing on every drawn frame, with a different amplitude than at 30 fps).
// The state from before the logic pass's call is put back first, so with the random numbers
// replayed (interp_fx.cpp) the hold pass gets exactly the logic pass's light.
namespace {
constexpr uint32_t kSetLightTarget = 0x101E8EC8, kSetLightEfTarget = 0x101E8ECC, kLightStatusPt = 0x101E8CC8;
struct SetLightState {
    uint64_t step = ~0ull;
    uint32_t target, ef_target, status, pos[3];
} g_setlight;
}  // namespace
extern "C" void hook_0255E854(Cpu* c) {
    g_hold_child_cpu = c;
    if (hold_skip_child(8)) return;
    if (interp::enabled() && called_from_frame_function()) {
        SetLightState& s = g_setlight;
        uint32_t st = ld32(kLightStatusPt);
        if (!interp::g_hold_frame) {
            s.step = interp::g_logic_steps;
            s.target = ld32(kSetLightTarget);
            s.ef_target = ld32(kSetLightEfTarget);
            s.status = st;
            for (int i = 0; i < 3; i++) s.pos[i] = st ? ld32(st + 4 * i) : 0;
        } else if (s.step == interp::g_logic_steps && s.status == st) {
            st32(kSetLightTarget, s.target);
            st32(kSetLightEfTarget, s.ef_target);
            for (int i = 0; i < 3 && st; i++) st32(st + 4 * i, s.pos[i]);
        }
    }
    // true 60: the hold pass ran with the preview camera; the next full pass continues from the
    // state the logic pass left, as the 30 fps game's next step does
    static SetLightState after_logic;
    if (true60::enabled() && called_from_frame_function() && !interp::g_hold_frame && after_logic.step + 1 == interp::g_logic_steps) {
        uint32_t st2 = ld32(kLightStatusPt);
        if (after_logic.status == st2) {
            st32(kSetLightTarget, after_logic.target);
            st32(kSetLightEfTarget, after_logic.ef_target);
            for (int i = 0; i < 3 && st2; i++) st32(st2 + 4 * i, after_logic.pos[i]);
        }
    }
    f_0255E854_orig(c);
    if (true60::enabled() && called_from_frame_function() && !interp::g_hold_frame) {
        uint32_t st2 = ld32(kLightStatusPt);
        after_logic.step = interp::g_logic_steps;
        after_logic.target = ld32(kSetLightTarget);
        after_logic.ef_target = ld32(kSetLightEfTarget);
        after_logic.status = st2;
        for (int i = 0; i < 3; i++) after_logic.pos[i] = st2 ? ld32(st2 + 4 * i) : 0;
    }
    uint32_t st = ld32(0x101E8CC8);  // lightStatusPt
    interp::light_trace_add(" setLight t%.2f r%u p(%.1f,%.1f,%.1f)", ldf32(0x101E8EC8), st ? ld8(st + 0x18) : 0, st ? ldf32(st) : 0.0,
                            st ? ldf32(st + 4) : 0.0, st ? ldf32(st + 8) : 0.0);
}
extern "C" void hook_02738438(Cpu* c) {
    g_hold_child_cpu = c;
    if (hold_skip_child(9)) return;
    uint32_t o = c->r[3];
    interp::light_trace_add(" hdl[%08X %08X %08X %08X]", ld32(o + 0x10), ld32(o + 0x14), ld32(o + 0x18), ld32(o + 0x1C));
    f_02738438_orig(c);
}
extern "C" void hook_0278FEBC(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(10)) f_0278FEBC_orig(c); }
extern "C" void hook_027199C0(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(11)) f_027199C0_orig(c); }
extern "C" void hook_02618940(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(12)) f_02618940_orig(c); }
extern "C" void hook_02728A74(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(13)) f_02728A74_orig(c); }
extern "C" void hook_02039834(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(14)) f_02039834_orig(c); }


// logic parts of fpcM_Management / fapGm_Execute, skipped on hold passes
// debug: WWHD_INTERP_RUN=mask runs selected parts on hold passes too
// (1 execute, 2 delete, 4 priority, 8 create, 16 fapGm_After, 32 counter)
static bool skip(int bit) {
    static const int run = getenv("WWHD_INTERP_RUN") ? atoi(getenv("WWHD_INTERP_RUN")) : 0;
    return interp::g_hold && !(run & bit);
}
static bool g_in_execute = false;  // inside fpcEx_Handler (actor Execute): logic, not drawing
namespace mods { void after_execute(Cpu* c, uint32_t execute_fn); }  // mods/turbo.cpp
extern "C" void hook_025DE788(Cpu* c) {
    if (skip(1) && !true60::enabled()) return;  // true 60: the per-process gate decides (true60.cpp)
    g_in_execute = true;
    uint32_t execute_fn = c->r[3];
    f_025DE788_orig(c);
    mods::after_execute(c, execute_fn);  // quick doors / fast scene changes: extra steps (full passes only)
    if (!interp::g_hold_frame) mods::packages::frame(interp::g_logic_steps);
    g_in_execute = false;
}
extern "C" void hook_025DE024(Cpu* c) { if (!skip(2)) f_025DE024_orig(c); }
extern "C" void hook_025E0EE4(Cpu* c) { if (skip(4)) c->r[3] = 1; else f_025E0EE4_orig(c); }
extern "C" void hook_025DDCEC(Cpu* c) { if (skip(8)) c->r[3] = 1; else f_025DDCEC_orig(c); }
extern "C" void hook_025D42C4(Cpu* c) { if (!skip(16)) f_025D42C4_orig(c); }
extern "C" void hook_0200E6EC(Cpu* c) { if (!skip(32)) f_0200E6EC_orig(c); }

namespace interp {
// The pads are read at the end of every frame. JUTGamePad derives "pressed this frame" from
// consecutive reads, so a change first seen on the read after a logic pass would be spent on a pass
// without logic. While interpolating, that read repeats the previous sample (see VPADRead /
// KPADReadEx), so every change reaches the logic exactly once.
void trace_read(const char* who) {
    // debug: WWHD_TRACE_INPUT_PHASE=n logs n controller reads with the pass they happen in
    static int n = getenv("WWHD_TRACE_INPUT_PHASE") ? atoi(getenv("WWHD_TRACE_INPUT_PHASE")) : 0;
    if (n > 0 && enabled()) {
        n--;
        LOG("[interp] %s read: logic_pass=%d hold=%d hold_next=%d", who, g_logic_pass, g_hold, g_hold_next);
    }
}
bool hold_pass() { return g_hold; }
// full (30 Hz) logic steps so far: test scenarios run on game time, so frame-time hitches (which
// slow the frame-locked game down) don't shift the input against the game
uint64_t logic_steps() { return g_logic_steps; }
// pass state for the effect blending (interp_fx.cpp)
bool logic_pass() { return g_logic_pass; }
bool in_execute() { return g_in_execute; }
uint64_t hold_pass_count() { return g_hold_passes; }
const char* phase_name() { return !enabled() ? "interp off" : g_hold ? "IN-BETWEEN" : g_logic_pass ? "logic" : "other"; }
// true 60: the sticks are read fresh on every pass (60 Hz processes use them on the half passes);
// buttons still change on full passes only, so every press reaches the 30 Hz processes and menus
bool fresh_sticks() { return true60::enabled(); }
bool repeat_input() {
    static const bool off = getenv("WWHD_INTERP_NO_REPEAT") != nullptr;  // debug
    return enabled() && g_hold_next && !off;
}
}  // namespace interp

// Sound effect starts (JAIZelBasic::seStart core and the mDoAud_* start wrappers). Some sounds are
// started from drawing code (animation-linked effects); an in-between frame only redraws, so a
// sound started there would play a second time. Suppressed while the hold pass draws, and with true
// 60 on the whole half pass: its executes are previews that the next full pass takes back and runs
// again (true60.cpp), so every sound starts once, on the full pass of the 30 fps game's step.
// debug: WWHD_SE_STATS=1 logs calls per second by frame phase every 5 s
// debug: WWHD_SE_TRACE=path logs every start that is not suppressed: logic step, full pass (1/0),
// wrapper index, r4 (the sound id for the seStart wrappers), caller (true60 comparisons)
static void se_trace(int fn, Cpu* c) {
    static FILE* f = getenv("WWHD_SE_TRACE") ? fopen(getenv("WWHD_SE_TRACE"), "w") : nullptr;
    if (!f || interp::g_hold) return;
    fprintf(f, "%llu %d %d %08X %08X\n", (unsigned long long)interp::g_logic_steps, interp::g_hold ? 0 : 1, fn, c->r[4], c->lr);
    fflush(f);
}
static void se_stat(int fn) {
    static const bool on = getenv("WWHD_SE_STATS") != nullptr;
    if (!on) return;
    static uint32_t n[7][4];
    int ph = !interp::enabled() ? 0 : interp::g_hold ? 1 : interp::g_logic_pass ? 2 : 3;
    n[fn][ph]++;
    static uint64_t t0 = timebase::now();
    uint64_t t = timebase::now();
    if (t - t0 > 5 * timebase::kTicksPerSec) {
        static const char* fns[7] = {"core", "w1988", "w19CC", "w1A04", "w1A40", "w1A7C", "w1AA4"};
        char buf[700];
        int k = snprintf(buf, sizeof buf, "[se] starts per 5 s [off/in-between/logic/other]:");
        for (int f = 0; f < 7; f++)
            if (n[f][0] + n[f][1] + n[f][2] + n[f][3])
                k += snprintf(buf + k, sizeof buf - k, " %s=%u/%u/%u/%u", fns[f], n[f][0], n[f][1], n[f][2], n[f][3]);
        LOG("%s", buf);
        memset(n, 0, sizeof n);
        t0 = t;
    }
}
extern "C" void hook_0201EBA0(Cpu* c) { se_stat(0); se_trace(0, c); if (interp::g_hold) { c->r[3] = 0; return; } f_0201EBA0_orig(c); }
extern "C" void hook_025E1988(Cpu* c) { se_stat(1); se_trace(1, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1988_orig(c); }
extern "C" void hook_025E19CC(Cpu* c) { se_stat(2); se_trace(2, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E19CC_orig(c); }
extern "C" void hook_025E1A04(Cpu* c) { se_stat(3); se_trace(3, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1A04_orig(c); }
extern "C" void hook_025E1A40(Cpu* c) { se_stat(4); se_trace(4, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1A40_orig(c); }
extern "C" void hook_025E1A7C(Cpu* c) { se_stat(5); se_trace(5, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1A7C_orig(c); }
extern "C" void hook_025E1AA4(Cpu* c) { se_stat(6); se_trace(6, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1AA4_orig(c); }

// The sound engine takes its listener from camera_draw. Feeding it the halfway camera as well as the
// exact one makes the listener hop every frame (Doppler/panning wobble: doubled-sounding effects),
// so only the exact camera (hold pass, once per logic step) updates it.
// mDoAud_getCameraInfo(eye, viewMtx, id): the sound engine keeps these POINTERS and reads them from
// its own thread (panning/volume of positional sounds such as the waves). The camera's eye and
// j3dSys's view matrix alternate between the halfway and the exact camera at 60 fps, so the engine
// gets private copies instead, refreshed only from the exact camera.
extern "C" void hook_025E1B44(Cpu* c) {
    if (interp::g_cam_blended) return;
    if (!interp::enabled()) {
        f_025E1B44_orig(c);
        return;
    }
    const uint32_t eye = mem::fixed_slot(mem::kFixInterpEye), mtx = mem::fixed_slot(mem::kFixInterpMtx);
    for (int i = 0; i < 3; i++) st32(eye + 4 * i, ld32(c->r[3] + 4 * i));
    for (int i = 0; i < 12; i++) st32(mtx + 4 * i, ld32(c->r[4] + 4 * i));
    c->r[3] = eye;
    c->r[4] = mtx;
    f_025E1B44_orig(c);
}
extern "C" void hook_02027754(Cpu* c) { if (!interp::g_cam_blended) f_02027754_orig(c); }

// WWHD's audio frame callback (020315CC, from the same frame-callback table as the per-frame
// function) advances the sound engine one game step (JAIZelBasic::gframeProcess: sequences, effect
// processing). The frame loop calls it every displayed frame, so at 60 fps the engine ran at double
// rate: sequences such as the waves triggered twice as often and overlapped. Once per logic step.
extern "C" void hook_020315CC(Cpu* c) {
    using namespace interp;
    static uint64_t done_for = ~0ull;
    if (enabled()) {
        if (done_for == g_logic_steps) return;
        done_for = g_logic_steps;
    }
    f_020315CC_orig(c);
}
