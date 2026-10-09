// Aspect ratio of the TV picture: 16:9 (original), the window's shape, 16:10, 21:9 or 32:9.
//
// The game keeps drawing its 1280x720 guest screen. What changes:
//  - 3D: the camera's projection uses the new aspect with the same vertical field of view (Hor+:
//    wider screens see more to the sides). For screens narrower than 16:9 the horizontal field of
//    view is kept and the vertical one grows (Vert+), so nothing of the original view is lost.
//    The culling frustum (mDoLib_clipper) gets the same values, so nothing pops at the new edges.
//  - Render targets (metal_surfaces.mm): screen-shaped buffers are allocated A/(16/9) times wider
//    (or taller) than their guest size; draws keep their guest viewports, which now cover the
//    wider texture, so every full-screen pass (post effects, depth sampling, sea, sky) lines up.
//  - 2D: the HD layouts (nw::lyt) drawn to the TV keep their proportions, centred, and panes near
//    the screen edges move out to the new edges (see "HD layouts" below).
//
// Game side (WWHD addresses, tools/recomp/hooks_aspect.txt):
//   camera_execute 024FFA3C: view.mAspect (+0xD8) = 16/9 constant 1004AAF0 every step   @024FFA98
//   init_phase2    02501F98: the same at camera creation                                @025020E0
//   camera_draw    024FFC40: C_MTXPerspective(proj, mFovy, mAspect, mNear, mFar)         @024FFD60
//   view_setup     024F8068: mDoLib_clipper::setup(mFovy, mAspect, mNear, far) (2 calls)  @024F811C @024F8168
//   dScnName_c::setView 025ADB04: C_MTXPerspective for the file-select scene             @025ADB38
//   nw::lyt::DrawInfo::LoadProjectionMtx 02874038, Pane::CalculateMtx 028766CC, Pane::Draw 02877100
// The aspect is latched once per frame on the main thread at GX2SwapScanBuffers; the render thread
// switches its render-target factors at the matching swap (the value travels with the swap
// command), so a change never lands in the middle of a frame.
#include "aspect.h"
#include "button_glyphs.h"
#include "mods/cemu_pack.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "gx2/gx2_cmd.h"
#include "runtime.h"

