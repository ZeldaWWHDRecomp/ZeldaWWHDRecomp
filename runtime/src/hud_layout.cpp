// The HUD point's game side (hud_layout.h).
#include "hud_layout.h"

#include "framegen.h"
#include "gfx/renderer.h"

namespace hud_layout {

bool marked() { return framegen::requested() || render::fxaa_before_hud(); }

}  // namespace hud_layout
