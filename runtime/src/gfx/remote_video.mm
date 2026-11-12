// The GamePad screen for the iPhone GamePad (remote_pad.h): while a connected phone asks for it, every
// present also draws the GamePad picture (display.mm's compose_drc, as for the GamePad window) into an
// IOSurface-backed pixel buffer, and once the GPU has finished, VideoToolbox encodes it as low-latency
// H.264 (hardware, no frame reordering) for remote_pad::send_video. No CPU copy of the picture.
//
// Threads: begin_frame / end_frame on the render thread (present_screens); the command buffer's
// completion handler starts the encode, and VideoToolbox's output callback sends the frame.
#import "remote_video.h"

#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <VideoToolbox/VideoToolbox.h>
#import <ImageIO/ImageIO.h>

#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

#include "../remote_pad/remote_pad.h"

void log_msg(const char* fmt, ...);

namespace remote_video {
namespace {

// pictures in the encoder (or on the GPU) at once: more and the phone sees old frames, so skip a present
constexpr int kMaxInFlight = 2;
constexpr int kMaxWidth = 1280;

struct Encoder {
    VTCompressionSessionRef session = nullptr;
    CVMetalTextureCacheRef cache = nullptr;
    int width = 0, height = 0;
    id<MTLDevice> device = nil;
};
Encoder g_enc;                       // render thread
std::atomic<int> g_in_flight{0};
std::atomic<bool> g_force_key{false};
bool g_was_wanted = false;           // render thread

void put_be32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v >> 24)); out.push_back(uint8_t(v >> 16)); out.push_back(uint8_t(v >> 8)); out.push_back(uint8_t(v));
}

// VideoToolbox's output: the frame's NAL units (AVCC), with SPS and PPS ahead of a keyframe
void encoded(void*, void* frame_ctx, OSStatus status, VTEncodeInfoFlags flags, CMSampleBufferRef sample) {
    g_in_flight.fetch_sub(1);
    const auto size = reinterpret_cast<uintptr_t>(frame_ctx);  // width << 16 | height
    if (status != noErr || !sample || (flags & kVTEncodeInfo_FrameDropped) || !CMSampleBufferDataIsReady(sample)) {
        g_force_key = true;  // the phone must not go on from a frame it never got
        return;
    }
    bool key = true;
    if (CFArrayRef att = CMSampleBufferGetSampleAttachmentsArray(sample, false); att && CFArrayGetCount(att) > 0) {
        auto d = (CFDictionaryRef)CFArrayGetValueAtIndex(att, 0);
        key = !CFDictionaryContainsKey(d, kCMSampleAttachmentKey_NotSync);
    }
    std::vector<uint8_t> out;
    int nal_len = 4;
    if (key) {
        CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(sample);
        size_t count = 0;
        CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 0, nullptr, nullptr, &count, &nal_len);
        for (size_t i = 0; i < count; i++) {
            const uint8_t* ps = nullptr;
            size_t n = 0;
            if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, i, &ps, &n, nullptr, nullptr) != noErr) return;
            put_be32(out, uint32_t(n));
            out.insert(out.end(), ps, ps + n);
        }
    }
    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
    const size_t total = CMBlockBufferGetDataLength(block);
    std::vector<uint8_t> nals(total);
    if (CMBlockBufferCopyDataBytes(block, 0, total, nals.data()) != kCMBlockBufferNoErr) return;
    // the protocol's 4-byte lengths (VideoToolbox's H.264 uses 4 too; convert if a session ever does not)
    for (size_t at = 0; at + nal_len <= total;) {
        uint32_t n = 0;
        for (int i = 0; i < nal_len; i++) n = n << 8 | nals[at + i];
        at += nal_len;
        if (n > total - at) return;
        put_be32(out, n);
        out.insert(out.end(), nals.begin() + at, nals.begin() + at + n);
        at += n;
    }
    const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
    const uint64_t t = pts.timescale ? uint64_t(double(pts.value) * 1e9 / pts.timescale) : remote_pad::clock_ns();
    remote_pad::send_video(out.data(), out.size(), key, t, uint16_t(size >> 16), uint16_t(size & 0xFFFF));
}

void set(VTCompressionSessionRef s, CFStringRef key, CFTypeRef value) {
    if (OSStatus e = VTSessionSetProperty(s, key, value); e != noErr && e != kVTPropertyNotSupportedErr)
        log_msg("[remote video] encoder property %s: %d", CFStringGetCStringPtr(key, kCFStringEncodingUTF8) ?: "?", (int)e);
}
void set_int(VTCompressionSessionRef s, CFStringRef key, int64_t v) {
    CFNumberRef n = CFNumberCreate(nullptr, kCFNumberSInt64Type, &v);
    set(s, key, n);
    CFRelease(n);
}

