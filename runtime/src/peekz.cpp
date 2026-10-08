// Adapted from GreenNaugahyde/ZeldaWWHDRecompAndroid commit 73b54e1.
// Depth peeks (dDlst_peekZ_c): the game asks for the depth at a few screen points and decides from
// the answer whether something is in front. The sun does this every step for five points around it
// (dKyr_sun_move 02565E0C: a point counts as visible when its depth is >= 0xFFFFFF, nothing drawn
// there); its corona and lens flare fade in only when enough points are visible.
//
// dDlst_peekZ_c (the draw list's member at +0x60EC): u8 count at +0, then up to 64 cells of
// { s16 x, s16 y, u32* result } from +4, in the 640x480 screen space of the GameCube game.
// newData (0252E34C) adds a cell; peekData (0252E388), called once a frame after the 3D scene is
// drawn (025F03F0), answers them: the HD game reads the depth surface's memory on the CPU. This
// renderer never writes GPU results back to guest memory, so that read found no depth and the sun
// counted as hidden. The hook sends the cells to the render thread instead (OP_PEEK_Z), which
// copies those pixels of the main depth buffer and writes the answers once the GPU is done: a
// frame later, which the game expects anyway (it judges the previous step's answers and asks
// again). WWHD_PEEKZ=0 keeps the game's own function.
#include <algorithm>
#include <cstdlib>
#include <vector>

#include "gx2/gx2_cmd.h"
#include "runtime.h"

extern "C" void f_0252E388_orig(Cpu* c);  // dDlst_peekZ_c::peekData

extern "C" void hook_0252E388(Cpu* c) {
    static const bool off = [] { const char* e = getenv("WWHD_PEEKZ"); return e && atoi(e) == 0; }();
    if (off) { f_0252E388_orig(c); return; }
    const uint32_t obj = c->r[3];
    const uint32_t n = std::min<uint32_t>(ld8(obj), 64);
    std::vector<uint32_t> cells;
    cells.reserve(n * 3);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t cell = obj + 4 + i * 8;
        uint32_t dst = ld32(cell + 4);
        if (!dst) continue;
        cells.push_back((uint32_t)(int32_t)(int16_t)ld16(cell));
        cells.push_back((uint32_t)(int32_t)(int16_t)ld16(cell + 2));
        cells.push_back(dst);
    }
    if (!cells.empty()) gx2::emit(gx2::OP_PEEK_Z, cells.data(), (uint32_t)cells.size());
    st8(obj, 0);  // as the game's function: the cells are taken
}

#include "gfx/depth_peek.h"
#include <atomic>
#include <mutex>
#include <unordered_map>
namespace gfx::depth_peek {
uint64_t next_ticket() { static std::atomic<uint64_t> serial{0}; return ++serial; }
void publish(uint64_t ticket, const std::vector<uint32_t>& destinations, const std::vector<uint32_t>& depths) {
    // Submission drains can retire newer fences before older ones. Never overwrite a newer answer.
    static std::mutex mutex;
    static std::unordered_map<uint32_t, uint64_t> completed;
    std::lock_guard lock(mutex);
    static const bool log = getenv("WWHD_PEEKZ_LOG") != nullptr;
    for (size_t i = 0; i < destinations.size(); i++) {
        uint32_t dst = destinations[i];
        if (!dst || completed[dst] >= ticket) continue;
        completed[dst] = ticket;
        st32(dst, depths[i]);
        if (log) LOG("[peekz] %08X: %06X", dst, depths[i]);
    }
}
}
