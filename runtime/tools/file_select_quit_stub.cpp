// Game-free host/UI fixtures have no guest file-select screen.
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
