// SPDX-License-Identifier: MPL-2.0
// Based on VR-WakerHD's draw batching (@mkooner), contributed under MPL-2.0.
// From VR-WakerHD (https://github.com/VR-WakerHD/VR-WakerHD) runtime/src/gfx/draw_batch.h at commit e06fb7b99cbb
// (ZeldaWWHDRecomp discussion #535). Copyright (C) 2026 VR-WakerHD (https://github.com/VR-WakerHD/).
// Changes for ZeldaWWHDRecomp: current(), consumed() and can_hold() return variables defined once (gx2_core.cpp)
// instead of function-local statics (the renderer and gx2 saw different copies of those in this binary);
// switch_on() (the default of the run switches).
// Batched same-state runs (issue #1, docs/VR_PERFORMANCE.md "Batched same-state runs"): what gx2 hands the renderer
// for a run of draws that differ only in the addresses of some vertex-shader uniform blocks (the grass draws one tuft
// a draw, each with its own matrix block). gx2 holds such a run (gx2_core.cpp, held_*) and gives it to the renderer as
// one draw with `instances` = its pieces and draw_batch::current() set; the renderer draws it as one instanced draw
// (gfx/vulkan/draw.cpp, batch_*) and sets consumed(), or leaves consumed() false without drawing anything, and gx2
// then draws the pieces one by one as before. Render thread only.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace draw_batch {

constexpr uint32_t kMaxPieces = 1024;  // a held run is given to the renderer at this many (it draws chunks)
struct Batch {
    uint32_t count = 0;         // pieces (the run's draws)
    uint32_t blocks = 0;        // bit i: vertex-shader uniform block i's address differs between the pieces
    uint32_t addrs[kMaxPieces][16];  // each piece's vertex-shader uniform block addresses (register word 0 of each)
};
// the run the renderer's draw is for; null for an ordinary draw
extern thread_local const Batch* t_current;
inline const Batch*& current() { return t_current; }
// the renderer drew current() (set by the renderer; cleared by gx2 before each run)
extern thread_local bool t_consumed;
inline bool& consumed() { return t_consumed; }
// the renderer: may a draw with these registers start a held run (its pieces are drawn on this thread, in order,
// from these registers)? Null: no renderer batches
using CanHold = bool (*)(const uint32_t* regs);
extern CanHold g_canHold;
inline CanHold& can_hold() { return g_canHold; }
// the run switches (WWHD_VK_BATCH_RUNS, WWHD_VK_INDEX_RUNS): "0" turns one off, any other value on; unset: on, except
// in the decomp fast build (its draws are native packets; no translated draws to join there)
inline bool switch_on(const char* name) {
    const char* e = std::getenv(name);
    if (e && *e) return std::strcmp(e, "0") != 0;
#if defined(WWHD_DECOMP_FAST)
    return false;
#else
    return true;
#endif
}

}  // namespace draw_batch
