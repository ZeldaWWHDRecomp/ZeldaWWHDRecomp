// Native draw packets (gx2/native_draw.h): the decompiled sites' entry points (game thread) and the render thread's
// packet table and dispatch. Decomp build, fast mode, WWHD_NATIVE_DRAW=ON only.
#include "gx2/native_draw.h"
#include "gx2/gx2_cmd.h"
#include "runtime.h"
#include "framegen.h"
#include "cloud_proto.h"
#include "mods/mod_actor_shadow_proof.h"
#include "gfx/renderer.h"
#include "gfx/stereo.h"
#ifdef WWHD_HAS_VULKAN
#include "gfx/vulkan/api.h"
#endif
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace gfxvk {
bool gamepad_backdrop_capture_active();
}

namespace native_draw {

uint64_t g_epoch = 1;

// ---------------------------------------------------------------- switches
static int env_int(const char* name, int fallback) {
    const char* e = std::getenv(name);
    return e && *e ? std::atoi(e) : fallback;
}
static bool grass_on() {
    static const bool on = [] {
        const bool master = env_int("WWHD_NATIVE_DRAW", 0) != 0, grass = env_int("WWHD_NATIVE_DRAW_GRASS", 1) != 0;
        LOG("[native-draw] grass packets: %s", master && grass ? "on (WWHD_NATIVE_DRAW=1)"
                                               : master ? "off (WWHD_NATIVE_DRAW_GRASS=0)" : "off (WWHD_NATIVE_DRAW=0)");
        return master && grass;
    }();
    return on;
}
uint32_t verify_every() {
    static const uint32_t n = uint32_t(std::max(0, env_int("WWHD_NATIVE_DRAW_VERIFY", 0)));
    return n;
}

// ---------------------------------------------------------------- game thread: the grass site
// j3d::RenderUniformBindEntries::bindMatrix (027FF88C), the grass model packets' getShape: binds the pair's previous
// buffer (RenderUniformPair: mBuffers at +0x10, 28 bytes each, CPU storage +4, stride +12; mPreviousIndex +0x4C) at
// the material's slot 2 locations (RenderUniformLocations_l: count +12, bindings +16, 20 bytes each; vertex +12,
// pixel +14, geometry +16)
constexpr uint32_t kBindMatrix = 0x027FF88C;
static bool matrix_locations(uint32_t material, int32_t loc[3]) {
    if (ld32(material + 12) <= 2) return false;  // (bindMatrix then reads guest address 0's record: left to it)
    const uint32_t entry = ld32(material + 16) + 2 * 20;
    for (int i = 0; i < 3; ++i) loc[i] = int16_t(ld16(entry + 12 + 2 * i));
    return loc[0] != -1 || loc[1] != -1 || loc[2] != -1;
}
struct SiteState {
    bool open = false;
    Packet pk;
    uint32_t data = 0, size = 0;  // the pending instance's block
    bool pending = false;
};
static thread_local SiteState t_grass;

}  // namespace native_draw

using namespace native_draw;

extern "C" unsigned wwhd_native_grass_begin(unsigned pass, unsigned material, unsigned indices, unsigned texture) {
    SiteState& s = t_grass;
    s.open = false;
    s.pending = false;
    if (!grass_on() || !material || pass >= 8) return 0;
    Packet& pk = s.pk;
    pk = Packet{};
    if (!matrix_locations(material, pk.location)) return 0;
    pk.id = kGrassFirstId + pass;
    pk.site = kSiteGrass;
    pk.pass = pass;
    pk.material = material;
    const uint32_t model = ld32(material);
    pk.program = model ? ld32(ld32(model + 0x7C) + 0x28) : 0;  // Model.mpShaderResource -> ShaderResource.mpShader
    pk.texture = texture;
    pk.textureBinding = (ld32(material + 0x14) ? ld32(material + 0x18) : 0) + 4;
    // the site's GX2DrawIndexedEx(+4, +12, +0, +8, 0, 1) of each kind's index record (24 bytes each)
    for (uint32_t k = 0; k < 2; ++k) {
        const uint32_t b = indices + 24 * k;
        pk.mesh[k] = {ld32(b + 4), ld32(b + 12), ld32(b + 0), ld32(b + 8)};
    }
    static_assert(sizeof(Packet) == kPacketWords * 4);
    gx2::emit(gx2::OP_PACKET_DEFINE, reinterpret_cast<const uint32_t*>(&pk), kPacketWords);
    s.open = true;
    return 1;
}

