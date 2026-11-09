// The iPhone GamePad's screen end to end on this Mac: Metal draws frames into remote_video's targets,
// VideoToolbox encodes them, the remote pad server sends them to a phone on loopback UDP, and the frames
// put back together from their pieces decode (VideoToolbox) to the colours drawn. Also: no video before a
// phone asks for it, a keyframe first and after a keyframe request, pieces within the datagram limit.
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <VideoToolbox/VideoToolbox.h>

#include "gfx/remote_video.h"
#include "remote_pad/protocol.h"
#include "remote_pad/remote_pad.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <thread>
#include <vector>

void log_msg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
namespace mods { double game_time() { return 0; } }

using namespace remote_pad;

struct Frame { uint32_t id; bool key; uint16_t w, h; std::vector<uint8_t> data; };

struct Phone {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in to{};
    uint32_t session = 0;
    std::map<uint32_t, std::map<uint16_t, std::vector<uint8_t>>> parts;
    explicit Phone(uint16_t port) {
        to.sin_family = AF_INET;
        to.sin_port = htons(port);
        to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int big = 4 << 20;
        setsockopt(s, SOL_SOCKET, SO_RCVBUF, &big, sizeof big);
        timeval tv{0, 200 * 1000};
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
    ~Phone() { close(s); }
    void send(const std::vector<uint8_t>& p) { sendto(s, p.data(), p.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof to); }
    // the next whole frame (nullopt after a quiet 200 ms)
    std::optional<Frame> frame() {
        for (;;) {
            uint8_t buf[2048];
            const ssize_t n = recv(s, buf, sizeof buf, 0);
            if (n <= 0) return std::nullopt;
            assert(size_t(n) <= kMaxDatagram);
            auto h = parse_header(buf, size_t(n));
            if (!h) continue;
            if (h->type == kWelcome) { session = h->session; continue; }
            auto v = parse_video(buf, size_t(n));
            if (!v) fprintf(stderr, "unexpected packet type %d\n", h->type);
            assert(v && h->session == session);
            auto& f = parts[v->frame];
            f[v->index] = std::vector<uint8_t>(v->data, v->data + v->size);
            if (f.size() == v->count) {
                Frame out{v->frame, v->key, v->width, v->height, {}};
                for (auto& [i, p] : f) out.data.insert(out.data.end(), p.begin(), p.end());
                parts.erase(v->frame);
                return out;
            }
        }
    }
};

// the frame's NAL units (4-byte big-endian lengths)
static std::vector<std::vector<uint8_t>> nals(const std::vector<uint8_t>& d) {
    std::vector<std::vector<uint8_t>> out;
    for (size_t at = 0; at + 4 <= d.size();) {
        const uint32_t n = uint32_t(d[at]) << 24 | uint32_t(d[at + 1]) << 16 | uint32_t(d[at + 2]) << 8 | d[at + 3];
        at += 4;
        assert(n <= d.size() - at);
        out.emplace_back(d.begin() + at, d.begin() + at + n);
        at += n;
    }
    return out;
}

// decodes one frame (after its keyframe's parameter sets) to BGRA, as the phone would
struct Decoder {
    CMVideoFormatDescriptionRef fmt = nullptr;
    VTDecompressionSessionRef session = nullptr;
    uint8_t bgra[4] = {};
    bool got = false;
    bool decode(const Frame& f) {
        std::vector<uint8_t> payload;
        std::vector<std::vector<uint8_t>> ps;
        for (auto& n : nals(f.data)) {
            const int type = n[0] & 0x1F;
            if (type == 7 || type == 8) { ps.push_back(n); continue; }
            const uint32_t len = uint32_t(n.size());
            const uint8_t be[4] = {uint8_t(len >> 24), uint8_t(len >> 16), uint8_t(len >> 8), uint8_t(len)};
            payload.insert(payload.end(), be, be + 4);
            payload.insert(payload.end(), n.begin(), n.end());
        }
        if (!ps.empty()) {
            assert(f.key && ps.size() == 2);
            const uint8_t* ptrs[2] = {ps[0].data(), ps[1].data()};
            const size_t sizes[2] = {ps[0].size(), ps[1].size()};
            if (fmt) CFRelease(fmt);
            assert(CMVideoFormatDescriptionCreateFromH264ParameterSets(nullptr, 2, ptrs, sizes, 4, &fmt) == noErr);
            const CMVideoDimensions dim = CMVideoFormatDescriptionGetDimensions(fmt);
            assert(dim.width == f.w && dim.height == f.h);
            if (session) { VTDecompressionSessionInvalidate(session); CFRelease(session); }
            NSDictionary* out = @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)};
            VTDecompressionOutputCallbackRecord cb{[](void* self, void*, OSStatus st, VTDecodeInfoFlags, CVImageBufferRef img, CMTime, CMTime) {
                auto* d = static_cast<Decoder*>(self);
                assert(st == noErr && img);
                CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
                const uint8_t* p = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(img));
                const size_t row = CVPixelBufferGetBytesPerRow(img);
                const size_t y = CVPixelBufferGetHeight(img) / 2, x = CVPixelBufferGetWidth(img) / 2;
                memcpy(d->bgra, p + y * row + x * 4, 4);
                CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
                d->got = true;
            }, this};
            assert(VTDecompressionSessionCreate(nullptr, fmt, nullptr, (__bridge CFDictionaryRef)out, &cb, &session) == noErr);
        }
        if (!session) return false;
        CMBlockBufferRef block = nullptr;
        assert(CMBlockBufferCreateWithMemoryBlock(nullptr, nullptr, payload.size(), nullptr, nullptr, 0, payload.size(), 0, &block) == noErr);
        CMBlockBufferReplaceDataBytes(payload.data(), block, 0, payload.size());
        CMSampleBufferRef sample = nullptr;
        const size_t size = payload.size();
        assert(CMSampleBufferCreateReady(nullptr, block, fmt, 1, 0, nullptr, 1, &size, &sample) == noErr);
        got = false;
        const OSStatus st = VTDecompressionSessionDecodeFrame(session, sample, 0, nullptr, nullptr);
        VTDecompressionSessionWaitForAsynchronousFrames(session);
        CFRelease(sample);
        CFRelease(block);
        return st == noErr && got;
    }
};

