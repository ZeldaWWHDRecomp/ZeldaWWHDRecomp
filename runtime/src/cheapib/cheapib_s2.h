// cheapib stage 2: the dynamic draw sites on replayed in-betweens (decomp build, fast mode; cheapib/stage2/PLAN.md).
// Stage 1 replays the logic pass's lists, so the sites that move between passes are frozen there. Stage 2 puts their
// motion back, per class, at the least cost that draws the same picture as a full in-between.
//
// Switch WWHD_CHEAPIB_S2=mask (default 0; with WWHD_CHEAPIB=1 only):
//   1  grass sway, 2 flower sway (blocks only: no game code runs again, nothing is re-recorded). The logic pass's
//      update (dGrass_packet_c::update 0254CF78 / dFlower_packet_c::update 02548B6C) tells the port when it is done
//      (decomp port point PORT_CHEAPIB_SITE); the port then notes each plant it placed: its model packet (a
//      RenderMatrixUniforms pair; the logic list binds mBuffers[mPreviousIndex]), its data entry and its animation
//      slot. On a replayed in-between the port computes each used slot's rotation at this pass's sway angles (the
//      slots are blended at the pass's fraction by interp_fx's fx_hold_blend, as on a full in-between) with the
//      game's own matrix helpers (mDoMtx_YrotS / XrotM / YrotM: pure math, no drawing context), puts the plant's
//      position into the translation column as the update does, and writes the 12 words straight into the logic
//      buffer's storage in the agl uniform layout (word-reversed). Plants without a slot (grass index < 0) do not
//      move. Clip results stay the logic pass's (culling is frozen within a step, lead decision).
//   8  tree sway, the same way (TreePacketState::update 025CA6EC): per drawn tree its trunk and (unless landed) its top
//      packet, at prepareRender's order on the logic pass (rooms sorted by view depth then: kept).
//  16  bush sway, the same way (dWood::Packet_c, hdWood_prepareDraw 025D0BEC): per drawn bush its leaves (the room or
//      the fade packet) and its trunk packet, at the logic pass's order (rooms sorted by view depth then: kept); the
//      matrices are the shared animations' (blended at the pass's fraction by interp_fx's bush hook) plus the position.
//   4  with 1/2/8/16: the sites' frame uniforms (the grass binding, the flower transform set: RenderFrameUniforms) get this
//      pass's camera when it differs from the logic pass's: view, projection x view and projection computed with the
//      game's helpers into the object and written into the logic buffer's fields 0, 1, 8. Their light and fog entries
//      (from the drawing context's environment, which exists only while the logic pass draws) stay the logic pass's.
//  32  material animations (cheapib_s2_material.cpp): the logic pass's draw-time key animator evaluations and model calls
//      are noted in order; a replayed in-between evaluates the moving animators at its frame, applies them to the
//      models they were attached to and writes those models' material uniforms (dirty words) into the logic slots.
//  64  CPU-written vertex sites (cheapib_s2_vertex.cpp): the sea grid (built again into the buffer the next logic pass
//      fills, its bytes written into the logic pass's buffer on the render thread before the pass's calls) and the sea
//      material's camera and scroll (written into the logic buffers, as bit 4 and 32 do).
// 128  sky clouds (vrkumo cards), 256 wave crests (cheapib_s2_sky.cpp): each noted sprite's 4 vertices computed from the
//      records and camera of the replayed pass with the site's own arithmetic, delivered as bit 64 does; the sprites are
//      the ones whose logic-pass bytes that arithmetic reproduces (self-check at the step's first replay).
// 512  the sun, moon and lens flare (cheapib_s2_sun.cpp): their draw functions run again on replayed in-betweens (through
//      interp's hooks, with the pass's camera), the packets' render skipped; the vertices sent as bit 64 does.
// 1024 the weather sprites (cheapib_s2_weather.cpp): the packets' draws run again on replayed in-betweens, their renders
//      skipped; the sprites written now sent into the sides the logic pass drew.
// 2048 particles (cheapib_s2_particles.cpp): the logic pass's JPA manager draws run again on replayed in-betweens with no
//      draw issued, with this pass's camera: each particle's quad into its pool's other buffer (its bytes go to the
//      logic buffer as bit 64's do), its uniform pair's and the emitter's view uniform's uploads into the buffers the
//      logic list binds; a replayed pass's own particle captures of those draws are skipped.
// Hooks: decomp d_grass.cpp / d_flower.cpp (PORT_CHEAPIB_SITE); cheapib_models.h logic_done() -> s2_logic_done(),
// replay() -> s2_rerun() (after stage 1's camera and models).
#pragma once
#include <cstdint>
#include <vector>