namespace aspect {
namespace {
constexpr float kBase = 16.0f / 9.0f;

int parse_env_mode(float& custom) {
    const char* e = getenv("WWHD_ASPECT");
    if (!e || !*e) return kOriginal;
    if (!strcmp(e, "window") || !strcmp(e, "match")) return kWindow;
    if (!strcmp(e, "16:9")) return kOriginal;
    if (!strcmp(e, "16:10")) return k16x10;
    if (!strcmp(e, "21:9")) return k21x9;
    if (!strcmp(e, "32:9")) return k32x9;
    float a = 0;
    if (const char* c = strchr(e, ':')) a = (float)atof(e) / (float)atof(c + 1);
    else a = (float)atof(e);
    if (!(a > 0.5f && a < 8.0f)) return kOriginal;
    custom = a;
    return kCustom;
}
float g_custom = kBase;
std::atomic<int> g_mode{parse_env_mode(g_custom)};
std::atomic<float> g_window{kBase};
// aspect of the frame being built: written by the main thread at the swap, read by the threads
// that compute and record the frame
std::atomic<float> g_game{kBase};

float clamp_aspect(float a) { return std::fmin(std::fmax(a, 1.0f), 4.0f); }

// horizontal/vertical widening of the 16:9 view for aspect a
void factors(float a, float& kx, float& ky) {
    kx = a >= kBase ? a / kBase : 1.0f;
    ky = a >= kBase ? 1.0f : kBase / a;
}

bool same(float a, float b) { return std::fabs(a - b) <= 1e-4f * std::fmax(1.0f, std::fabs(a)); }

// the main camera's projection: is this the game's standard aspect (16:9 or one we put there)?
// aspects this module stored into the camera (a switch back to 16:9 replaces leftovers)
float g_stored[4] = {};
bool ours(float v) {
    for (float x : g_stored)
        if (x && same(v, x)) return true;
    return false;
}
void remember(float v) {
    if (same(v, kBase) || ours(v)) return;
    memmove(g_stored + 1, g_stored, sizeof g_stored - sizeof *g_stored);
    g_stored[0] = v;
}
bool standard_aspect(float v) { return same(v, kBase) || same(v, g_game) || ours(v); }
float vert_plus_fovy(float fovyDeg, float ky) {
    if (ky == 1.0f) return fovyDeg;
    float t = std::tan(fovyDeg * 0.5f * (float)M_PI / 180.0f) * ky;
    return 2.0f * std::atan(t) * 180.0f / (float)M_PI;
}
// f(fovy), f(aspect) of a projection / clipper call of the main camera
// at 16:9 every hook leaves the game's values untouched (bit-identical original behaviour)
bool original() { return g_game == kBase; }
void adjust_projection_args(Cpu* c, int fovyReg, int aspectReg) {
    if (original() && !ours((float)c->f[aspectReg].ps0)) return;
    if (!standard_aspect((float)c->f[aspectReg].ps0)) return;  // a special aspect (demo): leave it
    remember(g_game);
    float kx, ky;
    factors(g_game, kx, ky);
    c->f[aspectReg].ps0 = c->f[aspectReg].ps1 = g_game;
    c->f[fovyReg].ps0 = c->f[fovyReg].ps1 = vert_plus_fovy((float)c->f[fovyReg].ps0, ky);
}
}  // namespace

const char* mode_name(int m) {
    static const char* n[kModes] = {"16:9 (original)", "Match window", "16:10", "21:9", "32:9", "custom"};
    return m >= 0 && m < kModes ? n[m] : "?";
}
float mode_value(int m) {
    switch (m) {
    case kOriginal: return kBase;
    case k16x10: return 16.0f / 10.0f;
    case k21x9: return 64.0f / 27.0f;  // 2560x1080 / 3440x1440-class monitors sold as 21:9
    case k32x9: return 32.0f / 9.0f;
    case kCustom: return g_custom;
    default: return 0;
    }
}
int mode() { return g_mode.load(std::memory_order_relaxed); }
void set_mode(int m) {
    if (m < 0 || m >= kModes) return;
    g_mode = m;
    LOG("[aspect] %s", mode_name(m));
}
void set_window_aspect(float a) {
    if (!(a > 0.1f && a < 20.0f)) return;
    // window resizes are continuous; steps of 0.5% keep render targets from being remade every frame
    float q = std::round(a * 200.0f) / 200.0f;
    if (std::fabs(q - kBase) < 0.006f) q = kBase;  // a 16:9 window (rounded pixel sizes) is 16:9
    g_window.store(q, std::memory_order_relaxed);
}
float requested() {
    if(float pack=mods::cemu::aspect_ratio())return pack;
    int m = mode();
    return clamp_aspect(m == kWindow ? g_window.load(std::memory_order_relaxed) : mode_value(m));
}
float game() { return g_game; }
static uint64_t g_swaps = 0, g_changed_swap = 0;
uint64_t game_frame() { return g_swaps; }

float on_swap() {
    // test aid: WWHD_ASPECT_AT=frame:aspect,... switches the aspect at those swaps (aspect 0 = window)
    static uint64_t swaps = 0;
    static std::vector<std::pair<uint64_t, float>> at = [] {
        std::vector<std::pair<uint64_t, float>> v;
        if (const char* e = getenv("WWHD_ASPECT_AT"))
            for (char* p = (char*)e; *p;) {
                uint64_t f = strtoull(p, &p, 10);
                if (*p++ != ':') break;
                v.push_back({f, (float)strtod(p, &p)});
                while (*p == ',') p++;
            }
        return v;
    }();
    swaps++;
    g_swaps = swaps;
    for (auto& [f, v] : at)
        if (f == swaps) {
            if (v == 0) set_mode(kWindow);
            else if (same(v, kBase)) set_mode(kOriginal);
            else { g_custom = v; set_mode(kCustom); }
        }
    float a = requested();
    bool changed = !same(a, g_game);
    // the GPU work of the frame being submitted now was built with the camera of the previous
    // frame (the painter draws the previous pass's lists): render targets follow one swap later
    float render = g_game.load();
    g_game = a;
    if (changed) {
        LOG("[aspect] game aspect %.4f from swap %llu", a, (unsigned long long)swaps);
        g_changed_swap = swaps;
    }
    return render;
}

}  // namespace aspect