static id<MTLDevice> g_dev;
static id<MTLCommandQueue> g_queue;

// one present: clear the remote target (if any) to rgb; true if a target was given
static bool present(float r, float g, float b, float w = 854, float h = 480) {
    id<MTLCommandBuffer> cb = [g_queue commandBuffer];
    id<MTLTexture> t = remote_video::begin_frame(g_dev, false, w, h);
    if (t) {
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = t;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(r, g, b, 1);
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        [[cb renderCommandEncoderWithDescriptor:rp] endEncoding];
        remote_video::end_frame(cb);
    }
    [cb commit];
    [cb waitUntilCompleted];
    return t != nil;
}

static bool near(int a, int b) { return std::abs(a - b) <= 24; }

int main() {
    @autoreleasepool {
        g_dev = MTLCreateSystemDefaultDevice();
        if (!g_dev) { puts("remote_video_test: no Metal device, skipped"); return 0; }
        // a virtual machine (CI) may have no hardware H.264 encoder, which remote_video requires
        NSDictionary* hw = @{(id)kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder: @YES};
        VTCompressionSessionRef probe = nullptr;
        if (VTCompressionSessionCreate(nullptr, 854, 480, kCMVideoCodecType_H264, (__bridge CFDictionaryRef)hw, nullptr,
                                       nullptr, nullptr, nullptr, &probe) != noErr) {
            puts("remote_video_test: no hardware H.264 encoder, skipped");
            return 0;
        }
        VTCompressionSessionInvalidate(probe);
        CFRelease(probe);
        g_queue = [g_dev newCommandQueue];
        setenv("WWHD_REMOTE_PAD", "26991", 1);
        start_from_env();
        assert(!present(1, 0, 0));  // nobody wants video: no target, no encoder

        Phone phone(26991);
        // an unknown phone must pair first: the code Settings would show, typed on the phone
        Hello hello{7, "Test iPhone", "", true};
        hello.id[0] = 0x42;
        phone.send(encode_hello(hello));
        std::optional<PairingCode> code;
        for (int i = 0; i < 100 && !code; i++) { code = pairing_code(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        assert(code && code->device == "Test iPhone" && take_pairing_request());
        assert(!video_wanted() && phone.session == 0);
        phone.send(encode_pair({7, hello.id, code->code}));
        std::optional<Paired> paired;
        for (int i = 0; i < 20 && !paired; i++) {
            uint8_t buf[2048];
            const ssize_t n = recv(phone.s, buf, sizeof buf, 0);
            if (n > 0) paired = parse_paired(buf, size_t(n));
        }
        assert(paired && paired->nonce == 7 && paired_phones() == 1);
        std::string saved;
        assert(take_paired_changed(saved) && saved.find("Test iPhone") != std::string::npos);
        hello.token = paired->token;
        phone.send(encode_hello(hello));
        assert(phone.frame() == std::nullopt && phone.session != 0);  // (the Welcome)
        assert(video_wanted());

        Decoder dec;
        // red, then green: the first frame is a keyframe with SPS and PPS, and both decode to their colour
        assert(present(1, 0, 0));
        auto f = phone.frame();
        assert(f && f->key && f->w == 854 && f->h == 480);
        assert(dec.decode(*f));
        printf("red frame: %zu bytes, decoded B%d G%d R%d\n", f->data.size(), dec.bgra[0], dec.bgra[1], dec.bgra[2]);
        assert(near(dec.bgra[2], 255) && near(dec.bgra[1], 0) && near(dec.bgra[0], 0));
        // the stream says what the pixels are: sRGB, as the TV window shows them (a BT.709 tag makes the
        // phone show the picture darker than the TV)
        CFTypeRef transfer = CMFormatDescriptionGetExtension(dec.fmt, kCMFormatDescriptionExtension_TransferFunction);
        assert(transfer && CFEqual(transfer, kCVImageBufferTransferFunction_sRGB));
        // mid-grey keeps its value through the encoder
        phone.send(encode_keyframe_request(phone.session));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        assert(present(0.5f, 0.5f, 0.5f));
        f = phone.frame();
        assert(f && dec.decode(*f) && near(dec.bgra[1], 128) && std::abs(dec.bgra[1] - 128) <= 3);
        assert(present(0, 1, 0));
        f = phone.frame();
        assert(f && !f->key && dec.decode(*f));
        assert(near(dec.bgra[1], 255) && near(dec.bgra[2], 0));
        // a keyframe request: the next frame is a keyframe
        phone.send(encode_keyframe_request(phone.session));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(present(0, 0, 1));
        f = phone.frame();
        assert(f && f->key && dec.decode(*f) && near(dec.bgra[0], 255));
        // a larger picture is sent at most 1280 wide, in its own shape
        assert(present(1, 1, 1, 1708, 960));
        f = phone.frame();
        assert(f && f->key && f->w == 1280 && f->h == 720 && dec.decode(*f) && near(dec.bgra[0], 255) && near(dec.bgra[2], 255));
        // a busy stream: every frame arrives and decodes
        int decoded = 0;
        for (int i = 0; i < 30; i++) {
            present(float(i % 2), 0.5f, 0, 1708, 960);
            if (auto g = phone.frame(); g && dec.decode(*g)) decoded++;
        }
        printf("30 frames: %d decoded\n", decoded);
        assert(decoded == 30);
        // the phone leaves: no more video
        phone.send(encode_bye(phone.session));
        for (int i = 0; i < 100 && video_wanted(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(!video_wanted() && !present(1, 0, 0));
        stop();
    }
    puts("remote_video_test: Metal frames encoded, sent in pieces, put back together and decoded; keyframes first, "
         "on request and on a new size; sRGB tag and grey levels; no video without a phone that asks passed");
}
