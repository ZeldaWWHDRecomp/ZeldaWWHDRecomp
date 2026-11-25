// Aspect ratio of the TV picture (Graphics menu, WWHD_ASPECT): see aspect.cpp.
#pragma once
#include <cstdint>

namespace ss { struct Writer; struct Reader; }
struct Cpu;

namespace aspect {
enum Mode : int { kOriginal = 0, kWindow, k16x10, k21x9, k32x9, kCustom, k4x3, kModes };
// Persisted IDs are stable; display order is independent of their numeric values.
inline constexpr Mode kPresets[] = {kOriginal, kWindow, k4x3, k16x10, k21x9, k32x9};
constexpr bool is_preset(int mode) {
    for (Mode preset : kPresets) if (mode == preset) return true;
    return false;
}
int mode();
void set_mode(int m);
const char* mode_name(int m);
float mode_value(int m);           // fixed aspect of a mode (0 for kWindow)
// Constrain HUD placement to a narrower aspect than the picture (Graphics menu): HUD panes keep
// their edge margins but follow the chosen shape's edges. kHudMatch is the existing placement.
// Persisted IDs are stable (settings.ini): append new modes, and list them in kHudPresets for the menu.
enum HudMode : int { kHudMatch = 0, kHud32x9, kHud21x9, kHud16x9, kHudModes };
inline constexpr HudMode kHudPresets[] = {kHudMatch, kHud16x9, kHud21x9, kHud32x9};
int hud_mode();
void set_hud_mode(int m);
const char* hud_mode_name(int m);
float hud_mode_value(int m);       // fixed aspect of a HUD mode (0 for kHudMatch)
bool hud_mode_fits(int m, float display);  // the Graphics option is not wider than the picture
float hud_shape();                 // the HUD's effective aspect (the picture's when matched)
// horizontal/vertical widening for HUD panes at the picture's display aspect `display`; never wider
// than the picture itself (a saved choice from a wider display falls back to it)
void hud_factors(float display, float& kx, float& ky);
// half the extra width, in canvas units, that mod HUD edge anchors should use on the TV
float hud_anchor_extra(float base_width);
void set_window_aspect(float a);   // shape of the TV window's drawable (render thread, every present)
float requested();                 // what the setting asks for now
// main thread, GX2SwapScanBuffers: latch the aspect for the next game frame, update the game's
// projections; returns the aspect the render thread should switch to at this swap
float on_swap();
float game();
float fov_scale();
void set_fov_scale(float scale); // 1 = authored gameplay FOV; events keep their original FOV
// game thread: layout units (720 across the 16:9 picture's height, the HUD's units) per unit of x / z in the main
// camera's view, for the camera's authored fovy (degrees) as drawn now: the aspect's Vert+ and the gameplay FOV scale
// (outside events), as adjust_projection_args draws it. 0 for a bad fovy.
float layout_scale(float fovyDeg);
// game thread: tan(fovy/2) of the main camera's projection for its authored fovy (degrees) as drawn now
// (the aspect's Vert+ and the gameplay FOV scale, as adjust_projection_args draws it); 0 for a bad
// fovy. 60 fps (generated) (framegen.cpp) reprojects the scene with it.
float scene_tan_half_fovy(float fovyDeg);
// game thread: the arguments of a culling frustum setup of the main camera, mDoLib_clipper::setup(f1 fovy (degrees),
// f2 aspect, f3 near, f4 far), adjusted as view_setup's calls are (the aspect's Vert+, the gameplay FOV scale outside
// events, the stereo margin). Frame interpolation's in-between frustum (interp.cpp clipper_blend) goes through it too.
void clipper_setup_args(Cpu* c);
uint64_t game_frame();                // swaps so far                      // aspect the game draws with this frame
// HD layouts: the game thread marks uploads of layout projection matrices and the drawing of layout
// roots; the render thread, which knows the target, narrows the projection for TV-shaped targets
// and reports where each root went
bool tagged_projection();                          // gx2: the vertex uniform upload in progress is one
void layout_root_target(uint32_t root, bool tv);   // render thread (OP_LAYOUT_ROOT)
void set_content_clip(bool clip);
bool content_clip();
void ss_save_particles(ss::Writer&);
bool ss_validate_particles(ss::Reader);
void ss_load_particles(ss::Reader);
void ss_reset();  // save state loaded: every layout recomputes its matrices once
}  // namespace aspect
