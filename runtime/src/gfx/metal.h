#include <atomic>
// Metal renderer internals (Objective-C++).
#pragma once
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "formats.h"

namespace gfx {
void peek_z(const uint32_t*, uint32_t);

// A host texture backing a guest surface (render target, depth buffer or sampled texture).
struct Surface {
    std::shared_ptr<Surface> mipChain; // sampled companion assembled from GPU-rendered levels
    uint64_t mipChainSeq = ~0ull;
    id<MTLTexture> tex = nil;
    uint32_t addr = 0, mipAddr = 0;
    uint32_t width = 0, height = 0, slices = 1, pitch = 0, mips = 1;
    uint32_t format = 0;       // E_GX2SURFFMT
    uint32_t dim = 1;          // E_DIM
    uint32_t tileMode = 0;     // E_HWTILEMODE
    uint32_t swizzle = 0;
    bool isDepth = false;
    bool gpuWritten = false;   // contents produced by the GPU; never reload from guest memory
    uint64_t writeSeq = 0;     // when the GPU last wrote it (several surfaces can alias one address)
    bool formatViews = false;     // another surface at this address has the same texel bits in another format (adopt_newer_alias)
    uint64_t writtenBackSeq = 0;  // writeSeq when last written back to guest memory (linear surfaces)
    uint64_t contentHash = 0;  // hash of all guest bytes (every level) at the last check
    uint64_t lastCheckedFrame = ~0ull;
    uint64_t sparseHash = 0;   // fallback without write tracking: cheap per-frame sampled check
    uint32_t dataSize = 0;     // base level size in guest memory
    bool dirty = true;         // new, or invalidated by the game: do a full check
    // CPU textures: guest ranges of all levels (base first) and the write stamp (write_watch.h) they
    // were armed with at the last full check; no newer stamp on their pages = unchanged
    std::vector<std::pair<uint32_t, uint32_t>> levelRanges;
    uint64_t watchStamp = 0;
    bool watched = false;
    FormatInfo fmt;
    // internal resolution: width/height above are the guest's (logical) size, used for every lookup and
    // guest-memory computation; the texture may be larger. sx/sy = texture size / logical size.
    float scale = 1.0f;        // the resolution factor the texture was made for
    float ax = 1.0f, ay = 1.0f;  // ... and the aspect-ratio factors (screen-shaped targets, metal_surfaces.mm)
    float sx = 1.0f, sy = 1.0f;
};

// A display output: the TV or the GamePad screen, each in its own window.
struct Screen {
    id<MTLTexture> tex = nil;          // last image copied to the scan buffer
    CAMetalLayer* layer = nil;
    std::atomic<bool> srgb{false};     // scan buffer is sRGB: the display applies the encoding
    std::atomic<bool> visible{true};   // nextDrawable blocks while the window is covered
};

uint64_t next_write_seq();
inline void mark_gpu_written(Surface* s) { s->gpuWritten = true; s->writeSeq = next_write_seq(); }

struct Renderer {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;

    id<MTLCommandBuffer> cmd = nil;
    id<MTLRenderCommandEncoder> enc = nil;
    bool binding = false;  // a draw is binding its textures to the open encoder (no blits now)
    // attachments of the open render encoder
    Surface* passColor[8] = {};
    uint32_t mainDepthAddr = 0;
    Surface* passDepth = nullptr;
    uint32_t passColorSlice[8] = {}, passDepthSlice = 0;

    // guest memory visible to the GPU (no-copy buffers)
    struct GuestRange {
        uint32_t base, size;
        id<MTLBuffer> buf;
    };
    std::vector<GuestRange> guest;

    // outputs
    Screen tv, drc;
    id<MTLRenderPipelineState> presentPipeline = nil;
    id<MTLRenderPipelineState> presentPipelineSRGB = nil;
    id<MTLSamplerState> linearClamp = nil;

    // surfaces keyed by guest address (several may share an address with different shapes)
    std::unordered_multimap<uint32_t, std::unique_ptr<Surface>> surfaces;
    std::vector<Surface*> linearTargets;  // linear-aligned colour surfaces (never removed): GX2DrawDone write-back

    uint64_t frame = 0;
    uint64_t drawCount = 0;
};
extern Renderer R;

// command buffer / encoder management
id<MTLCommandBuffer> command_buffer();
void end_encoder();

// guest memory -> (buffer, offset); returns nil if the address is not GPU-visible
id<MTLBuffer> guest_buffer(uint32_t addr, uint32_t* offset);

// surfaces
struct SurfaceDesc {
    uint32_t addr = 0, mipAddr = 0, width = 0, height = 0, slices = 1, pitch = 0, mips = 1;
    uint32_t format = 0, dim = 1, tileMode = 0, swizzle = 0;
    bool isDepth = false;
};
Surface* find_or_create_surface(const SurfaceDesc& d, bool forRendering);
// render targets; `slice` receives the array slice the view renders to
Surface* color_target(const uint32_t* regs, int index, uint32_t* slice = nullptr);  // from CB_COLOR* registers
Surface* depth_target(const uint32_t* regs, uint32_t* slice = nullptr);             // from DB_DEPTH_* registers
// from GX2ColorBuffer / GX2DepthBuffer structs; optional: the view's slice range
Surface* surface_from_color_buffer(uint32_t gx2ColorBuffer, uint32_t* firstSlice = nullptr, uint32_t* numSlices = nullptr);
Surface* surface_from_depth_buffer(uint32_t gx2DepthBuffer, uint32_t* firstSlice = nullptr, uint32_t* numSlices = nullptr);
Surface* sampled_texture(const uint32_t* texWords, bool isDepthSampler);  // from SQ_TEX_RESOURCE words
void upload_surface(Surface* s);

// internal resolution (Graphics menu / R / WWHD_RES_SCALE): render targets are allocated at this
// multiple of their guest size; a change takes effect at the next frame (targets are resized on next use)
float res_scale();          // the factor in effect this frame
void set_res_scale(float f);
void latch_res_scale();     // frame boundary: apply a requested change (resolution and aspect ratio)
// aspect ratio of the TV picture the game is drawing from the next frame on (aspect.cpp via the swap
// command); screen-shaped render targets are made that much wider/taller than their guest size
void set_frame_aspect(float a);
// draws `src` (whole texture, or its top-left uvMax fraction) stretched over `dst`'s rect (pixels; w=0: all)
void resample(id<MTLTexture> src, id<MTLTexture> dst, const FormatInfo& fmt, uint32_t slices, float uMax = 1, float vMax = 1,
              uint32_t dstW = 0, uint32_t dstH = 0);
void forget_texture_views();  // metal_draw.mm: textures were replaced

}  // namespace gfx