void teardown() {
    if (g_enc.session) {
        VTCompressionSessionCompleteFrames(g_enc.session, kCMTimeInvalid);
        VTCompressionSessionInvalidate(g_enc.session);
        CFRelease(g_enc.session);
        g_enc.session = nullptr;
    }
    if (g_enc.cache) { CFRelease(g_enc.cache); g_enc.cache = nullptr; }
    g_enc.width = g_enc.height = 0;
}

bool setup(id<MTLDevice> device, int w, int h) {
    if (g_enc.session && g_enc.width == w && g_enc.height == h && g_enc.device == device) return true;
    teardown();
    // the encoder's own pool: 32BGRA IOSurfaces that Metal can draw into
    NSDictionary* buffers = @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
                              (id)kCVPixelBufferWidthKey: @(w), (id)kCVPixelBufferHeightKey: @(h),
                              (id)kCVPixelBufferMetalCompatibilityKey: @YES,
                              (id)kCVPixelBufferIOSurfacePropertiesKey: @{}};
    NSDictionary* spec = @{(id)kVTVideoEncoderSpecification_EnableLowLatencyRateControl: @YES,
                           (id)kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder: @YES};
    OSStatus e = VTCompressionSessionCreate(nullptr, w, h, kCMVideoCodecType_H264, (__bridge CFDictionaryRef)spec,
                                            (__bridge CFDictionaryRef)buffers, nullptr, encoded, nullptr, &g_enc.session);
    if (e != noErr) {
        log_msg("[remote video] H.264 encoder %dx%d failed (%d)", w, h, (int)e);
        g_enc.session = nullptr;
        return false;
    }
    VTCompressionSessionRef s = g_enc.session;
    set(s, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
    set(s, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
    set(s, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_High_AutoLevel);
    // about 2 Mbit/s at 854 x 480, more for a larger picture: a busy home Wi-Fi link queues more than that for
    // seconds rather than dropping it (4-5 s picture lag at 6 Mbit/s on a congested 2.4 GHz link).
    // WWHD_REMOTE_VIDEO_KBPS sets it for a test.
    static const int64_t kbps = getenv("WWHD_REMOTE_VIDEO_KBPS") ? atoll(getenv("WWHD_REMOTE_VIDEO_KBPS")) : 0;
    const int64_t bits = kbps > 0 ? kbps * 1000 : std::clamp<int64_t>(int64_t(double(w) * h * 5), 1'500'000, 6'000'000);
    set_int(s, kVTCompressionPropertyKey_AverageBitRate, bits);
    set_int(s, kVTCompressionPropertyKey_ExpectedFrameRate, 60);
    set_int(s, kVTCompressionPropertyKey_MaxKeyFrameInterval, 600);
    set_int(s, kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, 10);
    // The pixels are sRGB, as the TV window shows them: tag the stream so (H.264 VUI transfer 13). Tagged
    // BT.709, the phone colour-manages it as video (BT.1886, gamma 2.4) and the picture looks darker.
    set(s, kVTCompressionPropertyKey_ColorPrimaries, kCVImageBufferColorPrimaries_ITU_R_709_2);
    set(s, kVTCompressionPropertyKey_TransferFunction, kCVImageBufferTransferFunction_sRGB);
    set(s, kVTCompressionPropertyKey_YCbCrMatrix, kCVImageBufferYCbCrMatrix_ITU_R_709_2);
    VTCompressionSessionPrepareToEncodeFrames(s);
    if (CVMetalTextureCacheCreate(nullptr, nullptr, device, nullptr, &g_enc.cache) != kCVReturnSuccess) {
        teardown();
        return false;
    }
    g_enc.width = w;
    g_enc.height = h;
    g_enc.device = device;
    g_force_key = true;
    log_msg("[remote video] GamePad screen to the phone: H.264 %dx%d, %.1f Mbit/s", w, h, bits / 1e6);
    return true;
}

// the frame being drawn (render thread, between begin_frame and end_frame)
CVPixelBufferRef g_pixels = nullptr;
CVMetalTextureRef g_texture = nullptr;

}  // namespace

