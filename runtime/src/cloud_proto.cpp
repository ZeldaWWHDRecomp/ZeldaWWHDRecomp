// Cloud research prototype (off by default; WWHD_CLOUDS_PROTO=sdf|volume|hide).
//
// The game's sky clouds (vrkumo) are drawn by drawVrkumo (02575B6C): 3 layers x up to 100 camera-facing
// textured quads on a sphere at the far plane, one draw each, positions from g_env_light.mpVrkumoPacket
// (+0xA94, VRKUMO_EFF[100] at +0xA4, 0x2C each). This hook reads that packet, the camera, the view and
// the environment palette READ-ONLY on the game thread, keeps a host-side snapshot and brackets the
// game's own draws with two render-queue ops (OP_CLOUDS_BEGIN / OP_CLOUDS_END). The renderer skips
// the game's cards between the two and draws its own clouds instead (gfx/metal_clouds.mm). Nothing
// is written to guest memory.
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "cloud_proto.h"
#include "guest_addr.h"
#include "gfx/renderer.h"
#include "gx2/gx2_cmd.h"
#include "runtime.h"
#include "savestate.h"

extern "C" void f_02575B6C_orig(Cpu* c);  // drawVrkumo
namespace interp { uint64_t logic_steps(); }

namespace cloud_proto {

uint32_t g_active_seq = 0;
bool g_drawn = false;
void (*g_on_end)() = nullptr;
static Snapshot g_ring[kRing];
static std::atomic<uint32_t> g_seq{0};

// research override: WWHD_CLOUDS_PROTO=sdf|volume|combo|hide|mark (-1: none, the Cloud style setting decides)
static int env_mode() {
    static const int m = [] {
        const char* e = getenv("WWHD_CLOUDS_PROTO");
        if (!e || !*e) return -1;
        if (!strcmp(e, "0") || !strcmp(e, "off")) return (int)kOff;
        if (!strcmp(e, "volume")) return (int)kVolume;
        if (!strcmp(e, "hide")) return (int)kHide;
        if (!strcmp(e, "mark")) return (int)kMark;
        if (!strcmp(e, "combo")) return (int)kCombo;
        return (int)kSdf;
    }();
    return m;
}
int mode() {
    const int e = env_mode();
    if (e >= 0) return e;
    return render::cloud_style() > 0 ? kSdf : kOff;
}
int tier() {
    const int style = render::cloud_style();
    return env_mode() >= 0 ? 2 : style;
}

const Snapshot* snapshot(uint32_t seq) { return &g_ring[seq % kRing]; }

static float ldf(uint32_t ea) { return u32_as_f32(ld32(ea)); }
static void vec3(uint32_t ea, float* o) { o[0] = ldf(ea), o[1] = ldf(ea + 4), o[2] = ldf(ea + 8); }
static void rgb(uint32_t ea, float* o) { o[0] = ld8(ea) / 255.0f, o[1] = ld8(ea + 1) / 255.0f, o[2] = ld8(ea + 2) / 255.0f; }

static bool take(Snapshot& s) {
    static const uint32_t kEnvLight = GD(0x10475A68);  // g_env_light (dKy_getEnvlight 02555D0C)
    static const uint32_t kPlay = GD(0x1046F0B0);      // g_dComIfG_gameInfo.play (025200D4)
    uint32_t packet = ld32(kEnvLight + 0xA94);
    uint32_t camera = ld32(kPlay + 0x5AF8), view = ld32(kPlay + 0x5FA4);
    if (!packet || !camera || !view) return false;
    vec3(camera + 0xDC, s.eye);
    vec3(camera + 0xE8, s.center);
    s.bank = (float)(int16_t)ld16(camera + 0x100) * (3.14159265f / 32768.0f);
    s.fovy = ldf(view + 0xD4);
    s.viewNear = ldf(view + 0xCC), s.viewFar = ldf(view + 0xD0), s.viewAspect = ldf(view + 0xD8);
    for (int i = 0; i < 12; i++) s.viewRot[i] = ldf(view + 0x1E4 + 4 * i);
    s.sunVisible = 0;
    if (uint32_t sun = ld32(kEnvLight + 0xA34)) {
        vec3(sun + 0x98, s.sunPos);
        s.sunVisible = 1;
        s.sunAlpha = ldf(sun + 0xCC);
    }
    vec3(kEnvLight + 0xB44, s.moonPos);  // mMoonPos
    rgb(kEnvLight + 0xB98, s.cloudColor);
    rgb(kEnvLight + 0xB9C, s.centerColor);
    rgb(kEnvLight + 0xB8C, s.fogColor);
    s.cloudScale = ldf(kEnvLight + 0x10DC);
    s.centerScale = ldf(kEnvLight + 0x10E0);
    s.strength = ldf(kEnvLight + 0xA90);
    s.count = (int32_t)ld32(kEnvLight + 0xA90 - 4);  // mVrkumoCount (+0xA8C)
    s.curTime = ldf(kEnvLight + 0x1020);
    s.phase = ldf(packet + 0x11D4);
    if (getenv("WWHD_CLOUDS_STORMPOINT")) {  // research: the sea storm point and radius (room 9 rule)
        static int n = 0;
        if (n++ % 600 == 0)
            LOG("[clouds] storm point %.0f %.0f %.0f radius %.0f, eye %.0f %.0f %.0f", ldf(GD(0x104775B0)), ldf(GD(0x104775B4)),
                ldf(GD(0x104775B8)), ldf(GD(0x10050130)), s.eye[0], s.eye[1], s.eye[2]);
    }
    static const uint32_t kConst[8] = {GD(0x1004FCD8), GD(0x1004FCDC), GD(0x1004FCF8), GD(0x1004FCFC),
                                       GD(0x1004FD00), GD(0x1004FD04), GD(0x1004FC98), GD(0x1004FCC8)};
    for (int i = 0; i < 8; i++) s.k[i] = ldf(kConst[i]);
    s.n = 0;
    s.nShadow = 0;
    // shadow anchors (host state, game memory untouched): a cloud slot keeps its world position and moves
    // only by its own drift (the change of its camera-relative position from frame to frame); a jump
    // (the game respawns or wraps the slot) re-anchors it around the camera
    static bool valid[100];
    static float anchor[100][2], lastRel[100][2];
    for (int i = 0; i < 100; i++) {
        uint32_t e = packet + 0xA4 + 0x2C * i;
        float rx = ldf(e + 4), rz = ldf(e + 12), a = ldf(e + 0x20);
        float dx = rx - lastRel[i][0], dz = rz - lastRel[i][1];
        if (!valid[i] || dx * dx + dz * dz > 400.0f * 400.0f || !(a > 0.0001f)) {
            anchor[i][0] = s.eye[0] + rx, anchor[i][1] = s.eye[2] + rz;
        } else {
            anchor[i][0] += dx, anchor[i][1] += dz;
        }
        lastRel[i][0] = rx, lastRel[i][1] = rz;
        valid[i] = a > 0.0001f;
        if (!valid[i]) continue;
        // a world size per cloud slot (the game's card sizes are angular, meant for the far sky): 1200..2500
        // units, a little larger in stronger weather
        static const float sizeK = getenv("WWHD_CLOUD_SHADOW_SIZE") ? (float)atof(getenv("WWHD_CLOUD_SHADOW_SIZE")) : 0.55f;
        uint32_t h = (uint32_t)i * 2654435761u;
        h ^= h >> 13;
        float r = (1200.0f + 1300.0f * (float)(h & 0xFFFF) / 65535.0f) * (1.0f + 0.4f * s.strength) * sizeK;
        float* o = s.shadow[s.nShadow++];
        o[0] = anchor[i][0] - s.eye[0], o[1] = anchor[i][1] - s.eye[2], o[2] = r, o[3] = a;
    }
    for (int i = 0; i < 100; i++) {
        uint32_t e = packet + 0xA4 + 0x2C * i;
        float alpha = ldf(e + 0x20);
        if (!(alpha > 0.0001f)) continue;
        Cloud& c = s.c[s.n++];
        vec3(e + 4, c.pos);
        c.height = ldf(e + 0x1C);
        c.alpha = alpha;
        c.falloff = ldf(e + 0x24);
        c.index = i;
    }
    return true;
}

}  // namespace cloud_proto