// ---- instruction hooks (tools/recomp/hooks_aspect.txt)
using namespace aspect;
// camera_execute / init_phase2: stfs f0 (16/9), view.mAspect
// (the stored value is the 16/9 constant; at 16:9 nothing is touched)
static void store_aspect(Cpu* c) {
    if (original() || !standard_aspect((float)c->f[0].ps0)) return;
    remember(game());
    c->f[0].ps0 = c->f[0].ps1 = game();
}
extern "C" void site_024FFA98(Cpu* c) { store_aspect(c); }
extern "C" void site_025020E0(Cpu* c) { store_aspect(c); }
// camera_draw: C_MTXPerspective(view.mProjMtx, f1 fovy, f2 aspect, f3 near, f4 far)
extern "C" void site_024FFD60(Cpu* c) { adjust_projection_args(c, 1, 2); }
// view_setup: mDoLib_clipper::setup(f1 fovy, f2 aspect, f3 near, f4 far)
extern "C" void site_024F811C(Cpu* c) { adjust_projection_args(c, 1, 2); }
extern "C" void site_024F8168(Cpu* c) { adjust_projection_args(c, 1, 2); }
// dScnName_c::setView: C_MTXPerspective (file select)
extern "C" void site_025ADB38(Cpu* c) { adjust_projection_args(c, 1, 2); }

