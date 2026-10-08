// The Metal renderer's entry in the renderer table (renderer.h). The renderer itself is unchanged:
// metal_main.mm, metal_draw.mm, metal_surfaces.mm, display.mm (namespace gfx).
#include <string>

#include "gx2/gx2.h"
#include "renderer.h"

namespace gfx {
void peek_z(const uint32_t*, uint32_t);
void request_tv_dump(const std::string& path, int frames_ahead);
void run_appkit_loop();
uint64_t frame_count();
void ss_reset_surfaces();
void request_capture();
float res_scale();
void set_res_scale(float f);
int ao_mode();
void set_ao_mode(int m);
bool ao_hires_enabled();
void set_ao_hires(bool v);
bool aniso_enabled();
void set_aniso(bool v);
bool fxaa_enabled();
void set_fxaa(bool v);
std::string device_name();
}  // namespace gfx

namespace render {
const Backend& metal_backend() {
    static const Backend b = [] {
        Backend b{};
        b.api = Api::Metal;
        b.init = gfx::init;
        b.run_main_loop = gfx::run_appkit_loop;  // [NSApp run] plus the test hooks (display.mm)
        b.draw = gfx::draw;
        b.clear_color = gfx::clear_color;
        b.clear_depth_stencil = gfx::clear_depth_stencil;
        b.copy_surface = gfx::copy_surface;
        b.copy_to_scan = gfx::copy_to_scan;
        b.swap = gfx::swap;
        b.peek_z = gfx::peek_z;
        b.set_frame_aspect = gfx::set_frame_aspect;
        b.target_aspect_factors = gfx::target_aspect_factors;
        b.frames_completed = gfx::frames_completed;
        b.with_autorelease_pool = gfx::with_autorelease_pool;
        b.set_tv_format = gfx::set_tv_format;
        b.invalidate = gfx::invalidate;
        b.guest_flush = gfx::flush;
        b.wait_idle = gfx::wait_idle;
        b.write_back = gfx::write_back_linear_targets;
        b.ss_reset = gfx::ss_reset_surfaces;
        b.frame_count = gfx::frame_count;
        b.request_tv_dump = gfx::request_tv_dump;
        b.request_capture = gfx::request_capture;
        b.shutdown = [] {};  // the shader cache is written as it grows
        b.res_scale = gfx::res_scale;
        b.set_res_scale = gfx::set_res_scale;
        b.ao_mode = gfx::ao_mode;
        b.set_ao_mode = gfx::set_ao_mode;
        b.ao_hires = gfx::ao_hires_enabled;
        b.set_ao_hires = gfx::set_ao_hires;
        b.aniso = gfx::aniso_enabled;
        b.set_aniso = gfx::set_aniso;
        b.fxaa = gfx::fxaa_enabled;
        b.set_fxaa = gfx::set_fxaa;
        b.feature_available = [](int) { return true; };
        b.device = gfx::device_name;
        return b;
    }();
    return b;
}
}  // namespace render
