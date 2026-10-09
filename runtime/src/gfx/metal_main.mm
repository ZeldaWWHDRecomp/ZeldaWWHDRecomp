#include "perf_metrics.h"
#include "gfx/depth_peek.h"
// Metal renderer: device, window, presentation, clears and copies.
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <atomic>
#include <set>

#include "gx2/gx2.h"
#include "gx2_texture_regs.h"
#include "metal.h"
#include "runtime.h"
#include "input.h"

// gameplay mods (runtime/src/mods): HUD drawn into the TV image
namespace mods { void draw_overlay(id<MTLCommandBuffer> cmd, id<MTLTexture> tex); }

namespace gfx {
Renderer R;
// GPU depth peeks for the game's sun visibility test (fork 73b54e1, Metal adaptation).
void peek_z(const uint32_t* cells, uint32_t n) {
    Surface* depth = nullptr;
    auto range = R.surfaces.equal_range(R.mainDepthAddr);
    for (auto it = range.first; it != range.second; ++it) {
        auto* s = it->second.get();
        if (s->isDepth && s->width == 1280 && s->height == 720 && s->tex &&
            (!depth || s->writeSeq > depth->writeSeq)) depth = s;
    }
    uint32_t points = std::min(n / 3,64u);
    if (!depth || !points) return;
    auto format = depth->tex.pixelFormat;
    if (format != MTLPixelFormatDepth16Unorm && format != MTLPixelFormatDepth32Float &&
        format != MTLPixelFormatDepth32Float_Stencil8) return;
    id<MTLBuffer> staging = [R.device newBufferWithLength:points*256 options:MTLResourceStorageModeShared];
    if (!staging) return;
    std::vector<uint32_t> destinations(points);
    end_encoder();
    id<MTLBlitCommandEncoder> b = [command_buffer() blitCommandEncoder];
    for (uint32_t i=0; i<points; i++) {
        int x = depth_peek::pixel(cells[i*3],(uint32_t)depth->tex.width,640);
        int y = depth_peek::pixel(cells[i*3+1],(uint32_t)depth->tex.height,480);
        [b copyFromTexture:depth->tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(x,y,0)
            sourceSize:MTLSizeMake(1,1,1) toBuffer:staging destinationOffset:i*256 destinationBytesPerRow:256
            destinationBytesPerImage:256 options:format == MTLPixelFormatDepth32Float_Stencil8 ?
                MTLBlitOptionDepthFromDepthStencil : MTLBlitOptionNone];
        destinations[i] = cells[i*3+2];
    }
    [b endEncoding];
    uint64_t ticket = depth_peek::next_ticket();
    [command_buffer() addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        if (cb.status != MTLCommandBufferStatusCompleted) return;
        const uint8_t* raw = static_cast<const uint8_t*>(staging.contents);
        std::vector<uint32_t> depths(points);
        for (uint32_t i=0; i<points; i++) {
            if (format == MTLPixelFormatDepth16Unorm) {
                uint16_t v; memcpy(&v,raw+i*256,2);
                depths[i] = v == 0xFFFF ? 0xFFFFFF : uint32_t(v) << 8;
            } else {
                float v; memcpy(&v,raw+i*256,4); depths[i] = depth_peek::from_float(v);
            }
        }
        depth_peek::publish(ticket,destinations,depths);
    }];
}


bool log_this_frame();

// windows, full screen, the present shader and the composition of the screens: display.mm
void display_init();
void present_screens();
void request_present_dump(const std::string& path);

// enhancement, toggled in game (Graphics menu or 8; WWHD_FXAA=1 starts with it on)
static std::atomic<bool> g_fxaa{[] { const char* e = getenv("WWHD_FXAA"); return e && atoi(e) != 0; }()};
bool fxaa_enabled() { return g_fxaa.load(std::memory_order_relaxed); }
void set_fxaa(bool v) { g_fxaa = v; LOG("[gfx] edge smoothing (FXAA) %s", v ? "on" : "off"); }