// ---- HD layouts (nw::lyt, WWHD's nw4f): TV draws see the 16:9 layout space centred in the wider
// screen, panes near the left/right edges move out to the new edges.
//   DrawInfo::LoadProjectionMtx 02874038: GX2SetVertexUniformReg(uProjection, 16, DrawInfo+0).
//     The layers' DrawInfos hold a perspective projection (fovy 40, aspect 16/9; the layout plane
//     at z = -989 maps 1 unit to 1 pixel). For TV-shaped render targets the x row is divided by kx
//     (y row by ky) for the upload: everything keeps its proportions, centred.
//   Pane (nw::lyt::Pane): +0x00 sibling link (next), +0x08 vtable, +0x0C parent, +0x14 child list
//     sentinel, +0x1C translate x/y/z, +0x34 scale x/y, +0x3C size w/h, +0x44 flags, +0x48 global
//     matrix 3x4, +0x80 name. Pane::CalculateMtx 028766CC (vtable, recursive over children),
//     Pane::Draw 02877100 (plain panes; layout roots).
//   Which screen a layout goes to is only known on the render thread (TV and GamePad layouts share
//     DrawInfos; they are recorded on threads that don't bind the target): projection uploads and
//     root draws are tagged in the command stream (OP_SET_PROJ_REGS, OP_LAYOUT_ROOT, gx2_core.cpp).
//   Anchoring, while a TV layout's root computes its matrices: root children further than 300 units
//     from the centre (hearts, magic, rupees, buttons, keys, place names...) are moved out by the
//     extra half width (height); L_EnemyHP_00 / L_CommandA_00 and roots the game moved off 0,0
//     (placed at an actor's projected position) are scaled by kx, ky instead; leaf panes covering the
//     whole 1280x720 screen (backgrounds, fades) are stretched. The translation/scale is changed only
//     for the call (dirty bit forced so the matrix is recomputed), so animations and game code keep
//     their values. Panes the game parks off the 16:9 screen are not drawn (they would show in the
//     extra space).
extern "C" {
void f_02874038_orig(Cpu* c);
void f_028766CC_orig(Cpu* c);
void f_02877100_orig(Cpu* c);
}
namespace aspect {
namespace {
constexpr uint32_t kPaneFlags = 0x44, kPaneMtxDirty = 0x10, kPaneGlobal = 0x48;
constexpr uint32_t kPaneParent = 0x0C, kPaneChildren = 0x14, kPaneTrans = 0x1C, kPaneScale = 0x34, kPaneSize = 0x3C, kPaneName = 0x80;

std::mutex g_root_mu;
std::vector<std::pair<uint32_t, bool>> g_root_tv;  // layout root pane -> last drawn to the TV
bool root_on_tv(uint32_t root) {
    std::lock_guard<std::mutex> lk(g_root_mu);
    for (auto& [r, tv] : g_root_tv)
        if (r == root) return tv;
    return true;  // not reported (yet): layouts are TV layouts unless seen on the GamePad
}
void set_root_tv(uint32_t root, bool tv) {
    std::lock_guard<std::mutex> lk(g_root_mu);
    for (auto& e : g_root_tv)
        if (e.first == root) { e.second = tv; return; }
    if (g_root_tv.size() > 256) g_root_tv.erase(g_root_tv.begin());
    g_root_tv.push_back({root, tv});
}

// the anchoring state each root's matrices were last computed with: a root whose state changes
// (its screen became known, the aspect changed) recomputes its whole tree once
std::vector<std::pair<uint32_t, bool>> g_root_calc;
bool anchor_changed(uint32_t root, bool anchor) {
    std::lock_guard<std::mutex> lk(g_root_mu);
    for (auto& e : g_root_calc)
        if (e.first == root) {
            bool ch = e.second != anchor;
            e.second = anchor;
            return ch;
        }
    if (g_root_calc.size() > 256) g_root_calc.erase(g_root_calc.begin());
    g_root_calc.push_back({root, anchor});
    return true;  // first time seen: its cached matrices may come from another aspect (save state, earlier session)
}

// offsets applied to anchored panes in their last matrix calculation (root children and placed
// roots): the draw hook tells panes the game parked off the 16:9 screen from panes we moved there
std::unordered_map<uint32_t, std::pair<float, float>> g_offsets;
void set_offset(uint32_t pane, float dx, float dy) {
    std::lock_guard<std::mutex> lk(g_root_mu);
    if (dx == 0.0f && dy == 0.0f) g_offsets.erase(pane);
    else g_offsets[pane] = {dx, dy};
}
std::pair<float, float> offset_of(uint32_t pane) {
    std::lock_guard<std::mutex> lk(g_root_mu);
    auto it = g_offsets.find(pane);
    return it == g_offsets.end() ? std::pair<float, float>{0, 0} : it->second;
}

bool name_is(uint32_t pane, const char* n) { return !strncmp((const char*)mem::ptr(pane + kPaneName), n, 24); }

// HUD parts that belong at the screen edges (only these are moved out; anything else stays where the
// game put it). Not N_All_00: a generic container name several layouts use (the button cluster, but
// also the message window's choice box, whose cursor the game places from the drawn row positions:
// moving it misplaced the Yes/No cursor at 16:10). The game reads the drawn
// positions of message rows to place the choice cursor, so moving them misplaces the cursor)
bool is_hud_edge_pane(uint32_t pane) {
    static const char* const kEdge[] = {
        "N_TV_00", "N_DRC_00", "N_Default_00", "N_HeartPos_01", "L_Rupy_00", "L_CompassClock_00",
        "L_DungeonKey_00", "N_SwimTimePos_00", "L_BossHP_00", "L_Arrow_00", "L_BatteryVol1_00",
        "L_RupySwordCounter_00", "N_Position_00", "N_Time_00"};
    for (const char* n : kEdge)
        if (name_is(pane, n)) return true;
    return false;
}

thread_local uint32_t t_root = 0;   // layout root whose matrices are being computed
thread_local bool t_anchor = false; // ... and it is a TV layout at another aspect ratio
}  // namespace
}  // namespace aspect

