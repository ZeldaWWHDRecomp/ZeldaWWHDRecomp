// Apple Vision Pro's 3D screen (vision_screen.mm, visionos/StereoScreen.swift): the game window shows the TV picture on
// a RealityKit plane, a picture per eye. display.mm composes each presented frame's pictures into a slot of a small
// ring; the screen copies the newest finished one into RealityKit's textures. Elsewhere it is never active.
#pragma once
#include <TargetConditionals.h>
#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

namespace vision_screen {
#if TARGET_OS_VISION
bool active();  // the game window's 3D screen shows the picture
#ifdef __OBJC__
// the textures (sRGB, WWHD_SCREEN_WIDTH x HEIGHT) to compose this frame's pictures into, OUT[0] the left eye's (or the
// mono picture), OUT[1] the right eye's; false while nothing shows them or every slot is in use. end_frame with the
// command buffer that composes them, STEREO when the right eye's was drawn too.
bool begin_frame(id<MTLDevice> device, __strong id<MTLTexture> out[2]);
void end_frame(id<MTLCommandBuffer> commands, bool stereo);
#endif
#else
inline bool active() { return false; }
#endif
}  // namespace vision_screen