// ---------------------------------------------------------------- guest memory
static void map_guest_range(uint32_t base, uint32_t size) {
    id<MTLBuffer> b = [R.device newBufferWithBytesNoCopy:mem::ptr(base)
                                                  length:size
                                                 options:MTLResourceStorageModeShared
                                             deallocator:nil];
    if (!b) fatal("cannot map guest memory %08X+%X for the GPU", base, size);
    R.guest.push_back({base, size, b});
}

id<MTLBuffer> guest_buffer(uint32_t addr, uint32_t* offset) {
    for (auto& g : R.guest) {
        if (addr - g.base < g.size) {
            *offset = addr - g.base;
            return g.buf;
        }
    }
    return nil;
}

// ---------------------------------------------------------------- command buffers
static uint64_t commandFrame = 0, commandToken = 0;
struct OverlayGpuTime {
    id<MTLCommandBuffer> buffer = nil; // ARC retains it until a later frame can read the result.
    uint64_t frame = 0, token = 0;
};
static std::array<OverlayGpuTime, 256> overlayGpuTimes;
static bool overlayGpuPending = false;
static void read_overlay_gpu_times() {
    if (!perf::enabled()) {
        if (overlayGpuPending) { overlayGpuTimes = {}; overlayGpuPending = false; }
        return;
    }
    static uint64_t lastPoll = UINT64_MAX;
    if (lastPoll == R.frame) return;
    lastPoll = R.frame;
    for (auto& q : overlayGpuTimes) {
        if (!q.buffer || q.frame >= R.frame) continue;
        auto status = q.buffer.status;
        if (status != MTLCommandBufferStatusCompleted && status != MTLCommandBufferStatusError) continue;
        perf::timestamp_read(R.frame - q.frame);
        double ns = status == MTLCommandBufferStatusCompleted
            ? (q.buffer.GPUEndTime - q.buffer.GPUStartTime) * 1e9 : -1;
        perf::gpu_complete(q.frame, q.token, ns);
        q = {};
    }
}
id<MTLCommandBuffer> command_buffer() {
    if (!R.cmd) {
        read_overlay_gpu_times();
        R.cmd = [R.queue commandBuffer];
        commandFrame = R.frame;
        commandToken = perf::gpu_begin(commandFrame);
    }
    return R.cmd;
}

uint32_t g_draws_since_commit = 0;
std::atomic<uint64_t> g_gpu_ns{0};  // GPU busy time, for the periodic report

static void track_gpu_time(id<MTLCommandBuffer> cb) {
    const uint64_t frame = commandFrame;
    const uint64_t token = commandToken;
    // Keep explicit profiler diagnostics, but add no timing completion handler while hidden.
    static const bool diagnostic = [] {
        const char* p = getenv("WWHD_PROFILE"); return p && !strcmp(p, "1");
    }();
    if (token) {
        bool stored = false;
        for (auto& q : overlayGpuTimes) if (!q.buffer) {
            q = {cb, frame, token}; overlayGpuPending = true; stored = true; break;
        }
        if (!stored) perf::gpu_complete(frame, token, -1); // bounded storage; omit partial frames
    }
    if (!diagnostic) return;
    [cb addCompletedHandler:^(id<MTLCommandBuffer> b) {
        double t = b.GPUEndTime - b.GPUStartTime;
        if (t > 0) g_gpu_ns += (uint64_t)(t * 1e9);
    }];
}

void end_encoder() {
    if (R.enc) {
        [R.enc endEncoding];
        R.enc = nil;
        // submit work in chunks so the GPU starts while the frame is still being built (like the
        // hardware command processor), instead of all at once on swap
        static const bool chunked = getenv("WWHD_NO_CHUNK") == nullptr;
        if (chunked && g_draws_since_commit >= 256 && R.cmd) {
            void pool_retire(id<MTLCommandBuffer> cmd);
            pool_retire(R.cmd);
            track_gpu_time(R.cmd);
            [R.cmd commit];
            R.cmd = nil;
            g_draws_since_commit = 0;
        }
    }
    for (auto& c : R.passColor) c = nullptr;
    R.passDepth = nullptr;
}

