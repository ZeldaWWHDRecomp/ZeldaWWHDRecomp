#pragma once
struct SDL_Window;
namespace gfxvk {
float res_scale();
float requested_res_scale();
void set_res_scale(float scale);
int ao_mode();
void set_ao_mode(int mode);
bool ao_hires_enabled();
void set_ao_hires(bool enabled);
bool aniso_enabled();
void set_aniso(bool enabled);
bool fxaa_enabled();
void set_fxaa(bool enabled);
int scale_filter(); // 0 smooth, 1 sharp, 2 integer
void set_scale_filter(int filter);
enum class GraphicsFeature { AO, AOHires, Anisotropy, FXAA, ScaleFilter, Count };
bool graphics_feature_available(GraphicsFeature feature);
void set_graphics_feature_available(GraphicsFeature feature, bool available = true);
// Presentation (swapchain present mode): 0 vsync (FIFO, default), 1 low latency (MAILBOX),
// 2 off / may tear (IMMEDIATE). WWHD_VK_PRESENT_MODE=fifo|mailbox|immediate sets it for one start.
// A change recreates the swapchains (as a resize); a mode the surface does not offer falls back to FIFO.
enum PresentMode { kPresentFifo, kPresentMailbox, kPresentImmediate, kPresentModes };
int present_mode();
void set_present_mode(int mode);
bool present_mode_from_env();        // WWHD_VK_PRESENT_MODE chose it (not saved)
bool present_mode_offered(int mode); // the TV window's surface advertises it (known once the swapchain exists)
void set_present_modes_offered(unsigned mask);
const char* present_mode_name(int mode);  // "fifo", "mailbox", "immediate"
// the mode the swapchains use: present_mode(), or while uncapped (gx2::uncapped, debug) immediate
// or mailbox, whichever the surface offers
int effective_present_mode();
// Returns true for reserved graphics keys, including unavailable effects.
bool graphics_hotkey(char key, bool activate);
#ifdef __APPLE__
void install_graphics_menu(SDL_Window* window);
#else
inline void install_graphics_menu(SDL_Window*) {}
#endif
}
