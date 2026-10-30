#include "mods/map_hud.h"
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
#include "savestate.h"
#include "gfx/stereo.h"
#include "button_glyphs.h"
#include "aspect_panes.h"
#include "aspect_picto.h"
#include "mods/cemu_pack.h"
#include "mods/play_context.h"
#include "mods/mods.h"

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
#include "guest_addr.h"

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
std::atomic<float> g_fov_requested{1.0f}, g_fov_game{1.0f};

float gameplay_fov_scale() {
    const float scale=g_fov_game.load();
    if(scale==1.0f)return scale;
    // Use the same HD event/control fields as portable saves and fast-forward.
    // Check at the camera/clipper call so an event beginning this tick is already
    // excluded. Never write the camera's authored FOV back into guest memory.
    const uint32_t play=GD(0x1046F0B0);
    const char* stage=reinterpret_cast<const char*>(mem::ptr(play+0x5134));
    const uint32_t link=ld32(play+0x5B34);
    if(!*stage || !std::strncmp(stage,"sea_T",8) || !std::strncmp(stage,"Name",8) ||
       !std::strncmp(stage,"ENDumi",8) || !link || ld32(play+0x5B2C)!=link ||
       ld8(play+0x5292) || ld8(play+0x5BB2) || ld8(play+0x5BB3) ||
       ld8(play+0x5140+12) || ld32(GD(0x101F36CC)) || ld8(GD(0x101EA069)))return 1.0f;
    return scale;
}

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
float vert_plus_fovy_rad(float fovy, float ky) { return ky == 1.0f ? fovy : 2.0f * std::atan(std::tan(fovy * 0.5f) * ky); }
float vert_plus_fovy(float fovyDeg, float ky) {
    return ky == 1.0f ? fovyDeg : vert_plus_fovy_rad(fovyDeg * (float)M_PI / 180.0f, ky) * 180.0f / (float)M_PI;
}
// f(fovy), f(aspect) of a projection / clipper call of the main camera (fovy in degrees, or radians)
// With original FOV at 16:9 every hook leaves the game's values untouched.
bool original() { return g_game == kBase; }
void adjust_projection_args(Cpu* c, int fovyReg, int aspectReg, bool radians = false, bool gameplayCamera = true) {
    const float fov = gameplayCamera ? gameplay_fov_scale() : 1.0f;
    if (original() && fov == 1.0f && !ours((float)c->f[aspectReg].ps0)) return;
    if (!standard_aspect((float)c->f[aspectReg].ps0)) return;  // a special aspect (demo): leave it
    remember(g_game);
    float kx, ky;
    factors(g_game, kx, ky);
    float fovy = (float)c->f[fovyReg].ps0;
    c->f[aspectReg].ps0 = c->f[aspectReg].ps1 = g_game;
    float angle = radians ? vert_plus_fovy_rad(fovy, ky * fov) : vert_plus_fovy(fovy, ky * fov);
    if (fov != 1.0f) angle = std::min(angle, radians ? 110.0f * (float)M_PI / 180.0f : 110.0f);
    c->f[fovyReg].ps0 = c->f[fovyReg].ps1 = angle;
}
}  // namespace