void pool_retire(id<MTLCommandBuffer> cmd);

void flush() {
    end_encoder();
    if (R.cmd) {
        pool_retire(R.cmd);
        track_gpu_time(R.cmd);
        [R.cmd commit];
        R.cmd = nil;
    }
}

void wait_idle() {
    end_encoder();
    if (R.cmd) {
        id<MTLCommandBuffer> c = R.cmd;
        pool_retire(c);
        [c commit];
        R.cmd = nil;
        [c waitUntilCompleted];
    }
}

// ---------------------------------------------------------------- init
void init() {
    R.device = MTLCreateSystemDefaultDevice();
    if (!R.device) fatal("no Metal device");
    R.queue = [R.device newCommandQueue];
    display_init();
    // GPU-visible guest memory: MEM2 (code data + heaps), runtime objects, foreground bucket, MEM1
    map_guest_range(0x10000000, 0x40000000);
    map_guest_range(0x60000000, 0x10000000);
    map_guest_range(0xE0000000, 0x04000000);
    map_guest_range(0xF4000000, 0x02000000);
    LOG("[gfx] Metal device: %s", R.device.name.UTF8String);
}

void run_main_loop() { [NSApp run]; }

// the MTLDevice's name, for reports (renderer.h Backend::device); "" before init
std::string device_name() { return R.device ? std::string(R.device.name.UTF8String) : std::string(); }

// ---------------------------------------------------------------- clears
static void clear_surface(Surface* s, const float* rgba, bool clearDepth, float depth, bool clearStencil, uint32_t stencil,
                          uint32_t firstSlice = 0, uint32_t numSlices = 1) {
    if (!s || !s->tex) return;
    end_encoder();
    for (uint32_t slice = firstSlice; slice < firstSlice + numSlices; slice++) {
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.depthAttachment.slice = rp.stencilAttachment.slice = slice;
    if (s->tex.textureType == MTLTextureType3D) rp.colorAttachments[0].depthPlane = slice;  // a volume's slice
    else rp.colorAttachments[0].slice = slice;
    if (s->isDepth) {
        rp.depthAttachment.texture = s->tex;
        rp.depthAttachment.loadAction = clearDepth ? MTLLoadActionClear : MTLLoadActionLoad;
        rp.depthAttachment.clearDepth = depth;
        rp.depthAttachment.storeAction = MTLStoreActionStore;
        if (s->fmt.stencil) {
            rp.stencilAttachment.texture = s->tex;
            rp.stencilAttachment.loadAction = clearStencil ? MTLLoadActionClear : MTLLoadActionLoad;
            rp.stencilAttachment.clearStencil = stencil;
            rp.stencilAttachment.storeAction = MTLStoreActionStore;
        }
    } else {
        rp.colorAttachments[0].texture = s->tex;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(rgba[0], rgba[1], rgba[2], rgba[3]);
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    }
    id<MTLRenderCommandEncoder> e = [command_buffer() renderCommandEncoderWithDescriptor:rp];
    [e endEncoding];
    }
    mark_gpu_written(s);
}

void clear_color(const uint32_t* regs, uint32_t cb, const float rgba[4]) {
    uint32_t first = 0, num = 1;
    Surface* s = surface_from_color_buffer(cb, &first, &num);
    if (log_this_frame() && s)
        LOG("[clear] color %08X %ux%u fmt %X -> %.3f %.3f %.3f %.3f", s->addr, s->width, s->height, s->format, rgba[0], rgba[1], rgba[2], rgba[3]);
    clear_surface(s, rgba, false, 0, false, 0, first, num);
}

void clear_depth_stencil(const uint32_t* regs, uint32_t db, float depth, uint32_t stencil, uint32_t flags) {
    // flags: 1 = depth, 2 = stencil
    uint32_t first = 0, num = 1;
    Surface* s = surface_from_depth_buffer(db, &first, &num);
    if (log_this_frame() && s)
        LOG("[clear] depth %08X %ux%ux%u fmt %X pixel %lu -> depth %.4f stencil %u flags %u slices %u+%u", s->addr, s->width, s->height,
            s->slices, s->format, (unsigned long)s->fmt.pixel, depth, stencil, flags, first, num);
    clear_surface(s, nullptr, flags & 1, depth, (flags & 2) != 0, stencil, first, num);
}

