// The HUD point of a frame: the first layout root (aspect.cpp hook_02877100) drawn into the TV picture.
// Everything drawn there before it is the scene (3D and its post-processing); from it on the HUD, text and
// menus draw. The game side marks each layout root (OP_HUD_LAYOUT) while a renderer feature needs the
// point; the renderer finds the first one on the TV picture with tv_picture():
//   - 60 fps (generated) (framegen.h; Vulkan reproj.cpp): the scene is copied aside there;
//   - FXAA (render::fxaa_before_hud, #587; Vulkan reproj.cpp, Metal display.mm): the scene is smoothed
//     there, so the HUD drawn after it stays sharp.
// While neither is on nothing is emitted: the command streams are the ones without the features.
#pragma once
#include <cmath>
#include <cstdint>

namespace hud_layout {

// the game side marks the layout roots (any thread after the renderer started)
bool marked();

// a colour target of guest size width x height is the TV picture: 16:9 at the console's size, not the
// GamePad's 854x480 family
inline bool tv_picture(uint32_t width, uint32_t height) {
    return height >= 360 && height != 480 && std::abs((float)width * 9.0f / ((float)height * 16.0f) - 1.0f) <= 0.03f;
}

}  // namespace hud_layout