float layout_scale(float fovyDeg) {
    if (!(fovyDeg > 1.0f && fovyDeg < 170.0f)) return 0.0f;
    float kx = 1.0f, ky = 1.0f, fov = 1.0f;
    if (!original() || g_fov_game.load() != 1.0f) {  // (as adjust_projection_args: 16:9 at the authored FOV is the game's)
        factors(g_game, kx, ky);
        fov = gameplay_fov_scale();
    }
    float angle = vert_plus_fovy(fovyDeg, ky * fov);
    if (fov != 1.0f) angle = std::min(angle, 110.0f);
    const float t = std::tan(angle * (float)M_PI / 360.0f);
    return t > 0 ? 360.0f * ky / t : 0.0f;
}

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
    return clamp_aspect(m == kWindow ? g_window.load(std::memory_order_relaxed) /
        (stereo::requestedMode == stereo::FullSBS ? 2.0f : 1.0f) : mode_value(m));
}
float game() { return g_game; }
float fov_scale() { return g_fov_requested.load(); }
void set_fov_scale(float scale) {
    if(!std::isfinite(scale))return;
    g_fov_requested=std::clamp(scale,0.75f,2.5f);
    LOG("[camera] gameplay FOV scale %.2f (cutscenes excluded)",g_fov_requested.load());
}
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
    g_fov_game=g_fov_requested.load();
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
// camera_draw: sead::PerspectiveProjection::set(camera+0x66C, f1 near, f2 far, f3 mFovy * pi/180,
// f4 mAspect), the projection the HD renderer draws the scene with (its matrices at camera+0x8B8,
// +0x8F8). The aspect reaches it through view.mAspect, the fovy only through this hook: without it
// a taller screen got the 16:9 picture cropped at the sides instead of Vert+.
extern "C" void site_0250007C(Cpu* c) { adjust_projection_args(c, 3, 4, true); }
// view_setup: mDoLib_clipper::setup(f1 fovy, f2 aspect, f3 near, f4 far)
static void stereo_clipper(Cpu* c) {
    adjust_projection_args(c, 1, 2);
    if (stereo::requestedMode == stereo::Off) return;
    // A conservative union of both off-axis views. Widen culling only, never
    // the visible camera. Include the largest shift at the near plane.
    const float nearPlane=std::max(1.0f,float(c->f[3].ps0));
    const float projectionX = 1.0f / (std::tan(float(c->f[1].ps0)*float(M_PI)/360.0f)*float(c->f[2].ps0));
    const float margin=stereo::requestedDepth.load()*stereo::eyeDistanceAtUnitDepth * projectionX *
        (1.0f/nearPlane + 1.0f/stereo::requestedConvergence.load());
    c->f[2].ps0 *= 1.0f+margin;
    c->f[2].ps1 = c->f[2].ps0;
}
extern "C" void site_024F811C(Cpu* c) { stereo_clipper(c); }
extern "C" void site_024F8168(Cpu* c) { stereo_clipper(c); }
// dScnName_c::setView: C_MTXPerspective (file select)
extern "C" void site_025ADB38(Cpu* c) { adjust_projection_args(c, 1, 2, false, false); }

// ---- The Picto Box (issue #303, aspect_picto.h): the photo and its subjects are the 16:9 part of the
// wider (taller) picture, as on a Wii U, not the whole picture squeezed into the 800x450 photo.
//   dSnap_packet::draw 025BF250: copies the finder's frame out of the TV picture (colour, then depth)
//     with drawColorTexture / drawDepthTexture(renderer, view, viewport, r6 scale, r7 position):
//     float pairs on the stack, texture coordinates of the 1280x720 screen. The depth copy reuses
//     the position (sp+0x28), so it is moved once, at the colour copy.
//   dSnap_packet::DebugDraw 025BFDC0 (the capture: draw, then the subjects): the TV projection, copied
//     to the stack and narrowed to the finder's frame, goes to matrixProjectionCtor (r4), which the
//     subjects are drawn with into the photo's alpha channel (their ids, counted by Judge), and,
//     halved, to setProjectionMtx(mpProjection, r4), which dSnap_project uses for the subjects'
//     expected areas (CalcArea2D).
// The factors are the frame's (game()), as the TV picture's targets and projection. The camera's
// projection is off-centre in the Picto Box (m[1][2] = 0.35: the finder sits above the screen
// centre), so in a taller picture its 16:9 part sits off the centre too; the rectangle follows it.
// At 16:9 nothing is touched.
namespace {
bool picto_log() {
    static const bool on = getenv("WWHD_PICTO_LOG") != nullptr;
    return on;
}
// test aid: WWHD_PICTO_FULL_PICTURE=1 keeps the earlier behaviour (the whole wider picture squeezed
// into the photo, subjects found in all of it), for comparisons
bool picto_16x9() {
    static const bool full = getenv("WWHD_PICTO_FULL_PICTURE") != nullptr;
    return !full;
}
void picto_factors(float& kx, float& ky) {
    factors(game(), kx, ky);
    if (!picto_16x9()) kx = ky = 1.0f;
}
// the off-centre terms m[0][2], m[1][2] of the TV projection the photo is taken from (DebugDraw's
// context: frame +0x18 the projection, its matrix at +0x44, as getProjectionMtx 0274D83C returns it)
float g_picto_m02 = 0.0f, g_picto_m12 = 0.0f;
// the TV copy's scale (and position) pairs at r6 (r7)
void picto_crop(Cpu* c, bool movePosition) {
    float kx, ky;
    picto_factors(kx, ky);
    const uint32_t scale = c->r[6], position = c->r[7];
    float sx = (float)ldf32(scale), sy = (float)ldf32(scale + 4);
    float px = (float)ldf32(position), py = (float)ldf32(position + 4);
    if (picto_log())
        LOG("[picto] %s copy: scale %.4f %.4f position %.4f %.4f, picture x%.4f wide x%.4f tall", movePosition ? "colour" : "depth",
            sx, sy, px, py, kx, ky);
    if (kx == 1.0f && ky == 1.0f) return;
    picto::crop_to_16x9(px, sx, kx, -g_picto_m02);
    picto::crop_to_16x9(py, sy, ky, g_picto_m12);
    stf32(scale, sx);
    stf32(scale + 4, sy);
    if (!movePosition) return;
    stf32(position, px);
    stf32(position + 4, py);
}
// a row-major 4x4 projection on the stack
void picto_projection(uint32_t m) {
    float kx, ky;
    picto_factors(kx, ky);
    if (kx == 1.0f && ky == 1.0f && !picto_log()) return;
    float v[16];
    for (int i = 0; i < 16; i++) v[i] = (float)ldf32(m + 4 * i);
    if (picto_log())
        LOG("[picto] subject projection: x %.5f %.5f %.5f y %.5f %.5f %.5f, picture x%.4f wide x%.4f tall", v[0], v[1],
            v[2], v[4], v[5], v[6], kx, ky);
    if (kx == 1.0f && ky == 1.0f) return;
    picto::narrow_to_16x9(v, kx, ky);
    stf32(m, v[0]);
    stf32(m + 20, v[5]);
}
}  // namespace
// DebugDraw: bl draw(packet, r4 context): note the TV projection's off-centre terms
extern "C" void site_025BFDF8(Cpu* c) {
    g_picto_m02 = g_picto_m12 = 0.0f;
    const uint32_t frame = ld32(c->r[4]), projection = frame ? ld32(frame + 0x18) : 0;
    if (!projection) return;
    g_picto_m02 = (float)ldf32(projection + 0x44 + 8);
    g_picto_m12 = (float)ldf32(projection + 0x44 + 24);
    if (picto_log()) LOG("[picto] TV projection off-centre %.5f %.5f", g_picto_m02, g_picto_m12);
}
// draw: bl drawColorTexture (the finder's frame of the TV colour picture)
extern "C" void site_025BF5D0(Cpu* c) { picto_crop(c, true); }
// draw: bl drawDepthTexture (the same frame of the TV depth; the position was moved at the colour copy)
extern "C" void site_025BF7C0(Cpu* c) { picto_crop(c, false); }
// DebugDraw: bl matrixProjectionCtor(adjusted, r4 matrix, 0): the subjects' projection
extern "C" void site_025BFFB0(Cpu* c) { picto_projection(c->r[4]); }
// DebugDraw: bl setProjectionMtx(mpProjection, r4 matrix, 0): dSnap_project's projection
extern "C" void site_025C0004(Cpu* c) { picto_projection(c->r[4]); }

