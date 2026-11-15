// Apple Vision Pro's 3D screen (vision_screen.h): the pictures' hand-off from the renderer to the game window's
// RealityKit screen (visionos/StereoScreen.swift).
#include "vision_screen.h"
#if TARGET_OS_VISION
#include "../../../visionos/bridge.h"
#include <atomic>

void log_msg(const char* fmt, ...);

extern "C" void wwhd_vision_log(const char* text) { log_msg("%s", text); }

namespace vision_screen {
namespace {
constexpr int kRing = 3;
// the composed pictures, a pair per slot (left, right; a mono frame fills the left only): display.mm writes a slot
// that is not the newest, not being read and not still being written by the GPU (frames are in flight: no drawable
// paces this path); the screen reads the newest, each copy holding its own slot until it completes
id<MTLTexture> g_ring[kRing][2];
bool g_stereo[kRing];
std::atomic<int> g_readers[kRing];
std::atomic<bool> g_busy[kRing];
std::atomic<int> g_latest{-1}, g_writing{-1};
std::atomic<uint64_t> g_generation{0};
std::atomic<bool> g_shown{false};
}  // namespace

bool active() { return g_shown.load(std::memory_order_relaxed); }

bool begin_frame(id<MTLDevice> device, __strong id<MTLTexture> out[2]) {
    if (!active()) return false;
    if (!g_ring[0][0]) {
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm_sRGB
                                                                                     width:WWHD_SCREEN_WIDTH
                                                                                    height:WWHD_SCREEN_HEIGHT mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModePrivate;
        for (auto& slot : g_ring)
            for (auto& eye : slot) eye = [device newTextureWithDescriptor:d];
    }
    const int latest = g_latest.load();
    for (int k = 1; k <= kRing; k++) {
        const int i = (latest + k + kRing) % kRing;
        if (i != latest && g_readers[i].load() == 0 && !g_busy[i].load()) {
            g_busy[i] = true;
            g_writing = i;
            out[0] = g_ring[i][0], out[1] = g_ring[i][1];
            return true;
        }
    }
    return false;
}

void end_frame(id<MTLCommandBuffer> commands, bool stereo) {
    const int i = g_writing.exchange(-1);
    if (i < 0) return;
    g_stereo[i] = stereo;
    [commands addCompletedHandler:^(id<MTLCommandBuffer>) {
        g_latest.store(i);
        g_busy[i] = false;
        g_generation.fetch_add(1);
    }];
}
}  // namespace vision_screen

extern "C" void wwhd_screen_attach(int on) {
    vision_screen::g_shown = on != 0;
    log_msg("[screen] the game window's 3D screen %s", on ? "shows" : "is gone");
}
extern "C" bool wwhd_screen_acquire(uint64_t* generation, int* slot, id<MTLTexture> __autoreleasing* left,
                                    id<MTLTexture> __autoreleasing* right) {
    using namespace vision_screen;
    const uint64_t now = g_generation.load();
    if (now == *generation) return false;
    const int i = g_latest.load();
    if (i < 0) return false;
    g_readers[i].fetch_add(1);
    if (g_latest.load() != i) {  // (a newer one was published meanwhile: take it next time)
        g_readers[i].fetch_sub(1);
        return false;
    }
    *generation = now;
    *slot = i;
    *left = g_ring[i][0];
    *right = g_stereo[i] ? g_ring[i][1] : g_ring[i][0];
    return true;
}
extern "C" void wwhd_screen_release(int slot) { vision_screen::g_readers[slot].fetch_sub(1); }
#endif