using namespace cloud_proto;

// research / parity gate: WWHD_CLOUDS_STATELOG=1 logs a digest of the game's sky-cloud state (read-only)
// every 30 logic steps counted from the last full save-state load (so two runs line up even when the
// load lands at a different frame), and every 30 calls before any load
static void state_log() {
    static const bool on = getenv("WWHD_CLOUDS_STATELOG") != nullptr;
    if (!on) return;
    static uint64_t calls = 0, lastStep = ~0ull;
    calls++;
    const uint64_t loadStep = ss::last_load_step(), step = interp::logic_steps();
    uint64_t at;
    if (loadStep) {
        if (step == lastStep || step < loadStep || (step - loadStep) % 30) return;
        at = step - loadStep;
    } else {
        if (calls % 30) return;
        at = calls;
    }
    lastStep = step;
    const uint32_t packet = ld32(GD(0x10475A68) + 0xA94);
    if (!packet) return;
    uint64_t h = 1469598103934665603ull;
    int alive = 0;
    for (uint32_t a = packet + 0xA4; a < packet + 0xA4 + 0x2C * 100; a += 4) h = (h ^ ld32(a)) * 1099511628211ull;
    for (int i = 0; i < 100; i++) alive += u32_as_f32(ld32(packet + 0xA4 + 0x2C * i + 0x20)) > 0.0001f;
    const uint32_t play = GD(0x1046F0B0), cam = ld32(play + 0x5AF8);
    LOG("[cloudstate] %s %llu: digest %016llx, %d visible, time %.4f, eye %.3f %.3f %.3f", loadStep ? "load+step" : "call",
        (unsigned long long)at, (unsigned long long)h, alive, u32_as_f32(ld32(GD(0x10475A68) + 0x1020)),
        cam ? u32_as_f32(ld32(cam + 0xDC)) : 0.f, cam ? u32_as_f32(ld32(cam + 0xE0)) : 0.f, cam ? u32_as_f32(ld32(cam + 0xE4)) : 0.f);
}

extern "C" void hook_02575B6C(Cpu* c) {
    state_log();
    // inactive (bit-identical to the original) unless toon clouds or a reduced horizon haze are on
    if (!mode() && render::horizon_haze() >= 1.0f && !render::cloud_shadows()) { f_02575B6C_orig(c); return; }
    uint32_t seq = g_seq.load(std::memory_order_relaxed) + 1;
    if (!take(g_ring[seq % kRing])) { f_02575B6C_orig(c); return; }
    g_seq.store(seq, std::memory_order_release);
    gx2::emit(gx2::OP_CLOUDS_BEGIN, {seq});
    f_02575B6C_orig(c);
    gx2::emit(gx2::OP_CLOUDS_END, {seq});
}