// dSnap_packet::Execute, after Judge (r31 the packet): debug, WWHD_PICTO_LOG=1 logs each photo's
// result and its subjects (registered objects with pixels in the photo: id type, pixels, pixels per
// expected area, bounding box in the 800x450 photo)
extern "C" void site_025BE898(Cpu* c) {
    if (!picto_log()) return;
    const uint32_t snap = c->r[31];
    LOG("[picto] photo at aspect %.4f: result %u detail %u, %d registered", game(), ld32(snap + 0xF68),
        ld8(snap + 0xF6C), (int)ld32(snap + 0x9C));
    for (int i = 0, n = std::min<int>((int)ld32(snap + 0x9C), 63); i < n; i++) {
        const uint32_t o = snap + 0xA0 + 0x3C * i;
        const int pixels = (int)ld32(o + 0x20);
        if (!pixels) continue;
        LOG("[picto]   subject %d: type 0x%02X pixels %d ratio %.3f box %d,%d-%d,%d", i, ld8(o + 0x18), pixels,
            (float)ldf32(o + 0x24), (int16_t)ld16(o + 0x28), (int16_t)ld16(o + 0x2A), (int16_t)ld16(o + 0x2C),
            (int16_t)ld16(o + 0x2E));
    }
}

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
//   Anchoring, while a TV layout's root computes its matrices: each pane with a rule in the pane
//     table (aspect_panes.h kRules: HUD at the edges, full-screen backgrounds, frames, markers placed
//     at an actor...) gets its translation/scale from it, and roots the game moved off 0,0 to an
//     actor's projected position are scaled by kx, ky. The translation/scale is changed only for
//     the call (dirty bit forced so the matrix is recomputed), so animations and game code keep
//     their values. Draw calls of panes without a rule (menus, content) are clipped to the native
//     region; panes with one cover the expanded picture.
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
// layout root pane -> the swaps at which it was last drawn to the TV and to the GamePad. Some layouts
// are drawn to both in the same frame with the same matrices (the Wind's Requiem compass, the
// screen wipes, the title menus): they are TV layouts while the TV draws them. Following the last
// report instead flipped them between anchored and not every few frames (the Wind's Requiem
// backdrop letterboxed and full screen in turn). Their GamePad draws then see TV matrices, which
// for these layouts only means full-screen backgrounds stretched past the GamePad's edges.
struct RootSeen { uint32_t root; uint64_t tv, drc; };
std::vector<RootSeen> g_root_tv;
bool root_on_tv(uint32_t root) {
    std::lock_guard<std::mutex> lk(g_root_mu);
    for (auto& e : g_root_tv)
        if (e.root == root) return e.tv && (e.tv >= e.drc || game_frame() + 1 <= e.tv + 3);
    return true;  // not reported (yet): layouts are TV layouts unless seen on the GamePad
}
void set_root_tv(uint32_t root, bool tv) {
    std::lock_guard<std::mutex> lk(g_root_mu);
    uint64_t now = game_frame() + 1;  // +1: 0 means never
    for (auto& e : g_root_tv)
        if (e.root == root) { (tv ? e.tv : e.drc) = now; return; }
    if (g_root_tv.size() > 256) g_root_tv.erase(g_root_tv.begin());
    g_root_tv.push_back({root, tv ? now : 0, tv ? 0 : now});
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

std::string_view name_of(uint32_t pane) {
    const char* n = (const char*)mem::ptr(pane + kPaneName);
    return std::string_view(n, strnlen(n, 24));
}
bool is_leaf(uint32_t pane) { return ld32(pane + kPaneChildren) == pane + kPaneChildren; }
bool has_child(uint32_t pane, std::string_view n) {
    uint32_t sentinel = pane + kPaneChildren;
    for (uint32_t child = ld32(sentinel); child != sentinel; child = ld32(child))
        if (name_of(child) == n) return true;
    return false;
}
// the pane's rule (aspect_panes.h kRules), nullptr for content
const panes::PaneRule* rule_of(uint32_t pane) {
    uint32_t parent = ld32(pane + kPaneParent);
    float w = (float)ldf32(pane + kPaneSize) * std::fabs((float)ldf32(pane + kPaneScale));
    float h = (float)ldf32(pane + kPaneSize + 4) * std::fabs((float)ldf32(pane + kPaneScale + 4));
    return panes::rule_for(name_of(pane), parent && !ld32(parent + kPaneParent), is_leaf(pane),
                           (float)ldf32(pane + kPaneTrans), (float)ldf32(pane + kPaneTrans + 4), w, h,
                           [pane](std::string_view child) { return has_child(pane, child); });
}
// root children placed at the edges or at an actor: the draw hook tells panes the game parked off the
// 16:9 screen from panes these offsets moved there
bool moved_root_child(const panes::PaneRule* r) {
    return r && (r->rule == panes::Rule::Hud || r->rule == panes::Rule::Projected);
}

bool has_world_anchor(uint32_t pane, int depth = 0) {
    if (depth > 32) return false;
    if (panes::is_marker(name_of(pane))) return true;
    uint32_t sentinel = pane + kPaneChildren;
    for (uint32_t child = ld32(sentinel); child != sentinel; child = ld32(child))
        if (has_world_anchor(child, depth + 1)) return true;
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
        // Only roots identified as world markers use projected placement. Menu selectors and map
        // layers also translate their roots, but those translations stay in native layout space.
        float tx = (float)ldf32(pane + kPaneTrans), ty = (float)ldf32(pane + kPaneTrans + 4);
        bool placed = t_anchor && (tx != 0.0f || ty != 0.0f) && has_world_anchor(pane);
        // debug: WWHD_ASPECT_LOG=1 logs each placed root (name, translation) once per name
        static const bool log_roots = getenv("WWHD_ASPECT_LOG") != nullptr;
        if (log_roots && placed) {
            static std::mutex mu;
            static std::unordered_map<std::string, int> seen;
            std::string nm(name_of(pane));
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
    const panes::PaneRule* rule = rule_of(pane);
    if (rule) {
        float w = (float)ldf32(pane + kPaneSize) * std::fabs(sx), h = (float)ldf32(pane + kPaneSize + 4) * std::fabs(sy);
        auto t = panes::apply(*rule, tx, ty, sx, sy, w, h, kx, ky);
        nx = t.x; ny = t.y; nsx = t.sx; nsy = t.sy;
    }
    bool moved = nx != tx || ny != ty, scaled = nsx != sx || nsy != sy;
    // debug: WWHD_ASPECT_LOG=1 logs each moved or stretched pane once per layout root and name
    static const bool log_panes = getenv("WWHD_ASPECT_LOG") != nullptr;
    if (log_panes && (moved || scaled)) {
        static std::mutex mu;
        static std::unordered_map<std::string, int> seen;
        std::string key = std::string(name_of(t_root)) + "/" + std::string(name_of(pane));
        std::lock_guard<std::mutex> lk(mu);
        if (seen[key]++ == 0) LOG("[aspect] %s '%s' %.1f,%.1f x%.3f,%.3f -> %.1f,%.1f x%.3f,%.3f", scaled ? "stretched" : "moved",
                                  key.c_str(), tx, ty, sx, sy, nx, ny, nsx, nsy);
    }
    if (parent == t_root || rule) set_offset(pane, nx - tx, ny - ty);
    if (moved || scaled) st8(pane + kPaneFlags, ld8(pane + kPaneFlags) | kPaneMtxDirty);  // recompute with the offsets
    if (moved) { stf32(pane + kPaneTrans, nx); stf32(pane + kPaneTrans + 4, ny); }
    if (scaled) { stf32(pane + kPaneScale, nsx); stf32(pane + kPaneScale + 4, nsy); }
    f_028766CC_orig(c);
    if (moved) { stf32(pane + kPaneTrans, tx); stf32(pane + kPaneTrans + 4, ty); }
    if (scaled) { stf32(pane + kPaneScale, sx); stf32(pane + kPaneScale + 4, sy); }
}

extern "C" void hook_028766CC(Cpu* c) {
    // HUD fades (mods/hud_fade.h): the hearts, rupees and magic bar fade by their own alpha (+0x45), which
    // the calculation passes on to the children that inherit alpha; set only for the call
    const uint32_t pane = c->r[3];
    const float fade = mods::hud_fade_alpha_of(pane);
    if (fade < 1.f) {
        const uint8_t a0 = ld8(pane + 0x45);
        st8(pane + 0x45, uint8_t(a0 * fade));
        button_glyphs::calculate_pane(c, calculate_aspect_pane);
        st8(pane + 0x45, a0);
        return;
    }
    button_glyphs::calculate_pane(c, calculate_aspect_pane);
}

// Pane::Draw(this, DrawInfo&): the render thread reports where each layout root is drawn. The
// game thread can't tell: TV and GamePad layouts share DrawInfos and are recorded on threads that
// don't bind the targets.
extern "C" void hook_02877100(Cpu* c) {
    using namespace aspect;
    uint32_t pane = c->r[3];
    if(guestmods::maphud::hide_pane(pane))return;
    static const uint64_t trace_frame = getenv("WWHD_ASPECT_TRACE_FRAME") ? strtoull(getenv("WWHD_ASPECT_TRACE_FRAME"), nullptr, 10) : 0;
    if (trace_frame && game_frame() == trace_frame) {
        auto trace = [](auto&& self, uint32_t p, int depth) -> void {
            if (depth > 32) return;
            LOG("[pane] %08X parent=%08X name=%.24s flags=%02X base=%02X t=%.3f,%.3f size=%.3f,%.3f global=%.3f,%.3f scale=%.3f,%.3f", p, ld32(p + kPaneParent), (const char*)mem::ptr(p+kPaneName), ld8(p+kPaneFlags), ld8(p+0x47), (float)ldf32(p+kPaneTrans), (float)ldf32(p+kPaneTrans+4), (float)ldf32(p+kPaneSize), (float)ldf32(p+kPaneSize+4), (float)ldf32(p+kPaneGlobal+12), (float)ldf32(p+kPaneGlobal+28), (float)ldf32(p+kPaneGlobal), (float)ldf32(p+kPaneGlobal+20));
            uint32_t sentinel=p+kPaneChildren;
            for(uint32_t ch=ld32(sentinel);ch!=sentinel;ch=ld32(ch)) self(self,ch,depth+1);
        };
        if (!ld32(pane+kPaneParent)) trace(trace,pane,0);
    }
    mods::play_context_pane(pane);  // the message's parts, the Picto Box screens... (returns at once unless wanted)
    {
        // the game's HUD parts the port hides: a faded-out heart / rupee / magic display, the button cluster
        // while the touch controls show the context buttons (only TV layouts: the GamePad keeps its own)
        const int hide = mods::hud_pane_hide(pane);
        if (hide == 1) return;
        if (hide == 2) {
            uint32_t root = pane;
            for (int i = 0; i < 32 && ld32(root + kPaneParent); i++) root = ld32(root + kPaneParent);
            if (root != pane && root_on_tv(root)) return;
        }
    }
    if (original()) { button_glyphs::draw_pane(c, f_02877100_orig); return; }
    uint32_t parent = ld32(pane + kPaneParent);
    if (!parent) {
        gx2::emit(gx2::OP_LAYOUT_ROOT, {pane});
    } else {
        // Preserve the existing hiding rule for anchored HUD/world markers. Menu containers
        // use per-DrawSelf clipping instead: their visible children may cross this boundary.
        uint32_t child = pane, root = parent;
        for (int i = 0; i < 16 && ld32(root + kPaneParent); i++) { child = root; root = ld32(root + kPaneParent); }
        bool world = !ld32(root + kPaneParent) &&
            (ldf32(root + kPaneTrans) != 0 || ldf32(root + kPaneTrans + 4) != 0) && has_world_anchor(root);
        if (!ld32(root + kPaneParent) && root_on_tv(root) && (world || moved_root_child(rule_of(child)))) {
            // where the game put it: the drawn position less every offset applied on the way down
            float gx = (float)ldf32(pane + kPaneGlobal + 12), gy = (float)ldf32(pane + kPaneGlobal + 28);
            for (uint32_t p = pane; p; p = ld32(p + kPaneParent)) {
                auto o = offset_of(p);
                gx -= o.first;
                gy -= o.second;
            }
            if (panes::parked_hud(gx, gy)) return;
        }
    }
    button_glyphs::draw_pane(c, f_02877100_orig);
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

// Scope clipping to DrawSelf, not the subtree: a background and menu content can be siblings
// under the same container, and tabs may have an offscreen origin but visible descendants.
static thread_local std::vector<bool> t_layoutScopes;
extern "C" void site_0287714C(Cpu* c) {
    using namespace aspect;
    // Cached prompts/dialogue can be recorded in mono and replayed after 3D is
    // enabled. Keep their 2D identity in the list, independent of the mode at
    // recording time. Immediate original-aspect mono still needs no markers.
    const bool scoped=gx2::recording_display_list()||!original()||stereo::requestedMode!=stereo::Off;
    // Remember the decision until the matching return, even if the user changes
    // modes during DrawSelf.
    if(scoped||!t_layoutScopes.empty())t_layoutScopes.push_back(scoped);
    if(!scoped)return;
    uint32_t pane = c->r[3];
    // The Pictobox and album display a captured photograph in this pane.
    // Its pixels, like the guest's saved photo, belong to one camera.
    const uint32_t photograph = name_of(pane) == "PF_Pictograph_00";
    if (original()) { gx2::emit(gx2::OP_LAYOUT_CONTENT, {0,1,photograph}); return; }
    // content unless the pane or one of its parents has a rule (it may reach past the 16:9 region)
    // or the layout is placed at an actor
    bool content = true;
    for (uint32_t p = pane; p && content; p = ld32(p + kPaneParent)) {
        if (ld32(p + kPaneParent) ? rule_of(p) != nullptr
                                  : (ldf32(p + kPaneTrans) != 0 || ldf32(p + kPaneTrans + 4) != 0) && has_world_anchor(p))
            content = false;
    }
    gx2::emit(gx2::OP_LAYOUT_CONTENT, {content ? 1u : 0u,1,photograph});
}
extern "C" void site_02877150(Cpu*) {
    if(t_layoutScopes.empty())return;
    const bool scoped=t_layoutScopes.back();t_layoutScopes.pop_back();
    if(!scoped)return;
    gx2::emit(gx2::OP_LAYOUT_CONTENT, {0,0});
}
namespace aspect {
// set and read by whichever thread executes GX2 commands (the render thread; inline without it,
// under the execution lock): a plain variable, no TLS lookup in the draw's scissor path
static bool g_content_clip = false;
void set_content_clip(bool clip) { g_content_clip = clip; }
bool content_clip() { return g_content_clip; }
}

// HUD particles copy pane coordinates, sometimes only at screen setup. Keep the
// pane association on the host so their draw projection can follow its live anchor.
namespace {
struct ParticlePane { uint32_t pane; std::string name; float dx,dy; };
std::pair<float,float> particle_pane_offset(uint32_t pane) {
    float dx=0,dy=0;
    for(unsigned depth=0;pane && depth<32;++depth,pane=ld32(pane+aspect::kPaneParent)) {
        const auto offset=aspect::offset_of(pane);
        const uint32_t parent=ld32(pane+aspect::kPaneParent);
        if(parent) {
            dx+=float(ldf32(parent+aspect::kPaneGlobal))*offset.first+
                float(ldf32(parent+aspect::kPaneGlobal+4))*offset.second;
            dy+=float(ldf32(parent+aspect::kPaneGlobal+16))*offset.first+
                float(ldf32(parent+aspect::kPaneGlobal+20))*offset.second;
        } else { dx+=offset.first;dy+=offset.second; }
    }
    return {dx,dy};
}
bool particle_pane_follows_aspect(uint32_t pane) {
    for(unsigned depth=0;pane && depth<32;++depth,pane=ld32(pane+aspect::kPaneParent)) {
        if(!ld32(pane+aspect::kPaneParent)) {
            return (ldf32(pane+aspect::kPaneTrans)!=0 || ldf32(pane+aspect::kPaneTrans+4)!=0) &&
                aspect::has_world_anchor(pane);
        }
        const auto* rule=aspect::rule_of(pane);
        if(rule && (rule->rule==aspect::panes::Rule::Hud || rule->rule==aspect::panes::Rule::Anchored ||
                    rule->rule==aspect::panes::Rule::Slide || rule->rule==aspect::panes::Rule::Projected))return true;
    }
    return false;
}
std::mutex particle_mu;
constexpr size_t kMaxParticlePanes=16384;
std::unordered_map<uint64_t, ParticlePane> particle_panes;
struct ParticleBinding { uint64_t position; ParticlePane source; };
std::unordered_map<uint32_t,ParticleBinding> particle_bindings;
uint64_t particle_key(float x, float y) {
    if(x==0)x=0; if(y==0)y=0; // canonicalise signed zero
    uint32_t xb, yb;
    std::memcpy(&xb,&x,4); std::memcpy(&yb,&y,4);
    return (uint64_t(xb)<<32)|yb;
}
}
extern "C" void f_02704D2C_orig(Cpu*);
extern "C" void hook_02704D2C(Cpu* c) {
    const uint32_t out=c->r[3], pane=c->r[4];
    f_02704D2C_orig(c);
    if (!pane || !particle_pane_follows_aspect(pane)) return;
    const float x=ldf32(out), y=ldf32(out+4);
    if(!std::isfinite(x)||!std::isfinite(y))return;
    std::lock_guard<std::mutex> lock(particle_mu);
    if(particle_panes.size()>=kMaxParticlePanes && !particle_panes.count(particle_key(x,-y)))return;
    const auto offset=particle_pane_offset(pane);
    particle_panes[particle_key(x,-y)]={pane,std::string(aspect::name_of(pane)),offset.first,offset.second};
    if (getenv("WWHD_EFFECT_ASPECT_TRACE")) {
        static std::unordered_map<uint32_t,uint64_t> seen;
        auto& frame=seen[pane];
        if (!frame || aspect::game_frame()-frame>=120) {
            frame=aspect::game_frame();
            LOG("[effect-pane] frame=%llu aspect=%.6f pane=%08X name=%s position=%.3f/%.3f",(unsigned long long)frame,aspect::game(),pane,particle_panes[particle_key(x,-y)].name.c_str(),x,y);
        }
    }
}
// The shared 2D/3D particle factory returns pooled emitters. Bind at creation,
// before later UI queries can reuse the same coordinate for another pane.
extern "C" void f_025A847C_orig(Cpu*);
extern "C" void hook_025A847C(Cpu* c) {
    const uint32_t group=c->r[4],position=c->r[6];
    ParticlePane source{};bool found=false;
    if(group>=7 && group<=10 && position) {
        const auto key=particle_key(ldf32(position),-float(ldf32(position+4)));
        std::lock_guard<std::mutex> lock(particle_mu);
        const auto it=particle_panes.find(key);
        if(it!=particle_panes.end()) { source=it->second;found=true; }
    }
    f_025A847C_orig(c);
    const uint32_t emitter=c->r[3];
    if(!emitter)return;
    std::lock_guard<std::mutex> lock(particle_mu);
    particle_bindings.erase(emitter);
    if(found && particle_bindings.size()<kMaxParticlePanes) {
        const auto key=particle_key(ldf32(emitter+0x22c),ldf32(emitter+0x230));
        particle_bindings.emplace(emitter,ParticleBinding{key,source});
        if(getenv("WWHD_EFFECT_ASPECT_TRACE"))
            LOG("[effect-create] frame=%llu aspect=%.6f emitter=%08X group=%u pane=%08X anchor=%.3f/%.3f",(unsigned long long)aspect::game_frame(),aspect::game(),emitter,group,source.pane,source.dx,source.dy);
    }
}
extern "C" void f_0282D9C0_orig(Cpu*);
extern "C" void hook_0282D9C0(Cpu* c) {
    const uint32_t emitter=c->r[3]-0x9c, projection=c->r[5];
    const uint32_t group=ld8(emitter+0x262);
    if (group<7 || group>10) { f_0282D9C0_orig(c); return; }
    const float x=ldf32(emitter+0x22c), y=ldf32(emitter+0x230);
    float dx=0,dy=0;
    {
        std::lock_guard<std::mutex> lock(particle_mu);
        const auto key=particle_key(x,y);
        auto bound=particle_bindings.find(emitter);
        if(bound==particle_bindings.end() || bound->second.position!=key) {
            const auto source=particle_panes.find(key);
            if(source!=particle_panes.end() && particle_bindings.size()<kMaxParticlePanes)
                bound=particle_bindings.insert_or_assign(emitter,ParticleBinding{key,source->second}).first;
            else { particle_bindings.erase(emitter);bound=particle_bindings.end(); }
        }
        if(bound!=particle_bindings.end()) {
            const auto& source=bound->second.source;
            if(aspect::name_of(source.pane)==source.name) {
                const auto offset=particle_pane_offset(source.pane);
                dx=offset.first-source.dx;
                dy=source.dy-offset.second;
            }
        }
    }
    float kx,ky; aspect::factors(aspect::game(),kx,ky);
    if (kx==1 && ky==1 && dx==0 && dy==0) { f_0282D9C0_orig(c); return; }
    uint32_t saved[8];
    for (unsigned i=0;i<8;++i) saved[i]=ld32(projection+4*i);
    stf32(projection+12,float(ldf32(projection+12))+float(ldf32(projection))*dx);
    stf32(projection+28,float(ldf32(projection+28))+float(ldf32(projection+20))*dy);
    for (unsigned i=0;i<4;++i) {
        stf32(projection+4*i,float(ldf32(projection+4*i))/kx);
        stf32(projection+16+4*i,float(ldf32(projection+16+4*i))/ky);
    }
    if (getenv("WWHD_EFFECT_ASPECT_TRACE")) {
        static std::unordered_map<uint32_t,uint64_t> seen;
        auto& frame=seen[emitter];
        if (!frame || aspect::game_frame()-frame>=120) {
            frame=aspect::game_frame();
            LOG("[effect-draw] frame=%llu aspect=%.6f emitter=%08X group=%u position=%.3f/%.3f shift=%.3f/%.3f scale=%.6f/%.6f",(unsigned long long)frame,aspect::game(),emitter,group,x,y,dx,dy,kx,ky);
        }
    }
    f_0282D9C0_orig(c);
    for (unsigned i=0;i<8;++i) st32(projection+4*i,saved[i]);
}

// Exact full states can be opened in another process. Preserve both cached
// coordinates and live emitter bindings, independent of the new host's UI history.
namespace {
void write_particle_source(ss::Writer& w,const ParticlePane& source) {
    w.u32(source.pane);w.str(source.name);w.pod(source.dx);w.pod(source.dy);
}
ParticlePane read_particle_source(ss::Reader& r) {
    ParticlePane source;
    source.pane=r.u32();source.name=r.str();source.dx=r.pod<float>();source.dy=r.pod<float>();
    return source;
}
bool valid_particle_source(const ParticlePane& source) {
    return source.pane>=mem::kMem2Start && source.pane<=mem::kMem2End-0xa0 && !(source.pane&3) &&
        source.name.size()<=24 && std::isfinite(source.dx) && std::isfinite(source.dy);
}
}
void aspect::ss_save_particles(ss::Writer& w) {
    std::lock_guard<std::mutex> lock(particle_mu);
    w.u32(1);w.u32(uint32_t(particle_panes.size()));
    for(const auto& entry:particle_panes) { w.u64(entry.first);write_particle_source(w,entry.second); }
    w.u32(uint32_t(particle_bindings.size()));
    for(const auto& entry:particle_bindings) {
        w.u32(entry.first);w.u64(entry.second.position);write_particle_source(w,entry.second.source);
    }
}
bool aspect::ss_validate_particles(ss::Reader r) {
    if(r.at_end())return true; // older states have no optional presentation metadata
    if(r.u32()!=1)return false;
    const auto count=r.u32();if(count>kMaxParticlePanes)return false;
    for(uint32_t i=0;i<count;++i) {
        r.u64();const auto source=read_particle_source(r);
        if(!r.ok || !valid_particle_source(source))return false;
    }
    const auto bindings=r.u32();if(bindings>kMaxParticlePanes)return false;
    for(uint32_t i=0;i<bindings;++i) {
        const auto emitter=r.u32();r.u64();const auto source=read_particle_source(r);
        if(!r.ok || !valid_particle_source(source) || emitter<mem::kMem2Start ||
           emitter>mem::kMem2End-0x3fc || (emitter&3))return false;
    }
    return r.ok && r.at_end();
}
void aspect::ss_load_particles(ss::Reader r) {
    if(!ss_validate_particles(r))return;
    std::lock_guard<std::mutex> lock(particle_mu);
    particle_panes.clear();particle_bindings.clear();
    if(r.at_end())return;
    r.u32();const auto count=r.u32();
    for(uint32_t i=0;i<count;++i) {
        const auto key=r.u64();particle_panes.emplace(key,read_particle_source(r));
    }
    const auto bindings=r.u32();
    for(uint32_t i=0;i<bindings;++i) {
        const auto emitter=r.u32();const auto key=r.u64();
        particle_bindings.emplace(emitter,ParticleBinding{key,read_particle_source(r)});
    }
}
