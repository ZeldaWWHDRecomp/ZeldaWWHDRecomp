// The Vulkan renderer's entry in the renderer table (gfx/renderer.h).
#include "api.h"
#include "buffer_cache.h"
#include "settings.h"
#include "gfx/renderer.h"

#include <stdexcept>

namespace gx2 { void checkpoint_vulkan_caches(); }

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
        b.draw = gfxvk::draw;
        b.clear_color = gfxvk::clear_color;
        b.clear_depth_stencil = gfxvk::clear_depth_stencil;
        b.copy_surface = gfxvk::copy_surface;
        b.copy_to_scan = gfxvk::copy_to_scan;
        b.swap = gfxvk::swap;
        b.peek_z = gfxvk::peek_z;
        b.set_frame_aspect = gfxvk::set_frame_aspect;
        b.target_aspect_factors = gfxvk::target_aspect_factors;
        b.frames_completed = gfxvk::frames_completed;
        b.with_autorelease_pool = gfxvk::with_autorelease_pool;
        b.set_tv_format = gfxvk::set_tv_format;
        b.invalidate = gfxvk::invalidate;
        b.guest_flush = gfxvk::flush_async;
        b.wait_idle = gfxvk::wait_idle;
        b.write_back = gfxvk::write_back_linear_targets;
        b.ss_reset = [] {
            gfxvk::ss_reset_surfaces();
            gfxvk::buffer_cache_invalidate_all();  // restored memory: every cached range is re-read
            // save-state replacement can change microcode without advancing the frame
            gfxvk::vk::reset_shader_memoization();
        };
        b.frame_count = gfxvk::frame_count;
        b.request_tv_dump = gfxvk::request_tv_dump;
        b.request_capture = gfxvk::request_capture;
        b.shutdown = gx2::checkpoint_vulkan_caches;
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
