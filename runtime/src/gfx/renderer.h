// Renderer selection and dispatch. One executable can contain both renderers (macOS default build:
// WWHD_RENDERER=BOTH): Metal (gfx/metal_*.mm, namespace gfx) and Vulkan (gfx/vulkan/*, namespace
// gfxvk). Each fills a Backend table; the GX2 layer and the host (windows, menus, save states) call
// the functions below, which forward to the renderer chosen once at start-up.
//
// Choice, highest priority first:
//   --renderer=metal|vulkan (or --renderer metal|vulkan)   command line
//   WWHD_RENDERER_RUNTIME=metal|vulkan                      environment (tests)
//   Graphics > Renderer                                     saved setting (display.plist, "renderer")
//   Metal                                                   default
// If Vulkan cannot start (no Vulkan loader / MoltenVK / suitable device), the game starts with Metal
// and says why (log, window title, a sheet on the TV window).
#pragma once
#include <cstdint>
#include <string>

namespace render {

enum class Api : int { Metal = 0, Vulkan = 1 };
const char* api_name(Api a);      // "Metal", "Vulkan"
const char* api_key(Api a);       // "metal", "vulkan" (setting / command line value)
bool compiled(Api a);             // built into this executable
bool can_choose();                // more than one renderer built in (the Graphics menu offers the choice)

struct Backend {
    Api api;
    // start-up on the main thread; throws std::exception when the renderer cannot start
    void (*init)();
    void (*run_main_loop)();
    // GX2 render thread (in submission order)
    void (*draw)(const uint32_t* regs, uint32_t prim, uint32_t count, uint32_t indexType, uint32_t indexAddr,
                 uint32_t baseVertex, uint32_t instances);
    void (*clear_color)(const uint32_t* regs, uint32_t colorBuffer, const float rgba[4]);
    void (*clear_depth_stencil)(const uint32_t* regs, uint32_t depthBuffer, float depth, uint32_t stencil, uint32_t flags);
    void (*copy_surface)(uint32_t src, uint32_t srcMip, uint32_t srcSlice, uint32_t dst, uint32_t dstMip, uint32_t dstSlice);
    void (*copy_to_scan)(uint32_t colorBuffer, uint32_t target);
    void (*swap)();
    void (*peek_z)(const uint32_t* cells, uint32_t words);
    void (*set_frame_aspect)(float a);
    bool (*target_aspect_factors)(uint32_t w, uint32_t h, float& kx, float& ky);
    uint64_t (*frames_completed)();
    void (*with_autorelease_pool)(void (*fn)());
    void (*set_tv_format)(uint32_t gx2Format, bool tv);
    void (*invalidate)(uint32_t flags, uint32_t addr, uint32_t size);
    void (*guest_flush)();         // GX2Flush
    void (*wait_idle)();           // GX2DrawDone
    void (*write_back)();          // GX2DrawDone: render results the CPU reads (linear targets) to guest memory
    void (*ss_reset)();            // a save state was loaded: forget surfaces and shader memos
    // any thread
    uint64_t (*frame_count)();
    void (*request_tv_dump)(const std::string& path, int frames_ahead);
    void (*request_capture)();     // P / F12: capture the next frame
    void (*shutdown)();            // orderly exit (main thread): write renderer caches
    // graphics options (Graphics menu, hotkeys)
    float (*res_scale)();          // requested internal resolution factor
    void (*set_res_scale)(float);
    int (*ao_mode)();
    void (*set_ao_mode)(int);
    bool (*ao_hires)();
    void (*set_ao_hires)(bool);
    bool (*aniso)();
    void (*set_aniso)(bool);
    bool (*fxaa)();
    void (*set_fxaa)(bool);
    // effects this renderer offers right now (menu items are greyed out otherwise)
    bool (*feature_available)(int feature);
    // the GPU for reports (performance report header): name, and for Vulkan the driver and Vulkan
    // version; "" before the renderer started
    std::string (*device)();
};
enum Feature : int { kFeatureAO, kFeatureAOHires, kFeatureAniso, kFeatureFXAA, kFeatureScaleFilter, kFeatureCapture,
                     kFeatureShaderHeadStart };

extern const Backend* g_backend;
#ifdef WWHD_HAS_METAL
const Backend& metal_backend();   // gfx/metal_backend.mm
#endif
#ifdef WWHD_HAS_VULKAN
const Backend& vulkan_backend();  // gfx/vulkan/backend_table.cpp
#endif

// ---- start-up (main.cpp)
// pick the renderer from the command line, environment and saved setting (before init)
void choose(int argc, char** argv);
// argv without --renderer options (restart with the saved choice)
void set_restart_args(int argc, char** argv);
// start the chosen renderer; falls back to Metal when Vulkan cannot start
void init();
void run_main_loop();

Api active();
inline bool vulkan() { return g_backend && g_backend->api == Api::Vulkan; }
Api requested();                  // what was asked for at start-up (differs from active() after a fallback)
std::string fallback_reason();    // why the requested renderer did not start ("" if it did)
Api preferred();                  // saved setting: the renderer the next start uses
void set_preferred(Api a);        // save the setting (Graphics > Renderer)
bool restart_pending();           // the setting was changed this session to another renderer than the active one
// relaunch the game with the saved choice (Graphics > Renderer > Restart now); returns on failure
bool restart();

// ---- dispatch
inline void draw(const uint32_t* regs, uint32_t prim, uint32_t count, uint32_t indexType, uint32_t indexAddr,
                 uint32_t baseVertex, uint32_t instances) {
    g_backend->draw(regs, prim, count, indexType, indexAddr, baseVertex, instances);
}
inline void clear_color(const uint32_t* regs, uint32_t cb, const float rgba[4]) { g_backend->clear_color(regs, cb, rgba); }
inline void clear_depth_stencil(const uint32_t* regs, uint32_t db, float d, uint32_t s, uint32_t f) {
    g_backend->clear_depth_stencil(regs, db, d, s, f);
}
inline void copy_surface(uint32_t src, uint32_t srcMip, uint32_t srcSlice, uint32_t dst, uint32_t dstMip, uint32_t dstSlice) {
    g_backend->copy_surface(src, srcMip, srcSlice, dst, dstMip, dstSlice);
}
inline void copy_to_scan(uint32_t cb, uint32_t target) { g_backend->copy_to_scan(cb, target); }
inline void swap() { g_backend->swap(); }
inline void peek_z(const uint32_t* cells, uint32_t words) { g_backend->peek_z(cells, words); }
inline void set_frame_aspect(float a) { g_backend->set_frame_aspect(a); }
inline bool target_aspect_factors(uint32_t w, uint32_t h, float& kx, float& ky) { return g_backend->target_aspect_factors(w, h, kx, ky); }
inline uint64_t frames_completed() { return g_backend->frames_completed(); }
inline void with_autorelease_pool(void (*fn)()) { g_backend->with_autorelease_pool(fn); }
inline void set_tv_format(uint32_t f, bool tv) { g_backend->set_tv_format(f, tv); }
inline void invalidate(uint32_t flags, uint32_t addr, uint32_t size) { g_backend->invalidate(flags, addr, size); }
inline void guest_flush() { g_backend->guest_flush(); }
inline void wait_idle() { g_backend->wait_idle(); }
inline void write_back() { if (g_backend->write_back) g_backend->write_back(); }
inline void ss_reset() { g_backend->ss_reset(); }
uint64_t frame_count();  // 0 before the renderer started
inline void request_tv_dump(const std::string& path, int frames_ahead) { g_backend->request_tv_dump(path, frames_ahead); }
inline void request_capture() { g_backend->request_capture(); }
void shutdown();         // once, on the way out (Quit, window closed, WWHD_EXIT_AT_FRAME)

inline float res_scale() { return g_backend->res_scale(); }
inline void set_res_scale(float f) { g_backend->set_res_scale(f); }
inline int ao_mode() { return g_backend->ao_mode(); }
inline void set_ao_mode(int m) { g_backend->set_ao_mode(m); }
inline bool ao_hires() { return g_backend->ao_hires(); }
inline void set_ao_hires(bool v) { g_backend->set_ao_hires(v); }
inline bool aniso() { return g_backend->aniso(); }
inline void set_aniso(bool v) { g_backend->set_aniso(v); }
// Bloom intensity multiplier: 0 off, 1 original, 2 double. Shared by both renderers.
float bloom_strength();
void set_bloom_strength(float strength);
// The bloom extract shader's remapped[2].z is cThresholdParam.z (intensity).
void scale_bloom_uniforms(void* remapped, size_t size);
inline bool fxaa() { return g_backend->fxaa(); }
inline void set_fxaa(bool v) { g_backend->set_fxaa(v); }
inline bool feature_available(Feature f) { return g_backend->feature_available(f); }
inline std::string device() { return g_backend && g_backend->device ? g_backend->device() : std::string(); }

}  // namespace render
