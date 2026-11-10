// Native draw packets (native renderer stage 1, P1: grass). Decomp build, fast mode, cmake WWHD_NATIVE_DRAW=ON only;
// nothing here exists in any other build.
//
// A hot decompiled draw site (decomp: wwhd_src/include/port_native_draw.h) describes its pass once as a packet
// (OP_PACKET_DEFINE) and each of its draws as an instance (OP_PACKET_DRAW: the clump's uniform block and its kind).
// Both are ordinary commands: recorded into the display list being built, replayed in order with everything else.
//
// The packet is an engine-level description (what is drawn: mesh, material, textures, uniform locations, pass), not
// pre-packed register words, so the site need not change when the backend does. Today the render thread lowers it
// through the existing path: the first instance is drawn by the full draw (the same translation, pipeline, texture
// and support-uniform lookups), which captures what it resolved; the next instances of the same packet only upload
// their uniform block, bind and draw (gfxvk::packet_draw). Anything else that changes state in between (a register
// write, another command, another draw, a new submission or frame, a texture upload, an ended pass) makes the next
// instance lower again. Port features that act per draw (stereo, 60 fps (generated), grass draw distance, GamePad
// backdrop capture, clouds) and the Metal renderer take the full draw for every instance.
//
// The instance's register effect is applied exactly as the site's GX2Set*UniformBlock calls would have applied it, so
// the register file, the context shadow and the full path see the same state as without packets.
//
// Run-time switches (ON builds): WWHD_NATIVE_DRAW=1 turns packets on (off by default); WWHD_NATIVE_DRAW_GRASS=0 keeps
// the grass site off; WWHD_NATIVE_DRAW_VERIFY=N draws every Nth replayable instance through the full path and compares
// what it resolved with the packet (mismatches logged, "[native-draw] VERIFY MISMATCH").
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include <cstdint>

namespace native_draw {

enum Site : uint32_t { kSiteGrass = 1 };
constexpr uint32_t kMaxPackets = 64;
constexpr uint32_t kGrassFirstId = 8;  // grass: ids 8..15, one per pass (RenderContext.mPass)

struct Mesh {
    uint32_t prim = 0, count = 0, indexType = 0, indexAddr = 0;  // as GX2DrawIndexedEx takes them
};
// OP_PACKET_DEFINE payload, word for word
struct Packet {
    uint32_t id = 0;
    uint32_t site = 0, pass = 0;
    uint32_t material = 0;       // guest material (its program and uniform binding table)
    uint32_t program = 0;        // guest shader program (informational today; the native shader's key later)
    uint32_t texture = 0;        // guest GX2Texture the site binds
    uint32_t textureBinding = 0; // guest sampler binding info the site binds it with (material attribute info + 4)
    int32_t location[3] = {-1, -1, -1};  // the instance block's uniform locations: vertex, pixel, geometry
    Mesh mesh[2];                // per instance kind (grass: 0 animated, 1 static)
};
constexpr uint32_t kPacketWords = sizeof(Packet) / 4;
// OP_PACKET_DRAW payload: id, kind, uniform block address, uniform block size
constexpr uint32_t kDrawWords = 4;

// render thread: bumped by every register change and every command other than register writes and packet draws
// (gx2_core.cpp); a packet's lowered state is valid only while it is unchanged
extern uint64_t g_epoch;

const Packet* packet(uint32_t id);  // render thread: the defined packet, or nullptr
void define(const uint32_t* p, uint32_t n);  // render thread: OP_PACKET_DEFINE
// render thread: OP_PACKET_DRAW after its registers were applied: the packet path, or the full draw
void draw(const uint32_t* regs, const Packet& pk, uint32_t kind, uint32_t addr, uint32_t size);
void frame_end();  // render thread, at each swap: the periodic counter line

// fallback / lowering reasons, counted per frame
enum Reason : uint32_t {
    kReplayed, kLowered, kVerified,
    kNotVulkan, kStereo, kFramegen, kGrassDistance, kBackdrop, kClouds, kRefused, kBufferChange, kReasons
};
void count(Reason r);
void verify_mismatch(const char* what);
uint32_t verify_every();  // WWHD_NATIVE_DRAW_VERIFY (0: off)

}  // namespace native_draw
#endif
