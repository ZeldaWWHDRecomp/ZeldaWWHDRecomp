// native_draw/j3d_desc_depth_inst.h: the flush points of shadow-slice instancing (WWHD_NATIVE_J3D_DESC_DEPTH_INST=1,
// native_draw/j3d_desc_depth.h), for files that do not include Vulkan: the render thread's command dispatch
// (gx2_core.cpp), the translated draw (gfx/vulkan/draw.cpp), depth clears (surfaces.cpp) and submissions (backend.cpp).
// Collected depth draws are drawn (inst_flush) before any command that may touch or read the cascade they render into;
// see NATIVE-J3D-DESC.md section 9.2. Decomp build, fast mode, WWHD_NATIVE_DRAW=ON only.
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include <cstdint>

namespace native_draw::j3ddesc::depth {
extern bool g_instPending;  // collected draws wait for their flush (the only test on the hot paths)
// a translated draw with this register file commutes with the collected draws: into the collected cascade (any layer),
// depth test + write with LESS or LEQUAL, no stencil, no colour written to a bound target. It may stay in between
// without a flush (the stored depth is the minimum over every passing fragment, in any order).
bool inst_commutes(const uint32_t* regs);
// the same rule without the target (a described draw's state image; the target is compared per draw)
bool commutes_image(const uint32_t* regs);
// the render thread's command dispatch, before command OP (payload P, N words, REGS: the settled register file):
// flushes unless OP leaves the cascade alone (register writes, descriptions, names, guest-memory invalidates), has its
// own check (described draws, translated draws, depth clears) or is a packet draw that commutes
void inst_before_op(uint32_t op, const uint32_t* p, uint32_t n, const uint32_t* regs);
// a depth clear of layers FIRST .. FIRST + NUM - 1 of SURFACE (gfxvk::Surface*): flushes when it clears a collected layer
void inst_before_clear(const void* surface, uint32_t first, uint32_t num);
// ends the open pass and draws the collection (nothing collected: nothing done). WHERE names the flush point (counters).
void inst_flush_now(const char* where);
}  // namespace native_draw::j3ddesc::depth
#endif
