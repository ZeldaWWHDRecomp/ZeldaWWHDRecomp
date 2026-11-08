// Game-free host/UI fixtures have no guest file-select screen.
#include <cstdint>
#include "file_select_quit.h"
namespace file_select_quit {
void filter(input::PadState &) {}
bool visible() { return false; }
bool confirming() { return false; }
bool pointer(float, float) { return false; }
void set_tv_region(float, float, float, float, bool) {}
void frame(float, float) {}
void backend_destroyed() {}
bool image_texture(uint64_t) { return false; }
} // namespace file_select_quit
// (nor a co-op Sea Chart: its icons' textures, guest_hud's release and image checks)
namespace mods::coop::radar {
void chart_backend_destroyed() {}
bool chart_texture(uint64_t) { return false; }
}  // namespace mods::coop::radar
