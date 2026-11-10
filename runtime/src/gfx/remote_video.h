// The GamePad screen for the iPhone GamePad (remote_video.mm, remote_pad.h). Metal renderer only.
#pragma once
#import <Metal/Metal.h>
#include <string>

namespace remote_video {
// Render thread, once per present: a target to draw the GamePad picture into (as for the GamePad window),
// or nil (no phone wants it, the encoder is behind). drc_w/drc_h: the picture's nominal size.
id<MTLTexture> begin_frame(id<MTLDevice> device, bool srgb, float drc_w, float drc_h);
// after drawing into it, in the command buffer that draws it: encoded and sent once the GPU is done.
// time_ns: when the present began (remote_pad::clock_ns), the frame's time the phone measures its age from
void end_frame(id<MTLCommandBuffer> cb, uint64_t time_ns);
// Start-up: the running game's GamePad boot image (meta/bootDrcTex.tga, an uncompressed 24 or 32-bit TGA, 854x480 in
// Wii U games) as a JPEG for the phone's background (remote_pad.h set_boot_image); nothing without one.
void load_boot_image(const std::string& tga);
}