extern "C" unsigned wwhd_native_grass_instance(unsigned packet, unsigned material) {
    SiteState& s = t_grass;
    s.pending = false;
    if (!s.open || material != s.pk.material) return 0;
    const uint32_t vt = ld32(packet + 0xC);
    if (!vt || ld32(vt + 0x2C) != kBindMatrix) return 0;
    int32_t loc[3];
    if (!matrix_locations(material, loc) || std::memcmp(loc, s.pk.location, sizeof loc)) return 0;
    const uint32_t buffer = packet + 0x10 + ld32(packet + 0x4C) * 28;
    s.data = ld32(buffer + 4);
    s.size = ld32(buffer + 12);
    s.pending = true;
    return 1;
}

extern "C" void wwhd_native_grass_draw(unsigned kind) {
    SiteState& s = t_grass;
    if (!s.pending || kind > 1) return;  // (never: the site asks for the draw only after a taken instance)
    s.pending = false;
    // as the three GX2Set*UniformBlock calls (pixel, vertex, geometry) and GX2DrawIndexedEx would
    for (int i : {1, 0, 2})
        if (s.pk.location[i] != -1) gx2::note_uniform_block(s.data, s.size);
    mod_actor_shadow_proof::draw_submitted(s.pk.mesh[kind].count);
    const uint32_t w[kDrawWords] = {s.pk.id, kind, s.data, s.size};
    gx2::emit(gx2::OP_PACKET_DRAW, w, kDrawWords);
}

namespace native_draw {

// ---------------------------------------------------------------- render thread
static std::array<Packet, kMaxPackets> g_packets;
static std::array<bool, kMaxPackets> g_defined{};

const Packet* packet(uint32_t id) { return id < kMaxPackets && g_defined[id] ? &g_packets[id] : nullptr; }

void define(const uint32_t* p, uint32_t n) {
    if (n != kPacketWords || p[0] >= kMaxPackets) return;
    std::memcpy(&g_packets[p[0]], p, sizeof(Packet));
    g_defined[p[0]] = true;
}

static uint64_t g_counts[kReasons], g_instances = 0, g_mismatches = 0, g_frames = 0;
void count(Reason r) { ++g_counts[r]; }
void verify_mismatch(const char* what) {
    if (g_mismatches++ < 32) LOG("[native-draw] VERIFY MISMATCH: %s", what);
}

void draw(const uint32_t* regs, const Packet& pk, uint32_t kind, uint32_t addr, uint32_t size) {
    ++g_instances;
    const Mesh& m = pk.mesh[kind & 1];
    Reason why = kNotVulkan;
#ifdef WWHD_HAS_VULKAN
    if (render::vulkan()) {
        if (stereo::frame.mode != stereo::Off) why = kStereo;
        else if (framegen::requested()) why = kFramegen;
        else if (render::grass_distance() > 0 && pk.site == kSiteGrass) why = kGrassDistance;
        else if (gfxvk::gamepad_backdrop_capture_active()) why = kBackdrop;
        else if (cloud_proto::g_active_seq) why = kClouds;
        else {
            gfxvk::packet_draw(regs, pk, kind & 1, addr, size);  // replayed, or drawn the full way (counted there)
            return;
        }
    }
#endif
    count(why);
    render::draw(regs, m.prim, m.count, m.indexType, m.indexAddr, 0, 1);
}

void frame_end() {
    ++g_frames;
    static const bool on = grass_on();
    if (g_frames % 300) return;
    static const char* const names[kReasons] = {"replayed", "lowered", "verified", "not Vulkan", "stereo", "framegen",
                                                "grass distance", "backdrop", "clouds", "refused", "buffer change"};
    char buf[600];
    int k = std::snprintf(buf, sizeof buf, "[native-draw] grass %s, last 300 frames: %.1f packet instances/frame",
                          on ? "on" : "off", g_instances / 300.0);
    for (uint32_t r = 0; r < kReasons; ++r)
        if (g_counts[r]) k += std::snprintf(buf + k, sizeof buf - k, ", %s %.1f", names[r], g_counts[r] / 300.0);
    if (verify_every()) std::snprintf(buf + k, sizeof buf - k, ", verify mismatches %llu", (unsigned long long)g_mismatches);
    LOG("%s", buf);
    g_instances = 0;
    std::memset(g_counts, 0, sizeof g_counts);
}

}  // namespace native_draw