// Pane::CalculateMtx(this, DrawInfo&, bool parentDirty)
static void calculate_aspect_pane(Cpu* c) {
    using namespace aspect;
    uint32_t pane = c->r[3];
    uint32_t parent = ld32(pane + kPaneParent);
    float kx, ky;
    factors(g_game, kx, ky);
    if (!parent) {  // a layout's root: decide for the whole tree
        uint32_t saveRoot = t_root;
        bool saveAnchor = t_anchor;
        t_root = pane;
        t_anchor = (kx != 1.0f || ky != 1.0f) && root_on_tv(pane);
        // a root placed by the game (layouts are authored with the root at 0,0): small layouts put at
        // an actor's projected screen position (cursors, markers) -> scaled out with the 3D view
        float tx = (float)ldf32(pane + kPaneTrans), ty = (float)ldf32(pane + kPaneTrans + 4);
        bool placed = t_anchor && (tx != 0.0f || ty != 0.0f);
        // debug: WWHD_ASPECT_LOG=1 logs each placed root (name, translation) once per name
        static const bool log_roots = getenv("WWHD_ASPECT_LOG") != nullptr;
        if (log_roots && placed) {
            static std::mutex mu;
            static std::unordered_map<std::string, int> seen;
            std::string nm((const char*)mem::ptr(pane + kPaneName), strnlen((const char*)mem::ptr(pane + kPaneName), 24));
            std::lock_guard<std::mutex> lk(mu);
            if (seen[nm]++ == 0) LOG("[aspect] placed root '%s' at %.1f, %.1f", nm.c_str(), tx, ty);
        }
        // matrices are only recomputed for dirty panes: after an aspect change, the whole tree
        if (anchor_changed(pane, t_anchor) || (g_changed_swap && g_swaps - g_changed_swap < 4)) c->r[5] = 1;
        if (placed) { stf32(pane + kPaneTrans, tx * kx); stf32(pane + kPaneTrans + 4, ty * ky); }
        set_offset(pane, placed ? tx * (kx - 1.0f) : 0.0f, placed ? ty * (ky - 1.0f) : 0.0f);
        f_028766CC_orig(c);
        if (placed) { stf32(pane + kPaneTrans, tx); stf32(pane + kPaneTrans + 4, ty); }
        t_root = saveRoot;
        t_anchor = saveAnchor;
        return;
    }
    if (!t_anchor) {
        if (parent == t_root) set_offset(pane, 0, 0);
        f_028766CC_orig(c);
        return;
    }
    float tx = (float)ldf32(pane + kPaneTrans), ty = (float)ldf32(pane + kPaneTrans + 4);
    float sx = (float)ldf32(pane + kPaneScale), sy = (float)ldf32(pane + kPaneScale + 4);
    float nx = tx, ny = ty, nsx = sx, nsy = sy;
    if (parent == t_root) {
        float half = 640.0f * (kx - 1.0f), halfY = 360.0f * (ky - 1.0f);
        // parts the game moves to an actor's projected position: enemy health, the floating A action
        if (name_is(pane, "L_EnemyHP_00") || name_is(pane, "L_CommandA_00")) {
            nx = tx * kx;
            ny = ty * ky;
        } else if (is_hud_edge_pane(pane)) {
            if (tx <= -300.0f) nx = tx - half;
            if (tx >= 300.0f) nx = tx + half;
            if (ty <= -200.0f) ny = ty - halfY;
            if (ty >= 200.0f) ny = ty + halfY;
        }
    }
    // a leaf covering the whole screen (background, fade): stretch it to the new shape
    uint32_t sentinel = pane + kPaneChildren;
    if (ld32(sentinel) == sentinel && std::fabs(tx) < 8.0f && std::fabs(ty) < 8.0f) {
        float w = (float)ldf32(pane + kPaneSize) * std::fabs(sx), h = (float)ldf32(pane + kPaneSize + 4) * std::fabs(sy);
        if (w >= 1270.0f && h >= 710.0f) {
            nsx = sx * kx;
            nsy = sy * ky;
            static const bool log_st = getenv("WWHD_ASPECT_LOG") != nullptr;
            if (log_st) {
                static std::mutex mu;
                static std::unordered_map<std::string, int> seen;
                std::string key = std::string((const char*)mem::ptr(t_root + kPaneName), strnlen((const char*)mem::ptr(t_root + kPaneName), 24)) +
                                  "/" + std::string((const char*)mem::ptr(pane + kPaneName), strnlen((const char*)mem::ptr(pane + kPaneName), 24));
                std::lock_guard<std::mutex> lk(mu);
                if (seen[key]++ == 0) LOG("[aspect] stretched '%s' (vtable %08X) %.0fx%.0f", key.c_str(), ld32(pane), w, h);
            }
        }
    }
    bool moved = nx != tx || ny != ty, scaled = nsx != sx || nsy != sy;
    static const bool log_moves = getenv("WWHD_ASPECT_LOG") != nullptr;
    if (log_moves && moved && parent == t_root) {
        static std::mutex mu;
        static std::unordered_map<std::string, int> seen;
        auto nm = [](uint32_t p) { return std::string((const char*)mem::ptr(p + kPaneName), strnlen((const char*)mem::ptr(p + kPaneName), 24)); };
        std::string key = nm(t_root) + "/" + nm(pane);
        std::lock_guard<std::mutex> lk(mu);
        if (seen[key]++ == 0) LOG("[aspect] moved '%s' %.1f,%.1f -> %.1f,%.1f", key.c_str(), tx, ty, nx, ny);
    }
    if (parent == t_root) set_offset(pane, nx - tx, ny - ty);
    if (moved || scaled) st8(pane + kPaneFlags, ld8(pane + kPaneFlags) | kPaneMtxDirty);  // recompute with the offsets
    if (moved) { stf32(pane + kPaneTrans, nx); stf32(pane + kPaneTrans + 4, ny); }
    if (scaled) { stf32(pane + kPaneScale, nsx); stf32(pane + kPaneScale + 4, nsy); }
    f_028766CC_orig(c);
    if (moved) { stf32(pane + kPaneTrans, tx); stf32(pane + kPaneTrans + 4, ty); }
    if (scaled) { stf32(pane + kPaneScale, sx); stf32(pane + kPaneScale + 4, sy); }
}

