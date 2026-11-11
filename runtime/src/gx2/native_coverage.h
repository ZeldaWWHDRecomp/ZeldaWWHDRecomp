// Native renderer coverage counter (decomp build, fast mode, cmake WWHD_NATIVE_DRAW=ON only; debug, off by default).
//
// WWHD_NATIVE_COVERAGE=<file>[:<from swap>:<to swap>] (default swaps 3600-3900) counts, on the game side, every
// graphics operation the game issues between the two swaps, per host call chain (frame-pointer walk, 24 frames):
//   draw      a GX2DrawEx / GX2DrawIndexedEx call: the full GX2 path
//   list      the same, recorded into a display list (drawn when the list is called)
//   packet    a native draw packet instance (native_draw.cpp: a site's wwhd_native_draw / _draw_range)
//   copy      GX2CopySurface, GX2CopyColorBufferToScanBuffer
//   clear     GX2ClearColor / GX2ClearDepthStencilEx / GX2ClearBuffersEx
//   expand    GX2ExpandAAColorBuffer / GX2ExpandDepthBuffer
//   target    GX2SetColorBuffer / GX2SetDepthBuffer (render-target switches)
//   calllist  GX2CallDisplayList / GX2DirectCallDisplayList
// plus, per chain, in-order runs and distinct meshes per render-target set and frame (as the P3 survey counter). The
// file is written at the last swap with the image slide; native-p6/tools/coverage.py symbolises it (nm) and groups the
// chains by family (UI/2D, post, shadows, render-to-texture, J3D, ...). Packets ON or OFF alike: with packets on, a
// covered draw counts as "packet" at its site, the rest as "draw". Mac only (other platforms: a log line, no file).
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include <cstdint>

namespace native_draw::coverage {
enum Op : uint32_t { kDraw, kList, kPacket, kCopy, kClear, kExpand, kTarget, kCallList, kOps };
bool on();  // WWHD_NATIVE_COVERAGE set (cheap after the first call)
// one operation of kind OP (game side). LR: the guest caller (0 if unknown); COUNT: indices / vertices; MESH: the
// index (or first vertex) address, for runs and distinct meshes; a target switch starts a new render-target set
void op(Op op, uint32_t lr, uint32_t count, uint32_t mesh);
void swap();  // GX2SwapScanBuffers
}  // namespace native_draw::coverage

#define NATIVE_COVERAGE(op_, lr_, count_, mesh_)                                                           \
    do {                                                                                                   \
        if (native_draw::coverage::on()) native_draw::coverage::op(native_draw::coverage::op_, lr_, count_, mesh_); \
    } while (0)
#else
#define NATIVE_COVERAGE(op_, lr_, count_, mesh_) ((void)0)
#endif
