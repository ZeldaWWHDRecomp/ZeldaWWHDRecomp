// The Vulkan renderer's entry in the renderer table (gfx/renderer.h).
#include "api.h"
#include "stereo.h"
#include "buffer_cache.h"
#include "settings.h"
#include "gfx/renderer.h"

#include <stdexcept>

namespace gx2 { void shutdown_vulkan_caches(); }
namespace gfxvk { void sky_clouds_begin(const uint32_t* regs, uint32_t seq); }  // clouds.cpp

#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
// AppKit host (gfx/display.mm): windows exist, each view gets a CAMetalLayer for a Vulkan surface
namespace gfx {
void display_create_windows();
void display_attach_vulkan(void** tvLayer, void** drcLayer);
void display_detach();
void display_start_input();
void display_vulkan_started();
void run_appkit_loop();
}  // namespace gfx
#endif

namespace render {
const Backend& vulkan_backend() {
    static const Backend b = [] {
        Backend b{};
        b.api = Api::Vulkan;
        b.sky_clouds_begin = gfxvk::sky_clouds_begin;
#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
        b.init = [] {
            gfx::display_create_windows();
            void *tv = nullptr, *drc = nullptr;
            gfx::display_attach_vulkan(&tv, &drc);
            try {
                gfxvk::init_appkit(tv, drc);
            } catch (...) {
                gfx::display_detach();  // the fallback renderer attaches fresh layers
                throw;
            }
            gfx::display_vulkan_started();
            gfx::display_start_input();
        };
        b.run_main_loop = gfx::run_appkit_loop;
#else
        b.init = gfxvk::init;
        b.run_main_loop = gfxvk::run_main_loop;
#endif
        b.draw = gfxvk::draw_stereo;
        b.clear_color = [](const uint32_t* r, uint32_t cb, const float* c) { gfxvk::stereo_pair([&] { gfxvk::clear_color(r, cb, c); }); };
        b.clear_depth_stencil = [](const uint32_t* r, uint32_t db, float d, uint32_t s, uint32_t f) { gfxvk::stereo_pair([&] { gfxvk::clear_depth_stencil(r, db, d, s, f); }); };
        b.copy_surface = [](uint32_t s, uint32_t sm, uint32_t sl, uint32_t d, uint32_t dm, uint32_t dl) { gfxvk::stereo_pair([&] { gfxvk::copy_surface(s, sm, sl, d, dm, dl); }); };
        b.copy_to_scan = [](uint32_t cb, uint32_t t) { gfxvk::stereo_pair([&] { gfxvk::copy_to_scan(cb, t); }); };
        b.swap = [] { gfxvk::stereo_select_eye(0); gfxvk::swap(); gfxvk::stereo_latch(); };
        b.peek_z = [](const uint32_t* c, uint32_t n) { gfxvk::stereo_select_eye(0); gfxvk::peek_z(c,n); };
        b.set_frame_aspect = gfxvk::set_frame_aspect;
        b.target_aspect_factors = gfxvk::target_aspect_factors;
        b.frames_completed = gfxvk::frames_completed;
        b.set_present_interval = gfxvk::set_present_interval;
        b.present_due = gfxvk::present_due;
        b.service_present = gfxvk::service_present;
        b.with_autorelease_pool = gfxvk::with_autorelease_pool;
        b.set_tv_format = gfxvk::set_tv_format;
        b.invalidate = gfxvk::stereo_invalidate;
        b.guest_flush = gfxvk::flush_async;
        b.wait_idle = gfxvk::wait_idle;
        b.write_back = [] { gfxvk::stereo_select_eye(0); gfxvk::write_back_linear_targets(); };
        b.ss_reset = [] {
            gfxvk::stereo_pair([] { gfxvk::ss_reset_surfaces(); }, true);
            gfxvk::stereo_select_eye(0);
            gfxvk::buffer_cache_invalidate_all();  // restored memory: every cached range is re-read
            // save-state replacement can change microcode without advancing the frame
            gfxvk::vk::reset_shader_memoization();
        };
        b.frame_count = gfxvk::frame_count;
        b.request_tv_dump = gfxvk::request_tv_dump;
        b.request_capture = gfxvk::request_capture;
        b.shutdown = gx2::shutdown_vulkan_caches;
        b.res_scale = gfxvk::requested_res_scale;
        b.set_res_scale = gfxvk::set_res_scale;
        b.ao_mode = gfxvk::ao_mode;
        b.set_ao_mode = gfxvk::set_ao_mode;
        b.ao_hires = gfxvk::ao_hires_enabled;
        b.set_ao_hires = gfxvk::set_ao_hires;
        b.aniso = gfxvk::aniso_enabled;
        b.set_aniso = gfxvk::set_aniso;
        b.fxaa = gfxvk::fxaa_enabled;
        b.set_fxaa = gfxvk::set_fxaa;
        b.feature_available = [](int f) {
            switch (f) {
            case kFeatureAO: return gfxvk::graphics_feature_available(gfxvk::GraphicsFeature::AO);
            case kFeatureAOHires: return gfxvk::graphics_feature_available(gfxvk::GraphicsFeature::AOHires);
            case kFeatureAniso: return gfxvk::graphics_feature_available(gfxvk::GraphicsFeature::Anisotropy);
            case kFeatureFXAA: return gfxvk::graphics_feature_available(gfxvk::GraphicsFeature::FXAA);
            case kFeatureScaleFilter: return gfxvk::graphics_feature_available(gfxvk::GraphicsFeature::ScaleFilter);
            case kFeatureCapture: return true;
            case kFeatureShaderHeadStart: return false;  // Metal shader cache only
            }
            return true;
        };
        b.device = gfxvk::device_description;
        return b;
    }();
    return b;
}
}  // namespace render