extern "C" void hook_028766CC(Cpu* c) {
    button_glyphs::calculate_pane(c, calculate_aspect_pane);
}

// Pane::Draw(this, DrawInfo&): the render thread reports where each layout root is drawn. The
// game thread can't tell: TV and GamePad layouts share DrawInfos and are recorded on threads that
// don't bind the targets.
extern "C" void hook_02877100(Cpu* c) {
    using namespace aspect;
    uint32_t pane = c->r[3];
    if (original()) { f_02877100_orig(c); return; }
    uint32_t parent = ld32(pane + kPaneParent);
    if (!parent) {
        gx2::emit(gx2::OP_LAYOUT_ROOT, {pane});
    } else {
        // the wider (taller) picture shows layout space the game uses to park panes out of sight:
        // a pane whose centre, without our offsets, is off the 16:9 screen stays hidden
        uint32_t child = pane, root = parent;
        for (int i = 0; i < 16 && ld32(root + kPaneParent); i++) { child = root; root = ld32(root + kPaneParent); }
        if (!ld32(root + kPaneParent) && root_on_tv(root)) {
            auto a = offset_of(child), b = offset_of(root);
            float gx = (float)ldf32(pane + kPaneGlobal + 0xC) - a.first - b.first;
            float gy = (float)ldf32(pane + kPaneGlobal + 0x1C) - a.second - b.second;
            if (std::fabs(gx) > 640.0f + 32.0f || std::fabs(gy) > 360.0f + 32.0f) return;
        }
    }
    f_02877100_orig(c);
}

// the projection matrix uploaded during `fn` is tagged: the render thread narrows it (rows x and y
// divided by kx, ky) when the target is the TV picture (gx2_core.cpp OP_SET_PROJ_REGS)
static thread_local int t_tag = 0;
static void with_tv_projection(Cpu* c, void (*fn)(Cpu*)) {
    if (aspect::original()) { fn(c); return; }
    t_tag++;
    fn(c);
    t_tag--;
}
namespace aspect {
bool tagged_projection() { return t_tag > 0; }
void layout_root_target(uint32_t root, bool tv) { set_root_tv(root, tv); }
}  // namespace aspect

// DrawInfo::LoadProjectionMtx(this): TV layouts are drawn into the wider screen undistorted
extern "C" void hook_02874038(Cpu* c) { with_tv_projection(c, f_02874038_orig); }
// nw::font text drawing (TextBox panes): content+4 -> projection matrix (multiplied with the view
// matrix at content+8 and uploaded)
extern "C" void f_028F8250_orig(Cpu* c);
extern "C" void hook_028F8250(Cpu* c) { with_tv_projection(c, f_028F8250_orig); }


// a loaded save state brings layout matrices cached under whatever aspect was active when it was
// saved: forget which roots were computed, so each recomputes its whole tree once
void aspect::ss_reset() {
    std::lock_guard<std::mutex> lk(aspect::g_root_mu);
    aspect::g_root_calc.clear();
}