struct Cpu;
namespace cheapib {
constexpr int kSiteKinds = 4;  // 0 grass, 1 flower, 2 tree, 3 bush
uint32_t s2_mask();            // WWHD_CHEAPIB_S2 (0 off)
void site(int kind, uint32_t self);
void s2_logic_done();
void s2_rerun(Cpu* c);
// bit 32 (cheapib_s2_material.cpp): interp_fx hook_027DF40C (logic pass, draw time), cheapib_models.h note() /
// logic_done() / replay()
bool s2_mat_on();
void s2_mat_eval(uint32_t anm, uint32_t ts, float from, float cur);
void s2_mat_call(uint32_t model, uint8_t index_before);
void s2_mat_anm_apply(uint32_t backend, uint32_t target);  // decomp port point PORT_CHEAPIB_ANM_APPLY
void s2_mat_logic_done();
void s2_mat_rerun(Cpu* c);
// bit 64 (cheapib_s2_vertex.cpp): interp_fx hook_0246C5E8 (any pass, before the builder), cheapib_models.h logic_done() /
// replay()
void s2_sea_fill(uint32_t pk);
void s2_vtx_logic_done();
void s2_vtx_rerun(Cpu* c);
// shared by the vertex sites: register a site's guest range (b1 notes the slices holding it); send a replayed pass's
// bytes (RANGES: guest address, size, offset into BYTES per range) to the render thread (OP_CHEAPIB_VTX)
void vtx_add_range(uint32_t lo, uint32_t size);
void vtx_emit(std::vector<uint32_t>&& ranges, std::vector<uint8_t>&& bytes);
// drawoff c2 (drawoff.cpp vsnap_build, game thread): this pass's replayed vertex writes into c2's copy DST of guest [ADDR, END)
void c2_vtx_patch(uint32_t addr, uint32_t end, uint8_t* dst);
// bits 128 / 256 (cheapib_s2_sky.cpp): sky clouds (vrkumo) and wave crests; cheapib_models.h logic_done() / replay()
void s2_sky_logic_done();
void s2_sky_rerun(Cpu* c);
// bit 512 (cheapib_s2_sun.cpp): the sun, moon and lens flare; decomp port point PORT_CHEAPIB_SKY_RENDER -> sky_render,
// cheapib_models.h replay()
int sky_render(int kind, uint32_t self, uint32_t a, uint32_t b);
void s2_sun_rerun(Cpu* c);
// bit 1024 (cheapib_s2_weather.cpp): the weather sprites (rain, sea spray, snow, ash, stars, poison, spores, cloud
// shadows), the same way; render kinds 2..9 routed from sky_render
int weather_render(int kind, uint32_t self);
void s2_weather_rerun(Cpu* c);
// bit 2048 (cheapib_s2_particles.cpp): decomp port point PORT_CHEAPIB_PTCL (wwhd_cheapib_ptcl), cheapib_models.h
// logic_done() / replay() (before stage 1's drain), vtx_site (the particles' vertex buffers)
int ptcl_point(int op, uint32_t a, uint32_t b, uint32_t c);
bool ptcl_site(uint32_t addr, uint32_t size);
void s2_ptcl_logic_done();
void s2_ptcl_rerun(Cpu* c);
}  // namespace cheapib
