// The Metal renderer's entry in the renderer table (renderer.h). The renderer itself is unchanged:
// metal_main.mm, metal_draw.mm, metal_surfaces.mm, display.mm (namespace gfx).
#include <string>

#include "gx2/gx2.h"
#include "renderer.h"
#include "metal_gamepad_backdrop.h"
#include "metal.h"
#include "stereo_gx2.h"

namespace gfx {
void sky_clouds_begin(const uint32_t* regs, uint32_t seq);  // metal_clouds.mm
void draw_stereo(const uint32_t* regs, uint32_t prim, uint32_t count, uint32_t indexType, uint32_t indexAddr,
                 uint32_t baseVertex, uint32_t instances);  // metal_draw.mm
void peek_z(const uint32_t*, uint32_t);
void peek_retire();
void flush();
void request_tv_dump(const std::string& path, int frames_ahead);
void run_appkit_loop();
uint64_t frame_count();
void ss_reset_surfaces();
void gamepad_backdrop_scope(const uint32_t*,bool);
void gamepad_backdrop_reset();
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
        b.sky_clouds_begin = gfx::sky_clouds_begin;
        b.api = Api::Metal;
        b.init = gfx::init;
        b.run_main_loop = gfx::run_appkit_loop;  // [NSApp run] plus the test hooks (display.mm)
        b.draw = gfx::draw_stereo;
        b.gamepad_backdrop_scope = gfx::gamepad_backdrop_scope;
        b.clear_color = [](const uint32_t* r, uint32_t cb, const float* c) {
            gfx::stereo_pair([&] { gfx::clear_color(r, cb, c); }, stereo::shared_color_buffer(cb));
        };
        b.clear_depth_stencil = [](const uint32_t* r, uint32_t db, float d, uint32_t s, uint32_t f) {
            gfx::stereo_pair([&] { gfx::clear_depth_stencil(r, db, d, s, f); }, stereo::shared_depth_buffer(db));
        };
        b.copy_surface = [](uint32_t s, uint32_t sm, uint32_t sl, uint32_t d, uint32_t dm, uint32_t dl) {
            gfx::stereo_pair([&] { gfx::copy_surface(s, sm, sl, d, dm, dl); }, stereo::shared_copy_destination(d, dm));
        };
        b.copy_to_scan = [](uint32_t cb, uint32_t t) { gfx::stereo_pair([&] { gfx::copy_to_scan(cb, t); }); };
        b.swap = [] {
            // the presentation reads eye 1's TV picture: eye 1's commands go first
            if (stereo::frame.mode != stereo::Off) gfx::flush();
            gfx::swap();
            gfx::stereo_latch();
        };
        b.peek_z = gfx::peek_z;
        b.peek_retire = gfx::peek_retire;
        b.set_frame_aspect = gfx::set_frame_aspect;
        b.target_aspect_factors = gfx::target_aspect_factors;
        b.frames_completed = gfx::frames_completed;
        b.with_autorelease_pool = gfx::with_autorelease_pool;
        b.set_tv_format = gfx::set_tv_format;
        b.invalidate = gfx::invalidate;
        b.guest_flush = gfx::flush;
        b.wait_idle = gfx::wait_idle;
        b.write_back = gfx::write_back_linear_targets;
        b.ss_reset = [] {
            gfx::gamepad_backdrop_reset();
            gfx::reset_gamepad_bar_pipelines();
            gfx::stereo_pair([] { gfx::ss_reset_surfaces(); });
        };
        b.frame_count = gfx::frame_count;
        b.request_tv_dump = gfx::request_tv_dump;
        b.request_capture = gfx::request_capture;
        b.shutdown = [] { gfx::gamepad_backdrop_reset(); gfx::reset_gamepad_bar_pipelines(); };  // the shader cache is written as it grows
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
        b.feature_available = [](int f) {
#if TARGET_OS_OSX || TARGET_OS_VISION
            return true;
#else
            return f != render::kFeatureStereo;  // (no 3D display on an iPhone or a TV through an Apple TV)
#endif
        };
        b.device = gfx::device_name;
        return b;
    }();
    return b;
}
}  // namespace render
