// Aspect ratio of the TV picture (Graphics menu, WWHD_ASPECT): see aspect.cpp.
#pragma once
#include <cstdint>

namespace ss { struct Writer; struct Reader; }

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
