// Stereo 3D in the Metal renderer (metal.h, gfx/stereo.h): the selected eye's surface cache, the
// frame-boundary latch and the eyes' TV pictures. Both eyes record into the one command buffer.
#include <atomic>
#include <memory>
#include <vector>

#include "metal.h"
#include "gfx/depth_peek.h"
#include "gfx/renderer.h"
#include "runtime.h"
#include "../fixed_seed.h"

namespace gfx {
int stereoEye = 0;
namespace {
SurfaceMap otherSurfaces;
std::vector<Surface*> otherLinearTargets;
uint32_t otherMainDepth = 0;
// the unselected eye's command buffer and open pass (Renderer's)
struct EyeCommands {
    id<MTLCommandBuffer> cmd = nil;
    id<MTLRenderCommandEncoder> enc = nil;
    Surface* passColor[8] = {};
    uint32_t passColorSlice[8] = {};
    Surface* passDepth = nullptr;
    uint32_t passDepthSlice = 0;
} other;
uint64_t flushGeneration = 1;
}  // namespace

void stereo_select_eye(int eye) {
    if (stereoEye == eye) return;
    std::swap(R.cmd, other.cmd);
    std::swap(R.enc, other.enc);
    std::swap(R.passColor, other.passColor);
    std::swap(R.passColorSlice, other.passColorSlice);
    std::swap(R.passDepth, other.passDepth);
    std::swap(R.passDepthSlice, other.passDepthSlice);
    R.surfaces.swap(otherSurfaces);
    R.linearTargets.swap(otherLinearTargets);
    std::swap(R.mainDepthAddr, otherMainDepth);
    stereoEye = eye;
}

SurfaceMap& stereo_other_surfaces() { return otherSurfaces; }

id<MTLCommandBuffer> stereo_take_other_commands() {
    if (other.enc) [other.enc endEncoding];
    other.enc = nil;
    for (auto& c : other.passColor) c = nullptr;
    other.passDepth = nullptr;
    id<MTLCommandBuffer> cmd = other.cmd;
    other.cmd = nil;
    ++flushGeneration;  // (also with nothing recorded: what eye 1 held is either submitted now or was never used)
    return cmd;
}

uint64_t stereo_flush_generation() { return flushGeneration; }

bool stereo_other_pending() { return other.cmd != nil; }

static uint64_t g_release_submits = 0;
void stereo_release_submit() {
    ++g_release_submits;
    flush();
}

// eye 1: where eye 0 rendered a surface both eyes share (stereo::eye_shared_target), eye 0's
static bool eye0_rendered(uint32_t addr) {
    auto range = otherSurfaces.equal_range(addr);
    for (auto it = range.first; it != range.second; ++it)
        if (it->second->gpuWritten) return true;
    return false;
}
SurfaceMap& stereo_surface_cache(const SurfaceDesc& d) {
    return stereoEye && stereo::eye_shared_target(d.width, d.height, d.slices, d.isDepth) && eye0_rendered(d.addr) ? otherSurfaces
                                                                                                             : R.surfaces;
}
SurfaceMap& stereo_copy_cache(uint32_t addr, uint32_t width, uint32_t height, uint32_t layers) {
    return stereoEye && (stereo::eye_shared_target(width, height, layers, true) || stereo::eye_shared_target(width, height, layers, false)) &&
                   eye0_rendered(addr)
               ? otherSurfaces
               : R.surfaces;
}

const Screen& tv_eye(int eye) {
    if (stereo::frame.mode == stereo::Off || !R.tvRight.tex) return R.tv;
    return (eye != 0) != stereo::frame.swap ? R.tvRight : R.tv;
}

// eye 1 starts with copies of eye 0's GPU-only surfaces: the game draws some of them once (shadows,
// captured pictures) and guest memory need not hold their pixels
static void seed_eye1() {
    std::vector<Surface*> seeds;
    for (auto& [addr, s] : R.surfaces)
        if (s->gpuWritten && s->tex && s->mips == 1 && !stereo::eye_shared_target(s->width, s->height, s->slices, s->isDepth) &&
            (s->tex.textureType == MTLTextureType2D || s->tex.textureType == MTLTextureType2DArray))
            seeds.push_back(s.get());
    stereo_select_eye(1);
    for (Surface* s : seeds) {
        SurfaceDesc d;
        d.addr = s->addr; d.mipAddr = s->mipAddr; d.width = s->width; d.height = s->height; d.slices = s->slices;
        d.pitch = s->pitch; d.format = s->format; d.dim = s->dim; d.tileMode = s->tileMode; d.swizzle = s->swizzle;
        d.isDepth = s->isDepth;
        d.sizeRequest.claim = d.sizeRequest.renews = s->sizeClaim.size;
        Surface* dst = find_or_create_surface(d, true);
        if (!dst || !dst->tex) continue;
        if (dst->tex.width == s->tex.width && dst->tex.height == s->tex.height && dst->tex.pixelFormat == s->tex.pixelFormat &&
            dst->tex.arrayLength == s->tex.arrayLength && dst->tex.textureType == s->tex.textureType) {
            id<MTLBlitCommandEncoder> b = [command_buffer() blitCommandEncoder];
            b.label = @"metal_stereo.mm seed";
            for (NSUInteger slice = 0; slice < s->tex.arrayLength; slice++)
                [b copyFromTexture:s->tex sourceSlice:slice sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                        sourceSize:MTLSizeMake(s->tex.width, s->tex.height, 1) toTexture:dst->tex destinationSlice:slice
                  destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
            [b endEncoding];
        } else if (!s->isDepth) {
            resample(s->tex, dst->tex, dst->fmt, s->slices);
        } else {
            continue;  // (eye 1 draws it again)
        }
        mark_gpu_written(dst);
        dst->writeSeq = s->writeSeq;  // the eyes' aliases keep their order
        dst->writtenBackSeq = s->writtenBackSeq;
    }
    stereo_select_eye(0);
    // before eye 0's next commands, which are committed first, change what the copies read
    void flush();
    flush();
}

// stereo 3D's automatic HUD distance (stereo::hud): the nearest depth of the frame just drawn, from a grid of
// eye 0's main depth buffer, read back when the GPU has drawn it (no wait), every other frame
static void sample_hud_depth() {
    if (stereo::frame.mode == stereo::Off || !stereo::requestedHudAuto || (R.frame & 1)) return;
    float nearPlane, k;
    Surface* depth = main_depth();
    if (!depth || !stereo::hud::depth_linearization(nearPlane, k)) return;
    constexpr uint32_t n = stereo::hud::kSamples, stride = 4;
    // readback buffers, reused once the GPU is done with them (a busy one skips this sample)
    struct Readback { id<MTLBuffer> buffer; std::shared_ptr<std::atomic<bool>> busy; };
    static Readback ring[3];
    static uint32_t next = 0;
    Readback& r = ring[next++ % 3];
    if (!r.buffer) {
        r.buffer = [R.device newBufferWithLength:n * stride options:MTLResourceStorageModeShared];
        r.busy = std::make_shared<std::atomic<bool>>(false);
    }
    if (!r.buffer || r.busy->exchange(true)) return;
    static uint32_t xy[n * 2], gridW = 0, gridH = 0;
    if (gridW != depth->tex.width || gridH != depth->tex.height) {
        gridW = (uint32_t)depth->tex.width, gridH = (uint32_t)depth->tex.height;
        for (uint32_t i = 0; i < n; i++) stereo::hud::grid_point(i, gridW, gridH, xy[i * 2], xy[i * 2 + 1]);
    }
    read_depth_texels(depth, xy, n, r.buffer, stride);
    const bool d16 = depth->tex.pixelFormat == MTLPixelFormatDepth16Unorm;
    id<MTLBuffer> buffer = r.buffer;
    std::shared_ptr<std::atomic<bool>> busy = r.busy;
    [command_buffer() addCompletedHandler:^(id<MTLCommandBuffer> done) {
        if (done.status == MTLCommandBufferStatusCompleted) {
            const uint8_t* raw = (const uint8_t*)buffer.contents;
            float depths[n];
            for (uint32_t i = 0; i < n; i++) depths[i] = depth_peek::texel(raw + i * stride, d16);
            stereo::hud::observe(depths, n, nearPlane, k);
        }
        *busy = false;
    }];
}

void stereo_latch() {
    render::stereo_test_switch(fixed_seed::script_frame(R.frame));
    if (R.frame % 300 == 1 && stereo::frame.mode != stereo::Off) {
        LOG("[stereo] last 300 frames: %llu early submissions of eye 1's commands (eye 0 rewrote a surface they sample)",
            (unsigned long long)g_release_submits);
        g_release_submits = 0;
    }
    sample_hud_depth();
    const bool wasOff = stereo::frame.mode == stereo::Off;
    stereo::latch();
    const bool off = stereo::frame.mode == stereo::Off;
    if (off && !wasOff) {
        stereo_select_eye(0);
        // command buffers in flight retain the textures they use
        otherSurfaces.clear();
        otherLinearTargets.clear();
        otherMainDepth = 0;
        forget_texture_views();
        stereo_release_eye_allocations();
        R.tvRight.tex = nil;
        R.tvRight.scanSpares.clear();
    } else if (!off && wasOff) {
        try { seed_eye1(); } catch (...) { stereo_select_eye(0); throw; }
    }
}
}  // namespace gfx