void load_boot_image(const std::string& tga) {
    @autoreleasepool {
        NSData* data = [NSData dataWithContentsOfFile:@(tga.c_str())];
        const uint8_t* d = (const uint8_t*)data.bytes;
        // no colour map, uncompressed true colour, 24 or 32 bits a pixel (Wind Waker HD's is 24)
        if (data.length < 18 || d[1] != 0 || d[2] != 2 || (d[16] != 24 && d[16] != 32)) return;
        const size_t w = d[12] | d[13] << 8, h = d[14] | d[15] << 8, start = 18 + d[0], bpp = d[16] / 8;
        if (!w || !h || w > 4096 || h > 4096 || data.length < start + w * h * bpp) return;
        const bool topDown = d[17] & 0x20;  // otherwise the first row is the bottom one
        std::vector<uint8_t> rgba(w * h * 4);
        for (size_t y = 0; y < h; y++) {
            const uint8_t* src = d + start + (topDown ? y : h - 1 - y) * w * bpp;
            uint8_t* dst = rgba.data() + y * w * 4;
            for (size_t x = 0; x < w; x++) {  // BGR(A) -> RGBA, opaque
                const uint8_t* p = src + x * bpp;
                dst[x * 4] = p[2]; dst[x * 4 + 1] = p[1]; dst[x * 4 + 2] = p[0]; dst[x * 4 + 3] = 255;
            }
        }
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef ctx = CGBitmapContextCreate(rgba.data(), w, h, 8, w * 4, cs, kCGImageAlphaNoneSkipLast);
        CGImageRef img = ctx ? CGBitmapContextCreateImage(ctx) : nullptr;
        NSMutableData* jpeg = [NSMutableData data];
        CGImageDestinationRef dest = img ? CGImageDestinationCreateWithData((__bridge CFMutableDataRef)jpeg, CFSTR("public.jpeg"), 1, nullptr) : nullptr;
        if (dest) {
            CGImageDestinationAddImage(dest, img, (__bridge CFDictionaryRef)@{(id)kCGImageDestinationLossyCompressionQuality: @0.85});
            if (CGImageDestinationFinalize(dest) && jpeg.length) {
                const uint8_t* b = (const uint8_t*)jpeg.bytes;
                remote_pad::set_boot_image(std::vector<uint8_t>(b, b + jpeg.length));
                log_msg("[remote video] the GamePad boot image for the phone: %zux%zu, %lu KB", w, h, (unsigned long)(jpeg.length >> 10));
            }
            CFRelease(dest);
        }
        if (img) CGImageRelease(img);
        if (ctx) CGContextRelease(ctx);
        CGColorSpaceRelease(cs);
    }
}

id<MTLTexture> begin_frame(id<MTLDevice> device, bool srgb, float drc_w, float drc_h) {
    const bool wanted = remote_pad::video_wanted() && drc_w >= 2 && drc_h >= 2;
    if (!wanted) {
        if (g_was_wanted && g_in_flight == 0) { teardown(); g_was_wanted = false; }
        return nil;
    }
    g_was_wanted = true;
    if (g_in_flight.load() >= kMaxInFlight) return nil;
    // the picture's own shape, at most kMaxWidth wide, even sizes (4:2:0)
    const int w = std::min(int(std::lround(drc_w / 2)) * 2, kMaxWidth);
    const int h = int(std::lround(w * drc_h / drc_w / 2)) * 2;  // (1708 x 960 -> 1280 x 720)
    if (!setup(device, w, h)) return nil;
    CVPixelBufferPoolRef pool = VTCompressionSessionGetPixelBufferPool(g_enc.session);
    if (!pool || CVPixelBufferPoolCreatePixelBuffer(nullptr, pool, &g_pixels) != kCVReturnSuccess) return nil;
    const MTLPixelFormat fmt = srgb ? MTLPixelFormatBGRA8Unorm_sRGB : MTLPixelFormatBGRA8Unorm;
    if (CVMetalTextureCacheCreateTextureFromImage(nullptr, g_enc.cache, g_pixels, nullptr, fmt, w, h, 0, &g_texture) != kCVReturnSuccess) {
        CVPixelBufferRelease(g_pixels);
        g_pixels = nullptr;
        return nil;
    }
    g_in_flight.fetch_add(1);
    return CVMetalTextureGetTexture(g_texture);
}

void end_frame(id<MTLCommandBuffer> cb, uint64_t time_ns) {
    if (!g_pixels) return;
    CVPixelBufferRef pixels = g_pixels;
    CVMetalTextureRef texture = g_texture;
    g_pixels = nullptr;
    g_texture = nullptr;
    VTCompressionSessionRef session = g_enc.session;
    CFRetain(session);
    const int64_t size = int64_t(g_enc.width) << 16 | g_enc.height;
    const uint64_t t = time_ns;
    [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
        bool sent = false;
        if (done.status == MTLCommandBufferStatusCompleted) {
            const bool asked = remote_pad::take_keyframe_request();  // (both flags are taken)
            const bool forced = g_force_key.exchange(false);
            NSDictionary* opts = asked || forced ? @{(id)kVTEncodeFrameOptionKey_ForceKeyFrame: @YES} : nil;
            sent = VTCompressionSessionEncodeFrame(session, pixels, CMTimeMake(int64_t(t), 1'000'000'000), kCMTimeInvalid,
                                                   (__bridge CFDictionaryRef)opts, reinterpret_cast<void*>(size), nullptr) == noErr;
        }
        if (!sent) g_in_flight.fetch_sub(1);  // (the output callback counts it down otherwise)
        CFRelease(texture);
        CVPixelBufferRelease(pixels);
        CFRelease(session);
    }];
}

}  // namespace remote_video