// ---------------------------------------------------------------- copies
void copy_surface(uint32_t src, uint32_t srcMip, uint32_t srcSlice, uint32_t dst, uint32_t dstMip, uint32_t dstSlice) {
    void copy_surface_impl(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    copy_surface_impl(src, srcMip, srcSlice, dst, dstMip, dstSlice);
}

void copy_to_scan(uint32_t cb, uint32_t target) {
    // target: 1 = TV, 4/8 = GamePad
    if (log_this_frame()) LOG("[scan] copy %08X to %s", cb, (target & 1) ? "TV" : "DRC");
    Screen& scr = (target & 1) ? R.tv : R.drc;
    Surface* s = surface_from_color_buffer(cb);
    if (!s || !s->tex) return;
    end_encoder();
    // the image as rendered (internal resolution); the present pass scales it to the window
    NSUInteger w = s->tex.width, h = s->tex.height;
    if (!scr.tex || scr.tex.width != w || scr.tex.height != h || scr.tex.pixelFormat != s->tex.pixelFormat) {
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:s->tex.pixelFormat
                                                                                     width:w
                                                                                    height:h
                                                                                 mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;  // render target: mod overlays
        d.storageMode = MTLStorageModePrivate;
        scr.tex = [R.device newTextureWithDescriptor:d];
    }
    id<MTLBlitCommandEncoder> b = [command_buffer() blitCommandEncoder];
    [b copyFromTexture:s->tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
            sourceSize:MTLSizeMake(w, h, 1) toTexture:scr.tex destinationSlice:0 destinationLevel:0
     destinationOrigin:MTLOriginMake(0, 0, 0)];
    [b endEncoding];
    if (target & 1) ::mods::draw_overlay(command_buffer(), scr.tex);  // HUD of gameplay mods (stamina wheel)
}

// debug: WWHD_DUMP_FRAMES=100,300 writes the TV image of those frames to frame_<n>.png
static std::set<uint64_t> g_dump_frames = [] {
    std::set<uint64_t> f;
    if (const char* e = getenv("WWHD_DUMP_FRAMES"))
        for (const char* p = e; *p;) {
            f.insert(strtoull(p, (char**)&p, 10));
            while (*p == ',') p++;
        }
    return f;
}();

// async: write the file when the GPU gets there instead of stalling (keeps frame timing intact)
void set_tv_format(uint32_t gx2Format, bool tv) {
    (tv ? R.tv : R.drc).srgb = (gx2Format & 0x400) != 0;
}

// depth buffers: grey image, contrast-stretched to the range of values present
static void dump_depth(id<MTLTexture> src, const char* name) {
    // all array slices side by side
    uint32_t w = (uint32_t)src.width, h = (uint32_t)src.height, n = (uint32_t)std::max<NSUInteger>(src.arrayLength, 1);
    MTLPixelFormat pf = src.pixelFormat;
    uint32_t bpp = pf == MTLPixelFormatDepth16Unorm ? 2 : 4;
    id<MTLBuffer> buf = [R.device newBufferWithLength:(NSUInteger)w * h * bpp * n options:MTLResourceStorageModeShared];
    end_encoder();
    id<MTLBlitCommandEncoder> b = [command_buffer() blitCommandEncoder];
    for (uint32_t z = 0; z < n; z++)
        [b copyFromTexture:src sourceSlice:z sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
                  toBuffer:buf destinationOffset:(NSUInteger)z * w * h * bpp destinationBytesPerRow:w * bpp destinationBytesPerImage:w * h * bpp
                   options:pf == MTLPixelFormatDepth32Float_Stencil8 ? MTLBlitOptionDepthFromDepthStencil : MTLBlitOptionNone];
    [b endEncoding];
    wait_idle();
    uint32_t W = w * n;
    std::vector<uint8_t> px((size_t)W * h * 4);
    std::string ranges;
    for (uint32_t z = 0; z < n; z++) {
        std::vector<float> v((size_t)w * h);
        for (size_t i = 0; i < v.size(); i++) {
            size_t k = (size_t)z * w * h + i;
            v[i] = bpp == 2 ? ((const uint16_t*)buf.contents)[k] / 65535.0f : ((const float*)buf.contents)[k];
        }
        float lo = 1, hi = 0;
        for (float x : v) if (x < 1.0f) { lo = std::min(lo, x); hi = std::max(hi, x); }
        char r[64];
        snprintf(r, sizeof r, " [%u] %.4f..%.4f", z, lo, hi);
        ranges += r;
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) {
                float f = v[(size_t)y * w + x];
                uint8_t g = f >= 1.0f ? 255 : (uint8_t)std::clamp((f - lo) / std::max(hi - lo, 1e-6f) * 230.0f, 0.0f, 230.0f);
                uint8_t* o = &px[((size_t)y * W + z * w + x) * 4];
                o[0] = o[1] = o[2] = g;
                o[3] = 255;
            }
    }
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(px.data(), W, h, 8, W * 4, cs, (CGBitmapInfo)kCGImageAlphaNoneSkipLast);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL(
        (__bridge CFURLRef)[NSURL fileURLWithPath:[NSString stringWithUTF8String:name]], (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
    CGImageDestinationAddImage(dst, img, nullptr);
    CGImageDestinationFinalize(dst);
    CFRelease(dst);
    CGImageRelease(img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    LOG("[gfx] wrote %s (%ux%u depth x%u, ranges%s)", name, w, h, n, ranges.c_str());
}

void dump_texture(id<MTLTexture> src, const char* name, bool async, bool srgbEncode) {
    if (!src) return;
    uint32_t w = (uint32_t)src.width, h = (uint32_t)src.height;
    MTLPixelFormat pf = src.pixelFormat;
    if (pf == MTLPixelFormatDepth16Unorm || pf == MTLPixelFormatDepth32Float || pf == MTLPixelFormatDepth32Float_Stencil8) {
        dump_depth(src, name);
        return;
    }
    bool rgb10 = pf == MTLPixelFormatRGB10A2Unorm;
    bool rgba8 = pf == MTLPixelFormatRGBA8Unorm || pf == MTLPixelFormatRGBA8Unorm_sRGB;
    bool f16 = pf == MTLPixelFormatRGBA16Float;
    bool r8 = pf == MTLPixelFormatR8Unorm;
    bool rg11 = pf == MTLPixelFormatRG11B10Float;
    if (!rgb10 && !rgba8 && !f16 && !r8 && !rg11) { LOG("[gfx] dump: unsupported format %lu", (unsigned long)pf); return; }
    uint32_t bpp = f16 ? 8 : r8 ? 1 : 4;
    MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pf width:w height:h mipmapped:NO];
    d.storageMode = MTLStorageModeShared;
    id<MTLTexture> t = [R.device newTextureWithDescriptor:d];
    end_encoder();
    id<MTLBlitCommandEncoder> b = [command_buffer() blitCommandEncoder];
    [b copyFromTexture:src sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
             toTexture:t destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    [b endEncoding];
    std::string file = name;
    auto write = [=]() {
    std::vector<uint8_t> raw((size_t)w * h * bpp), px((size_t)w * h * 4);
    [t getBytes:raw.data() bytesPerRow:w * bpp fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint8_t* o = &px[i * 4];
        if (rgb10) {
            uint32_t v; memcpy(&v, &raw[i * 4], 4);
            o[0] = (v & 0x3FF) >> 2; o[1] = ((v >> 10) & 0x3FF) >> 2; o[2] = ((v >> 20) & 0x3FF) >> 2;
        } else if (rgba8) {
            memcpy(o, &raw[i * 4], 3);
        } else if (r8) {
            o[0] = o[1] = o[2] = raw[i];
        } else if (rg11) {
            uint32_t v; memcpy(&v, &raw[i * 4], 4);
            auto f11 = [](uint32_t m, uint32_t e) { float f = e ? ldexpf(1.0f + m / 64.0f, (int)e - 15) : ldexpf(m / 64.0f, -14); return f; };
            float r = f11(v & 0x3F, (v >> 6) & 0x1F), g = f11((v >> 11) & 0x3F, (v >> 17) & 0x1F), bl = ldexpf(1.0f + ((v >> 22) & 0x1F) / 32.0f, (int)((v >> 27) & 0x1F) - 15);
            o[0] = (uint8_t)std::min(255.0f, r * 255); o[1] = (uint8_t)std::min(255.0f, g * 255); o[2] = (uint8_t)std::min(255.0f, bl * 255);
        } else {
            for (int c = 0; c < 3; c++) {
                __fp16 hv; memcpy(&hv, &raw[i * 8 + c * 2], 2);
                o[c] = (uint8_t)std::clamp((float)hv * 255.0f, 0.0f, 255.0f);
            }
        }
        o[3] = 255;
        if (srgbEncode)
            for (int c = 0; c < 3; c++) {
                float v = o[c] / 255.0f;
                v = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1 / 2.4f) - 0.055f;
                o[c] = (uint8_t)std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f);
            }
    }
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(px.data(), w, h, 8, w * 4, cs, (CGBitmapInfo)kCGImageAlphaNoneSkipLast);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    NSString* path = [NSString stringWithUTF8String:file.c_str()];
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:path],
                                                                (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
    CGImageDestinationAddImage(dst, img, nullptr);
    CGImageDestinationFinalize(dst);
    CFRelease(dst);
    CGImageRelease(img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    LOG("[gfx] wrote %s (%ux%u)", file.c_str(), w, h);
    };
    if (async) {
        [command_buffer() addCompletedHandler:^(id<MTLCommandBuffer>) { write(); }];
    } else {
        wait_idle();
        write();
    }
}

// save states: thumbnails and test dumps of the TV image a number of frames from now
static std::mutex g_tv_dump_mu;
static std::vector<std::pair<uint64_t, std::string>> g_tv_dumps;
uint64_t frame_count() { return __atomic_load_n(&R.frame, __ATOMIC_RELAXED); }
void request_tv_dump(const std::string& path, int frames_ahead) {
    std::lock_guard<std::mutex> lk(g_tv_dump_mu);
    g_tv_dumps.push_back({frame_count() + frames_ahead, path});
}
static void service_tv_dumps() {
    std::lock_guard<std::mutex> lk(g_tv_dump_mu);
    for (auto it = g_tv_dumps.begin(); it != g_tv_dumps.end();)
        if (it->first <= R.frame && R.tv.tex) {
            dump_texture(R.tv.tex, it->second.c_str(), true, R.tv.srgb);
            it = g_tv_dumps.erase(it);
        } else {
            ++it;
        }
}

static void dump_tv(uint64_t frame) {
    char name[64];
    snprintf(name, sizeof name, "frame_%llu.png", (unsigned long long)frame);
    dump_texture(R.tv.tex, name, true, R.tv.srgb);
    if (R.drc.tex) {
        snprintf(name, sizeof name, "frame_%llu_drc.png", (unsigned long long)frame);
        dump_texture(R.drc.tex, name, true, R.drc.srgb);
    }
    static const bool present = getenv("WWHD_DUMP_PRESENT") != nullptr;
    if (present) {
        snprintf(name, sizeof name, "frame_%llu_present.png", (unsigned long long)frame);
        request_present_dump(name);
    }
}

void with_autorelease_pool(void (*fn)()) {
    @autoreleasepool {
        fn();
    }
}

static std::atomic<uint64_t> g_frames_completed{0};
uint64_t frames_completed() { return g_frames_completed; }

void cache_warm_step();

void swap() {
    cache_warm_step();
    static bool sync_gpu = getenv("WWHD_SYNC_GPU") != nullptr;  // debug: no CPU/GPU overlap
    if (sync_gpu) wait_idle();
    end_encoder();
    if (log_this_frame()) LOG("[frame] end %llu", (unsigned long long)R.frame);
    R.frame++;
    latch_res_scale();
    if (g_dump_frames.count(R.frame)) dump_tv(R.frame);
    service_tv_dumps();
    const char* capture_begin_frame();
    static std::string pendingCapture;  // the TV image is dumped once the captured frame has been drawn
    if (!pendingCapture.empty()) {
        dump_texture(R.tv.tex, (pendingCapture + "/tv.png").c_str(), true, R.tv.srgb);
        if (R.drc.tex) dump_texture(R.drc.tex, (pendingCapture + "/gamepad.png").c_str(), true, R.drc.srgb);
        request_present_dump(pendingCapture + "/present.png");  // the TV window as shown (overlay, bars)
        LOG("[gfx] capture written to %s", pendingCapture.c_str());
        pendingCapture.clear();
    }
    if (const char* dir = capture_begin_frame()) pendingCapture = dir;
    @autoreleasepool {
        present_screens();
        [command_buffer() addCompletedHandler:^(id<MTLCommandBuffer>) { g_frames_completed++; }];
        flush();
    }
    perf::gpu_advance(R.frame);
    if (R.frame % 300 == 1) {
        // render thread CPU time (excludes sleeping and GPU waits), for performance comparisons
        timespec ts{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        static uint64_t lastCpu = 0;
        uint64_t cpu = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
        LOG("[gfx] frame %llu, %llu draws so far, GPU %.1f ms/frame, render thread CPU %.2f ms/frame", (unsigned long long)R.frame,
            (unsigned long long)R.drawCount, g_gpu_ns.exchange(0) / 1e6 / 300.0, lastCpu ? (cpu - lastCpu) / 1e6 / 300.0 : 0.0);
        lastCpu = cpu;
        void report_skips();
        report_skips();
    }
}

extern uint64_t g_stat_invalidates, g_stat_invalidated_surfaces;
void invalidate(uint32_t flags, uint32_t addr, uint32_t size) {
    g_stat_invalidates++;
    static int logged = 0;
    if (getenv("WWHD_LOG_INVALIDATE") && (flags & 0x2) && logged++ < 400)
        LOG("[inval] frame %llu flags %X addr %08X size %X", (unsigned long long)R.frame, flags, addr, size);
    // GX2_INVALIDATE_MODE_TEXTURE (0x2): the CPU wrote texture data; force a full check of surfaces in
    // range on next use. Uniform/attribute/shader invalidations need nothing here.
    if (!(flags & 0x2)) return;
    // "invalidate everything" (sent several times per frame) carries no information about CPU writes;
    // changed textures are caught by the write tracking (write_watch.h, metal_surfaces.mm check_texture)
    if (size >= 0x10000000) return;
    uint64_t end = (uint64_t)addr + size;
    auto hits = [&](Surface* s) {
        // every level once known (after the first check), else the base level estimate
        if (s->levelRanges.empty()) return addr < (uint64_t)s->addr + std::max<uint32_t>(s->dataSize, s->pitch * s->height * 4) && s->addr < end;
        for (auto& [b, n] : s->levelRanges)
            if (addr < (uint64_t)b + n && b < end) return true;
        return false;
    };
    for (auto& [a, s] : R.surfaces)
        // MEM1 holds render targets; CPU-side surfaces there are views of GPU data, not CPU uploads
        if (!s->gpuWritten && !(a >= 0xF4000000 && a < 0xF6000000) && hits(s.get())) {
            s->lastCheckedFrame = ~0ull;
            s->dirty = true;
            g_stat_invalidated_surfaces++;
            static int lg = 0;
            if (getenv("WWHD_LOG_INVALIDATE") && lg++ < 300)
                LOG("[inval]   marks %08X %ux%u fmt %X size %X (range %08X+%X)", a, s->width, s->height, s->format, s->dataSize, addr, size);
        }
}

}  // namespace gfx
